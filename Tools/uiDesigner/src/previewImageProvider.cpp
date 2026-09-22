#include "previewImageProvider.hpp"

namespace visionDesigner {

PreviewImageProvider::PreviewImageProvider()
	: QQuickImageProvider(QQuickImageProvider::Image) {}

QImage PreviewImageProvider::requestImage(
	const QString& id, QSize* size, const QSize& requestedSize) {
	QMutexLocker locker(&mutex_);
	const auto sourceId = static_cast<quint32>(id.toUInt());
	const auto it = frames_.constFind(sourceId);
	if (it == frames_.constEnd()) {
		return {};
	}
	if (size) {
		*size = it.value().size();
	}
	if (requestedSize.isValid() && requestedSize != it.value().size()) {
		return it.value().scaled(requestedSize,
			Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
	return it.value();
}

void PreviewImageProvider::updateFrame(quint32 sourceId, const QImage& image) {
	QMutexLocker locker(&mutex_);
	frames_[sourceId] = image;
}

} // namespace visionDesigner
