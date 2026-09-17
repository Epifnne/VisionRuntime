#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QHash>
#include <QMutex>
#include <cstdint>

namespace visionShell {

class FrameImageProvider : public QQuickImageProvider {
public:
	FrameImageProvider();

	[[nodiscard]] QImage requestImage(
		const QString& id, QSize* size, const QSize& requestedSize) override;

	void updateFrame(quint32 sourceId, const QImage& image);

private:
	QHash<quint32, QImage> frames_;
	QMutex mutex_;
};

} // namespace visionShell
