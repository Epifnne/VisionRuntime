#include "serviceClient.hpp"
#include "exports/productEntry.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>

#include <chrono>
#include <fstream>
#include <thread>

namespace {
using namespace std::chrono_literals;

bool await(const std::function<bool()>& predicate) {
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (std::chrono::steady_clock::now() < deadline) {
		QCoreApplication::processEvents();
		if (predicate()) { return true; }
		std::this_thread::sleep_for(2ms);
	}
	return false;
}

TEST(EventServiceClientTest, TwoViewsSummaryStopAndRecoveryUseTheSameBusinessSession) {
	visionShell::ServiceClient client(visionProduct::createApplication);
	ASSERT_TRUE(client.loadProfile(VISION_EVENT_TEST_PROFILE));
	std::size_t frames[2]{};
	std::vector<nlohmann::json> results;
	QObject::connect(&client, &visionShell::ServiceClient::frameReceived, &client,
		[&](auto index, const QImage& image) {
			ASSERT_LT(index, 2U);
			EXPECT_FALSE(image.isNull());
			++frames[index];
		});
	QObject::connect(&client, &visionShell::ServiceClient::stateChanged, &client,
		[&](const QString& name, const QString& payload) {
			if (name == "workpiece.result") {
				auto result = nlohmann::json::parse(payload.toStdString());
				if (result.value("decision", "NONE") != "NONE") { results.push_back(std::move(result)); }
			}
		});
	EXPECT_FALSE(client.invokeCommand("camera.top.softwareTrigger"));
	ASSERT_TRUE(client.invokeCommand("session.start"));
	ASSERT_TRUE(await([&] {
		return results.size() == 4 && client.stateSnapshot("session.state").contains("\"idle\"");
	}));
	EXPECT_EQ(frames[0], 4U);
	EXPECT_EQ(frames[1], 4U);
	EXPECT_EQ(results.back()["okCount"], 2);
	EXPECT_EQ(results.back()["ngCount"], 2);
	EXPECT_EQ(results.back()["faultCount"], 0);
	auto counts = nlohmann::json::parse(client.stateSnapshot("session.summary").toStdString());
	EXPECT_EQ(counts["received"], 8);
	EXPECT_EQ(counts["completed"], 8);
	EXPECT_EQ(counts["receivedPerSource"], nlohmann::json::array({4, 4}));
	EXPECT_EQ(counts["failed"], 0);
	auto outcome = nlohmann::json::parse(client.stateSnapshot("inspection.result").toStdString());
	EXPECT_TRUE(outcome.contains("captureId"));
	EXPECT_TRUE(outcome.contains("inspectionId"));
	const auto oldInspection = results.back()["inspectionId"].get<std::uint64_t>();
	ASSERT_TRUE(client.invokeCommand("session.recover"));
	const auto reset = nlohmann::json::parse(client.stateSnapshot("workpiece.result").toStdString());
	EXPECT_EQ(reset["okCount"], 0);
	EXPECT_EQ(reset["ngCount"], 0);
	ASSERT_TRUE(client.invokeCommand("session.start"));
	ASSERT_TRUE(await([&] {
		return results.size() == 8 && client.stateSnapshot("session.state").contains("\"idle\"");
	}));
	EXPECT_GT(results.back()["inspectionId"].get<std::uint64_t>(), oldInspection);
	EXPECT_EQ(results.back()["okCount"], 2);
	EXPECT_EQ(results.back()["ngCount"], 2);
	EXPECT_EQ(frames[0], 8U);
	EXPECT_EQ(frames[1], 8U);
}

TEST(EventServiceClientTest, StopCancelsPendingBusinessTimersAndRecoveryHasNoOldRequests) {
	QTemporaryDir directory;
	ASSERT_TRUE(directory.isValid());
	std::ifstream input(VISION_EVENT_TEST_PROFILE);
	auto profile = nlohmann::json::parse(input);
	profile["simulation"]["topDelayMilliseconds"] = 400;
	profile["simulation"]["sideDelayMilliseconds"] = 450;
	const auto path = directory.filePath("profile.json");
	std::ofstream(path.toStdString()) << profile.dump();
	visionShell::ServiceClient client(visionProduct::createApplication);
	ASSERT_TRUE(client.loadProfile(path));
	std::size_t frames = 0;
	QObject::connect(&client, &visionShell::ServiceClient::frameReceived, &client,
		[&](auto, const QImage&) { ++frames; });
	ASSERT_TRUE(client.invokeCommand("session.start"));
	ASSERT_TRUE(await([&] { return client.stateSnapshot("session.state").contains("\"running\""); }));
	ASSERT_TRUE(client.invokeCommand("session.stop"));
	ASSERT_TRUE(await([&] { return client.stateSnapshot("session.state").contains("\"idle\""); }));
	std::this_thread::sleep_for(500ms);
	QCoreApplication::processEvents();
	EXPECT_EQ(frames, 0U);
	ASSERT_TRUE(client.invokeCommand("session.recover"));
	profile["simulation"]["topDelayMilliseconds"] = 10;
	profile["simulation"]["sideDelayMilliseconds"] = 70;
	std::ofstream(path.toStdString()) << profile.dump();
	ASSERT_TRUE(client.invokeCommand("session.recover"));
	ASSERT_TRUE(client.invokeCommand("session.start"));
	ASSERT_TRUE(await([&] {
		return frames == 8 && client.stateSnapshot("session.state").contains("\"idle\"");
	}));
	auto result = nlohmann::json::parse(client.stateSnapshot("workpiece.result").toStdString());
	EXPECT_EQ(result["okCount"], 2);
	EXPECT_EQ(result["ngCount"], 2);
	EXPECT_EQ(result["faultCount"], 0);
}

TEST(EventServiceClientTest, MissingViewIsAnAggregateFaultNotAFrameExecutionFailure) {
	QTemporaryDir directory;
	ASSERT_TRUE(directory.isValid());
	std::ifstream input(VISION_EVENT_TEST_PROFILE);
	auto profile = nlohmann::json::parse(input);
	profile["simulation"]["missingView"] = true;
	const auto path = directory.filePath("profile.json");
	std::ofstream(path.toStdString()) << profile.dump();
	visionShell::ServiceClient client(visionProduct::createApplication);
	ASSERT_TRUE(client.loadProfile(path));
	ASSERT_TRUE(client.invokeCommand("session.start"));
	ASSERT_TRUE(await([&] {
		return client.stateSnapshot("workpiece.result").contains("\"faultCount\":1") &&
			client.stateSnapshot("session.state").contains("\"idle\"");
	}));
	auto result = nlohmann::json::parse(client.stateSnapshot("workpiece.result").toStdString());
	EXPECT_EQ(result["decision"], "FAULT");
	EXPECT_EQ(result["okCount"], 1);
	EXPECT_EQ(result["ngCount"], 2);
	auto counts = nlohmann::json::parse(client.stateSnapshot("session.summary").toStdString());
	EXPECT_EQ(counts["completed"], 7);
	EXPECT_EQ(counts["failed"], 0);
}
} // namespace
