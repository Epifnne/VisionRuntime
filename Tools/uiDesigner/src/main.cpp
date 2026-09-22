/**
 * @file main.cpp
 * @brief visionDesigner entry point. With --selftest, runs the headless
 * acceptance checks (template validation, save/reload roundtrip, publish)
 * without showing the UI.
 */

#include "designerBackend.hpp"
#include "previewClient.hpp"
#include "previewImageProvider.hpp"

#include <QCommandLineParser>
#include <QDeadlineTimer>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace {

/// Appends a node to a page's children, mimicking a canvas edit.
void appendNode(QJsonObject& root, int page, const QJsonObject& node) {
	auto ui = root.value(QStringLiteral("ui")).toObject();
	auto pages = ui.value(QStringLiteral("pages")).toArray();
	auto pageObject = pages.at(page).toObject();
	auto children = pageObject.value(QStringLiteral("children")).toArray();
	children.append(node);
	pageObject.insert(QStringLiteral("children"), children);
	pages.replace(page, pageObject);
	ui.insert(QStringLiteral("pages"), pages);
	root.insert(QStringLiteral("ui"), ui);
}

[[nodiscard]] QString compactOf(const QJsonValue& value) {
	if (value.isObject()) {
		return QString::fromUtf8(QJsonDocument(value.toObject())
			.toJson(QJsonDocument::Compact));
	}
	return QString::fromUtf8(QJsonDocument(value.toArray())
		.toJson(QJsonDocument::Compact));
}

int runSelfTest(visionDesigner::DesignerBackend& backend) {
	int failures = 0;
	const auto check = [&failures](bool ok, const QString& label) {
		qInfo("SELFTEST %s: %s", ok ? "PASS" : "FAIL",
			label.toUtf8().constData());
		if (!ok) {
			++failures;
		}
	};

	// 1. Templates parse; the two concrete templates validate clean (the
	// empty skeleton intentionally fails validation until sources and the
	// model package are filled in).
	QString twoCameraJson;
	for (const auto& templateId : backend.templateIds()) {
		const auto text = backend.newFromTemplate(templateId);
		const auto document = QJsonDocument::fromJson(text.toUtf8());
		check(!text.isEmpty() && document.isObject(),
			QStringLiteral("template %1 parses").arg(templateId));
		backend.deriveEndpoints(text);
		if (templateId == QLatin1String("emptyProduct")) {
			check(!backend.validate(text),
				QStringLiteral("template %1 reports missing fields")
					.arg(templateId));
			continue;
		}
		const bool valid = backend.validate(text);
		if (!valid) {
			for (const auto& issue : backend.issues()) {
				qInfo("SELFTEST issue: %s",
					QJsonDocument::fromVariant(issue)
						.toJson(QJsonDocument::Compact).constData());
			}
		}
		check(valid, QStringLiteral("template %1 validates").arg(templateId));
		if (templateId == QLatin1String("twoCameraDirectory")) {
			twoCameraJson = text;
		}
	}

	// 2. Derived universe carries the core endpoint contract.
	QString deriveError;
	const auto universe = visionDesigner::DesignerBackend
		::deriveEndpointEntries(twoCameraJson, &deriveError);
	QStringList names;
	for (const auto& entry : universe) {
		names << entry.toMap().value(QStringLiteral("name")).toString();
	}
	check(deriveError.isEmpty()
			&& names.contains(QStringLiteral("session.start"))
			&& names.contains(QStringLiteral("session.summary"))
			&& names.contains(QStringLiteral("metrics.performance"))
			&& names.contains(QStringLiteral("stream.camA"))
			&& names.contains(QStringLiteral("stream.camB"))
			&& names.contains(
				QStringLiteral("camera.camA.exposureMicroseconds")),
		QStringLiteral("derived endpoint universe"));

	// 3. Canvas-edit roundtrip: append a node, save, reload — the canvas
	// state must be fully restored from the file.
	auto root = QJsonDocument::fromJson(twoCameraJson.toUtf8()).object();
	const QJsonObject appended{
		{QStringLiteral("type"), QStringLiteral("statNumber")},
		{QStringLiteral("endpoint"), QStringLiteral("session.summary")},
		{QStringLiteral("field"), QStringLiteral("completed")},
		{QStringLiteral("title"), QStringLiteral("Completed Copy")},
	};
	appendNode(root, 0, appended);
	const auto editedJson = QString::fromUtf8(
		QJsonDocument(root).toJson(QJsonDocument::Indented));
	const auto workDirectory =
		QDir::temp().absoluteFilePath(QStringLiteral("visionDesignerSelftest"));
	QDir().mkpath(workDirectory);
	const auto profilePath =
		workDirectory + QStringLiteral("/profile.json");
	check(backend.saveProfile(profilePath, editedJson),
		QStringLiteral("save edited profile"));
	const auto reloaded = backend.loadProfile(profilePath);
	const auto reloadedRoot =
		QJsonDocument::fromJson(reloaded.toUtf8()).object();
	const auto reloadedChildren = reloadedRoot
		.value(QStringLiteral("ui")).toObject()
		.value(QStringLiteral("pages")).toArray()
		.first().toObject()
		.value(QStringLiteral("children")).toArray();
	bool restored = false;
	for (const auto& child : reloadedChildren) {
		if (compactOf(child) == compactOf(appended)) {
			restored = true;
			break;
		}
	}
	check(restored, QStringLiteral("canvas roundtrip restores edits"));

	// 4. Publish produces a loadable product package.
	const auto publishRoot =
		workDirectory + QStringLiteral("/publish");
	check(backend.publish(publishRoot, editedJson),
		QStringLiteral("publish product package"));
	const auto publishedPath = publishRoot + QStringLiteral("/")
		+ root.value(QStringLiteral("id")).toString()
		+ QStringLiteral("/profile.json");
	check(QJsonDocument::fromJson(
			backend.loadProfile(publishedPath).toUtf8()).isObject(),
		QStringLiteral("published package reloads"));

	// 5. Validation catches an unregistered endpoint binding.
	appendNode(root, 0, QJsonObject{
		{QStringLiteral("type"), QStringLiteral("imageView")},
		{QStringLiteral("endpoint"), QStringLiteral("stream.nope")},
	});
	backend.deriveEndpoints(QString::fromUtf8(
		QJsonDocument(root).toJson(QJsonDocument::Compact)));
	check(!backend.validate(QString::fromUtf8(
			QJsonDocument(root).toJson(QJsonDocument::Compact))),
		QStringLiteral("invalid endpoint binding is rejected"));

	// 6. Preview simulation drives all four endpoint kinds (command ->
	// session lifecycle, state snapshots, parameter read/write, stream
	// frames with the generated-pattern fallback).
	{
		visionDesigner::PreviewClient preview;
		preview.setProfile(twoCameraJson, QString());
		int frames = 0;
		QObject::connect(&preview,
			&visionDesigner::PreviewClient::frameReceived,
			&preview, [&frames](quint32, const QImage& image) {
				if (!image.isNull()) {
					++frames;
				}
			});
		check(preview.streamSourceId(QStringLiteral("stream.camB")) == 1,
			QStringLiteral("preview stream sourceId resolution"));
		check(preview.invokeCommand(QStringLiteral("session.start")),
			QStringLiteral("preview session.start command"));
		check(preview.stateSnapshot(QStringLiteral("session.state"))
				.contains(QLatin1String("running")),
			QStringLiteral("preview state snapshot"));
		check(preview.writeParameter(
				QStringLiteral("camera.camA.exposureMicroseconds"), 25000.0)
			&& preview.readParameter(
				QStringLiteral("camera.camA.exposureMicroseconds"))
					.toDouble() == 25000.0,
			QStringLiteral("preview parameter read/write"));
		const QDeadlineTimer deadline(std::chrono::milliseconds(2000));
		while (frames < 4 && !deadline.hasExpired()) {
			QCoreApplication::processEvents(
				QEventLoop::AllEvents, 100);
		}
		check(frames >= 4,
			QStringLiteral("preview stream frames (%1)").arg(frames));
		check(preview.invokeCommand(QStringLiteral("session.stop"))
				&& preview.stateSnapshot(QStringLiteral("session.state"))
					.contains(QLatin1String("idle")),
			QStringLiteral("preview session.stop command"));
	}

	qInfo("SELFTEST %s (%d failure(s))",
		failures == 0 ? "DONE" : "FAILED", failures);
	return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
	QGuiApplication app(argc, argv);
	QCoreApplication::setApplicationName("visionDesigner");
	QCoreApplication::setOrganizationName("VisionRuntime");
	QCoreApplication::setApplicationVersion("1.0.0");

	QCommandLineParser parser;
	parser.setApplicationDescription(
		"visionDesigner - product profile and UI composition tool");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument("profile",
		"Open this product profile JSON at startup");
	const QCommandLineOption selfTestOption(QStringLiteral("selftest"),
		"Run the headless acceptance checks and exit");
	parser.addOption(selfTestOption);
	const QCommandLineOption previewOption(QStringLiteral("preview"),
		"Start directly in preview mode (requires a profile argument)");
	parser.addOption(previewOption);
	parser.process(app);

	visionDesigner::DesignerBackend backend;
	if (parser.isSet(selfTestOption)) {
		return runSelfTest(backend);
	}

	visionDesigner::PreviewClient previewClient;
	// addImageProvider() transfers ownership to the engine, which deletes the
	// provider at teardown — it must be heap-allocated.
	auto* imageProvider = new visionDesigner::PreviewImageProvider;
	QObject::connect(&previewClient, &visionDesigner::PreviewClient::frameReceived,
		imageProvider,
		[imageProvider](quint32 sourceId, const QImage& image) {
			imageProvider->updateFrame(sourceId, image);
		});

	QQmlApplicationEngine engine;
	// The preview pane loads Shell/controls by file URL; those controls bind
	// against a "serviceClient" context property, which the preview client
	// satisfies with simulated data.
	engine.rootContext()->setContextProperty("serviceClient", &previewClient);
	engine.rootContext()->setContextProperty("designerBackend", &backend);
	engine.rootContext()->setContextProperty(QStringLiteral("startupProfile"),
		parser.positionalArguments().isEmpty()
			? QString() : parser.positionalArguments().first());
	engine.rootContext()->setContextProperty(QStringLiteral("startupPreview"),
		parser.isSet(previewOption));
	engine.addImageProvider(QStringLiteral("frames"), imageProvider);

	const QUrl url(QStringLiteral("qrc:/VisionDesigner/qml/main.qml"));
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
		&app, [url](QObject* obj, const QUrl& objUrl) {
			if (!obj && url == objUrl) {
				QCoreApplication::exit(-1);
			}
		}, Qt::QueuedConnection);
	engine.load(url);

	return app.exec();
}
