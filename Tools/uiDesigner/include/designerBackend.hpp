/**
 * @file designerBackend.hpp
 * @brief Non-UI services for visionDesigner: template instantiation, profile
 * load/save, endpoint universe derivation (mirroring visionService naming),
 * schema validation and product package publishing.
 *
 * The designer deliberately does not link Runtime/Service; all profile and
 * manifest handling here is plain JSON via QtCore.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace visionDesigner {

class DesignerBackend : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
	Q_PROPERTY(QString fileDirectory READ fileDirectory NOTIFY filePathChanged)
	Q_PROPERTY(QString controlsPath READ controlsPath WRITE setControlsPath
		NOTIFY controlsPathChanged)
	Q_PROPERTY(QVariantList endpoints READ endpoints NOTIFY endpointsChanged)
	Q_PROPERTY(QVariantList issues READ issues NOTIFY issuesChanged)
	Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY
		statusMessageChanged)
	Q_PROPERTY(QStringList templateIds READ templateIds CONSTANT)

public:
	explicit DesignerBackend(QObject* parent = nullptr);

	[[nodiscard]] QString filePath() const;
	[[nodiscard]] QString fileDirectory() const;
	[[nodiscard]] QString controlsPath() const;
	void setControlsPath(const QString& path);
	[[nodiscard]] QVariantList endpoints() const;
	[[nodiscard]] QVariantList issues() const;
	[[nodiscard]] QString statusMessage() const;
	[[nodiscard]] QStringList templateIds() const;

	/// Returns the profile JSON text of a built-in template ("" on unknown id).
	Q_INVOKABLE QString newFromTemplate(const QString& templateId) const;
	/// Loads a profile JSON file; returns its text or "" (statusMessage set).
	Q_INVOKABLE QString loadProfile(const QString& path);
	Q_INVOKABLE bool saveProfile(const QString& path, const QString& jsonText);
	/// Loads a manifest JSON exported by visionManifestDump; returns "" on
	/// success or the error message.
	Q_INVOKABLE QString loadManifest(const QString& path);
	/// Rebuilds the endpoint universe from the profile itself, following the
	/// visionService registration conventions (core endpoints + per-source
	/// stream + camera parameters).
	Q_INVOKABLE void deriveEndpoints(const QString& profileJson);
	/// Validates the profile against the schema rules and the current
	/// endpoint universe; returns true when no error-severity issue remains.
	Q_INVOKABLE bool validate(const QString& profileJson);
	/// Validates, then writes <targetDirectory>/<productId>/profile.json.
	Q_INVOKABLE bool publish(
		const QString& targetDirectory, const QString& jsonText);

	/// Endpoint universe entry factory shared by derivation and tests.
	[[nodiscard]] static QVariantList deriveEndpointEntries(
		const QString& profileJson, QString* errorMessage = nullptr);

signals:
	void filePathChanged();
	void controlsPathChanged();
	void endpointsChanged();
	void issuesChanged();
	void statusMessageChanged();

private:
	void setStatus(const QString& message);
	void setFilePath(const QString& path);
	[[nodiscard]] static QString toLocalFile(const QString& pathOrUrl);
	[[nodiscard]] static QString detectControlsPath();

	QString filePath_;
	QString controlsPath_;
	QVariantList endpoints_;
	QVariantList issues_;
	QString statusMessage_;
	/// True only when endpoints_ came from a manifest file; derived universes
	/// are profile-specific and must not leak into another profile's
	/// validation.
	bool manifestLoaded_ = false;
};

} // namespace visionDesigner
