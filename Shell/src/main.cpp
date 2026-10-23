#include "serviceClient.hpp"
#include "frameImageProvider.hpp"
#include "releaseManifest.hpp"
#ifdef VISION_BUSINESS_APPLICATION
#include "exports/productEntry.hpp"
#include "profile/profileLoader.hpp"
#endif
#ifdef VISION_SAMPLE_SHELL
#include "productShell.hpp"
#endif

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <QHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <memory>

int main(int argc, char* argv[]) {
	QGuiApplication app(argc, argv);
	QCoreApplication::setApplicationName("visionShell");
	QCoreApplication::setApplicationVersion(VISION_RUNTIME_VERSION);

	QCommandLineParser parser;
	parser.setApplicationDescription(
		"Vision Shell - generic QML host for visionService profiles");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument("profile", "Path to the product profile JSON");
	const QCommandLineOption smokeOption({"s", "smoke"},
		"Run detection for <seconds> and fail unless valid inspections complete "
		"without acquisition or execution errors (acceptance harness).", "seconds");
	parser.addOption(smokeOption);
	const QCommandLineOption checkOption(QStringLiteral("check"),
		"Validate package checksums, profile, model, plugin and camera setup without starting detection.");
	parser.addOption(checkOption);
	const QCommandLineOption sealOption(QStringLiteral("seal-package"),
		"Write package checksums without loading a runtime or opening cameras.", "root");
	parser.addOption(sealOption);
	const QCommandLineOption exportOption(QStringLiteral("export-manifest"),
		"Export this product's actual endpoint manifest without starting acquisition.", "output");
	parser.addOption(exportOption);
	parser.process(app);

	if (parser.positionalArguments().isEmpty()) {
		qWarning() << "Usage: visionShell [--smoke <seconds>] <profile.json>";
		return 1;
	}
	const auto profilePath = parser.positionalArguments().first();
	if (parser.isSet(sealOption)) {
		const auto productId = QFileInfo(profilePath).absoluteDir().dirName();
		const QDir root(parser.value(sealOption));
		const auto expectedProfile = root.filePath(
			QStringLiteral("products/%1/profile.json").arg(productId));
		if (!visionPackage::safeId(productId) ||
			QFileInfo(expectedProfile).canonicalFilePath().isEmpty() ||
			QFileInfo(expectedProfile).canonicalFilePath() != QFileInfo(profilePath).canonicalFilePath()) {
			qCritical() << "Seal requires an existing products/<id>/profile.json inside the package root.";
			return 3;
		}
		QString error;
#ifdef Q_OS_WIN
		const auto platform = QStringLiteral("windows");
#else
		const auto platform = QStringLiteral("linux");
#endif
		if (!visionPackage::writeManifest(root.absolutePath(), productId, platform, error)) {
			qCritical().noquote() << error;
			return 3;
		}
		return 0;
	}
	if (QFileInfo::exists(QFileInfo(profilePath).absoluteDir()
		.filePath(QStringLiteral("publishing.lock")))) {
		qCritical() << "Package assembly did not finish; republish the product before running it.";
		return 3;
	}
	const auto manifestPath = QFileInfo(profilePath).absoluteDir()
		.filePath(QStringLiteral("release-manifest.json"));
	if (QFileInfo::exists(manifestPath)) {
		QString error;
		if (!visionPackage::verifyManifest(manifestPath, error)) {
			qCritical().noquote() << error;
			return 3;
		}
	}

#ifdef VISION_BUSINESS_APPLICATION
	if (parser.isSet(exportOption)) {
		auto profile = visionService::profile::ProfileLoader::load(profilePath.toStdString());
		if (!profile) {
			qCritical().noquote() << QString::fromStdString(profile.status().toString());
			return 3;
		}
		auto manifest = visionProduct::describeApplication(profile.value());
		if (!manifest) {
			qCritical().noquote() << QString::fromStdString(manifest.status().toString());
			return 3;
		}
		QSaveFile output(parser.value(exportOption));
		const auto data = QByteArray::fromStdString(manifest.value());
		if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() ||
			!output.commit()) {
			qCritical() << "Cannot write endpoint manifest:" << output.errorString();
			return 3;
		}
		return 0;
	}
	visionShell::ServiceClient serviceClient(visionProduct::createApplication);
#elif defined(VISION_SAMPLE_SHELL)
	visionShell::ServiceClient serviceClient(visionSample::createProductRuntime);
#else
	visionShell::ServiceClient serviceClient;
#endif
	if (!serviceClient.loadProfile(profilePath)) {
		qWarning() << "Failed to load profile:" << serviceClient.lastError();
		return 1;
	}
	if (parser.isSet(exportOption)) {
		QSaveFile output(parser.value(exportOption));
		const auto data = serviceClient.exportManifest().toUtf8();
		if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() ||
			!output.commit()) {
			qCritical() << "Cannot write endpoint manifest:" << output.errorString();
			return 3;
		}
		return 0;
	}
	if (parser.isSet(checkOption)) {
		qInfo() << (QFileInfo::exists(manifestPath)
			? "Package checksums and profile preflight passed; detection was not started."
			: "Profile preflight passed; no release manifest is present. Detection was not started.");
		return 0;
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
		bool secondsValid = false;
		const auto seconds = parser.value(smokeOption).toInt(&secondsValid);
		if (!secondsValid || seconds < 1 || seconds > 86400) {
			qCritical() << "SMOKE seconds must be an integer between 1 and 86400.";
			return 2;
		}
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
			const bool stopped = serviceClient.invokeCommand("session.stop");
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
			QJsonParseError parseError;
			const auto document = QJsonDocument::fromJson(summary.toUtf8(), &parseError);
			bool passed = stopped && total > 0 &&
				parseError.error == QJsonParseError::NoError && document.isObject();
			const auto payload = document.object();
			const auto completed = payload.value(QStringLiteral("completed"));
			const auto failed = payload.value(QStringLiteral("failed"));
			passed = passed && completed.isDouble() && failed.isDouble() &&
				completed.toDouble() > failed.toDouble();
			for (const auto* field : {"failed", "sourceFailures", "missingFrames",
				"rejected", "expired", "dropped"}) {
				const auto value = payload.value(QLatin1String(field));
				passed = passed && value.isDouble() && value.toDouble() == 0;
			}
			if (!passed) {
				qCritical() << "SMOKE acceptance failed: require final counters, valid "
					"inspections and no failed, missing, rejected, expired or dropped frames.";
			}
			QCoreApplication::exit(passed ? 0 : 2);
		});
	}

	return app.exec();
}
