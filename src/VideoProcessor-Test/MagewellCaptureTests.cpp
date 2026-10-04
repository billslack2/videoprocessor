#include "pch.h"
#include "CppUnitTest.h"

#include <magewell/MagewellCaptureDevice.h>
#include <MWFOURCC.h>
#include "MagewellSdkTestAccess.h"

#include <array>
#include <condition_variable>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

class MagewellCaptureTestAccess
{
public:
	static void Choose(MWCAP_VIDEO_COLOR_FORMAT color, BYTE depth,
		const std::vector<DWORD>& formats,
		DWORD& fourcc, VideoFrameEncoding& encoding,
		unsigned int width = 1920)
	{
		MagewellCaptureDevice::SignalDescription signal;
		signal.colorFormat = color;
		signal.bitDepth = depth;
		signal.width = width;
		MagewellCaptureDevice::ChooseCaptureFormat(signal, formats, fourcc, encoding);
	}

	static void RepackRgb(const uint8_t* source, uint32_t sourceStride,
		uint8_t* destination, uint32_t destinationStride,
		uint32_t width, uint32_t height)
	{
		MagewellCaptureDevice::RepackRGB10ToR210(
			source, sourceStride, destination, destinationStride, width, height);
	}

	static void RepackYuv(const uint8_t* source, uint32_t sourceStride,
		uint8_t* destination, uint32_t destinationStride,
		uint32_t width, uint32_t height)
	{
		MagewellCaptureDevice::RepackP210ToV210(
			source, sourceStride, destination, destinationStride, width, height);
	}

	static void RunBody(MagewellCaptureDevice& device)
	{
		device.m_channel.store(reinterpret_cast<HCHANNEL>(1));
		device.CaptureThreadBody(1);
	}
	static void SignalErrorAfterCapture(MagewellCaptureDevice& device)
	{
		device.m_state.store(CAPTUREDEVICESTATE_CAPTURING);
		device.Error(TEXT("fake failure"));
	}
	static void SetClockBaseline(MagewellCaptureDevice& device,
		timingclocktime_t baseline)
	{
		device.m_channel.store(reinterpret_cast<HCHANNEL>(1));
		device.m_timingBaselineTicks.store(baseline);
	}
};

namespace Tests
{
	namespace
	{
		std::atomic<int> stopCalls{ 0 };
		std::atomic<int> unregisterCalls{ 0 };
		std::atomic<LONGLONG> fakeDeviceTime{ 0 };
		std::atomic<DWORD> selectedInput{ 0 };
		std::atomic<int> notifyReads{ 0 };
		std::atomic<int> startCalls{ 0 };
		std::atomic<int> closeCalls{ 0 };
		std::atomic_bool failOpen{ false };
		HANDLE fakeNotifyEvent = nullptr;
		HANDLE fakeCaptureEvent = nullptr;
		MWCAP_VIDEO_COLOR_FORMAT requestedColor = MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN;
		MWCAP_VIDEO_QUANTIZATION_RANGE requestedRange =
			MWCAP_VIDEO_QUANTIZATION_UNKNOWN;

		HCHANNEL FakeOpen(const WCHAR*) { return reinterpret_cast<HCHANNEL>(1); }
		void FakeClose(HCHANNEL) { ++closeCalls; }
		MW_RESULT FakeInputs(HCHANNEL, DWORD* sources, DWORD* count)
		{
			if (sources) sources[0] = INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 0);
			*count = 1;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeCurrentInput(HCHANNEL, DWORD* source)
		{
			*source = INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 0);
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeStart(HCHANNEL, HANDLE) { return MW_SUCCEEDED; }
		HNOTIFY FakeRegister(HCHANNEL, HANDLE, DWORD64)
		{
			return reinterpret_cast<HNOTIFY>(1);
		}
		MW_RESULT FakeUnregister(HCHANNEL, HNOTIFY)
		{
			++unregisterCalls;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeStop(HCHANNEL)
		{
			++stopCalls;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeDeviceTime(HCHANNEL, LONGLONG* value)
		{
			*value = fakeDeviceTime.load();
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeSelectInput(HCHANNEL, DWORD source)
		{
			selectedInput = source;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeSelectInputFails(HCHANNEL, DWORD)
		{
			return MW_FAILED;
		}
		HCHANNEL FakeConditionalOpen(const WCHAR*)
		{
			return failOpen.load() ? nullptr : reinterpret_cast<HCHANNEL>(1);
		}
		MW_RESULT FakeTwoInputs(HCHANNEL, DWORD* sources, DWORD* count)
		{
			if (sources)
			{
				sources[0] = INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 0);
				sources[1] = INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 1);
			}
			*count = 2;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeSignal(HCHANNEL, MWCAP_VIDEO_SIGNAL_STATUS* signal)
		{
			*signal = {};
			signal->state = MWCAP_VIDEO_SIGNAL_LOCKED;
			signal->cx = 102;
			signal->cy = 100;
			signal->dwFrameDuration = 166667;
			signal->colorFormat = MWCAP_VIDEO_COLOR_FORMAT_YUV601;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeInputSpecific(HCHANNEL, MWCAP_INPUT_SPECIFIC_STATUS*)
		{
			return MW_FAILED;
		}
		MW_RESULT FakeHdrValid(HCHANNEL, DWORD*) { return MW_FAILED; }
		MW_RESULT FakeFamily(HCHANNEL, void*, DWORD) { return MW_FAILED; }
		MW_RESULT FakeFormats(HCHANNEL, DWORD* formats, int* count)
		{
			if (formats) formats[0] = MWFOURCC_P210;
			*count = 1;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeCaptureStart(HCHANNEL, HANDLE eventHandle)
		{
			fakeCaptureEvent = eventHandle;
			if (++startCalls == 2 && fakeNotifyEvent)
				SetEvent(fakeNotifyEvent);
			return MW_SUCCEEDED;
		}
		HNOTIFY FakeCaptureRegister(HCHANNEL, HANDLE eventHandle, DWORD64)
		{
			fakeNotifyEvent = eventHandle;
			SetEvent(eventHandle);
			return reinterpret_cast<HNOTIFY>(1);
		}
		MW_RESULT FakeNotify(HCHANNEL, HNOTIFY, ULONGLONG* bits)
		{
			*bits = (++notifyReads == 1)
				? MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE
				: MWCAP_NOTIFY_VIDEO_FRAME_BUFFERED;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeBufferInfo(HCHANNEL, MWCAP_VIDEO_BUFFER_INFO* info)
		{
			*info = {};
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeFrameStatus(HCHANNEL, MWCAP_VIDEO_CAPTURE_STATUS* status)
		{
			*status = {};
			status->bFrameCompleted = TRUE;
			status->iFrame = 0;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeFrameInfo(HCHANNEL, BYTE, MWCAP_VIDEO_FRAME_INFO* info)
		{
			*info = {};
			info->allFieldStartTimes[0] = 1001000;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeCopy(HCHANNEL, int, LPBYTE frame, DWORD frameBytes,
			DWORD stride, BOOLEAN, MWCAP_PTR64, DWORD fourcc,
			int width, int height, DWORD, int, HOSD, const RECT*, int,
			SHORT, SHORT, SHORT, SHORT,
			MWCAP_VIDEO_DEINTERLACE_MODE,
			MWCAP_VIDEO_ASPECT_RATIO_CONVERT_MODE,
			const RECT*, const RECT*, int, int,
			MWCAP_VIDEO_COLOR_FORMAT color,
			MWCAP_VIDEO_QUANTIZATION_RANGE range,
			MWCAP_VIDEO_SATURATION_RANGE)
		{
			requestedColor = color;
			requestedRange = range;
			if (fourcc != MWFOURCC_P210 || frameBytes != 40800 ||
				stride != 204 || width != 102 || height != 100)
				return MW_FAILED;
			memset(frame, 0, frameBytes);
			const uint16_t luma[] = { 64, 128, 192, 256, 320, 384 };
			const uint16_t chroma[] = { 704, 768, 832, 896, 960, 1024 };
			memcpy(frame, luma, sizeof(luma));
			memcpy(frame + stride * height, chroma, sizeof(chroma));
			SetEvent(fakeCaptureEvent);
			return MW_SUCCEEDED;
		}

		class HoldingCallback : public ICaptureDeviceCallback
		{
		public:
			std::mutex mutex;
			std::condition_variable arrived;
			VideoStateComPtr state;
			std::unique_ptr<VideoFrame> heldFrame;
			CString error;
			void OnCaptureDeviceState(CaptureDeviceState) override {}
			void OnCaptureDeviceCardStateChange(CaptureDeviceCardStateComPtr) override {}
			void OnCaptureDeviceVideoStateChange(
				ACaptureDevice*, CaptureRunToken, VideoStateComPtr next) override
			{
				state = next;
			}
			void OnCaptureDeviceVideoFrame(
				ACaptureDevice*, CaptureRunToken, VideoFrame& frame) override
			{
				frame.SourceBufferAddRef();
				std::lock_guard<std::mutex> lock(mutex);
				heldFrame.reset(new VideoFrame(frame));
				arrived.notify_one();
			}
			void OnCaptureDeviceError(const CString& message) override
			{
				std::lock_guard<std::mutex> lock(mutex);
				error = message;
				arrived.notify_one();
			}
		};

		class SetupFailureCallback : public ICaptureDeviceCallback
		{
		public:
			int ready = 0;
			int errors = 0;
			void OnCaptureDeviceState(CaptureDeviceState state) override
			{
				if (state == CAPTUREDEVICESTATE_READY) ++ready;
			}
			void OnCaptureDeviceCardStateChange(CaptureDeviceCardStateComPtr) override {}
			void OnCaptureDeviceVideoStateChange(
				ACaptureDevice*, CaptureRunToken, VideoStateComPtr) override {}
			void OnCaptureDeviceVideoFrame(
				ACaptureDevice*, CaptureRunToken, VideoFrame&) override {}
			void OnCaptureDeviceError(const CString&) override { ++errors; }
		};

		class ThrowAtCapturing : public ICaptureDeviceCallback
		{
		public:
			void OnCaptureDeviceState(CaptureDeviceState state) override
			{
				if (state == CAPTUREDEVICESTATE_CAPTURING)
					throw std::runtime_error("callback failure");
			}
			void OnCaptureDeviceCardStateChange(CaptureDeviceCardStateComPtr) override {}
			void OnCaptureDeviceVideoStateChange(
				ACaptureDevice*, CaptureRunToken, VideoStateComPtr) override {}
			void OnCaptureDeviceVideoFrame(
				ACaptureDevice*, CaptureRunToken, VideoFrame&) override {}
			void OnCaptureDeviceError(const CString&) override {}
		};
	}

	TEST_CLASS(MagewellCaptureTests)
	{
	public:
		TEST_METHOD(BufferOutlivesCapturePool)
		{
			auto buffer = std::make_shared<MagewellCaptureBuffer>(16);
			std::weak_ptr<MagewellCaptureBuffer> weak = buffer;
			Assert::AreEqual<ULONG>(1, buffer->AddRef());
			buffer.reset(); // The capture pool disappears on shutdown/exception.
			Assert::IsFalse(weak.expired());
			auto retained = weak.lock();
			Assert::AreEqual<ULONG>(0, retained->Release());
			retained.reset();
			Assert::IsTrue(weak.expired());
		}

		TEST_METHOD(ThrowingCallbackStopsCaptureAndUnregistersNotification)
		{
			stopCalls = 0;
			unregisterCalls = 0;
			MagewellSdkApi api;
			api.MWOpenChannelByPath = &FakeOpen;
			api.MWCloseChannel = &FakeClose;
			api.MWGetVideoInputSourceArray = &FakeInputs;
			api.MWGetVideoInputSource = &FakeCurrentInput;
			api.MWStartVideoCapture = &FakeStart;
			api.MWRegisterNotify = &FakeRegister;
			api.MWUnregisterNotify = &FakeUnregister;
			api.MWStopVideoCapture = &FakeStop;
			const auto sdk = MagewellSdkTestAccess::Create(api);
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			ThrowAtCapturing callback;
			device.SetCallbackHandler(&callback);
			Assert::ExpectException<std::runtime_error>([&]() {
				MagewellCaptureTestAccess::RunBody(device);
			});
			Assert::AreEqual(1, stopCalls.load());
			Assert::AreEqual(1, unregisterCalls.load());
			device.SetCallbackHandler(nullptr);
		}

		TEST_METHOD(ErrorKeepsClockAvailableUntilStop)
		{
			MagewellSdkApi api;
			api.MWOpenChannelByPath = &FakeOpen;
			api.MWCloseChannel = &FakeClose;
			api.MWGetVideoInputSourceArray = &FakeInputs;
			api.MWGetVideoInputSource = &FakeCurrentInput;
			const auto sdk = MagewellSdkTestAccess::Create(api);
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			MagewellCaptureTestAccess::SignalErrorAfterCapture(device);
			Assert::IsNotNull(device.GetTimingClock());
		}

		TEST_METHOD(ClockUsesSameOriginBeforeAndAfterFirstFrame)
		{
			MagewellSdkApi api;
			api.MWOpenChannelByPath = &FakeOpen;
			api.MWCloseChannel = &FakeClose;
			api.MWGetVideoInputSourceArray = &FakeInputs;
			api.MWGetVideoInputSource = &FakeCurrentInput;
			api.MWGetDeviceTime = &FakeDeviceTime;
			const auto sdk = MagewellSdkTestAccess::Create(api);
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			MagewellCaptureTestAccess::SetClockBaseline(device, 1000000);
			fakeDeviceTime = 1020000;
			Assert::AreEqual<timingclocktime_t>(20000, device.TimingClockNow());
			fakeDeviceTime = 1050000;
			Assert::AreEqual<timingclocktime_t>(50000, device.TimingClockNow());
		}

		TEST_METHOD(SetupFailureReturnsToReadyWithoutThrowing)
		{
			failOpen = false;
			MagewellSdkApi api;
			api.MWOpenChannelByPath = &FakeConditionalOpen;
			api.MWCloseChannel = &FakeClose;
			api.MWGetVideoInputSourceArray = &FakeInputs;
			api.MWGetVideoInputSource = &FakeCurrentInput;
			const auto sdk = MagewellSdkTestAccess::Create(api);
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			SetupFailureCallback callback;
			device.SetCallbackHandler(&callback);
			Assert::AreEqual(1, callback.ready);
			failOpen = true;
			device.StartCapture(1);
			failOpen = false;
			Assert::AreEqual(1, callback.errors);
			Assert::AreEqual(2, callback.ready);
			device.SetCallbackHandler(nullptr);
		}

		TEST_METHOD(FailedInputSelectionClosesChannelAndReturnsReady)
		{
			closeCalls = 0;
			MagewellSdkApi api;
			api.MWOpenChannelByPath = &FakeOpen;
			api.MWCloseChannel = &FakeClose;
			api.MWGetVideoInputSourceArray = &FakeInputs;
			api.MWGetVideoInputSource = &FakeCurrentInput;
			api.MWSetVideoInputSource = &FakeSelectInputFails;
			const auto sdk = MagewellSdkTestAccess::Create(api);
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			Assert::AreEqual(1, closeCalls.load());
			SetupFailureCallback callback;
			device.SetCallbackHandler(&callback);
			device.StartCapture(1);
			Assert::AreEqual(1, callback.errors);
			Assert::AreEqual(2, callback.ready);
			Assert::AreEqual(2, closeCalls.load());
			device.SetCallbackHandler(nullptr);
		}

		TEST_METHOD(FakeCardCapturesBt601AndRetainsFrameAcrossStop)
		{
			selectedInput = 0;
			notifyReads = 0;
			startCalls = 0;
			fakeNotifyEvent = nullptr;
			fakeCaptureEvent = nullptr;
			fakeDeviceTime = 1000000;
			MagewellSdkApi api;
			api.MWOpenChannelByPath = &FakeOpen;
			api.MWCloseChannel = &FakeClose;
			api.MWGetVideoInputSourceArray = &FakeTwoInputs;
			api.MWGetVideoInputSource = &FakeCurrentInput;
			api.MWSetVideoInputSource = &FakeSelectInput;
			api.MWGetDeviceTime = &FakeDeviceTime;
			api.MWStartVideoCapture = &FakeCaptureStart;
			api.MWStopVideoCapture = &FakeStop;
			api.MWRegisterNotify = &FakeCaptureRegister;
			api.MWUnregisterNotify = &FakeUnregister;
			api.MWGetNotifyStatus = &FakeNotify;
			api.MWGetVideoSignalStatus = &FakeSignal;
			api.MWGetInputSpecificStatus = &FakeInputSpecific;
			api.MWGetHDMIInfoFrameValidFlag = &FakeHdrValid;
			api.MWGetFamilyInfo = &FakeFamily;
			api.MWGetVideoCaptureSupportColorFormat = &FakeFormats;
			api.MWGetVideoBufferInfo = &FakeBufferInfo;
			api.MWCaptureVideoFrameToVirtualAddressEx = &FakeCopy;
			api.MWGetVideoCaptureStatus = &FakeFrameStatus;
			api.MWGetVideoFrameInfo = &FakeFrameInfo;
			const auto sdk = MagewellSdkTestAccess::Create(api);
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			device.SetCaptureInput(INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 1));
			HoldingCallback callback;
			device.SetCallbackHandler(&callback);
			device.StartCapture(1);
			bool completed = false;
			{
				std::unique_lock<std::mutex> lock(callback.mutex);
				completed = callback.arrived.wait_for(lock,
					std::chrono::seconds(2), [&]() {
						return callback.heldFrame != nullptr || !callback.error.IsEmpty();
					});
			}
			device.StopCapture();
			if (!completed || !callback.error.IsEmpty())
			{
				Logger::WriteMessage(callback.error.IsEmpty()
					? L"fake Magewell frame never arrived" : callback.error.GetString());
				Logger::WriteMessage((L"start=" + std::to_wstring(startCalls.load()) +
					L" notify=" + std::to_wstring(notifyReads.load())).c_str());
			}
			Assert::IsTrue(completed && callback.heldFrame != nullptr);
			Assert::AreEqual<DWORD>(
				INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 1), selectedInput.load());
			Assert::IsTrue(requestedColor == MWCAP_VIDEO_COLOR_FORMAT_YUV601);
			Assert::IsTrue(requestedRange == MWCAP_VIDEO_QUANTIZATION_LIMITED);
			Assert::IsTrue(callback.state->colorspace == ColorSpace::REC_601_525);
			Assert::IsTrue(callback.state->videoFrameEncoding == VideoFrameEncoding::V210);
			const uint32_t* words = reinterpret_cast<const uint32_t*>(
				callback.heldFrame->GetData());
			Assert::AreEqual<uint32_t>(11u | (1u << 10) | (12u << 20), words[0]);
			Assert::AreEqual<timingclocktime_t>(1000,
				callback.heldFrame->GetCaptureTimingTimestamp());
			callback.heldFrame->SourceBufferRelease();
			callback.heldFrame.reset();
			device.SetCallbackHandler(nullptr);
		}

		TEST_METHOD(CapabilityPolicyPreservesRgbAndRejectsUnsupportedDepth)
		{
			DWORD fourcc = 0;
			VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
			const std::vector<DWORD> formats = {
				MWFOURCC_ARGB, MWFOURCC_RGB10, MWFOURCC_P210 };
			MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
				8, formats, fourcc, encoding);
			Assert::AreEqual<DWORD>(MWFOURCC_ARGB, fourcc);
			Assert::IsTrue(encoding == VideoFrameEncoding::ARGB_8BIT);
			MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
				10, formats, fourcc, encoding);
			Assert::AreEqual<DWORD>(MWFOURCC_RGB10, fourcc);
			Assert::IsTrue(encoding == VideoFrameEncoding::R210);
			Assert::ExpectException<std::runtime_error>([&]() {
				MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
					12, formats, fourcc, encoding);
			});
			Assert::ExpectException<std::runtime_error>([&]() {
				MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_YUV709,
					10, { MWFOURCC_ARGB }, fourcc, encoding);
			});
			Assert::ExpectException<std::runtime_error>([&]() {
				MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN,
					10, formats, fourcc, encoding);
			});
			Assert::ExpectException<std::runtime_error>([&]() {
				MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_YUV709,
					10, formats, fourcc, encoding, 1919);
			});
		}

		TEST_METHOD(Rgb10ToR210PreservesChannelCodesAndRowPadding)
		{
			// Two rows, three pixels each. R210 rows are padded to 256 bytes.
			std::array<uint32_t, 6> source = {
				1u | (2u << 10) | (3u << 20) | (3u << 30),
				1023u | (0u << 10) | (512u << 20),
				64u | (940u << 10) | (64u << 20),
				4u | (5u << 10) | (6u << 20),
				0, 0 };
			std::array<uint8_t, 512> result = {};
			MagewellCaptureTestAccess::RepackRgb(
				reinterpret_cast<const uint8_t*>(source.data()), 12,
				result.data(), 256, 3, 2);
			const std::array<uint8_t, 12> firstRow = {
				0x00, 0x10, 0x08, 0x03,
				0x3f, 0xf0, 0x02, 0x00,
				0x04, 0x0e, 0xb0, 0x40 };
			for (size_t i = 0; i < firstRow.size(); ++i)
				Assert::AreEqual(firstRow[i], result[i]);
			Assert::AreEqual<uint8_t>(0, result[255]);
			Assert::AreEqual<uint8_t>(0, result[256]);
			Assert::AreEqual<uint8_t>(0x40, result[257]);
		}

		TEST_METHOD(P210ToV210PreservesSixPixelGroup)
		{
			std::array<uint16_t, 12> source = {
				1u << 6, 2u << 6, 3u << 6, 4u << 6, 5u << 6, 6u << 6,
				11u << 6, 12u << 6, 13u << 6, 14u << 6, 15u << 6, 16u << 6 };
			std::array<uint8_t, 128> result = {};
			MagewellCaptureTestAccess::RepackYuv(
				reinterpret_cast<const uint8_t*>(source.data()), 12,
				result.data(), 128, 6, 1);
			const uint32_t* words = reinterpret_cast<const uint32_t*>(result.data());
			Assert::AreEqual<uint32_t>(11u | (1u << 10) | (12u << 20), words[0]);
			Assert::AreEqual<uint32_t>(2u | (13u << 10) | (3u << 20), words[1]);
			Assert::AreEqual<uint32_t>(14u | (4u << 10) | (15u << 20), words[2]);
			Assert::AreEqual<uint32_t>(5u | (16u << 10) | (6u << 20), words[3]);
		}

		TEST_METHOD(P210TailAcrossRowsDoesNotReadOrWritePastRow)
		{
			// Width two exercises the partial six-pixel group on both rows.
			std::array<uint16_t, 8> source = {
				1u << 6, 2u << 6, 3u << 6, 4u << 6,
				11u << 6, 12u << 6, 13u << 6, 14u << 6 };
			std::array<uint8_t, 256> result;
			result.fill(0xCD);
			MagewellCaptureTestAccess::RepackYuv(
				reinterpret_cast<const uint8_t*>(source.data()), 4,
				result.data(), 128, 2, 2);
			const uint32_t* first = reinterpret_cast<const uint32_t*>(result.data());
			const uint32_t* second = reinterpret_cast<const uint32_t*>(result.data() + 128);
			Assert::AreEqual<uint32_t>(11u | (1u << 10) | (12u << 20), first[0]);
			Assert::AreEqual<uint32_t>(2u | (12u << 10) | (2u << 20), first[1]);
			Assert::AreEqual<uint32_t>(13u | (3u << 10) | (14u << 20), second[0]);
			Assert::AreEqual<uint32_t>(4u | (14u << 10) | (4u << 20), second[1]);
			Assert::AreEqual<uint8_t>(0xCD, result[16]);
			Assert::AreEqual<uint8_t>(0xCD, result[127]);
			Assert::AreEqual<uint8_t>(0xCD, result[144]);
		}
	};
}
