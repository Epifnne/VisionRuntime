#include "designerBackend.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSettings>
#include <QUrl>

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
	if (entries.isEmpty()) {
		const auto message = QStringLiteral("manifest has no endpoints array");
		setStatus(message);
		return message;
	}
	QVariantList universe;
	universe.reserve(entries.size());
	for (const auto& entry : entries) {
		universe.append(entry.toObject().toVariantMap());
	}
	endpoints_ = universe;
	manifestLoaded_ = true;
	emit endpointsChanged();
	setStatus(QStringLiteral("manifest loaded: %1 endpoints from %2")
		.arg(endpoints_.size()).arg(local));
	return {};
}

void DesignerBackend::deriveEndpoints(const QString& profileJson) {
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
	if (id.isEmpty()) {
		issues.append(makeIssue(QStringLiteral("profile"),
			QStringLiteral("product id must not be empty"),
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
		if (sourceId.isEmpty()) {
			issues.append(makeIssue(path,
				QStringLiteral("source id must not be empty"),
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
		} else {
			issues.append(makeIssue(path,
				QStringLiteral("unknown source type: %1").arg(type),
				QStringLiteral("error")));
		}
	}

	// Model and pipeline.
	const auto model = root.value(QStringLiteral("model")).toObject();
	if (model.value(QStringLiteral("packagePath")).toString().isEmpty()) {
		issues.append(makeIssue(QStringLiteral("model"),
			QStringLiteral("model.packagePath must not be empty"),
			QStringLiteral("error")));
	}
	if (model.value(QStringLiteral("backendId")).toString().isEmpty()) {
		issues.append(makeIssue(QStringLiteral("model"),
			QStringLiteral("model.backendId must not be empty"),
			QStringLiteral("error")));
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
	const QString& targetDirectory, const QString& jsonText) {
	if (!validate(jsonText)) {
		setStatus(QStringLiteral("publish aborted: validation errors"));
		return false;
	}
	const auto document = QJsonDocument::fromJson(jsonText.toUtf8());
	const auto id = document.object()
		.value(QStringLiteral("id")).toString();
	if (id.isEmpty()) {
		setStatus(QStringLiteral("publish aborted: product id is empty"));
		return false;
	}
	const auto local = toLocalFile(targetDirectory);
	const auto productDirectory = QDir(local).absoluteFilePath(id);
	if (!QDir().mkpath(productDirectory)) {
		setStatus(QStringLiteral("cannot create %1").arg(productDirectory));
		return false;
	}
	const auto profilePath =
		productDirectory + QStringLiteral("/profile.json");
	QFile file(profilePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		setStatus(QStringLiteral("cannot write %1").arg(profilePath));
		return false;
	}
	file.write(document.toJson(QJsonDocument::Indented));
	file.write("\n");
	setStatus(QStringLiteral("published %1").arg(profilePath));
	return true;
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

} // namespace visionDesigner
