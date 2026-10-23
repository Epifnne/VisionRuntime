#include "designerBackend.hpp"
#include "releaseManifest.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <QSaveFile>
#include <cmath>

namespace visionDesigner {

namespace {

/// Control type → required endpoint kind; mirrors Shell/controls/NodeView.qml.
[[nodiscard]] const QHash<QString, QString>& controlKindRequirements() {
	static const QHash<QString, QString> requirements = {
		{QStringLiteral("imageView"), QStringLiteral("stream")},
		{QStringLiteral("stateCard"), QStringLiteral("state")},
		{QStringLiteral("commandButton"), QStringLiteral("command")},
		{QStringLiteral("parameterForm"), QStringLiteral("parameter")},
		{QStringLiteral("statNumber"), QStringLiteral("state")},
		{QStringLiteral("resultTable"), QStringLiteral("state")},
	};
	return requirements;
}

[[nodiscard]] bool isContainerType(const QString& type) {
	return type == QLatin1String("row") || type == QLatin1String("column");
}

/// Recursively copies \p sourceRoot into \p targetRoot. Existing target
/// files are skipped (release-template plugins win over profile-local
/// duplicates). Paths listed in \p excludeTopLevel are not copied.
bool copyDirectoryContents(const QString& sourceRoot, const QString& targetRoot,
		const QSet<QString>& excludeTopLevel = {}, bool topLevel = true) {
	const QDir source(sourceRoot);
	if (!source.exists()) {
		return false;
	}
	if (!QDir().mkpath(targetRoot)) {
		return false;
	}
	const auto entries = source.entryInfoList(
		QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
	for (const auto& entry : entries) {
		if (topLevel && excludeTopLevel.contains(entry.fileName())) {
			continue;
		}
		const auto target = targetRoot + QLatin1Char('/') + entry.fileName();
		if (entry.isSymLink() && entry.isDir()) {
			return false;
		}
		if (entry.isDir()) {
			if (!copyDirectoryContents(entry.absoluteFilePath(), target, {}, false)) {
				return false;
			}
		} else if (QFileInfo::exists(target)) {
			const auto hash = visionPackage::sha256(entry.absoluteFilePath());
			if (hash.isEmpty() || hash != visionPackage::sha256(target)) {
				return false;
			}
		} else if (!QFile::copy(entry.absoluteFilePath(), target)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] int countFiles(const QString& root) {
	int count = 0;
	const QDir dir(root);
	const auto entries = dir.entryInfoList(
		QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
	for (const auto& entry : entries) {
		count += entry.isDir() ? countFiles(entry.absoluteFilePath()) : 1;
	}
	return count;
}

/// Strips characters that are invalid in file names on either platform.
[[nodiscard]] QString sanitizeFileName(QString name) {
	const QString invalid = QStringLiteral("\\\\/:*?\"<>|");
	for (const auto& ch : invalid) {
		name.replace(ch, QLatin1Char('_'));
	}
	return name.trimmed();
}

[[nodiscard]] bool writeTextFile(const QString& path, const QByteArray& data) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(data) == data.size() && file.commit();
}

[[nodiscard]] QVariantMap makeIssue(
	const QString& path, const QString& message, const QString& severity) {
	return {
		{QStringLiteral("path"), path},
		{QStringLiteral("message"), message},
		{QStringLiteral("severity"), severity},
	};
}

} // namespace

DesignerBackend::DesignerBackend(QObject* parent)
	: QObject(parent) {
	const QSettings settings(QStringLiteral("VisionRuntime"),
		QStringLiteral("visionDesigner"));
	controlsPath_ = settings.value(QStringLiteral("controlsPath"))
		.toString();
	if (controlsPath_.isEmpty()
			|| !QFileInfo::exists(controlsPath_ + QStringLiteral("/NodeView.qml"))) {
		controlsPath_ = detectControlsPath();
	}
}

QString DesignerBackend::filePath() const { return filePath_; }

QString DesignerBackend::fileDirectory() const {
	return filePath_.isEmpty() ? QString()
		: QFileInfo(filePath_).absolutePath();
}

QString DesignerBackend::controlsPath() const { return controlsPath_; }

void DesignerBackend::setControlsPath(const QString& path) {
	if (controlsPath_ == path) {
		return;
	}
	controlsPath_ = path;
	QSettings(QStringLiteral("VisionRuntime"), QStringLiteral("visionDesigner"))
		.setValue(QStringLiteral("controlsPath"), path);
	emit controlsPathChanged();
}

QVariantList DesignerBackend::endpoints() const { return endpoints_; }
QVariantList DesignerBackend::issues() const { return issues_; }
QString DesignerBackend::statusMessage() const { return statusMessage_; }

QStringList DesignerBackend::templateIds() const {
	return {
		QStringLiteral("emptyProduct"),
		QStringLiteral("twoCameraDirectory"),
		QStringLiteral("singleCameraDebug"),
	};
}

QString DesignerBackend::newFromTemplate(const QString& templateId) const {
	QFile file(QStringLiteral(":/templates/%1.json").arg(templateId));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QString::fromUtf8(file.readAll());
}

QString DesignerBackend::loadProfile(const QString& path) {
	const auto local = toLocalFile(path);
	QFile file(local);
	if (!file.open(QIODevice::ReadOnly)) {
		setStatus(QStringLiteral("cannot open profile: %1").arg(local));
		return {};
	}
	const QString text = QString::fromUtf8(file.readAll());
	QJsonParseError parseError{};
	const auto document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		setStatus(QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
		return {};
	}
	setFilePath(local);
	setStatus(QStringLiteral("loaded %1").arg(local));
	return text;
}

bool DesignerBackend::saveProfile(const QString& path, const QString& jsonText) {
	const auto local = toLocalFile(path);
	QJsonParseError parseError{};
	const auto document = QJsonDocument::fromJson(
		jsonText.toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		setStatus(QStringLiteral("refusing to save invalid JSON: %1")
			.arg(parseError.errorString()));
		return false;
	}
	QFile file(local);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		setStatus(QStringLiteral("cannot write profile: %1").arg(local));
		return false;
	}
	file.write(document.toJson(QJsonDocument::Indented));
	file.write("\n");
	setFilePath(local);
	setStatus(QStringLiteral("saved %1").arg(local));
	return true;
}

QString DesignerBackend::loadManifest(const QString& path) {
	const auto local = toLocalFile(path);
	QFile file(local);
	if (!file.open(QIODevice::ReadOnly)) {
		const auto message = QStringLiteral("cannot open manifest: %1").arg(local);
		setStatus(message);
		return message;
	}
	QJsonParseError parseError{};
	const auto document = QJsonDocument::fromJson(
		file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		const auto message = QStringLiteral("invalid manifest JSON: %1")
			.arg(parseError.errorString());
		setStatus(message);
		return message;
	}
	const auto entries = document.object()
		.value(QStringLiteral("endpoints")).toArray();
	const auto version = document.object().value(QStringLiteral("schemaVersion")).toObject();
	if (!version.isEmpty() && version.value(QStringLiteral("major")).toInt(-1) != 1) {
		const auto message = QStringLiteral("unsupported endpoint manifest schema major version");
		setStatus(message);
		return message;
	}
	if (entries.isEmpty()) {
		const auto message = QStringLiteral("manifest has no endpoints array");
		setStatus(message);
		return message;
	}
	QVariantList universe;
	universe.reserve(entries.size());
	QSet<QString> names;
	const QSet<QString> kinds{"parameter", "command", "state", "stream"};
	const QSet<QString> valueTypes{"decimal", "integer", "flag", "text"};
	for (const auto& entry : entries) {
		const auto endpoint = entry.toObject();
		const auto name = endpoint.value(QStringLiteral("name")).toString();
		const auto kind = endpoint.value(QStringLiteral("kind")).toString();
		bool valid = !name.isEmpty() && !names.contains(name) && kinds.contains(kind);
		if (kind == QLatin1String("parameter")) {
			valid = valid && valueTypes.contains(
				endpoint.value(QStringLiteral("valueType")).toString());
		}
		if (endpoint.contains(QStringLiteral("fields"))) {
			valid = valid && kind == QLatin1String("state") &&
				endpoint.value(QStringLiteral("fields")).isObject();
			const auto fields = endpoint.value(QStringLiteral("fields")).toObject();
			for (auto field = fields.constBegin(); field != fields.constEnd(); ++field) {
				valid = valid && !field.key().isEmpty() && field.value().isObject() &&
					valueTypes.contains(field.value().toObject()
						.value(QStringLiteral("valueType")).toString());
			}
		}
		if (!valid) {
			const auto message = QStringLiteral("invalid or duplicate manifest endpoint: %1").arg(name);
			setStatus(message);
			return message;
		}
		names.insert(name);
		universe.append(endpoint.toVariantMap());
	}
	endpoints_ = universe;
	manifestLoaded_ = true;
	emit endpointsChanged();
	setStatus(QStringLiteral("manifest loaded: %1 endpoints from %2")
		.arg(endpoints_.size()).arg(local));
	return {};
}

void DesignerBackend::deriveEndpoints(const QString& profileJson) {
	endpoints_.clear();
	manifestLoaded_ = false;
	emit endpointsChanged();
	const auto profile = QJsonDocument::fromJson(profileJson.toUtf8()).object();
	const auto product = profile.value(QStringLiteral("runtimeProduct")).toString();
	if (!product.isEmpty() && visionPackage::safeId(product)) {
#ifdef Q_OS_WIN
		const auto platform = QStringLiteral("windows");
#else
		const auto platform = QStringLiteral("linux");
#endif
		const auto profileFile = QFileInfo(filePath_);
		const QStringList candidates{
			profileFile.absoluteDir().filePath(
				profileFile.completeBaseName() + QStringLiteral(".manifest.json")),
			profileFile.absoluteDir().filePath(product + QStringLiteral(".manifest.json")),
			QDir(runtimeTemplate(platform)).filePath(product + QStringLiteral(".manifest.json")),
			QDir(QCoreApplication::applicationDirPath()).filePath(product + QStringLiteral(".manifest.json"))};
		for (const auto& candidate : candidates) {
			if (!QFileInfo::exists(candidate)) {
				continue;
			}
			QFile file(candidate);
			if (!file.open(QIODevice::ReadOnly)) {
				setStatus(QStringLiteral("cannot read product manifest: %1").arg(candidate));
				return;
			}
			const auto declared = QJsonDocument::fromJson(file.readAll()).object()
				.value(QStringLiteral("runtimeProduct")).toString();
			if (!declared.isEmpty() && declared != product) {
				setStatus(QStringLiteral("manifest belongs to another product: %1").arg(candidate));
				return;
			}
			static_cast<void>(loadManifest(candidate));
			return;
		}
	}
	QString error;
	auto universe = deriveEndpointEntries(profileJson, &error);
	if (!error.isEmpty()) {
		setStatus(error);
		return;
	}
	endpoints_ = universe;
	manifestLoaded_ = false;
	emit endpointsChanged();
	setStatus(QStringLiteral("derived %1 endpoints from profile")
		.arg(endpoints_.size()));
}

QVariantList DesignerBackend::deriveEndpointEntries(
	const QString& profileJson, QString* errorMessage) {
	QJsonParseError parseError{};
	const auto document = QJsonDocument::fromJson(
		profileJson.toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("invalid profile JSON: %1")
				.arg(parseError.errorString());
		}
		return {};
	}

	QVariantList universe;
	const auto add = [&universe](QVariantMap entry) {
		entry.insert(QStringLiteral("access"),
			entry.value(QStringLiteral("access"),
				QStringLiteral("operator")));
		universe.append(std::move(entry));
	};
	// Core endpoints, mirroring VisionService::registerCoreEndpoints().
	add({{QStringLiteral("name"), QStringLiteral("session.start")},
		{QStringLiteral("kind"), QStringLiteral("command")},
		{QStringLiteral("description"),
			QStringLiteral("start the detection session")}});
	add({{QStringLiteral("name"), QStringLiteral("session.stop")},
		{QStringLiteral("kind"), QStringLiteral("command")},
		{QStringLiteral("description"),
			QStringLiteral("request a graceful stop of the running session")}});
	add({{QStringLiteral("name"), QStringLiteral("session.recover")},
		{QStringLiteral("kind"), QStringLiteral("command")},
		{QStringLiteral("access"), QStringLiteral("engineer")},
		{QStringLiteral("description"), QStringLiteral("recreate session resources")}});
	add({{QStringLiteral("name"), QStringLiteral("inspection.result")},
		{QStringLiteral("kind"), QStringLiteral("state")},
		{QStringLiteral("description"), QStringLiteral("correlated inspection outcome")}});
	add({{QStringLiteral("name"), QStringLiteral("source.fault")},
		{QStringLiteral("kind"), QStringLiteral("state")},
		{QStringLiteral("description"), QStringLiteral("acquisition faults and sequence gaps")}});
	add({{QStringLiteral("name"), QStringLiteral("session.state")},
		{QStringLiteral("kind"), QStringLiteral("state")},
		{QStringLiteral("description"),
			QStringLiteral("session lifecycle state")}});
	add({{QStringLiteral("name"), QStringLiteral("session.summary")},
		{QStringLiteral("kind"), QStringLiteral("state")},
		{QStringLiteral("description"),
			QStringLiteral("session counters and per-source statistics")}});
	add({{QStringLiteral("name"), QStringLiteral("metrics.performance")},
		{QStringLiteral("kind"), QStringLiteral("state")},
		{QStringLiteral("description"),
			QStringLiteral("last batch performance counters")}});

	// Per-source endpoints, mirroring SessionAssembler + CameraService.
	const auto sources = document.object()
		.value(QStringLiteral("sources")).toArray();
	int index = 0;
	for (const auto& sourceValue : sources) {
		const auto source = sourceValue.toObject();
		const auto id = source.value(QStringLiteral("id")).toString();
		if (id.isEmpty()) {
			continue;
		}
		add({{QStringLiteral("name"), QStringLiteral("stream.%1").arg(id)},
			{QStringLiteral("kind"), QStringLiteral("stream")},
			{QStringLiteral("sourceId"), index},
			{QStringLiteral("description"),
				QStringLiteral("live frames of source %1").arg(id)}});
		add({{QStringLiteral("name"),
				QStringLiteral("camera.%1.exposureMicroseconds").arg(id)},
			{QStringLiteral("kind"), QStringLiteral("parameter")},
			{QStringLiteral("valueType"), QStringLiteral("decimal")},
			{QStringLiteral("writable"), true},
			{QStringLiteral("minimum"), 0.0},
			{QStringLiteral("maximum"), 1000000.0},
			{QStringLiteral("description"),
				QStringLiteral("camera exposure in microseconds (%1)").arg(id)}});
		add({{QStringLiteral("name"), QStringLiteral("camera.%1.gain").arg(id)},
			{QStringLiteral("kind"), QStringLiteral("parameter")},
			{QStringLiteral("valueType"), QStringLiteral("decimal")},
			{QStringLiteral("writable"), true},
			{QStringLiteral("minimum"), 0.0},
			{QStringLiteral("maximum"), 100.0},
			{QStringLiteral("description"),
				QStringLiteral("camera gain (%1)").arg(id)}});
		add({{QStringLiteral("name"),
				QStringLiteral("camera.%1.softwareTrigger").arg(id)},
			{QStringLiteral("kind"), QStringLiteral("command")},
			{QStringLiteral("description"),
				QStringLiteral("software trigger (%1)").arg(id)}});
		++index;
	}
	return universe;
}

bool DesignerBackend::validate(const QString& profileJson) {
	QVariantList issues;

	QJsonParseError parseError{};
	const auto document = QJsonDocument::fromJson(
		profileJson.toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		issues.append(makeIssue(QString(),
			QStringLiteral("invalid JSON: %1").arg(parseError.errorString()),
			QStringLiteral("error")));
		issues_ = issues;
		emit issuesChanged();
		return false;
	}
	const auto root = document.object();

	// The endpoint universe: an explicitly loaded manifest wins; otherwise
	// derive from the profile being validated (a derived universe cached from
	// another profile would produce bogus "not registered" errors).
	QString deriveError;
	const auto universe = manifestLoaded_
		? endpoints_
		: deriveEndpointEntries(profileJson, &deriveError);
	QHash<QString, QVariantMap> byName;
	for (const auto& entry : universe) {
		byName.insert(entry.toMap().value(QStringLiteral("name")).toString(),
			entry.toMap());
	}

	const auto id = root.value(QStringLiteral("id")).toString();
	if (!visionPackage::safeId(id)) {
		issues.append(makeIssue(QStringLiteral("profile"),
			QStringLiteral("product id must match [A-Za-z0-9][A-Za-z0-9_.-]*"),
			QStringLiteral("error")));
	}
	if (root.value(QStringLiteral("name")).toString().isEmpty()) {
		issues.append(makeIssue(QStringLiteral("profile"),
			QStringLiteral("product name must not be empty"),
			QStringLiteral("error")));
	}

	// Sources.
	const auto sources = root.value(QStringLiteral("sources")).toArray();
	if (sources.isEmpty()) {
		issues.append(makeIssue(QStringLiteral("sources"),
			QStringLiteral("at least one source is required"),
			QStringLiteral("error")));
	}
	QSet<QString> sourceIds;
	int sourceIndex = 0;
	for (const auto& sourceValue : sources) {
		const auto source = sourceValue.toObject();
		const auto path = QStringLiteral("sources/%1").arg(sourceIndex++);
		const auto sourceId = source.value(QStringLiteral("id")).toString();
		if (!visionPackage::safeId(sourceId)) {
			issues.append(makeIssue(path,
				QStringLiteral("source id must match [A-Za-z0-9][A-Za-z0-9_.-]*"),
				QStringLiteral("error")));
		} else if (sourceIds.contains(sourceId)) {
			issues.append(makeIssue(path,
				QStringLiteral("duplicate source id: %1").arg(sourceId),
				QStringLiteral("error")));
		} else {
			sourceIds.insert(sourceId);
		}
		const auto type = source.value(QStringLiteral("type")).toString();
		if (type == QLatin1String("directory")) {
			if (source.value(QStringLiteral("directory")).toString().isEmpty()) {
				issues.append(makeIssue(path,
					QStringLiteral("directory source requires a directory"),
					QStringLiteral("error")));
			}
		} else if (type == QLatin1String("camera")) {
			if (source.value(QStringLiteral("serialNumber")).toString().isEmpty()
					&& source.value(QStringLiteral("ipAddress")).toString()
						.isEmpty()) {
				issues.append(makeIssue(path,
					QStringLiteral("camera source requires serialNumber or "
						"ipAddress"),
					QStringLiteral("error")));
			}
			if (source.contains(QStringLiteral("noFrameTimeoutMilliseconds"))) {
				const auto value = source.value(QStringLiteral("noFrameTimeoutMilliseconds"));
				const auto timeout = value.toDouble(-1);
				if (!value.isDouble() || !std::isfinite(timeout) || timeout < 1 ||
					timeout > 86400000 || std::floor(timeout) != timeout) {
					issues.append(makeIssue(path,
						QStringLiteral("noFrameTimeoutMilliseconds must be an integer in [1, 86400000]"),
						QStringLiteral("error")));
				}
			}
		} else {
			issues.append(makeIssue(path,
				QStringLiteral("unknown source type: %1").arg(type),
				QStringLiteral("error")));
		}
	}

	// Model and pipeline.
	const auto model = root.value(QStringLiteral("model")).toObject();
	const bool customWithoutModel = !root.value(QStringLiteral("runtimeProduct"))
		.toString().isEmpty() && !root.contains(QStringLiteral("model"));
	if (!customWithoutModel && model.value(QStringLiteral("packagePath")).toString().isEmpty()) {
		issues.append(makeIssue(QStringLiteral("model"),
			QStringLiteral("model.packagePath must not be empty"),
			QStringLiteral("error")));
	}
	if (!customWithoutModel && model.value(QStringLiteral("backendId")).toString().isEmpty()) {
		issues.append(makeIssue(QStringLiteral("model"),
			QStringLiteral("model.backendId must not be empty"),
			QStringLiteral("error")));
	}
	const auto pipeline = root.value(QStringLiteral("pipeline")).toObject();
	const auto faultPolicy = pipeline.value(QStringLiteral("faultPolicy"))
		.toString(QStringLiteral("reportAndContinue"));
	if (faultPolicy != QLatin1String("reportAndContinue") &&
		faultPolicy != QLatin1String("stopSession")) {
		issues.append(makeIssue(QStringLiteral("pipeline/faultPolicy"),
			QStringLiteral("faultPolicy must be reportAndContinue or stopSession"),
			QStringLiteral("error")));
	}
	if (pipeline.contains(QStringLiteral("taskTimeoutMilliseconds"))) {
		const auto value = pipeline.value(QStringLiteral("taskTimeoutMilliseconds"));
		const auto timeout = value.toDouble(-1);
		if (!value.isDouble() || !std::isfinite(timeout) || timeout < 1 ||
			timeout > 86400000 || std::floor(timeout) != timeout) {
			issues.append(makeIssue(QStringLiteral("pipeline/taskTimeoutMilliseconds"),
				QStringLiteral("taskTimeoutMilliseconds must be an integer in [1, 86400000]"),
				QStringLiteral("error")));
		}
	}

	// Exposed endpoint subset.
	QSet<QString> exposed;
	const auto exposedArray = root.value(QStringLiteral("endpoints")).toArray();
	if (exposedArray.isEmpty()) {
		issues.append(makeIssue(QStringLiteral("endpoints"),
			QStringLiteral("profile must expose an endpoints list"),
			QStringLiteral("error")));
	}
	static const QSet<QString> kAccessLevels = {
		QStringLiteral("operator"), QStringLiteral("engineer"),
		QStringLiteral("administrator"),
	};
	int exposedIndex = 0;
	for (const auto& exposureValue : exposedArray) {
		const auto exposure = exposureValue.toObject();
		const auto path =
			QStringLiteral("endpoints/%1").arg(exposedIndex++);
		const auto name = exposure.value(QStringLiteral("name")).toString();
		if (!byName.contains(name)) {
			issues.append(makeIssue(path,
				QStringLiteral("exposed endpoint is not registered: %1")
					.arg(name),
				QStringLiteral("error")));
			continue;
		}
		exposed.insert(name);
		const auto access = exposure.value(QStringLiteral("access")).toString();
		if (!access.isEmpty() && !kAccessLevels.contains(access)) {
			issues.append(makeIssue(path,
				QStringLiteral("unknown access level: %1").arg(access),
				QStringLiteral("error")));
		}
	}

	// UI control tree.
	const auto ui = root.value(QStringLiteral("ui")).toObject();
	const auto pages = ui.value(QStringLiteral("pages")).toArray();
	if (pages.isEmpty()) {
		issues.append(makeIssue(QStringLiteral("ui"),
			QStringLiteral("ui section defines no pages"),
			QStringLiteral("warning")));
	}

	std::function<void(const QJsonObject&, const QString&)> walkNode =
		[&](const QJsonObject& node, const QString& path) {
			const auto type = node.value(QStringLiteral("type")).toString();
			const auto& kinds = controlKindRequirements();
			if (isContainerType(type)) {
				const auto children =
					node.value(QStringLiteral("children")).toArray();
				int childIndex = 0;
				for (const auto& child : children) {
					walkNode(child.toObject(),
						path + QStringLiteral("/%1").arg(childIndex++));
				}
				return;
			}
			if (!kinds.contains(type)) {
				issues.append(makeIssue(path,
					QStringLiteral("unknown control type: %1").arg(type),
					QStringLiteral("error")));
				return;
			}
			const auto endpoint =
				node.value(QStringLiteral("endpoint")).toString();
			if (endpoint.isEmpty()) {
				issues.append(makeIssue(path,
					QStringLiteral("%1 requires an endpoint binding").arg(type),
					QStringLiteral("error")));
				return;
			}
			const auto found = byName.constFind(endpoint);
			if (found == byName.constEnd()) {
				issues.append(makeIssue(path,
					QStringLiteral("endpoint is not registered: %1")
						.arg(endpoint),
					QStringLiteral("error")));
				return;
			}
			const auto kind = found->value(QStringLiteral("kind")).toString();
			if (kind != kinds.value(type)) {
				issues.append(makeIssue(path,
					QStringLiteral("%1 requires a %2 endpoint, %3 is %4")
						.arg(type, kinds.value(type), endpoint, kind),
					QStringLiteral("error")));
			}
			const auto fields = found->value(QStringLiteral("fields")).toMap();
			const auto field = node.value(QStringLiteral("field")).toString();
			if (type == QLatin1String("statNumber") && !field.isEmpty() &&
				!fields.isEmpty() && !fields.contains(field)) {
				issues.append(makeIssue(path,
					QStringLiteral("state field is not exported: %1.%2").arg(endpoint, field),
					QStringLiteral("error")));
			}
			if (!exposed.isEmpty() && !exposed.contains(endpoint)) {
				issues.append(makeIssue(path,
					QStringLiteral("endpoint %1 is bound but not listed in "
						"the profile endpoints section").arg(endpoint),
					QStringLiteral("error")));
			}
		};

	int pageIndex = 0;
	for (const auto& pageValue : pages) {
		const auto page = pageValue.toObject();
		const auto children = page.value(QStringLiteral("children")).toArray();
		int childIndex = 0;
		for (const auto& child : children) {
			walkNode(child.toObject(),
				QStringLiteral("%1/%2").arg(pageIndex).arg(childIndex++));
		}
		++pageIndex;
	}

	issues_ = issues;
	emit issuesChanged();
	int errors = 0;
	for (const auto& issue : issues) {
		if (issue.toMap().value(QStringLiteral("severity")).toString()
				== QLatin1String("error")) {
			++errors;
		}
	}
	setStatus(errors == 0
		? QStringLiteral("validation passed (%1 issue(s))").arg(issues.size())
		: QStringLiteral("validation failed: %1 error(s)").arg(errors));
	return errors == 0;
}

bool DesignerBackend::publish(
		const QString& targetDirectory, const QString& jsonText,
		const QString& platform) {
	if (!validate(jsonText)) {
		setStatus(QStringLiteral("publish aborted: validation errors"));
		return false;
	}
	auto root = QJsonDocument::fromJson(jsonText.toUtf8()).object();
	const auto id = root.value(QStringLiteral("id")).toString();
	const auto name = root.value(QStringLiteral("name")).toString();
	if (id.isEmpty()) {
		setStatus(QStringLiteral("publish aborted: product id is empty"));
		return false;
	}
#ifdef Q_OS_WIN
	const QString hostPlatform = QStringLiteral("windows");
#else
	const QString hostPlatform = QStringLiteral("linux");
#endif
	const auto targetPlatform = platform.isEmpty() ? hostPlatform : platform;
	if (targetPlatform != QLatin1String("windows")
			&& targetPlatform != QLatin1String("linux")) {
		setStatus(QStringLiteral("publish aborted: unknown platform %1")
			.arg(targetPlatform));
		return false;
	}

	const auto local = toLocalFile(targetDirectory);
	if (local.isEmpty()) {
		setStatus(QStringLiteral("publish aborted: no target directory"));
		return false;
	}
	const QDir targetRoot(local);
	const auto productDirectory = targetRoot.absoluteFilePath(
		QStringLiteral("products/%1").arg(id));
	if (QFileInfo::exists(productDirectory + QStringLiteral("/profile.json"))) {
		setStatus(QStringLiteral("publish aborted: %1 already published; "
			"rename the product id or remove it first").arg(id));
		return false;
	}
	if (!QDir().mkpath(targetRoot.absolutePath())) {
		setStatus(QStringLiteral("cannot create target directory %1")
			.arg(targetRoot.absolutePath()));
		return false;
	}

	// 1. Runtime payload: the platform's publishShell release root becomes
	// bin/ (its own example products/ are not carried over).
	const auto templateRoot = runtimeTemplate(targetPlatform);
	if (templateRoot.isEmpty()) {
		setStatus(QStringLiteral("publish aborted: no %1 runtime template; "
			"build the publishShell target first, then set the template "
			"directory in the publish dialog").arg(targetPlatform));
		return false;
	}
	const auto binDirectory = targetRoot.absoluteFilePath(QStringLiteral("bin"));
	if (!copyDirectoryContents(templateRoot, binDirectory,
		{QStringLiteral("products")})) {
		setStatus(QStringLiteral("publish aborted: runtime copy failed or conflicts with an existing runtime"));
		return false;
	}
	const auto versionPath = binDirectory + QStringLiteral("/runtime-version.json");
	if (QFileInfo::exists(versionPath)) {
		QFile versionFile(versionPath);
		QJsonParseError parseError{};
		if (!versionFile.open(QIODevice::ReadOnly)) {
			setStatus(QStringLiteral("publish aborted: cannot read runtime metadata"));
			return false;
		}
		const auto version = QJsonDocument::fromJson(versionFile.readAll(), &parseError);
		if (parseError.error != QJsonParseError::NoError || !version.isObject()) {
			setStatus(QStringLiteral("publish aborted: invalid runtime metadata"));
			return false;
		}
		const auto runtimeProduct = version.object().value(QStringLiteral("productRuntime")).toString();
		const auto expectedProduct = root.value(QStringLiteral("runtimeProduct")).toString();
		if (!expectedProduct.isEmpty() && expectedProduct != runtimeProduct) {
			setStatus(QStringLiteral("publish aborted: profile and runtime template belong to different products"));
			return false;
		}
		if (!runtimeProduct.isEmpty()) {
			root.insert(QStringLiteral("runtimeProduct"), runtimeProduct);
		} else if (!root.value(QStringLiteral("runtimeProduct")).toString().isEmpty()) {
			setStatus(QStringLiteral("publish aborted: profile requires its product-specific runtime template"));
			return false;
		}
	} else if (!root.value(QStringLiteral("runtimeProduct")).toString().isEmpty()) {
		setStatus(QStringLiteral("publish aborted: product runtime metadata is missing"));
		return false;
	}

	// 2. Product resources: resolve against the profile's own directory,
	// copy into the package, rewrite to in-package relative paths.
	if (fileDirectory().isEmpty()) {
		setStatus(QStringLiteral("publish aborted: save the profile first so "
			"relative resource paths can be resolved"));
		return false;
	}
	const QDir baseDirectory(fileDirectory());
	QStringList warnings;
	if (!QDir().mkpath(productDirectory)) {
		setStatus(QStringLiteral("cannot create %1").arg(productDirectory));
		return false;
	}
	const auto publishingLock = productDirectory + QStringLiteral("/publishing.lock");
	if (!writeTextFile(publishingLock, QByteArray("package assembly in progress\n"))) {
		setStatus(QStringLiteral("publish aborted: cannot create package assembly marker"));
		return false;
	}
	auto model = root.value(QStringLiteral("model")).toObject();
	if (root.contains(QStringLiteral("model"))) {
		const auto packagePath =
			model.value(QStringLiteral("packagePath")).toString();
		const auto resolvedModel = QDir::cleanPath(
			baseDirectory.absoluteFilePath(packagePath));
		if (packagePath.isEmpty() || !QFileInfo::exists(resolvedModel)) {
			setStatus(QStringLiteral("publish aborted: model package not found: "
				"%1").arg(resolvedModel));
			return false;
		}
		// Validate the backend/device against the target platform's plugin
		// manifest (e.g. NPU is Linux-only) before copying anything.
		const auto backendId =
			model.value(QStringLiteral("backendId")).toString();
		const auto device = model.value(QStringLiteral("device")).toString();
		const auto catalog = backendCatalog(targetPlatform);
		if (!catalog.isEmpty()) {
			const auto problem = validateBackendForPlatform(
				catalog, backendId, device, targetPlatform);
			if (!problem.isEmpty()) {
				setStatus(problem);
				return false;
			}
		}
		if (!QFileInfo::exists(resolvedModel + QStringLiteral("/manifest.json")) ||
			!copyDirectoryContents(resolvedModel,
				productDirectory + QStringLiteral("/model"))) {
			setStatus(QStringLiteral("publish aborted: model manifest missing or model copy failed"));
			return false;
		}
		model.insert(QStringLiteral("packagePath"), QStringLiteral("model"));
		const auto pluginDirectory =
			model.value(QStringLiteral("pluginDirectory")).toString();
		if (!pluginDirectory.isEmpty()) {
			const auto resolvedPlugins = QDir::cleanPath(
				baseDirectory.absoluteFilePath(pluginDirectory));
			if (QFileInfo::exists(resolvedPlugins)) {
				if (!copyDirectoryContents(resolvedPlugins,
					binDirectory + QStringLiteral("/plugins"))) {
					setStatus(QStringLiteral("publish aborted: plugin copy failed or conflicting plugin versions"));
					return false;
				}
			} else {
				warnings.append(QStringLiteral(
					"plugin directory not found: %1").arg(resolvedPlugins));
			}
		}
		const auto libraryName = QStringLiteral("vision-backend-%1.%2")
			.arg(backendId, targetPlatform == QLatin1String("windows")
				? QStringLiteral("dll") : QStringLiteral("so"));
		if (!QFileInfo::exists(binDirectory + QStringLiteral("/plugins/%1/%2")
			.arg(backendId, libraryName))) {
			setStatus(QStringLiteral("publish aborted: selected backend plugin is not present in the target runtime"));
			return false;
		}
		// The shell resolves pluginDirectory relative to the profile file, so
		// products/<id>/ -> ../../bin/plugins lands in the shared runtime.
		model.insert(QStringLiteral("pluginDirectory"),
			QStringLiteral("../../bin/plugins"));
		root.insert(QStringLiteral("model"), model);
	}

	auto sources = root.value(QStringLiteral("sources")).toArray();
	for (auto index = 0; index < sources.size(); ++index) {
		auto source = sources.at(index).toObject();
		if (source.value(QStringLiteral("type")).toString()
				!= QLatin1String("directory")) {
			continue;
		}
		const auto sourceId = source.value(QStringLiteral("id")).toString();
		const auto directory =
			source.value(QStringLiteral("directory")).toString();
		const auto resolved = QDir::cleanPath(
			baseDirectory.absoluteFilePath(directory));
		if (directory.isEmpty() || countFiles(resolved) == 0) {
			setStatus(QStringLiteral("publish aborted: directory source %1 is empty or missing: %2")
				.arg(sourceId, resolved));
			return false;
		}
		if (!copyDirectoryContents(resolved, productDirectory
			+ QStringLiteral("/assets/%1").arg(sourceId))) {
			setStatus(QStringLiteral("publish aborted: source assets copy failed"));
			return false;
		}
		source.insert(QStringLiteral("directory"),
			QStringLiteral("assets/%1").arg(sourceId));
		sources.replace(index, source);
	}
	root.insert(QStringLiteral("sources"), sources);

	const auto profilePath =
		productDirectory + QStringLiteral("/profile.json");
	if (!writeTextFile(profilePath,
			QJsonDocument(root).toJson(QJsonDocument::Indented) + '\n')) {
		setStatus(QStringLiteral("cannot write %1").arg(profilePath));
		return false;
	}
	if (manifestLoaded_) {
		const QJsonObject endpointManifest{
			{"schemaVersion", QJsonObject{{"major", 1}, {"minor", 1}}},
			{"runtimeProduct", root.value(QStringLiteral("runtimeProduct"))},
			{"endpoints", QJsonArray::fromVariantList(endpoints_)}};
		if (!writeTextFile(productDirectory + QStringLiteral("/profile.manifest.json"),
			QJsonDocument(endpointManifest).toJson())) {
			setStatus(QStringLiteral("cannot write product endpoint manifest"));
			return false;
		}
	}

	// 3. Double-click entry script for the target platform.
	const auto scriptBase = sanitizeFileName(name.isEmpty() ? id : name);
	if (targetPlatform == QLatin1String("windows")) {
		// UTF-8 BOM so cmd.exe on Chinese Windows renders the name right.
		QByteArray script;
		script.append("\xEF\xBB\xBF");
		script.append("@echo off\r\n\"%~dp0bin\\visionShell.exe\" "
			"\"%~dp0products\\");
		script.append(id.toUtf8());
		script.append("\\profile.json\"\r\nif errorlevel 1 pause\r\n");
		if (!writeTextFile(targetRoot.absoluteFilePath(
			QStringLiteral("启动-%1.cmd").arg(scriptBase)), script)) {
			setStatus(QStringLiteral("publish aborted: cannot write Windows entry script"));
			return false;
		}
	} else {
		QByteArray script("#!/bin/sh\nhere=\"$(dirname \"$0\")\"\n"
			"exec \"$here/bin/visionShell\" \"$here/products/");
		script.append(id.toUtf8());
		script.append("/profile.json\"\n");
		const auto scriptPath = targetRoot.absoluteFilePath(
			QStringLiteral("run-%1.sh").arg(scriptBase));
		if (!writeTextFile(scriptPath, script)) {
			setStatus(QStringLiteral("publish aborted: cannot write Linux entry script"));
			return false;
		}
		// The executable bit only exists on Linux file systems; on a
		// Windows host this is a no-op and the target machine needs one
		// chmod +x (documented in the package).
		QFile::setPermissions(scriptPath,
			QFileDevice::ReadOwner | QFileDevice::WriteOwner
				| QFileDevice::ExeOwner | QFileDevice::ReadGroup
				| QFileDevice::ExeGroup | QFileDevice::ReadOther
				| QFileDevice::ExeOther);
	}

	QString manifestError;
	if (!visionPackage::writeManifest(targetRoot.absolutePath(), id,
		targetPlatform, manifestError)) {
		setStatus(manifestError);
		return false;
	}
	if (!QFile::remove(publishingLock)) {
		setStatus(QStringLiteral("publish aborted: cannot finalize package assembly marker"));
		return false;
	}
	const auto warningSuffix = warnings.isEmpty() ? QString()
		: QStringLiteral(" (%1)").arg(warnings.join(QLatin1String("; ")));
	setStatus(QStringLiteral("published %1 package to %2%3")
		.arg(targetPlatform, profilePath, warningSuffix));
	return true;
}

QString DesignerBackend::runtimeTemplate(const QString& platform) const {
	const auto exeName = platform == QLatin1String("windows")
		? QStringLiteral("visionShell.exe") : QStringLiteral("visionShell");
	const auto selected = runtimeOverrides_.value(platform);
	if (!selected.isEmpty() && QFileInfo::exists(selected + QLatin1Char('/') + exeName)) {
		return selected;
	}
	QDir bundled(QCoreApplication::applicationDirPath());
	for (int level = 0; level < 8; ++level) {
		const auto candidate = bundled.absoluteFilePath(
			QStringLiteral("runtimeTemplates/%1").arg(platform));
		if (QFileInfo::exists(candidate + QLatin1Char('/') + exeName)) {
			return candidate;
		}
		if (!bundled.cdUp()) {
			break;
		}
	}
	const auto fromSettings = QSettings(QStringLiteral("VisionRuntime"),
		QStringLiteral("visionDesigner"))
		.value(QStringLiteral("runtimeTemplate/%1").arg(platform)).toString();
	// Trust a stored override only while it still points at a real template;
	// a stale path must fall back to build-tree auto-detection.
	if (!fromSettings.isEmpty()
			&& QFileInfo::exists(fromSettings + QLatin1Char('/') + exeName)) {
		return fromSettings;
	}
	return detectRuntimeTemplate(platform);
}

QVariantList DesignerBackend::backendCatalog(const QString& platform) const {
#ifdef Q_OS_WIN
	const QString hostPlatform = QStringLiteral("windows");
#else
	const QString hostPlatform = QStringLiteral("linux");
#endif
	const auto targetPlatform = platform.isEmpty() ? hostPlatform : platform;
	const auto root = runtimeTemplate(targetPlatform);
	if (root.isEmpty()) {
		return {};
	}
	// The manifest sits at the plugins root inside bin/.
	const auto manifestPath = QDir(root).absoluteFilePath(
		QStringLiteral("plugins/plugins.json"));
	QFile file(manifestPath);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	const auto backends = document.object()
		.value(QStringLiteral("backends")).toArray();
	QVariantList result;
	for (const auto& entry : backends) {
		result.append(entry.toObject().toVariantMap());
	}
	return result;
}

QString DesignerBackend::validateBackendForPlatform(
		const QVariantList& catalog, const QString& backendId,
		const QString& device, const QString& platform) {
	for (const auto& entry : catalog) {
		const auto backend = entry.toMap();
		if (backend.value(QStringLiteral("id")).toString() != backendId) {
			continue;
		}
		const auto devices = backend.value(QStringLiteral("devices")).toList();
		for (const auto& deviceEntry : devices) {
			const auto deviceMap = deviceEntry.toMap();
			if (deviceMap.value(QStringLiteral("id")).toString() != device) {
				continue;
			}
			const auto platforms = deviceMap.value(
				QStringLiteral("platforms")).toStringList();
			if (platforms.contains(platform)) {
				return {};
			}
			return QStringLiteral("publish aborted: %1 device %2 is not "
				"available on %3 (supported: %4)")
				.arg(backendId, device, platform,
					platforms.join(QLatin1String(", ")));
		}
		return QStringLiteral("publish aborted: unknown device %1 for "
			"backend %2").arg(device, backendId);
	}
	return QStringLiteral("publish aborted: unknown backend %1")
		.arg(backendId);
}

void DesignerBackend::setRuntimeTemplate(
		const QString& platform, const QString& path) {
	runtimeOverrides_.insert(platform, toLocalFile(path));
	QSettings(QStringLiteral("VisionRuntime"),
		QStringLiteral("visionDesigner"))
		.setValue(QStringLiteral("runtimeTemplate/%1").arg(platform),
			toLocalFile(path));
}

void DesignerBackend::setStatus(const QString& message) {
	if (statusMessage_ == message) {
		return;
	}
	statusMessage_ = message;
	emit statusMessageChanged();
}

void DesignerBackend::setFilePath(const QString& path) {
	if (filePath_ == path) {
		return;
	}
	filePath_ = path;
	emit filePathChanged();
}

QString DesignerBackend::toLocalFile(const QString& pathOrUrl) {
	if (pathOrUrl.startsWith(QLatin1String("file:"))) {
		return QUrl(pathOrUrl).toLocalFile();
	}
	return pathOrUrl;
}

QString DesignerBackend::detectControlsPath() {
	QDir dir(QCoreApplication::applicationDirPath());
	for (int level = 0; level < 8; ++level) {
		const auto candidate =
			dir.absoluteFilePath(QStringLiteral("Shell/controls"));
		if (QFileInfo::exists(
				candidate + QStringLiteral("/NodeView.qml"))) {
			return candidate;
		}
		if (!dir.cdUp()) {
			break;
		}
	}
	return {};
}

QString DesignerBackend::detectRuntimeTemplate(const QString& platform) {
	const auto exeName = platform == QLatin1String("windows")
		? QStringLiteral("visionShell.exe") : QStringLiteral("visionShell");
	QDir dir(QCoreApplication::applicationDirPath());
	for (int level = 0; level < 8; ++level) {
		const auto candidate = dir.absoluteFilePath(
			QStringLiteral("Shell/release/visionShell"));
		if (QFileInfo::exists(candidate + QLatin1Char('/') + exeName)) {
			return candidate;
		}
		if (!dir.cdUp()) {
			break;
		}
	}
	return {};
}

} // namespace visionDesigner
