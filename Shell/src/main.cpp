#include "serviceClient.hpp"
#include "frameImageProvider.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <QHash>
#include <memory>

int main(int argc, char* argv[]) {
	QGuiApplication app(argc, argv);
	QCoreApplication::setApplicationName("visionShell");
	QCoreApplication::setApplicationVersion("1.0.0");

	QCommandLineParser parser;
	parser.setApplicationDescription(
		"Vision Shell - generic QML host for visionService profiles");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument("profile", "Path to the product profile JSON");
	const QCommandLineOption smokeOption({"s", "smoke"},
		"Auto-start the session, count stream frames for <seconds>, "
		"print statistics and exit (acceptance harness).", "seconds");
	parser.addOption(smokeOption);
	parser.process(app);

	if (parser.positionalArguments().isEmpty()) {
		qWarning() << "Usage: visionShell [--smoke <seconds>] <profile.json>";
		return 1;
	}
	const auto profilePath = parser.positionalArguments().first();

	visionShell::ServiceClient serviceClient;
	if (!serviceClient.loadProfile(profilePath)) {
		qWarning() << "Failed to load profile:" << serviceClient.lastError();
		return 1;
	}

	// addImageProvider() transfers ownership to the engine, which deletes the
	// provider at teardown — it must be heap-allocated.
	auto* imageProvider = new visionShell::FrameImageProvider;

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty("serviceClient", &serviceClient);
	engine.addImageProvider("frames", imageProvider);

	QObject::connect(&serviceClient, &visionShell::ServiceClient::frameReceived,
		imageProvider, [imageProvider](quint32 sourceId, const QImage& image) {
			imageProvider->updateFrame(sourceId, image);
		});

	const QUrl url(QStringLiteral("qrc:/VisionShell/qml/main.qml"));
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
		&app, [url](QObject* obj, const QUrl& objUrl) {
			if (!obj && url == objUrl) {
				QCoreApplication::exit(-1);
			}
		}, Qt::QueuedConnection);
	engine.load(url);

	if (parser.isSet(smokeOption)) {
		const auto seconds = parser.value(smokeOption).toInt();
		auto counters = std::make_shared<QHash<quint32, int>>();
		QObject::connect(&serviceClient,
			&visionShell::ServiceClient::frameReceived,
			&app, [counters](quint32 sourceId, const QImage&) {
				(*counters)[sourceId] += 1;
			});
		if (!serviceClient.invokeCommand("session.start")) {
			qWarning() << "SMOKE start failed:" << serviceClient.lastError();
			return 2;
		}
		// Sample live statistics mid-run to verify the periodic publish path.
		QTimer::singleShot(seconds * 500, &app, [&serviceClient] {
			qInfo().noquote() << "SMOKE mid-run summary:"
				<< serviceClient.stateSnapshot("session.summary");
			qInfo().noquote() << "SMOKE mid-run performance:"
				<< serviceClient.stateSnapshot("metrics.performance");
		});
		QTimer::singleShot(seconds * 1000, &app, [&serviceClient, counters, seconds] {
			serviceClient.invokeCommand("session.stop");
			int total = 0;
			QStringList perStream;
			for (auto it = counters->constBegin(); it != counters->constEnd(); ++it) {
				total += it.value();
				perStream << QString("source%1=%2").arg(it.key()).arg(it.value());
			}
			qInfo().noquote() << QString(
				"SMOKE totalFrames=%1 seconds=%2 fps=%3 streams=[%4]")
				.arg(total).arg(seconds)
				.arg(seconds > 0 ? double(total) / seconds : 0.0, 0, 'f', 1)
				.arg(perStream.join(' '));
			// Final summary/performance replace the live payloads when the batch
			// finishes (live caches clear on publishSummary/batchObserver). The
			// state endpoint flips to idle only after the control thread's wait()
			// completes; poll a bit longer so the printed values are the finals.
			QString summary;
			QString performance;
			for (int attempt = 0; attempt < 200; ++attempt) {
				const auto state = serviceClient.stateSnapshot("session.state");
				if (state.contains("\"idle\"")) {
					QThread::msleep(700); // live publish period + margin
					summary = serviceClient.stateSnapshot("session.summary");
					performance = serviceClient.stateSnapshot("metrics.performance");
					break;
				}
				QThread::msleep(100);
			}
			qInfo().noquote() << "SMOKE summary:" << summary;
			qInfo().noquote() << "SMOKE performance:" << performance;
			QCoreApplication::exit(total > 0 ? 0 : 2);
		});
	}

	return app.exec();
}
