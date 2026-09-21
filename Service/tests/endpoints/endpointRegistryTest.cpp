#include "endpoints/endpointDispatcher.hpp"
#include "endpoints/endpointRegistry.hpp"
#include "memory/cpuAllocator.hpp"
#include "vision/frame.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <string>
#include <thread>

namespace {

using visionRuntime::core::Result;
using visionRuntime::core::Status;
using visionRuntime::core::StatusCode;
using visionService::endpoints::AccessLevel;
using visionService::endpoints::CommandEndpoint;
using visionService::endpoints::EndpointDispatcher;
using visionService::endpoints::EndpointRegistry;
using visionService::endpoints::ParameterEndpoint;
using visionService::endpoints::ParameterType;
using visionService::endpoints::ParameterValue;
using visionService::endpoints::StateEndpoint;
using visionService::endpoints::StateSnapshot;
using visionService::endpoints::StreamEndpoint;

[[nodiscard]] bool waitLatchFor(
	std::latch& latch,
	std::chrono::milliseconds timeout) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (latch.try_wait()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return latch.try_wait();
}

class RegistryFixture : public ::testing::Test {
protected:
	EndpointDispatcher dispatcher;
	EndpointRegistry registry{dispatcher};
};

TEST_F(RegistryFixture, RegistersAndReadsParameter) {
	auto registered = registry.registerParameter(ParameterEndpoint{
		.descriptor = {
			.name = "camera.cam0.exposureMicroseconds",
			.type = ParameterType::Decimal,
			.writable = true,
			.accessLevel = AccessLevel::Engineer,
			.minimum = 1.0,
			.maximum = 1000000.0,
		},
		.read = [] { return Result<ParameterValue>::success(12000.0); },
		.write = [](const ParameterValue&) { return Result<void>::success(); },
	});
	ASSERT_TRUE(registered) << registered.status().toString();

	auto value = registry.readParameter("camera.cam0.exposureMicroseconds");
	ASSERT_TRUE(value) << value.status().toString();
	EXPECT_DOUBLE_EQ(std::get<double>(value.value()), 12000.0);
}

TEST_F(RegistryFixture, RejectsDuplicateEndpointNames) {
	CommandEndpoint command{
		.name = "session.start",
		.invoke = [] { return Result<void>::success(); },
	};
	ASSERT_TRUE(registry.registerCommand(command));
	auto duplicate = registry.registerCommand(command);
	ASSERT_FALSE(duplicate);
	EXPECT_EQ(duplicate.status().code(), StatusCode::AlreadyExists);
}

TEST_F(RegistryFixture, WriteParameterValidatesTypeRangeAndWritability) {
	std::atomic<double> applied{0.0};
	ASSERT_TRUE(registry.registerParameter(ParameterEndpoint{
		.descriptor = {
			.name = "gain",
			.type = ParameterType::Decimal,
			.writable = true,
			.minimum = 0.0,
			.maximum = 24.0,
		},
		.read = [] { return Result<ParameterValue>::success(0.0); },
		.write = [&applied](const ParameterValue& value) {
			applied.store(std::get<double>(value));
			return Result<void>::success();
		},
	}));

	auto wrongType = registry.writeParameter("gain", ParameterValue{"high"});
	EXPECT_EQ(wrongType.status().code(), StatusCode::InvalidArgument);

	auto outOfRange = registry.writeParameter("gain", ParameterValue{48.0});
	EXPECT_EQ(outOfRange.status().code(), StatusCode::InvalidArgument);

	auto accepted = registry.writeParameter("gain", ParameterValue{12.5});
	ASSERT_TRUE(accepted) << accepted.status().toString();
	EXPECT_DOUBLE_EQ(applied.load(), 12.5);

	ASSERT_TRUE(registry.registerParameter(ParameterEndpoint{
		.descriptor = {.name = "productName", .type = ParameterType::Text},
		.read = [] { return Result<ParameterValue>::success(std::string{"o-ring"}); },
		.write = {},
	}));
	auto readOnly = registry.writeParameter(
		"productName", ParameterValue{std::string{"other"}});
	EXPECT_EQ(readOnly.status().code(), StatusCode::Unsupported);
}

TEST_F(RegistryFixture, InvokesCommands) {
	std::atomic<int> invocations{0};
	ASSERT_TRUE(registry.registerCommand(CommandEndpoint{
		.name = "session.start",
		.invoke = [&invocations] {
			invocations.fetch_add(1);
			return Result<void>::success();
		},
	}));
	ASSERT_TRUE(registry.invokeCommand("session.start"));
	ASSERT_TRUE(registry.invokeCommand("session.start"));
	EXPECT_EQ(invocations.load(), 2);

	auto missing = registry.invokeCommand("session.stop");
	EXPECT_EQ(missing.status().code(), StatusCode::NotFound);
}

TEST_F(RegistryFixture, PublishesVersionedStateSnapshotsOnDispatchThread) {
	std::atomic<int> counter{0};
	ASSERT_TRUE(registry.registerState(StateEndpoint{
		.name = "session.state",
		.current = [&counter] {
			return StateSnapshot{
				.payload = "{\"count\":" + std::to_string(counter.load()) + "}",
			};
		},
	}));

	std::latch delivered(2);
	std::atomic<std::uint64_t> lastVersion{0};
	std::string lastPayload;
	auto subscription = registry.subscribeState(
		"session.state",
		[&](const StateSnapshot& snapshot) {
			lastVersion.store(snapshot.version);
			lastPayload = snapshot.payload;
			delivered.count_down();
		});
	ASSERT_TRUE(subscription) << subscription.status().toString();

	counter.store(1);
	ASSERT_TRUE(registry.publishState("session.state"));
	counter.store(2);
	ASSERT_TRUE(registry.publishState("session.state"));

	ASSERT_TRUE(waitLatchFor(delivered, std::chrono::seconds(5)));
	EXPECT_EQ(lastVersion.load(), 2U);
	EXPECT_NE(lastPayload.find("\"count\":2"), std::string::npos);

	registry.unsubscribe(subscription.value());
	auto snapshot = registry.stateSnapshot("session.state");
	ASSERT_TRUE(snapshot);
	EXPECT_EQ(snapshot->version, 2U);
}

TEST_F(RegistryFixture, StreamsCoalescePendingFramesToLatest) {
	ASSERT_TRUE(registry.registerStream(StreamEndpoint{
		.name = "stream.cam0",
		.sourceId = 0,
	}));

	std::latch delivered(1);
	std::atomic<std::uint64_t> lastSequence{0};
	std::atomic<std::size_t> callbacks{0};
	auto subscription = registry.subscribeStream(
		"stream.cam0",
		[&](std::uint32_t sourceId, const visionRuntime::vision::Frame& frame) {
			EXPECT_EQ(sourceId, 0U);
			lastSequence.store(frame.metadata().sequenceNumber);
			callbacks.fetch_add(1);
			// Slow subscriber: blocks the dispatch thread so that further
			// publishFrame calls only replace the pending frame.
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
			delivered.count_down();
		});
	ASSERT_TRUE(subscription) << subscription.status().toString();

	for (std::uint64_t sequence = 1; sequence <= 8; ++sequence) {
		auto buffer = visionRuntime::memory::CpuAllocator{}.allocate(1);
		ASSERT_TRUE(buffer);
		visionRuntime::vision::FrameMetadata metadata;
		metadata.sequenceNumber = sequence;
		auto frame = visionRuntime::vision::Frame::create(
			std::move(buffer).value(), 1, 1,
			visionRuntime::vision::PixelFormat::Gray8, 0, metadata);
		ASSERT_TRUE(frame);
		registry.publishFrame("stream.cam0", 0, std::move(frame).value());
	}

	// Wait until all 8 frames have been observed or dropped, then read the
	// last delivered sequence.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (std::chrono::steady_clock::now() < deadline &&
		callbacks.load() + registry.droppedFrames("stream.cam0") < 8U) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	EXPECT_EQ(lastSequence.load(), 8U);
	EXPECT_LE(callbacks.load(), 8U);
}

TEST_F(RegistryFixture, ListsDescriptorsForManifest) {
	ASSERT_TRUE(registry.registerParameter(ParameterEndpoint{
		.descriptor = {
			.name = "threshold",
			.type = ParameterType::Decimal,
			.writable = true,
		},
		.read = [] { return Result<ParameterValue>::success(2.0); },
		.write = [](const ParameterValue&) { return Result<void>::success(); },
	}));
	ASSERT_TRUE(registry.registerCommand(CommandEndpoint{
		.name = "session.start",
		.invoke = [] { return Result<void>::success(); },
	}));
	ASSERT_TRUE(registry.registerState(StateEndpoint{
		.name = "session.state",
		.current = [] { return StateSnapshot{}; },
	}));
	ASSERT_TRUE(registry.registerStream(StreamEndpoint{
		.name = "stream.cam0",
		.sourceId = 3,
	}));

	EXPECT_EQ(registry.parameters().size(), 1U);
	EXPECT_EQ(registry.commands().size(), 1U);
	EXPECT_EQ(registry.states().size(), 1U);
	ASSERT_EQ(registry.streams().size(), 1U);
	EXPECT_EQ(registry.streams().front().sourceId, 3U);
	EXPECT_TRUE(registry.contains("threshold"));
	EXPECT_EQ(registry.endpointNames().size(), 4U);

	auto applied = registry.setAccessLevel("threshold", AccessLevel::Administrator);
	ASSERT_TRUE(applied);
	EXPECT_EQ(registry.parameters().front().accessLevel,
		AccessLevel::Administrator);
}

TEST_F(RegistryFixture, ClearRemovesAllEndpoints) {
	ASSERT_TRUE(registry.registerCommand(CommandEndpoint{
		.name = "session.stop",
		.invoke = [] { return Result<void>::success(); },
	}));
	registry.clear();
	EXPECT_FALSE(registry.contains("session.stop"));
	EXPECT_TRUE(registry.endpointNames().empty());
}

} // namespace
