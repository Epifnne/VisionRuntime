#include "exports/businessExports.hpp"
#include "exports/exportContract.hpp"
#include "manifest/manifestExporter.hpp"
#include "service/visionService.hpp"
#include "camera/fileSource.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/runtimeFactory.hpp"
#include "memory/cpuAllocator.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

using namespace visionRuntime;
using namespace visionService::exports;
namespace endpoints = visionService::endpoints;

struct Measurement {
	double width = 12.5;
	bool accepted = true;
	std::string part = "part-001";
};

StateSchema<Measurement> measurementSchema() {
	StateSchema<Measurement> schema;
	schema.field("width", &Measurement::width, "measured width", "mm")
		.field("accepted", &Measurement::accepted)
		.field("part", &Measurement::part);
	return schema;
}

TEST(BusinessExportsTest, MetricsPreserveOptionalWorkpieceCaptureIdentity) {
	endpoints::EndpointDispatcher dispatcher;
	endpoints::EndpointRegistry registry(dispatcher);
	ASSERT_TRUE(registry.registerState({.name = "inspection.result",
		.current = [] { return endpoints::StateSnapshot{}; }}));
	visionService::session::SessionController controller;
	visionService::metrics::MetricsAggregator metrics(registry, controller);
	pipeline::FrameIdentity identity{.executionId = 5, .sourceId = 2,
		.metadata = {.sequenceNumber = 9, .capture = vision::CaptureIdentity{10, 11, 12, 2}}};
	metrics.observeOutcome(executor::FrameOutcome::Completed, identity, 4, core::Status::ok());
	const auto payload = nlohmann::json::parse(metrics.resultPayload());
	EXPECT_EQ(payload.at("workpieceId"), 10);
	EXPECT_EQ(payload.at("inspectionId"), 11);
	EXPECT_EQ(payload.at("captureId"), 12);
	EXPECT_EQ(payload.at("viewId"), 2);
	EXPECT_EQ(payload.at("executionId"), 5);
	EXPECT_EQ(payload.at("sequenceNumber"), 9);
	identity.metadata.capture.reset();
	metrics.observeOutcome(executor::FrameOutcome::Completed, identity, 4, core::Status::ok());
	EXPECT_FALSE(nlohmann::json::parse(metrics.resultPayload()).contains("workpieceId"));
}

TEST(BusinessExportsTest, DeclarationRegistersBindingsAndTheSameFieldSchema) {
	endpoints::EndpointDispatcher dispatcher;
	endpoints::EndpointRegistry registry(dispatcher);
	BusinessExports exports;
	auto state = exports.state({.name = "measure.result"}, measurementSchema(), Measurement{});
	ASSERT_TRUE(state);
	std::atomic<double> tolerance{0.5};
	exports.parameter({
		.descriptor = {.name = "measure.tolerance", .type = endpoints::ParameterType::Decimal,
			.writable = true, .minimum = 0.0, .maximum = 5.0},
		.read = [&] { return core::Result<endpoints::ParameterValue>::success(tolerance.load()); },
		.write = [&](const auto& value) {
			tolerance = std::get<double>(value);
			return core::Result<void>::success();
		}});
	exports.command({.name = "measure.reset",
		.invoke = [state = state.value()] { return state->set(Measurement{}); }});
	ASSERT_TRUE(exports.registerWith(registry));
	EXPECT_FALSE(exports.registerWith(registry));
	const auto manifest = nlohmann::json::parse(visionService::manifest::exportManifest(registry));
	EXPECT_EQ(manifest["schemaVersion"]["minor"], 1);
	for (const auto& endpoint : manifest["endpoints"]) {
		if (endpoint["name"] == "measure.result") {
			EXPECT_EQ(endpoint["fields"]["width"]["valueType"], "decimal");
			EXPECT_EQ(endpoint["fields"]["width"]["unit"], "mm");
			EXPECT_EQ(endpoint["fields"]["accepted"]["valueType"], "flag");
			EXPECT_EQ(endpoint["fields"]["part"]["valueType"], "text");
		}
	}
	std::vector<std::string> published;
	auto subscribed = registry.subscribeState("measure.result", [&](const auto& snapshot) {
		published.push_back(snapshot.payload);
	});
	ASSERT_TRUE(subscribed);
	ASSERT_TRUE(state.value()->set({20, false, "part-002"}));
	ASSERT_TRUE(registry.invokeCommand("measure.reset"));
	dispatcher.drain();
	ASSERT_EQ(published.size(), 2U);
	EXPECT_EQ(nlohmann::json::parse(published.front())["part"], "part-002");
	EXPECT_EQ(nlohmann::json::parse(published.back())["width"], 12.5);
	ASSERT_TRUE(registry.writeParameter("measure.tolerance", 2.0));
	EXPECT_EQ(tolerance, 2.0);
	EXPECT_FALSE(registry.writeParameter("measure.tolerance", 8.0));
	EXPECT_FALSE(registry.writeParameter("measure.tolerance", true));
	EXPECT_FALSE(registry.writeParameter("measure.tolerance",
		std::numeric_limits<double>::quiet_NaN()));
	EXPECT_EQ(tolerance, 2.0);
}

TEST(BusinessExportsTest, InvalidSchemasAndPayloadsFailWithoutReplacingTheLastValue) {
	BusinessExports exports;
	StateSchema<Measurement> duplicate;
	duplicate.field("width", &Measurement::width).field("width", &Measurement::width);
	EXPECT_FALSE(exports.state({.name = "bad"}, std::move(duplicate), Measurement{}));
	auto state = exports.state({.name = "good"}, measurementSchema(), Measurement{});
	ASSERT_TRUE(state);
	const auto before = state.value()->snapshot().payload;
	EXPECT_FALSE(state.value()->set({std::numeric_limits<double>::infinity(), true, "bad"}));
	EXPECT_FALSE(state.value()->set({1, true, std::string(1, static_cast<char>(0xff))}));
	EXPECT_EQ(state.value()->snapshot().payload, before);
}

TEST(BusinessExportsTest, RegistrationFailureRollsBackOnlyItsOwnEndpoints) {
	endpoints::EndpointDispatcher dispatcher;
	endpoints::EndpointRegistry registry(dispatcher);
	ASSERT_TRUE(registry.registerCommand({.name = "existing",
		.invoke = [] { return core::Result<void>::success(); }}));
	BusinessExports exports;
	exports.command({.name = "new", .invoke = [] { return core::Result<void>::success(); }})
		.command({.name = "existing", .invoke = [] { return core::Result<void>::success(); }});
	EXPECT_FALSE(exports.registerWith(registry));
	EXPECT_TRUE(registry.contains("existing"));
	EXPECT_FALSE(registry.contains("new"));
}

TEST(BusinessExportsTest, TypedParametersDeriveMetadataAndPropagateBusinessErrors) {
	endpoints::EndpointDispatcher dispatcher;
	endpoints::EndpointRegistry registry(dispatcher);
	BusinessExports exports;
	exports.parameter<std::int64_t>({.name = "count"},
		[] { return core::Result<std::int64_t>::success(42); });
	exports.parameter<bool>({.name = "enabled"},
		[] { return core::Result<bool>::success(false); },
		[](const bool&) { return core::Result<void>::failure(core::Status::error(
			core::StatusCode::InvalidState, "machine is running")); });
	ASSERT_TRUE(exports.registerWith(registry));
	EXPECT_FALSE(registry.writeParameter("count", std::int64_t{1}));
	auto written = registry.writeParameter("enabled", true);
	ASSERT_FALSE(written);
	EXPECT_EQ(written.status().code(), core::StatusCode::InvalidState);
	EXPECT_EQ(std::get<std::int64_t>(registry.readParameter("count").value()), 42);
	const auto descriptors = registry.parameters();
	for (const auto& parameter : descriptors) {
		if (parameter.name == "count") {
			EXPECT_EQ(parameter.type, endpoints::ParameterType::Integer);
			EXPECT_FALSE(parameter.writable);
		}
	}
}

TEST(BusinessExportsTest, ExportedStreamsPublishFramesAndDisconnectWithTheProduct) {
	endpoints::EndpointDispatcher dispatcher;
	endpoints::EndpointRegistry registry(dispatcher);
	BusinessExports exports;
	auto stream = exports.stream({.name = "processed.side", .sourceId = 3});
	auto buffer = memory::CpuAllocator{}.allocate(4);
	ASSERT_TRUE(buffer);
	auto frame = visionRuntime::vision::Frame::create(
		std::move(buffer).value(), 2, 2, visionRuntime::vision::PixelFormat::Gray8);
	ASSERT_TRUE(frame);
	ASSERT_TRUE(exports.registerWith(registry));
	std::atomic_size_t delivered{0};
	auto subscribed = registry.subscribeStream("processed.side", [&](auto source, const auto& image) {
		EXPECT_EQ(source, 3U);
		EXPECT_EQ(image.width(), 2U);
		++delivered;
	});
	ASSERT_TRUE(subscribed);
	ASSERT_TRUE(stream->publish(std::move(frame).value()));
	dispatcher.drain();
	EXPECT_EQ(delivered, 1U);
	exports.disconnect();
	EXPECT_FALSE(stream->publish({}));
}

TEST(BusinessExportsTest, OldStateCannotPublishIntoARecoveredRegistry) {
	endpoints::EndpointDispatcher dispatcher;
	endpoints::EndpointRegistry registry(dispatcher);
	std::shared_ptr<ExportedState<Measurement>> oldState;
	{
		BusinessExports exports;
		auto state = exports.state({.name = "measure.result"}, measurementSchema(), Measurement{});
		ASSERT_TRUE(state);
		oldState = state.value();
		ASSERT_TRUE(exports.registerWith(registry));
	}
	registry.clear();
	BusinessExports replacement;
	auto current = replacement.state({.name = "measure.result"}, measurementSchema(), Measurement{});
	ASSERT_TRUE(current);
	ASSERT_TRUE(replacement.registerWith(registry));
	EXPECT_FALSE(oldState->set({99, false, "old"}));
	EXPECT_EQ(nlohmann::json::parse(current.value()->snapshot().payload)["width"], 12.5);
}

TEST(BusinessExportsTest, PureContractDescribesFieldsWithoutConstructingAResultOrCallingHandlers) {
	struct NonDefaultResult {
		explicit NonDefaultResult(double value) : width(value) {}
		double width;
	};
	ExportContract contract;
	StateSchema<NonDefaultResult> schema;
	schema.field("width", &NonDefaultResult::width, "width", "mm");
	const auto state = contract.state({.name = "measurement"}, std::move(schema));
	const auto parameter = contract.parameter<double>({.name = "threshold", .writable = true});
	visionService::profile::ProductProfile profile;
	profile.runtimeProduct = "offline-test";
	profile.sources.push_back({.id = "unavailable-camera", .type = "camera"});
	auto description = contract.describe(profile);
	ASSERT_TRUE(description);
	const auto document = nlohmann::json::parse(description.value());
	EXPECT_EQ(document["runtimeProduct"], "offline-test");
	EXPECT_NE(description->find("\"width\""), std::string::npos);
	BusinessExports bindings;
	EXPECT_FALSE(parameter.bind(bindings, [] { return core::Result<double>::success(1.0); }));
	EXPECT_TRUE(state.bind(bindings, NonDefaultResult{42}));
	EXPECT_FALSE(validateExportContract(description.value(), R"({"endpoints":[]})"));
}

TEST(BusinessExportsTest, OfflineContractRejectsDuplicateNamesAndInvalidSchemas) {
	visionService::profile::ProductProfile profile;
	profile.runtimeProduct = "offline-test";
	ExportContract duplicate;
	static_cast<void>(duplicate.command({.name = "session.start"}));
	EXPECT_FALSE(duplicate.describe(profile));
	ExportContract invalid;
	StateSchema<Measurement> fields;
	fields.field("width", &Measurement::width).field("width", &Measurement::width);
	static_cast<void>(invalid.state({.name = "measurement"}, std::move(fields)));
	EXPECT_FALSE(invalid.describe(profile));
}

class MeasureAlgorithm final : public pipeline::IOpenCvAlgorithm<Measurement> {
public:
	core::Result<Measurement> process(const pipeline::PipelinePacket&) override {
		return core::Result<Measurement>::success(Measurement{25, false, "opencv-part"});
	}
};

class BusinessProductFixture : public testing::Test {
protected:
	void SetUp() override {
		root = std::filesystem::temp_directory_path() /
			("business-exports-" + std::to_string(
				std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(root);
		std::ofstream(root / "frame.pgm") << "P2\n1 1\n255\n200\n";
		document = {{"schemaVersion", {{"major", 1}}}, {"id", "business-test"},
			{"runtimeProduct", "business-test"},
			{"sources", {{{"id", "side"}, {"type", "directory"},
				{"directory", "."}, {"extensions", {".pgm"}}, {"loop", false}}}},
			{"endpoints", {{{"name", "measure.result"}}, {{"name", "session.start"}}}}};
		std::ofstream(root / "profile.json") << document.dump();
	}
	void TearDown() override {
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}
	core::Result<visionService::ProductApplication> create(
		const visionService::profile::ProductProfile& profile) {
		visionService::ProductApplication application;
		auto state = application.exports.state(
			{.name = "measure.result"}, measurementSchema(), Measurement{});
		if (!state) {
			return core::Result<visionService::ProductApplication>::failure(state.status());
		}
		lastState = state.value();
		auto source = camera::FileSource::create({
			.directory = profile.sources.front().directory.directory, .extensions = {".pgm"}});
		if (!source) {
			return core::Result<visionService::ProductApplication>::failure(source.status());
		}
		pipeline::OpenCvPipelineBuilder<Measurement> builder;
		auto pipeline = builder.setAlgorithm(std::make_unique<MeasureAlgorithm>()).build();
		if (!pipeline) {
			return core::Result<visionService::ProductApplication>::failure(pipeline.status());
		}
		executor::FrameExecutionOptions<Measurement> options;
		options.resultObserver = [state = state.value()](auto, const auto&, auto, const auto& result) {
			ASSERT_TRUE(result);
			EXPECT_TRUE(state->set(result.value()));
		};
		auto runtime = runtime::RuntimeFactory::createRuntime<Measurement>(
			std::move(source).value(),
			std::make_unique<pipeline::OpenCvPipeline<Measurement>>(std::move(pipeline).value()),
			config::DeploymentConfig{.executor = {
				.performancePolicy = config::PerformancePolicy::Serial}}, std::move(options));
		if (!runtime) {
			return core::Result<visionService::ProductApplication>::failure(runtime.status());
		}
		application.session = visionService::session::adoptProductRuntime<Measurement>(
			std::move(runtime).value());
		return core::Result<visionService::ProductApplication>::success(std::move(application));
	}
	std::filesystem::path root;
	nlohmann::json document;
	std::shared_ptr<ExportedState<Measurement>> lastState;
};

TEST_F(BusinessProductFixture, CustomOpenCvResultsWorkWithoutAModelAndSurviveRecovery) {
	visionService::VisionService service([this](const auto& profile) { return create(profile); });
	ASSERT_TRUE(service.loadProfile(root / "profile.json"));
	ASSERT_TRUE(service.endpoints().contains("measure.result"));
	std::atomic_size_t deliveries{0};
	auto subscribed = service.endpoints().subscribeState("measure.result", [&](const auto& snapshot) {
		EXPECT_EQ(nlohmann::json::parse(snapshot.payload)["part"], "opencv-part");
		++deliveries;
	});
	ASSERT_TRUE(subscribed);
	ASSERT_TRUE(service.endpoints().invokeCommand("session.start"));
	service.sessionController().waitForIdle();
	auto oldState = lastState;
	ASSERT_TRUE(service.endpoints().invokeCommand("session.recover"));
	EXPECT_FALSE(oldState->set(Measurement{}));
	ASSERT_TRUE(service.endpoints().invokeCommand("session.start"));
	service.sessionController().waitForIdle();
	auto snapshot = service.endpoints().stateSnapshot("measure.result");
	ASSERT_TRUE(snapshot);
	EXPECT_EQ(nlohmann::json::parse(snapshot->payload)["width"], 25);
	EXPECT_EQ(service.sessionController().counters().completed, 1U);
	auto outcome = service.endpoints().stateSnapshot("inspection.result");
	ASSERT_TRUE(outcome);
	EXPECT_EQ(nlohmann::json::parse(outcome->payload)["valid"], true);
	service.shutdown();
	EXPECT_EQ(deliveries, 1U);
}

TEST_F(BusinessProductFixture, FailedApplicationBindingLeavesNoStaleExports) {
	visionService::VisionService service([](const auto&) {
		visionService::ProductApplication application;
		application.exports.command({.name = "business.command",
			.invoke = [] { return core::Result<void>::success(); }});
		return core::Result<visionService::ProductApplication>::success(std::move(application));
	});
	EXPECT_FALSE(service.loadProfile(root / "profile.json"));
	EXPECT_TRUE(service.endpoints().endpointNames().empty());
}

TEST(BusinessExportsTest, ContractRejectsAnotherRuntimeProduct) {
	const std::string contract = R"({"runtimeProduct":"inspection","endpoints":[]})";
	EXPECT_TRUE(visionService::exports::validateExportContract(contract, contract));
	EXPECT_FALSE(visionService::exports::validateExportContract(
		contract, R"({"runtimeProduct":"another-product","endpoints":[]})"));
	EXPECT_FALSE(visionService::exports::validateExportContract(
		contract, R"({"endpoints":[]})"));
}

TEST_F(BusinessProductFixture, RuntimeCannotStartWithBindingsDifferentFromTheDeclaration) {
	visionService::VisionService service([this](const auto& profile) {
		auto application = create(profile);
		if (application) {
			application->expectedManifest = R"({"endpoints":[]})";
		}
		return application;
	});
	auto loaded = service.loadProfile(root / "profile.json");
	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), core::StatusCode::InvalidState);
	EXPECT_TRUE(service.endpoints().endpointNames().empty());
}

} // namespace
