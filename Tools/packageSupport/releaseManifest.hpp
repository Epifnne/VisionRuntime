#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace visionPackage {

inline bool safeId(const QString& id) {
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
	return pattern.match(id).hasMatch();
}

inline QByteArray sha256(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return {};
	}
	return hash.result().toHex();
}

inline bool writeManifest(const QString& packageRoot, const QString& productId,
	const QString& platform, QString& error) {
	QJsonArray files;
	const QDir root(packageRoot);
	for (const auto& subtree : {QStringLiteral("bin"),
		QStringLiteral("products/%1").arg(productId)}) {
		QDirIterator iterator(root.filePath(subtree), QDir::Files,
			QDirIterator::Subdirectories);
		while (iterator.hasNext()) {
			const auto path = iterator.next();
			if (QFileInfo(path).fileName() == QLatin1String("release-manifest.json") ||
				QFileInfo(path).fileName() == QLatin1String("publishing.lock")) {
				continue;
			}
			const auto hash = sha256(path);
			if (hash.isEmpty()) {
				error = QStringLiteral("cannot hash package file: %1").arg(path);
				return false;
			}
			files.append(QJsonObject{
				{QStringLiteral("path"), root.relativeFilePath(path)},
				{QStringLiteral("sha256"), QString::fromLatin1(hash)}});
		}
	}
	QSaveFile manifest(root.filePath(
		QStringLiteral("products/%1/release-manifest.json").arg(productId)));
	const auto data = QJsonDocument(QJsonObject{
		{QStringLiteral("schemaVersion"), 1},
		{QStringLiteral("runtimeVersion"), QStringLiteral("1.0.0")},
		{QStringLiteral("productId"), productId},
		{QStringLiteral("platform"), platform},
		{QStringLiteral("files"), files}}).toJson(QJsonDocument::Indented);
	if (!manifest.open(QIODevice::WriteOnly) ||
		manifest.write(data) != data.size() || !manifest.commit()) {
		error = QStringLiteral("cannot write release manifest: %1").arg(manifest.fileName());
		return false;
	}
	return true;
}

inline bool verifyManifest(const QString& manifestPath, QString& error) {
	QFile file(manifestPath);
	if (!file.open(QIODevice::ReadOnly)) {
		error = QStringLiteral("cannot open release manifest: %1").arg(manifestPath);
		return false;
	}
	QJsonParseError parseError{};
	const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
	const auto object = document.object();
	if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
		object.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
		!object.value(QStringLiteral("files")).isArray() ||
		object.value(QStringLiteral("files")).toArray().isEmpty()) {
		error = QStringLiteral("invalid release manifest: %1").arg(manifestPath);
		return false;
	}
	const QDir root(QFileInfo(manifestPath).absoluteDir().absoluteFilePath(QStringLiteral("../..")));
	const auto canonicalRoot = QFileInfo(root.absolutePath()).canonicalFilePath();
	for (const auto& entry : object.value(QStringLiteral("files")).toArray()) {
		const auto item = entry.toObject();
		const auto relative = item.value(QStringLiteral("path")).toString();
		const auto expected = item.value(QStringLiteral("sha256")).toString().toLatin1();
		const auto path = QFileInfo(root.filePath(relative)).canonicalFilePath();
		if (relative.isEmpty() || QDir::isAbsolutePath(relative) || path.isEmpty() ||
			!path.startsWith(canonicalRoot + QLatin1Char('/')) ||
			expected.size() != 64 || sha256(path) != expected) {
			error = QStringLiteral("package file missing or checksum mismatch: %1").arg(relative);
			return false;
		}
	}
	return true;
}

} // namespace visionPackage
