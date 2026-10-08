#include "serviceClient.hpp"

#include "service/visionService.hpp"
#include "endpoints/endpointRegistry.hpp"
#include "endpoints/endpointTypes.hpp"
#include "vision/frame.hpp"
#include "vision/frameSpec.hpp"

#include <QImage>
#include <QDebug>
#include <QMutex>
#include <variant>
#include <algorithm>

namespace visionShell {

namespace {

// GUI snapshots must not retain camera SDK buffer leases across recovery.
[[nodiscard]] QImage copyPixels(
	const visionRuntime::vision::Frame& frame, QImage::Format format) {
	return QImage(static_cast<const uchar*>(frame.data()),
		static_cast<int>(frame.width()), static_cast<int>(frame.height()),
		static_cast<int>(frame.rowStride()), format).copy();
}

QImage convertFrame(const visionRuntime::vision::Frame& frame) {
	using namespace visionRuntime::vision;
	const auto width = static_cast<int>(frame.width());
	const auto height = static_cast<int>(frame.height());
	const auto* data = static_cast<const uchar*>(frame.data());
	if (!data || width <= 0 || height <= 0) return {};

	switch (frame.pixelFormat()) {
	case PixelFormat::Gray8:
		return copyPixels(frame, QImage::Format_Grayscale8);
	case PixelFormat::Gray16:
		return copyPixels(frame, QImage::Format_Grayscale16);
	case PixelFormat::Bgr8:
		return copyPixels(frame, QImage::Format_BGR888);
	case PixelFormat::Rgb8:
		return copyPixels(frame, QImage::Format_RGB888);
	case PixelFormat::Bgra8:
		return copyPixels(frame, QImage::Format_ARGB32);
	case PixelFormat::Rgba8:
		return copyPixels(frame, QImage::Format_RGBA8888);
	case PixelFormat::Float32Gray: {
		// Convert float [0,1] or arbitrary to grayscale8
		const auto* src = static_cast<const float*>(frame.data());
		QImage out(width, height, QImage::Format_Grayscale8);
		for (int y = 0; y < height; ++y) {
			const float* row = reinterpret_cast<const float*>(
				static_cast<const uchar*>(frame.data()) + y * frame.rowStride());
			uchar* dst = out.scanLine(y);
			for (int x = 0; x < width; ++x) {
				float v = row[x];
				dst[x] = static_cast<uchar>(std::clamp(v * 255.0f, 0.0f, 255.0f));
			}
		}
		return out;
	}
	}
	return {};
}

} // namespace

class ServiceClient::Impl {
public:
	explicit Impl(visionService::ProductRuntimeFactory factory = {})
		: service(std::move(factory)) {}
	visionService::VisionService service;
	QString lastError;
	QHash<quint32, QImage> latestFrames;
	QMutex framesMutex;
	std::vector<std::uint64_t> subscriptions;
};

ServiceClient::ServiceClient(QObject* parent)
	: QObject(parent), impl_(std::make_unique<Impl>()) {}

ServiceClient::ServiceClient(visionService::ProductRuntimeFactory factory, QObject* parent)
	: QObject(parent), impl_(std::make_unique<Impl>(std::move(factory))) {}

ServiceClient::~ServiceClient() {
	impl_->service.shutdown();
}

QString ServiceClient::exportManifest() const {
	return QString::fromStdString(impl_->service.exportManifest());
}

QVariantList ServiceClient::parameters() const {
	QVariantList result;
	for (const auto& descriptor : impl_->service.endpoints().parameters()) {
		QVariantMap map;
		map["name"] = QString::fromStdString(descriptor.name);
		map["description"] = QString::fromStdString(descriptor.description);
		map["type"] = QString::fromStdString(
			std::string(visionService::endpoints::parameterTypeName(descriptor.type)));
		map["writable"] = descriptor.writable;
		map["accessLevel"] = static_cast<int>(descriptor.accessLevel);
		if (descriptor.minimum) map["minimum"] = *descriptor.minimum;
		if (descriptor.maximum) map["maximum"] = *descriptor.maximum;
		result.append(map);
	}
	return result;
}

QVariantList ServiceClient::commands() const {
	QVariantList result;
	for (const auto& descriptor : impl_->service.endpoints().commands()) {
		QVariantMap map;
		map["name"] = QString::fromStdString(descriptor.name);
		map["description"] = QString::fromStdString(descriptor.description);
		map["accessLevel"] = static_cast<int>(descriptor.accessLevel);
		result.append(map);
	}
	return result;
}

QVariantList ServiceClient::states() const {
	QVariantList result;
	for (const auto& descriptor : impl_->service.endpoints().states()) {
		QVariantMap map;
		map["name"] = QString::fromStdString(descriptor.name);
		map["description"] = QString::fromStdString(descriptor.description);
		map["accessLevel"] = static_cast<int>(descriptor.accessLevel);
		result.append(map);
	}
	return result;
}

QVariantList ServiceClient::streams() const {
	QVariantList result;
	for (const auto& descriptor : impl_->service.endpoints().streams()) {
		QVariantMap map;
		map["name"] = QString::fromStdString(descriptor.name);
		map["description"] = QString::fromStdString(descriptor.description);
		map["accessLevel"] = static_cast<int>(descriptor.accessLevel);
		map["sourceId"] = static_cast<quint32>(descriptor.sourceId);
		result.append(map);
	}
	return result;
}

QString ServiceClient::lastError() const {
	return impl_->lastError;
}

QString ServiceClient::uiJson() const {
	return QString::fromStdString(impl_->service.profile().uiJson);
}

bool ServiceClient::loadProfile(const QString& path) {
	impl_->service.shutdown();
	for (const auto id : impl_->subscriptions) {
		impl_->service.endpoints().unsubscribe(id);
	}
	impl_->subscriptions.clear();

	auto result = impl_->service.loadProfile(path.toStdString());
	if (!result) {
		impl_->lastError = QString::fromStdString(result.status().toString());
		emit lastErrorChanged();
		return false;
	}
	impl_->lastError.clear();
	emit lastErrorChanged();

	return subscribeEndpoints();
}

bool ServiceClient::subscribeEndpoints() {
	impl_->subscriptions.clear();
	auto& registry = impl_->service.endpoints();
	auto failed = [this, &registry](const visionRuntime::core::Status& status) {
		for (const auto token : impl_->subscriptions) {
			registry.unsubscribe(token);
		}
		impl_->subscriptions.clear();
		impl_->lastError = QString::fromStdString(status.toString());
		qWarning().noquote() << impl_->lastError;
		emit lastErrorChanged();
		return false;
	};
	for (const auto& descriptor : registry.states()) {
		auto name = QString::fromStdString(descriptor.name);
		auto callback = [this, name](
			const visionService::endpoints::StateSnapshot& snapshot) {
			emit stateChanged(name, QString::fromStdString(snapshot.payload));
		};
		auto subscribed = registry.subscribeState(descriptor.name, callback);
		if (!subscribed) {
			return failed(subscribed.status());
		}
		impl_->subscriptions.push_back(subscribed.value());
	}
	for (const auto& descriptor : registry.streams()) {
		auto callback = [this](
			std::uint32_t sid, const visionRuntime::vision::Frame& frame) {
			QImage image = convertFrame(frame);
			if (!image.isNull()) {
				emit frameReceived(sid, image);
			}
		};
		auto subscribed = registry.subscribeStream(descriptor.name, callback);
		if (!subscribed) {
			return failed(subscribed.status());
		}
		impl_->subscriptions.push_back(subscribed.value());
	}

	emit registryChanged();
	return true;
}

QVariant ServiceClient::readParameter(const QString& name) {
	auto result = impl_->service.endpoints().readParameter(name.toStdString());
	if (!result) {
		return {};
	}
	return std::visit([](const auto& value) -> QVariant {
		using T = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<T, bool>) return value;
		else if constexpr (std::is_same_v<T, double>) return value;
		else if constexpr (std::is_same_v<T, std::int64_t>) return QVariant::fromValue(value);
		else return QString::fromStdString(value);
	}, result.value());
}

bool ServiceClient::writeParameter(const QString& name, const QVariant& value) {
	using namespace visionService::endpoints;
	ParameterValue parameterValue;
	switch (value.typeId()) {
	case QMetaType::Bool: parameterValue = value.toBool(); break;
	case QMetaType::Double: parameterValue = value.toDouble(); break;
	case QMetaType::Int:
	case QMetaType::LongLong: parameterValue = static_cast<std::int64_t>(value.toLongLong()); break;
	default: parameterValue = value.toString().toStdString(); break;
	}
	auto result = impl_->service.endpoints().writeParameter(
		name.toStdString(), parameterValue);
	if (!result) {
		impl_->lastError = QString::fromStdString(result.status().toString());
		emit lastErrorChanged();
		return false;
	}
	return true;
}

bool ServiceClient::invokeCommand(const QString& name) {
	auto result = impl_->service.endpoints().invokeCommand(name.toStdString());
	if (!result) {
		impl_->lastError = QString::fromStdString(result.status().toString());
		emit lastErrorChanged();
		return false;
	}
	impl_->lastError.clear();
	emit lastErrorChanged();
	if (name == QStringLiteral("session.recover")) {
		return subscribeEndpoints();
	}
	return true;
}

QString ServiceClient::stateSnapshot(const QString& name) {
	auto result = impl_->service.endpoints().stateSnapshot(name.toStdString());
	if (!result) {
		return {};
	}
	return QString::fromStdString(result.value().payload);
}

int ServiceClient::streamSourceId(const QString& name) const {
	const auto wanted = name.toStdString();
	for (const auto& descriptor : impl_->service.endpoints().streams()) {
		if (descriptor.name == wanted) {
			return static_cast<int>(descriptor.sourceId);
		}
	}
	return -1;
}

} // namespace visionShell
