/**
 * @file main.cpp
 * @brief visionDesigner entry point. With --selftest, runs the headless
 * acceptance checks (template validation, save/reload roundtrip, publish)
 * without showing the UI.
 */

#include "designerBackend.hpp"
#include "releaseManifest.hpp"
#include <QTemporaryDir>
#include <QSettings>
#include "previewClient.hpp"
#include "previewImageProvider.hpp"

#include <QCommandLineParser>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

/// Mirrors DesignerBackend's entry-script file-name rule for the selftest.
[[nodiscard]] QString sanitizeExpected(const QJsonObject& root) {
	auto name = root.value(QStringLiteral("name")).toString();
	if (name.isEmpty()) {
		name = root.value(QStringLiteral("id")).toString();
	}
	const QString invalid = QStringLiteral("\\\\/:*?\"<>|");
	for (const auto& ch : invalid) {
		name.replace(ch, QLatin1Char('_'));
	}
	return name.trimmed();
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
	QTemporaryDir fixture(QDir::temp().filePath(QStringLiteral("visionDesignerSelftest-XXXXXX")));
	if (!fixture.isValid()) {
		qCritical() << "Cannot create selftest fixture directory";
		return 1;
	}
	const auto workDirectory = fixture.path();
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

	// 4. Publish assembles a self-contained package: bin/ from the runtime
	// template, resources copied in, profile paths rewritten, entry script.
	// Fabricate a minimal runtime template and the assets the edited
	// profile points at (paths are rewritten to be local to workDirectory).
#ifdef Q_OS_WIN
	const auto shellBinaryName = QStringLiteral("visionShell.exe");
#else
	const auto shellBinaryName = QStringLiteral("visionShell");
#endif
	const auto templateRoot =
		workDirectory + QStringLiteral("/runtimeTemplate");
	QDir().mkpath(templateRoot);
	{
		QFile file(templateRoot + QLatin1Char('/') + shellBinaryName);
		Q_UNUSED(file.open(QIODevice::WriteOnly));
		file.write("fake shell");
	}
	// Minimal backend capability manifest so the publish platform check runs.
	QDir().mkpath(templateRoot + QStringLiteral("/plugins"));
	QDir().mkpath(templateRoot + QStringLiteral("/plugins/openvino"));
	{
		QFile file(templateRoot + QStringLiteral("/plugins/openvino/vision-backend-openvino.")
#ifdef Q_OS_WIN
			+ QStringLiteral("dll"));
#else
			+ QStringLiteral("so"));
#endif
		Q_UNUSED(file.open(QIODevice::WriteOnly));
		file.write("fake plugin");
	}
	{
		QFile file(templateRoot + QStringLiteral("/plugins/plugins.json"));
		Q_UNUSED(file.open(QIODevice::WriteOnly));
		file.write(R"({"backends":[{"id":"openvino","devices":[
			{"id":"CPU","platforms":["windows","linux"]},
			{"id":"NPU","platforms":["linux"]}]}]})");
	}
	QDir().mkpath(workDirectory + QStringLiteral("/model/pkg"));
	{
		QFile file(workDirectory + QStringLiteral("/model/manifest.json"));
		Q_UNUSED(file.open(QIODevice::WriteOnly));
		file.write("{\"schemaVersion\":{\"major\":1}}");
	}
	{
		QFile file(workDirectory + QStringLiteral("/model/pkg/model.bin"));
		Q_UNUSED(file.open(QIODevice::WriteOnly));
		file.write("fake model");
	}
	QDir().mkpath(workDirectory + QStringLiteral("/image"));
	{
		QFile file(workDirectory + QStringLiteral("/image/frame0.ppm"));
		Q_UNUSED(file.open(QIODevice::WriteOnly));
		file.write("P6 1 1 255\n");
	}
	auto publishRootObject =
		QJsonDocument::fromJson(editedJson.toUtf8()).object();
	auto publishModel = publishRootObject
		.value(QStringLiteral("model")).toObject();
	publishModel.insert(QStringLiteral("packagePath"),
		QStringLiteral("model"));
	publishModel.insert(QStringLiteral("pluginDirectory"),
		QStringLiteral("plugins-missing"));
	publishRootObject.insert(QStringLiteral("model"), publishModel);
	auto publishSources = publishRootObject
		.value(QStringLiteral("sources")).toArray();
	for (auto index = 0; index < publishSources.size(); ++index) {
		auto source = publishSources.at(index).toObject();
		source.insert(QStringLiteral("directory"), QStringLiteral("image"));
		publishSources.replace(index, source);
	}
	publishRootObject.insert(QStringLiteral("sources"), publishSources);
	const auto publishJson = QString::fromUtf8(
		QJsonDocument(publishRootObject).toJson(QJsonDocument::Indented));
	QSettings settings(QStringLiteral("VisionRuntime"), QStringLiteral("visionDesigner"));
#ifdef Q_OS_WIN
	const auto settingsKey = QStringLiteral("runtimeTemplate/windows");
#else
	const auto settingsKey = QStringLiteral("runtimeTemplate/linux");
#endif
	const auto previousTemplate = settings.value(settingsKey);
	const auto platform = settingsKey.section(QLatin1Char('/'), 1);
	const auto bundledTemplate = QDir(QCoreApplication::applicationDirPath())
		.absoluteFilePath(QStringLiteral("../runtimeTemplates/") + platform);
	if (QFileInfo::exists(bundledTemplate + QLatin1Char('/') + shellBinaryName)) {
		settings.setValue(settingsKey, templateRoot);
		visionDesigner::DesignerBackend freshBackend;
		check(QDir(freshBackend.runtimeTemplate(platform)).absolutePath()
				== QDir(bundledTemplate).absolutePath(),
			QStringLiteral("bundled runtime overrides stale global setting"));
	}
	backend.setRuntimeTemplate(
#ifdef Q_OS_WIN
		QStringLiteral("windows"),
#else
		QStringLiteral("linux"),
#endif
		templateRoot);
	check(backend.runtimeTemplate(platform) == templateRoot,
		QStringLiteral("explicit runtime selection overrides bundled template"));
	const auto publishRoot =
		workDirectory + QStringLiteral("/publish");
	check(backend.publish(publishRoot, publishJson),
		QStringLiteral("publish product package"));
	const auto productRoot = publishRoot + QStringLiteral("/")
		+ QStringLiteral("products/")
		+ publishRootObject.value(QStringLiteral("id")).toString();
	const auto publishedPath =
		productRoot + QStringLiteral("/profile.json");
	const auto published = QJsonDocument::fromJson(
		backend.loadProfile(publishedPath).toUtf8()).object();
	check(!published.isEmpty(), QStringLiteral("published package reloads"));
	check(published.value(QStringLiteral("model")).toObject()
			.value(QStringLiteral("packagePath")).toString()
				== QLatin1String("model")
		&& published.value(QStringLiteral("model")).toObject()
			.value(QStringLiteral("pluginDirectory")).toString()
				== QLatin1String("../../bin/plugins")
		&& published.value(QStringLiteral("sources")).toArray()
			.first().toObject().value(QStringLiteral("directory")).toString()
				== QLatin1String("assets/camA"),
		QStringLiteral("published paths rewritten to in-package relative"));
	check(QFileInfo::exists(
			productRoot + QStringLiteral("/model/pkg/model.bin"))
		&& QFileInfo::exists(
			productRoot + QStringLiteral("/assets/camA/frame0.ppm"))
		&& QFileInfo::exists(publishRoot + QStringLiteral("/bin/")
			+ shellBinaryName),
		QStringLiteral("model/assets/runtime copied into package"));
#ifdef Q_OS_WIN
	check(QFileInfo::exists(publishRoot + QStringLiteral("/启动-")
			+ sanitizeExpected(publishRootObject) + QStringLiteral(".cmd")),
		QStringLiteral("windows entry script generated"));
#else
	check(QFileInfo::exists(publishRoot + QStringLiteral("/run-")
			+ sanitizeExpected(publishRootObject) + QStringLiteral(".sh")),
		QStringLiteral("linux entry script generated"));
#endif
	// A second publish of the same id into the same root must be refused.
	check(!backend.publish(publishRoot, publishJson),
		QStringLiteral("duplicate product id is refused"));
	QString manifestError;
	const auto releaseManifest = productRoot + QStringLiteral("/release-manifest.json");
	check(visionPackage::verifyManifest(releaseManifest, manifestError),
		QStringLiteral("package checksums verify"));
	{
		QFile file(productRoot + QStringLiteral("/model/pkg/model.bin"));
		Q_UNUSED(file.open(QIODevice::Append));
		file.write("corruption");
	}
	check(!visionPackage::verifyManifest(releaseManifest, manifestError),
		QStringLiteral("package checksum rejects changed model"));

	// The manifest lists NPU as Linux-only; publishing an NPU product for
	// Windows must be rejected by the platform check.
	auto npuObject = publishRootObject;
	auto npuModel = npuObject.value(QStringLiteral("model")).toObject();
	npuModel.insert(QStringLiteral("backendId"), QStringLiteral("openvino"));
	npuModel.insert(QStringLiteral("device"), QStringLiteral("NPU"));
	npuObject.insert(QStringLiteral("model"), npuModel);
	npuObject.insert(QStringLiteral("id"), QStringLiteral("npu-probe"));
	const auto npuJson = QString::fromUtf8(
		QJsonDocument(npuObject).toJson(QJsonDocument::Indented));
#ifdef Q_OS_WIN
	check(!backend.publish(
			workDirectory + QStringLiteral("/publish-npu"), npuJson),
		QStringLiteral("NPU device rejected on windows"));
#else
	check(backend.publish(
			workDirectory + QStringLiteral("/publish-npu"), npuJson),
		QStringLiteral("NPU device accepted on linux"));
#endif

	auto missingModelObject = publishRootObject;
	missingModelObject.insert(QStringLiteral("id"), QStringLiteral("missing-model"));
	auto missingModel = publishModel;
	missingModel.insert(QStringLiteral("packagePath"), QStringLiteral("missing-model"));
	missingModelObject.insert(QStringLiteral("model"), missingModel);
	check(!backend.publish(publishRoot, QString::fromUtf8(
		QJsonDocument(missingModelObject).toJson(QJsonDocument::Compact))),
		QStringLiteral("missing model refuses publication"));
	check(QFileInfo::exists(publishRoot
		+ QStringLiteral("/products/missing-model/publishing.lock"))
		&& !QFileInfo::exists(publishRoot
			+ QStringLiteral("/products/missing-model/profile.json")),
		QStringLiteral("failed product remains marked incomplete"));

	auto unsafeObject = publishRootObject;
	unsafeObject.insert(QStringLiteral("id"), QStringLiteral("../escape"));
	check(!backend.publish(publishRoot, QString::fromUtf8(
		QJsonDocument(unsafeObject).toJson(QJsonDocument::Compact))),
		QStringLiteral("unsafe product id refuses publication"));

	auto conflictObject = publishRootObject;
	conflictObject.insert(QStringLiteral("id"), QStringLiteral("runtime-conflict"));
	{
		QFile file(templateRoot + QLatin1Char('/') + shellBinaryName);
		check(file.open(QIODevice::WriteOnly) && file.write("different shell") == 15,
			QStringLiteral("create conflicting runtime fixture"));
	}
	check(!backend.publish(publishRoot, QString::fromUtf8(
		QJsonDocument(conflictObject).toJson(QJsonDocument::Compact))),
		QStringLiteral("different shared runtime refuses publication"));
	{
		QFile file(publishRoot + QStringLiteral("/bin/") + shellBinaryName);
		check(file.open(QIODevice::ReadOnly) && file.readAll() == "fake shell",
			QStringLiteral("runtime conflict preserves existing binary"));
	}

	// 5. Product binding and endpoint validation.
	check(!backend.loadProfile(profilePath).isEmpty(),
		QStringLiteral("restore working profile asset base"));
	{
		QFile metadata(templateRoot + QStringLiteral("/runtime-version.json"));
		check(metadata.open(QIODevice::WriteOnly) &&
			metadata.write(R"({"runtimeVersion":"1.0.0","productRuntime":"sample-business"})") > 0,
			QStringLiteral("create product runtime metadata"));
	}
	const auto productPublishRoot = workDirectory + QStringLiteral("/publish-business");
	check(backend.publish(productPublishRoot, publishJson),
		QStringLiteral("publish with product runtime template"));
	const auto productProfile = QJsonDocument::fromJson(backend.loadProfile(
		productPublishRoot + QStringLiteral("/products/") +
		publishRootObject.value(QStringLiteral("id")).toString() +
		QStringLiteral("/profile.json")).toUtf8()).object();
	check(productProfile.value(QStringLiteral("runtimeProduct")).toString() ==
		QStringLiteral("sample-business"),
		QStringLiteral("published profile retains compiled business runtime identity"));
	auto mismatchedProduct = publishRootObject;
	mismatchedProduct.insert(QStringLiteral("runtimeProduct"), QStringLiteral("another-business"));
	check(!backend.publish(workDirectory + QStringLiteral("/publish-wrong-business"),
		QString::fromUtf8(QJsonDocument(mismatchedProduct).toJson())),
		QStringLiteral("profile cannot silently switch to another business runtime"));

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

	if (previousTemplate.isValid()) {
		settings.setValue(settingsKey, previousTemplate);
	} else {
		settings.remove(settingsKey);
	}
	qInfo("SELFTEST %s (%d failure(s))",
		failures == 0 ? "DONE" : "FAILED", failures);
	return failures == 0 ? 0 : 1;
}

/// Loads the real main.qml offscreen to catch QML errors (bad properties,
/// missing imports) that the headless checks above cannot see. Returns true
/// when the root window component was created.
[[nodiscard]] bool checkMainQmlLoads(
	visionDesigner::DesignerBackend& backend,
	visionDesigner::PreviewClient& previewClient) {
	// offscreen platform: create the real component tree without a window.
	auto* imageProvider = new visionDesigner::PreviewImageProvider;
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty("serviceClient", &previewClient);
	engine.rootContext()->setContextProperty("designerBackend", &backend);
	engine.rootContext()->setContextProperty(
		QStringLiteral("startupProfile"), QString());
	engine.rootContext()->setContextProperty(
		QStringLiteral("startupPreview"), false);
	engine.addImageProvider(QStringLiteral("frames"), imageProvider);
	engine.load(QUrl(QStringLiteral("qrc:/VisionDesigner/qml/main.qml")));
	return !engine.rootObjects().isEmpty();
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
		// QPA offscreen so the QML-load check can build the component tree
		// without a display.
		if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
			qputenv("QT_QPA_PLATFORM", "offscreen");
		}
		const int result = runSelfTest(backend);
		visionDesigner::PreviewClient qmlPreview;
		const bool qmlOk = checkMainQmlLoads(backend, qmlPreview);
		qInfo("SELFTEST %s: %s", qmlOk ? "PASS" : "FAIL",
			"main.qml component loads");
		return (result == 0 && qmlOk) ? 0 : 1;
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
