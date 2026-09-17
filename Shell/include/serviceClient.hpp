#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <memory>
#include <cstdint>
#include <functional>

namespace visionService {
class VisionService;
}

namespace visionShell {

class ServiceClient : public QObject {
	Q_OBJECT
	Q_PROPERTY(QVariantList parameters READ parameters NOTIFY registryChanged)
	Q_PROPERTY(QVariantList commands READ commands NOTIFY registryChanged)
	Q_PROPERTY(QVariantList states READ states NOTIFY registryChanged)
	Q_PROPERTY(QVariantList streams READ streams NOTIFY registryChanged)
	Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
	Q_PROPERTY(QString uiJson READ uiJson NOTIFY registryChanged)

public:
	explicit ServiceClient(QObject* parent = nullptr);
	~ServiceClient() override;

	[[nodiscard]] QVariantList parameters() const;
	[[nodiscard]] QVariantList commands() const;
	[[nodiscard]] QVariantList states() const;
	[[nodiscard]] QVariantList streams() const;
	[[nodiscard]] QString lastError() const;
	[[nodiscard]] QString uiJson() const;

	Q_INVOKABLE bool loadProfile(const QString& path);
	Q_INVOKABLE QVariant readParameter(const QString& name);
	Q_INVOKABLE bool writeParameter(const QString& name, const QVariant& value);
	Q_INVOKABLE bool invokeCommand(const QString& name);
	Q_INVOKABLE QString stateSnapshot(const QString& name);
	Q_INVOKABLE int streamSourceId(const QString& name) const;

signals:
	void registryChanged();
	void lastErrorChanged();
	void stateChanged(const QString& name, const QString& payload);
	void frameReceived(quint32 sourceId, const QImage& image);

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace visionShell
