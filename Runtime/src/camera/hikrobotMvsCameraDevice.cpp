#include "camera/hikrobotMvsCameraDevice.hpp"

#include "core/status.hpp"
#include "core/tensorBuffer.hpp"
#include "vision/frame.hpp"

#include <MvCameraControl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace visionRuntime::camera {
namespace {

std::mutex deviceEnumerationMutex;

[[nodiscard]] core::Status error(core::StatusCode code, std::string message) {
	return core::Status::error(code, std::move(message));
}

[[nodiscard]] core::Status mvsError(const char* operation, int code) {
	core::StatusCode statusCode = core::StatusCode::BackendError;
	switch (static_cast<unsigned int>(code)) {
	case MV_E_PARAMETER:
	case MV_E_GC_ARGUMENT:
	case MV_E_GC_RANGE:
		statusCode = core::StatusCode::InvalidArgument;
		break;
	case MV_E_SUPPORT:
	case MV_E_NOT_IMPLEMENTED:
		statusCode = core::StatusCode::Unsupported;
		break;
	case MV_E_RESOURCE:
	case MV_E_NOENOUGH_BUF:
	case MV_E_NOOUTBUF:
		statusCode = core::StatusCode::ResourceExhausted;
		break;
	case MV_E_NODATA:
	case MV_E_GC_TIMEOUT:
		statusCode = core::StatusCode::DeadlineExceeded;
		break;
	case MV_E_BUSY:
	case MV_E_NETER:
	case MV_E_USB_DEVICE:
		statusCode = core::StatusCode::Unavailable;
		break;
	case MV_E_ABNORMAL_IMAGE:
	case MV_E_PACKET:
		statusCode = core::StatusCode::DataLoss;
		break;
	default:
		break;
	}

	std::ostringstream message;
	message << operation << " failed with MVS error 0x" << std::hex
		<< std::uppercase << static_cast<unsigned int>(code);
	return error(statusCode, message.str());
}

[[nodiscard]] bool isTimeout(int code) noexcept {
	return static_cast<unsigned int>(code) == MV_E_NODATA ||
		static_cast<unsigned int>(code) == MV_E_GC_TIMEOUT ||
		static_cast<unsigned int>(code) == MV_E_NOOUTBUF;
}

template<std::size_t Size>
[[nodiscard]] std::string fixedString(const unsigned char (&text)[Size]) {
	const auto* begin = reinterpret_cast<const char*>(text);
	const auto* end = std::find(begin, begin + Size, '\0');
	return {begin, end};
}

[[nodiscard]] std::string ipv4Address(unsigned int address) {
	std::ostringstream text;
	text << ((address >> 24U) & 0xffU) << '.'
		 << ((address >> 16U) & 0xffU) << '.'
		 << ((address >> 8U) & 0xffU) << '.' << (address & 0xffU);
	return text.str();
}

[[nodiscard]] core::Result<CameraDeviceInfo> parseDeviceInfo(
	const MV_CC_DEVICE_INFO& mvsInfo) {
	CameraDeviceInfo info;
	if (mvsInfo.nTLayerType == MV_GIGE_DEVICE) {
		const auto& gigE = mvsInfo.SpecialInfo.stGigEInfo;
		info.transport = CameraTransport::GigE;
		info.serialNumber = fixedString(gigE.chSerialNumber);
		info.modelName = fixedString(gigE.chModelName);
		info.userDefinedName = fixedString(gigE.chUserDefinedName);
		info.ipAddress = ipv4Address(gigE.nCurrentIp);
	} else if (mvsInfo.nTLayerType == MV_USB_DEVICE) {
		const auto& usb = mvsInfo.SpecialInfo.stUsb3VInfo;
		info.transport = CameraTransport::Usb;
		info.serialNumber = fixedString(usb.chSerialNumber);
		info.modelName = fixedString(usb.chModelName);
		info.userDefinedName = fixedString(usb.chUserDefinedName);
	} else {
		return core::Result<CameraDeviceInfo>::failure(
			error(core::StatusCode::Unsupported, "unsupported MVS transport"));
	}
	if (info.serialNumber.empty()) {
		return core::Result<CameraDeviceInfo>::failure(
			error(core::StatusCode::DataLoss, "MVS device has no serial number"));
	}
	return core::Result<CameraDeviceInfo>::success(std::move(info));
}

[[nodiscard]] core::Result<unsigned int> mvsPixelFormat(
	vision::PixelFormat format) {
	switch (format) {
	case vision::PixelFormat::Gray8:
		return core::Result<unsigned int>::success(PixelType_Gvsp_Mono8);
	case vision::PixelFormat::Gray16:
		return core::Result<unsigned int>::success(PixelType_Gvsp_Mono16);
	case vision::PixelFormat::Rgb8:
		return core::Result<unsigned int>::success(PixelType_Gvsp_RGB8_Packed);
	case vision::PixelFormat::Bgr8:
		return core::Result<unsigned int>::success(PixelType_Gvsp_BGR8_Packed);
	case vision::PixelFormat::Rgba8:
		return core::Result<unsigned int>::success(PixelType_Gvsp_RGBA8_Packed);
	case vision::PixelFormat::Bgra8:
		return core::Result<unsigned int>::success(PixelType_Gvsp_BGRA8_Packed);
	case vision::PixelFormat::Float32Gray:
		break;
	}
	return core::Result<unsigned int>::failure(error(
		core::StatusCode::Unsupported,
		"MVS zero-copy capture does not support the requested pixel format"));
}

[[nodiscard]] core::Result<vision::PixelFormat> pixelFormat(
	MvGvspPixelType format) {
	switch (format) {
	case PixelType_Gvsp_Mono8:
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Gray8);
	case PixelType_Gvsp_Mono16:
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Gray16);
	case PixelType_Gvsp_RGB8_Packed:
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Rgb8);
	case PixelType_Gvsp_BGR8_Packed:
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Bgr8);
	case PixelType_Gvsp_RGBA8_Packed:
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Rgba8);
	case PixelType_Gvsp_BGRA8_Packed:
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Bgra8);
	default:
		return core::Result<vision::PixelFormat>::failure(error(
			core::StatusCode::Unsupported,
			"MVS returned a pixel format that requires conversion"));
	}
}

class DeviceState {
public:
	explicit DeviceState(void* handle) noexcept : handle_(handle) {}

	~DeviceState() {
		if (handle_ != nullptr) {
			if (opened_) {
				MV_CC_CloseDevice(handle_);
			}
			MV_CC_DestroyHandle(handle_);
		}
	}

	[[nodiscard]] void* handle() const noexcept { return handle_; }
	void markOpened() noexcept { opened_ = true; }

private:
	void* handle_ = nullptr;
	bool opened_ = false;
};

class PendingDevice {
public:
	explicit PendingDevice(void* handle) noexcept : handle_(handle) {}

	~PendingDevice() {
		if (handle_ != nullptr) {
			if (opened_) {
				MV_CC_CloseDevice(handle_);
			}
			MV_CC_DestroyHandle(handle_);
		}
	}

	void markOpened() noexcept { opened_ = true; }
	void release() noexcept { handle_ = nullptr; }

private:
	void* handle_ = nullptr;
	bool opened_ = false;
};

class PendingFrame {
public:
	PendingFrame(std::shared_ptr<DeviceState> device, MV_FRAME_OUT frame)
		: device_(std::move(device)), frame_(frame) {}

	~PendingFrame() {
		if (!released_) {
			MV_CC_FreeImageBuffer(device_->handle(), &frame_);
		}
	}

	void release() noexcept { released_ = true; }

private:
	std::shared_ptr<DeviceState> device_;
	MV_FRAME_OUT frame_{};
	bool released_ = false;
};

class FrameLease {
public:
	FrameLease(std::shared_ptr<DeviceState> device, MV_FRAME_OUT frame)
		: device_(std::move(device)), frame_(frame) {}

	~FrameLease() {
		MV_CC_FreeImageBuffer(device_->handle(), &frame_);
	}

private:
	std::shared_ptr<DeviceState> device_;
	MV_FRAME_OUT frame_{};
};

[[nodiscard]] CameraCapabilities hikCapabilities() {
	return {
		.pixelFormats = {vision::PixelFormat::Gray8, vision::PixelFormat::Gray16,
			vision::PixelFormat::Rgb8, vision::PixelFormat::Bgr8,
			vision::PixelFormat::Rgba8, vision::PixelFormat::Bgra8},
		.supportsSoftwareTrigger = true,
		.supportsHardwareTimestamp = false,
		.supportsSdkBufferLease = true,
		.supportsUserBuffers = false,
	};
}

} // namespace

class HikrobotMvsCameraDevice::Impl {
public:
	Impl(CameraDeviceOptions options, CameraDeviceInfo info,
		std::shared_ptr<DeviceState> device)
		: options_(std::move(options)), info_(std::move(info)),
		  capabilities_(hikCapabilities()), device_(std::move(device)) {}

	~Impl() {
		requestStop();
		wait();
	}

	[[nodiscard]] core::Result<void> startAcquisition(
		CameraAcquisitionOptions acquisition, FrameCallback callback) {
		std::scoped_lock lock(lifecycleMutex_);
		if (started_) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidState,
				"MVS device has already started"));
		}
		if (!callback) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument, "frame callback must not be empty"));
		}
		if (!acquisition.isValid()) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument,
				"invalid camera acquisition options"));
		}
		if (acquisition.frameRate &&
			*acquisition.frameRate > std::numeric_limits<float>::max()) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument,
				"MVS acquisition frame rate is out of range"));
		}
		unsigned int triggerMode;
		switch (acquisition.mode) {
		case AcquisitionMode::Continuous:
			triggerMode = MV_TRIGGER_MODE_OFF;
			break;
		case AcquisitionMode::SoftwareTrigger:
			triggerMode = MV_TRIGGER_MODE_ON;
			break;
		default:
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument,
				"unsupported camera acquisition mode"));
		}
		auto status = MV_CC_SetEnumValue(
			device_->handle(), "TriggerMode", triggerMode);
		if (status == MV_OK && triggerMode == MV_TRIGGER_MODE_ON) {
			status = MV_CC_SetEnumValue(
				device_->handle(), "TriggerSource", MV_TRIGGER_SOURCE_SOFTWARE);
		}
		if (status == MV_OK && acquisition.frameRate) {
			status = MV_CC_SetBoolValue(
				device_->handle(), "AcquisitionFrameRateEnable", true);
		}
		if (status == MV_OK && acquisition.frameRate) {
			status = MV_CC_SetFloatValue(device_->handle(), "AcquisitionFrameRate",
				static_cast<float>(*acquisition.frameRate));
		}
		if (status != MV_OK) {
			return core::Result<void>::failure(
				mvsError("configure MVS acquisition", status));
		}
		status = MV_CC_StartGrabbing(device_->handle());
		if (status != MV_OK) {
			return core::Result<void>::failure(mvsError("MV_CC_StartGrabbing", status));
		}
		stopSource_ = std::stop_source{};
		running_ = true;
		try {
			worker_ = std::jthread([this, callback = std::move(callback),
				stopToken = stopSource_.get_token()](std::stop_token) mutable {
				run(stopToken, callback);
			});
		} catch (...) {
			running_ = false;
			MV_CC_StopGrabbing(device_->handle());
			return core::Result<void>::failure(error(
				core::StatusCode::ResourceExhausted,
				"failed to create MVS acquisition thread"));
		}
		acquisitionMode_ = acquisition.mode;
		started_ = true;
		return core::Result<void>::success();
	}

	void requestStop() noexcept {
		std::scoped_lock lock(lifecycleMutex_);
		stopSource_.request_stop();
	}

	void wait() noexcept {
		std::scoped_lock waitLock(waitMutex_);
		std::jthread worker;
		{
			std::scoped_lock lock(lifecycleMutex_);
			if (!worker_.joinable()) {
				running_ = false;
				return;
			}
			if (worker_.get_id() == std::this_thread::get_id()) {
				return;
			}
			joining_ = true;
			worker = std::move(worker_);
		}
		worker.join();
		MV_CC_StopGrabbing(device_->handle());
		{
			std::scoped_lock lock(lifecycleMutex_);
			running_ = false;
			joining_ = false;
		}
	}

	[[nodiscard]] bool isAcquiring() const noexcept { return running_; }

	[[nodiscard]] core::Result<void> softwareTrigger() {
		if (!started_ || acquisitionMode_ != AcquisitionMode::SoftwareTrigger) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidState,
				"software trigger requires SoftwareTrigger acquisition mode"));
		}
		if (!running_) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidState, "MVS device is not acquiring"));
		}
		const auto status = MV_CC_SetCommandValue(device_->handle(), "TriggerSoftware");
		return status == MV_OK
			? core::Result<void>::success()
			: core::Result<void>::failure(mvsError("software trigger", status));
	}

	[[nodiscard]] const CameraDeviceInfo& deviceInfo() const noexcept { return info_; }
	[[nodiscard]] const CameraCapabilities& capabilities() const noexcept {
		return capabilities_;
	}
	[[nodiscard]] vision::FrameSpec outputSpec() const {
		return {{options_.pixelFormat}, {}, {}, core::Device::cpu()};
	}

private:
	void run(std::stop_token stopToken, FrameCallback& callback) noexcept {
		std::uint32_t previousFrame = 0;
		std::uint64_t sequenceNumber = 0;
		bool hasPreviousFrame = false;
		try {
		while (!stopToken.stop_requested()) {
			MV_FRAME_OUT sdkFrame{};
			const auto timeout = static_cast<unsigned int>(options_.frameTimeout.count());
			const auto status = MV_CC_GetImageBuffer(
				device_->handle(), &sdkFrame, timeout);
			if (status != MV_OK) {
				if (isTimeout(status)) {
					continue;
				}
				invoke(callback, core::Result<vision::Frame>::failure(
					mvsError("MV_CC_GetImageBuffer", status)));
				break;
			}

			PendingFrame pendingFrame(device_, sdkFrame);
			auto lease = std::make_shared<FrameLease>(device_, sdkFrame);
			pendingFrame.release();
			auto buffer = core::TensorBuffer::share(
				lease, sdkFrame.pBufAddr, sdkFrame.stFrameInfo.nFrameLen,
				core::Device::cpu(), core::MemoryKind::Host, false);
			auto format = pixelFormat(sdkFrame.stFrameInfo.enPixelType);
			if (!buffer || !format) {
				invoke(callback, core::Result<vision::Frame>::failure(
					buffer ? format.status() : buffer.status()));
				continue;
			}

			const auto rawFrame = sdkFrame.stFrameInfo.nFrameNum;
			if (!hasPreviousFrame) {
				sequenceNumber = rawFrame;
				hasPreviousFrame = true;
			} else {
				sequenceNumber += static_cast<std::uint32_t>(rawFrame - previousFrame);
			}
			previousFrame = rawFrame;

			vision::FrameMetadata metadata;
			metadata.sequenceNumber = sequenceNumber;
			metadata.capturedAt = std::chrono::steady_clock::now();
			const auto width = static_cast<std::size_t>(sdkFrame.stFrameInfo.nWidth);
			const auto height = static_cast<std::size_t>(sdkFrame.stFrameInfo.nHeight);
			const auto rowStride = width * vision::pixelFormatSize(*format);
			auto frame = vision::Frame::create(
				std::move(buffer).value(), width, height, *format, rowStride, metadata);
			invoke(callback, std::move(frame));
		}
		} catch (const std::exception& exception) {
			invoke(callback, core::Result<vision::Frame>::failure(error(
				core::StatusCode::ResourceExhausted,
				std::string("MVS acquisition failed: ") + exception.what())));
		} catch (...) {
			invoke(callback, core::Result<vision::Frame>::failure(error(
				core::StatusCode::Internal,
				"MVS acquisition failed with an unknown exception")));
		}
		running_ = false;
	}

	static void invoke(FrameCallback& callback,
		core::Result<vision::Frame> frame) noexcept {
		try {
			callback(std::move(frame));
		} catch (...) {
		}
	}

	CameraDeviceOptions options_;
	CameraDeviceInfo info_;
	CameraCapabilities capabilities_;
	std::shared_ptr<DeviceState> device_;
	std::mutex waitMutex_;
	mutable std::mutex lifecycleMutex_;
	std::stop_source stopSource_;
	std::jthread worker_;
	std::atomic_bool running_ = false;
	AcquisitionMode acquisitionMode_ = AcquisitionMode::Continuous;
	bool started_ = false;
	bool joining_ = false;
};

core::Result<std::vector<CameraDeviceInfo>> HikrobotMvsCameraDevice::enumerate() {
	std::scoped_lock enumerationLock(deviceEnumerationMutex);
	MV_CC_DEVICE_INFO_LIST devices{};
	const auto status = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devices);
	if (status != MV_OK) {
		return core::Result<std::vector<CameraDeviceInfo>>::failure(
			mvsError("MV_CC_EnumDevices", status));
	}
	std::vector<CameraDeviceInfo> result;
	result.reserve(devices.nDeviceNum);
	for (unsigned int index = 0; index < devices.nDeviceNum; ++index) {
		if (devices.pDeviceInfo[index] == nullptr) {
			continue;
		}
		auto info = parseDeviceInfo(*devices.pDeviceInfo[index]);
		if (info) {
			result.push_back(std::move(info).value());
		}
	}
	return core::Result<std::vector<CameraDeviceInfo>>::success(std::move(result));
}

core::Result<std::unique_ptr<HikrobotMvsCameraDevice>> HikrobotMvsCameraDevice::create(
	CameraDeviceOptions options) {
	if ((!options.serialNumber.empty() && !options.ipAddress.empty()) ||
		options.maxFramesInFlight == 0 ||
		options.maxFramesInFlight > std::numeric_limits<unsigned int>::max() ||
		options.frameTimeout.count() <= 0 ||
		options.frameTimeout.count() > std::numeric_limits<unsigned int>::max()) {
		return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(error(
			core::StatusCode::InvalidArgument,
			"MVS device selection, frame count or timeout is invalid"));
	}
	auto configuredPixelFormat = mvsPixelFormat(options.pixelFormat);
	if (!configuredPixelFormat) {
		return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(
			configuredPixelFormat.status());
	}

	std::scoped_lock enumerationLock(deviceEnumerationMutex);
	MV_CC_DEVICE_INFO_LIST devices{};
	auto status = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devices);
	if (status != MV_OK) {
		return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(
			mvsError("MV_CC_EnumDevices", status));
	}
	MV_CC_DEVICE_INFO* selected = nullptr;
	CameraDeviceInfo selectedInfo;
	for (unsigned int index = 0; index < devices.nDeviceNum; ++index) {
		if (devices.pDeviceInfo[index] == nullptr) {
			continue;
		}
		auto info = parseDeviceInfo(*devices.pDeviceInfo[index]);
		if (info && (options.serialNumber.empty() ||
			info->serialNumber == options.serialNumber) &&
			(options.ipAddress.empty() ||
				info->ipAddress == options.ipAddress)) {
			if (selected != nullptr && options.serialNumber.empty() &&
				options.ipAddress.empty()) {
				return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(error(
					core::StatusCode::InvalidArgument,
					"multiple MVS cameras found; serial number or IP address is required"));
			}
			selected = devices.pDeviceInfo[index];
			selectedInfo = std::move(info).value();
		}
	}
	if (selected == nullptr) {
		return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(error(
			core::StatusCode::NotFound, "requested MVS camera was not found"));
	}

	void* rawHandle = nullptr;
	status = MV_CC_CreateHandleWithoutLog(&rawHandle, selected);
	if (status != MV_OK) {
		return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(
			mvsError("MV_CC_CreateHandleWithoutLog", status));
	}
	PendingDevice pendingDevice(rawHandle);
	status = MV_CC_OpenDevice(rawHandle, MV_ACCESS_Exclusive, 0);
	if (status != MV_OK) {
		return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(
			mvsError("MV_CC_OpenDevice", status));
	}
	pendingDevice.markOpened();
	auto device = std::make_shared<DeviceState>(rawHandle);
	device->markOpened();
	pendingDevice.release();

	if (selectedInfo.transport == CameraTransport::GigE) {
		const auto packetSize = MV_CC_GetOptimalPacketSize(rawHandle);
		if (packetSize > 0) {
			status = MV_CC_SetIntValue(
				rawHandle, "GevSCPSPacketSize", static_cast<unsigned int>(packetSize));
			if (status != MV_OK) {
				return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(
					mvsError("set GigE packet size", status));
			}
		}
	}
	status = MV_CC_SetImageNodeNum(
		rawHandle, static_cast<unsigned int>(options.maxFramesInFlight));
	if (status == MV_OK) {
		status = MV_CC_SetEnumValue(rawHandle, "PixelFormat", *configuredPixelFormat);
	}
	if (status != MV_OK) {
		return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(
			mvsError("configure MVS acquisition", status));
	}

	auto setFloat = [rawHandle](const char* autoNode, const char* valueNode,
		const std::optional<double>& value, bool allowZero) -> core::Result<void> {
		if (!value) {
			return core::Result<void>::success();
		}
		if (!std::isfinite(*value) || *value < 0.0 ||
			(!allowZero && *value == 0.0) ||
			*value > std::numeric_limits<float>::max()) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument, "MVS floating setting is out of range"));
		}
		if (autoNode != nullptr) {
			const auto autoStatus = MV_CC_SetEnumValue(rawHandle, autoNode, 0);
			if (autoStatus != MV_OK) {
				return core::Result<void>::failure(mvsError(autoNode, autoStatus));
			}
		}
		const auto valueStatus = MV_CC_SetFloatValue(
			rawHandle, valueNode, static_cast<float>(*value));
		return valueStatus == MV_OK ? core::Result<void>::success()
			: core::Result<void>::failure(mvsError(valueNode, valueStatus));
	};
	auto configured = setFloat(
		"ExposureAuto", "ExposureTime", options.exposureMicroseconds, false);
	if (configured) {
		configured = setFloat("GainAuto", "Gain", options.gain, true);
	}
	if (!configured) {
		return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::failure(
			configured.status());
	}

	auto impl = std::make_unique<Impl>(
		std::move(options), std::move(selectedInfo), std::move(device));
	return core::Result<std::unique_ptr<HikrobotMvsCameraDevice>>::success(
		std::unique_ptr<HikrobotMvsCameraDevice>(
			new HikrobotMvsCameraDevice(std::move(impl))));
}

HikrobotMvsCameraDevice::HikrobotMvsCameraDevice(std::unique_ptr<Impl> impl) noexcept
	: impl_(std::move(impl)) {}

HikrobotMvsCameraDevice::~HikrobotMvsCameraDevice() = default;

core::Result<void> HikrobotMvsCameraDevice::startAcquisition(
	CameraAcquisitionOptions options, FrameCallback callback) {
	return impl_->startAcquisition(std::move(options), std::move(callback));
}

void HikrobotMvsCameraDevice::requestStop() noexcept { impl_->requestStop(); }
void HikrobotMvsCameraDevice::wait() noexcept { impl_->wait(); }
bool HikrobotMvsCameraDevice::isAcquiring() const noexcept {
	return impl_->isAcquiring();
}
core::Result<void> HikrobotMvsCameraDevice::softwareTrigger() {
	return impl_->softwareTrigger();
}
const CameraDeviceInfo& HikrobotMvsCameraDevice::deviceInfo() const noexcept {
	return impl_->deviceInfo();
}
const CameraCapabilities& HikrobotMvsCameraDevice::capabilities() const noexcept {
	return impl_->capabilities();
}
vision::FrameSpec HikrobotMvsCameraDevice::outputSpec() const {
	return impl_->outputSpec();
}

} // namespace visionRuntime::camera