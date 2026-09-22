#include "previewClient.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QtMath>

namespace visionDesigner {

namespace {

[[nodiscard]] QString compactJson(const QJsonObject& object) {
	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

PreviewClient::PreviewClient(QObject* parent)
	: QObject(parent) {
	frameTimer_.setInterval(100);
	statsTimer_.setInterval(500);
	connect(&frameTimer_, &QTimer::timeout, this, [this] {
		if (!running_) {
			return;
		}
		++tick_;
		for (int index = 0; index < sources_.size(); ++index) {
			emit frameReceived(static_cast<quint32>(index),
				nextImage(index));
			++completed_;
		}
	});
	connect(&statsTimer_, &QTimer::timeout, this, [this] { publishLive(); });
	frameTimer_.start();
	statsTimer_.start();
	publishState();
}

QVariantList PreviewClient::parameters() const { return parameters_; }
QVariantList PreviewClient::commands() const { return commands_; }
QVariantList PreviewClient::states() const { return states_; }
QVariantList PreviewClient::streams() const { return streams_; }
QString PreviewClient::lastError() const { return lastError_; }
QString PreviewClient::uiJson() const { return uiJson_; }

void PreviewClient::setProfile(
	const QString& profileJson, const QString& baseDirectory) {
	running_ = false;
	completed_ = 0;
	failed_ = 0;
	tick_ = 0;
	parameters_.clear();
	commands_.clear();
	states_.clear();
	streams_.clear();
	streamIds_.clear();
	sources_.clear();
	parameterValues_.clear();

	const auto document = QJsonDocument::fromJson(profileJson.toUtf8());
	const auto root = document.object();

	uiJson_ = QString::fromUtf8(QJsonDocument(
		root.value(QStringLiteral("ui")).toObject())
			.toJson(QJsonDocument::Compact));

	commands_.append(QVariantMap{
		{QStringLiteral("name"), QStringLiteral("session.start")},
		{QStringLiteral("description"),
			QStringLiteral("start the detection session")}});
	commands_.append(QVariantMap{
		{QStringLiteral("name"), QStringLiteral("session.stop")},
		{QStringLiteral("description"),
			QStringLiteral("request a graceful stop of the running session")}});
	states_.append(QVariantMap{
		{QStringLiteral("name"), QStringLiteral("session.state")},
		{QStringLiteral("description"),
			QStringLiteral("session lifecycle state")}});
	states_.append(QVariantMap{
		{QStringLiteral("name"), QStringLiteral("session.summary")},
		{QStringLiteral("description"),
			QStringLiteral("session counters and per-source statistics")}});
	states_.append(QVariantMap{
		{QStringLiteral("name"), QStringLiteral("metrics.performance")},
		{QStringLiteral("description"),
			QStringLiteral("last batch performance counters")}});

	const QDir base(baseDirectory);
	const auto sources = root.value(QStringLiteral("sources")).toArray();
	int index = 0;
	for (const auto& sourceValue : sources) {
		const auto source = sourceValue.toObject();
		const auto id = source.value(QStringLiteral("id")).toString();

		streams_.append(QVariantMap{
			{QStringLiteral("name"), QStringLiteral("stream.%1").arg(id)},
			{QStringLiteral("sourceId"), index},
			{QStringLiteral("description"),
				QStringLiteral("live frames of source %1").arg(id)}});
		streamIds_.insert(QStringLiteral("stream.%1").arg(id), index);

		parameters_.append(QVariantMap{
			{QStringLiteral("name"),
				QStringLiteral("camera.%1.exposureMicroseconds").arg(id)},
			{QStringLiteral("description"),
				QStringLiteral("camera exposure in microseconds (%1, "
					"simulated)").arg(id)},
			{QStringLiteral("type"), QStringLiteral("decimal")},
			{QStringLiteral("writable"), true},
			{QStringLiteral("minimum"), 0.0},
			{QStringLiteral("maximum"), 1000000.0}});
		parameterValues_.insert(
			QStringLiteral("camera.%1.exposureMicroseconds").arg(id),
			source.value(QStringLiteral("exposureMicroseconds"))
				.toDouble(10000.0));
		parameters_.append(QVariantMap{
			{QStringLiteral("name"),
				QStringLiteral("camera.%1.gain").arg(id)},
			{QStringLiteral("description"),
				QStringLiteral("camera gain (%1, simulated)").arg(id)},
			{QStringLiteral("type"), QStringLiteral("decimal")},
			{QStringLiteral("writable"), true},
			{QStringLiteral("minimum"), 0.0},
			{QStringLiteral("maximum"), 100.0}});
		parameterValues_.insert(QStringLiteral("camera.%1.gain").arg(id),
			source.value(QStringLiteral("gain")).toDouble(10.0));
		commands_.append(QVariantMap{
			{QStringLiteral("name"),
				QStringLiteral("camera.%1.softwareTrigger").arg(id)},
			{QStringLiteral("description"),
				QStringLiteral("software trigger (%1, simulated)").arg(id)}});

		SimulatedSource simulated;
		simulated.id = id;
		if (source.value(QStringLiteral("type")).toString()
				== QLatin1String("directory")) {
			const auto directory = QDir(base.absoluteFilePath(
				source.value(QStringLiteral("directory")).toString()));
			QStringList filters;
			for (const auto& extension : source
					.value(QStringLiteral("extensions")).toArray()) {
				filters << QStringLiteral("*%1").arg(extension.toString());
			}
			if (filters.isEmpty()) {
				filters = QStringList{
					QStringLiteral("*.bmp"), QStringLiteral("*.png"),
					QStringLiteral("*.jpg"), QStringLiteral("*.ppm")};
			}
			const auto entries = directory.entryList(
				filters, QDir::Files, QDir::Name);
			for (const auto& entry : entries) {
				simulated.imageFiles.append(
					directory.absoluteFilePath(entry));
			}
		}
		sources_.append(std::move(simulated));
		++index;
	}

	setLastError(QString());
	publishState();
	publishLive();
	emit registryChanged();
}

QVariant PreviewClient::readParameter(const QString& name) {
	return parameterValues_.value(name);
}

bool PreviewClient::writeParameter(const QString& name, const QVariant& value) {
	if (!parameterValues_.contains(name)) {
		setLastError(QStringLiteral("unknown parameter: %1").arg(name));
		return false;
	}
	parameterValues_.insert(name, value);
	setLastError(QString());
	return true;
}

bool PreviewClient::invokeCommand(const QString& name) {
	if (name == QLatin1String("session.start")) {
		running_ = true;
		publishState();
		return true;
	}
	if (name == QLatin1String("session.stop")) {
		running_ = false;
		publishState();
		publishLive();
		return true;
	}
	if (name.endsWith(QLatin1String(".softwareTrigger"))) {
		// Simulate one frame per bound source for the triggered camera.
		const auto sourceId = streamIds_.value(
			QStringLiteral("stream.%1")
				.arg(name.section(QLatin1Char('.'), 1, 1)), -1);
		if (sourceId >= 0) {
			emit frameReceived(static_cast<quint32>(sourceId),
				nextImage(sourceId));
			++completed_;
		}
		return true;
	}
	setLastError(QStringLiteral("unknown command: %1").arg(name));
	return false;
}

QString PreviewClient::stateSnapshot(const QString& name) {
	return snapshots_.value(name);
}

int PreviewClient::streamSourceId(const QString& name) const {
	return streamIds_.value(name, -1);
}

void PreviewClient::publishState() {
	const auto payload = compactJson({
		{QStringLiteral("state"),
			running_ ? QStringLiteral("running") : QStringLiteral("idle")},
	});
	snapshots_.insert(QStringLiteral("session.state"), payload);
	emit stateChanged(QStringLiteral("session.state"), payload);
}

void PreviewClient::publishLive() {
	const auto summary = compactJson({
		{QStringLiteral("completed"), static_cast<double>(completed_)},
		{QStringLiteral("failed"), static_cast<double>(failed_)},
		{QStringLiteral("dropped"), 0.0},
	});
	snapshots_.insert(QStringLiteral("session.summary"), summary);
	emit stateChanged(QStringLiteral("session.summary"), summary);

	const double fps = running_
		? 8.0 + 4.0 * std::sin(static_cast<double>(tick_) / 10.0)
		: 0.0;
	const auto performance = compactJson({
		{QStringLiteral("framesPerSecond"), fps},
		{QStringLiteral("frames"), static_cast<double>(completed_)},
		{QStringLiteral("p50Milliseconds"), 3.4},
		{QStringLiteral("p95Milliseconds"), 5.1},
		{QStringLiteral("p99Milliseconds"), 8.2},
	});
	snapshots_.insert(QStringLiteral("metrics.performance"), performance);
	emit stateChanged(QStringLiteral("metrics.performance"), performance);
}

QImage PreviewClient::nextImage(int sourceIndex) {
	auto& source = sources_[sourceIndex];
	while (!source.imageFiles.isEmpty()) {
		const auto path = source.imageFiles.at(
			source.nextFile % source.imageFiles.size());
		++source.nextFile;
		QImage image(path);
		if (!image.isNull()) {
			return image;
		}
	}
	return patternImage(sourceIndex);
}

QImage PreviewClient::patternImage(int sourceIndex) {
	QImage image(640, 480, QImage::Format_RGB32);
	image.fill(QColor::fromHsv((tick_ * 5 + sourceIndex * 90) % 360,
		120, 90));
	QPainter painter(&image);
	painter.setPen(Qt::white);
	auto font = painter.font();
	font.setPixelSize(32);
	painter.setFont(font);
	painter.drawText(image.rect(), Qt::AlignCenter,
		QStringLiteral("%1\nframe %2")
			.arg(sources_.at(sourceIndex).id)
			.arg(tick_));
	return image;
}

void PreviewClient::setLastError(const QString& message) {
	if (lastError_ == message) {
		return;
	}
	lastError_ = message;
	emit lastErrorChanged();
}

} // namespace visionDesigner
