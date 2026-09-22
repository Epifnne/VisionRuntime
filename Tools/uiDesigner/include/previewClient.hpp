/**
 * @file previewClient.hpp
 * @brief Simulated service client for the visionDesigner preview mode.
 *
 * Implements the same context-property API surface that Shell/controls bind
 * against (the visionShell ServiceClient), driven by a fake data generator:
 * counters increment, performance follows a sine curve, directory sources
 * carousel through image files (or a generated pattern when no files exist).
 */

#pragma once

#include <QObject>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <cstdint>

namespace visionDesigner {

class PreviewClient : public QObject {
	Q_OBJECT
	Q_PROPERTY(QVariantList parameters READ parameters NOTIFY registryChanged)
	Q_PROPERTY(QVariantList commands READ commands NOTIFY registryChanged)
	Q_PROPERTY(QVariantList states READ states NOTIFY registryChanged)
	Q_PROPERTY(QVariantList streams READ streams NOTIFY registryChanged)
	Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
	Q_PROPERTY(QString uiJson READ uiJson NOTIFY registryChanged)

public:
	explicit PreviewClient(QObject* parent = nullptr);

	[[nodiscard]] QVariantList parameters() const;
	[[nodiscard]] QVariantList commands() const;
	[[nodiscard]] QVariantList states() const;
	[[nodiscard]] QVariantList streams() const;
	[[nodiscard]] QString lastError() const;
	[[nodiscard]] QString uiJson() const;

	/// Loads a profile for preview; baseDirectory resolves relative
	/// directory-source paths (typically the profile file's directory).
	Q_INVOKABLE void setProfile(
		const QString& profileJson, const QString& baseDirectory);
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
	struct SimulatedSource {
		QString id;
		QStringList imageFiles;
		int nextFile = 0;
	};

	void publishState();
	void publishLive();
	[[nodiscard]] QImage nextImage(int sourceIndex);
	[[nodiscard]] QImage patternImage(int sourceIndex);
	void setLastError(const QString& message);

	QVariantList parameters_;
	QVariantList commands_;
	QVariantList states_;
	QVariantList streams_;
	QHash<QString, int> streamIds_;
	QVector<SimulatedSource> sources_;
	QHash<QString, QVariant> parameterValues_;
	QHash<QString, QString> snapshots_;
	QString uiJson_ = QStringLiteral("{}");
	QString lastError_;
	bool running_ = false;
	qint64 completed_ = 0;
	qint64 failed_ = 0;
	int tick_ = 0;
	QTimer frameTimer_;
	QTimer statsTimer_;
};

} // namespace visionDesigner
