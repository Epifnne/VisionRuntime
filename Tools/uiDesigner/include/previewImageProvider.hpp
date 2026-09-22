/**
 * @file previewImageProvider.hpp
 * @brief QQuickImageProvider holding the latest simulated frame per source,
 * served to Shell's ImageView control ("image://frames/<sourceId>").
 */

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <cstdint>

namespace visionDesigner {

class PreviewImageProvider : public QQuickImageProvider {
public:
	PreviewImageProvider();

	[[nodiscard]] QImage requestImage(
		const QString& id, QSize* size, const QSize& requestedSize) override;

	void updateFrame(quint32 sourceId, const QImage& image);

private:
	QHash<quint32, QImage> frames_;
	QMutex mutex_;
};

} // namespace visionDesigner
