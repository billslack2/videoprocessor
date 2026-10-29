#include "pch.h"
#include "CppUnitTest.h"

#include <magewell/MagewellCaptureDevice.h>
#include <MWFOURCC.h>
#include <AnalysisLumaSource.h>
#include <DolbyVisionTunnelInput.h>
#include <vprenderer/AlphaNativeRgbIngress.h>
#include <vprenderer/AlphaQueuePolicy.h>
#include "MagewellSdkTestAccess.h"
#include "DolbyVisionTunnelTestSender.h"
#include <magewell/MagewellRgbTunnelRange.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <sstream>
#include <vector>

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

	static void PublishRgb8(MagewellCaptureDevice& device,
        MWCAP_VIDEO_QUANTIZATION_RANGE range,
        VideoFrameEncoding encoding = VideoFrameEncoding::ARGB_8BIT)
    {
        MagewellCaptureDevice::SignalDescription signal;
        signal.width = 130; signal.height = 128; signal.frameDuration100ns = 416667;
        signal.colorFormat = MWCAP_VIDEO_COLOR_FORMAT_RGB;
        signal.bitDepth = 8; signal.eotf = EOTF::SDR; signal.quantRange = range;
        device.PublishVideoState(signal, encoding, 1);
    }

	static void RepackRgb(const uint8_t* source, uint32_t sourceStride,
		uint8_t* destination, uint32_t destinationStride,
		uint32_t width, uint32_t height)
	{
		MagewellCaptureDevice::RepackRGB10ToR10l(
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
	static void SetBudget(MagewellCaptureDevice& device, uint64_t bytes)
	{
		device.m_memoryBudget = std::make_shared<MagewellCaptureMemoryBudget>(bytes);
	}
	static bool ReadFormat(MagewellCaptureDevice& device, BYTE& depth,
		int& sampling, VideoFrameEncoding& encoding)
	{
		MagewellCaptureDevice::SignalDescription signal;
		if (!device.ReadSignal(reinterpret_cast<HCHANNEL>(1), signal)) return false;
		depth = signal.bitDepth;
		sampling = signal.inputSampling;
		DWORD fourcc = 0;
		MagewellCaptureDevice::ChooseCaptureFormat(signal,
			{ MWFOURCC_ARGB, MWFOURCC_RGB10, MWFOURCC_P210 }, fourcc, encoding);
		return true;
	}
};

namespace Tests
{
	namespace
	{
		std::atomic<int> stopCalls{ 0 };
		std::atomic<int> unregisterCalls{ 0 };
		std::atomic<LONGLONG> fakeDeviceTime{ 0 };
		std::atomic_bool failDeviceTime{ false };
		std::atomic<DWORD> selectedInput{ 0 };
		std::atomic_bool scanEnabled{ true };
		std::atomic<int> notifyReads{ 0 };
		std::atomic<int> startCalls{ 0 };
		std::atomic<int> closeCalls{ 0 };
		std::atomic_bool failOpen{ false };
		std::atomic_bool streamFrames{ false };
		std::atomic_bool formatEventPending{ false };
		std::atomic<int> signalMode{ 0 }; // YUV, RGB12, invalid geometry, SDI RGB, unsupported RGB16.
		std::atomic<int> sdiDepth{ SDI_BIT_DEPTH_10BIT };
        const std::vector<BYTE>* rgbSenderBytes = nullptr;
        DWORD rgbCaptureFourcc = MWFOURCC_ARGB;
        std::vector<DWORD> rgbCaptureFormats{ MWFOURCC_ARGB };
        // Exact list returned by the installed Pro Capture HDMI 4K+ driver.
        const std::vector<DWORD> proCapture4kPlusFormats{
            MWFOURCC_YUY2, MWFOURCC_YUYV, MWFOURCC_UYVY, MWFOURCC_NV12,
            MWFOURCC_I420, MWFOURCC_YV12, MWFOURCC_V308, MWFOURCC_IYU2,
            MWFOURCC_V408, MWFOURCC_V410, MWFOURCC_Y410, MWFOURCC_BGR24,
            MWFOURCC_BGRA, MWFOURCC_I422, MWFOURCC_YV16, MWFOURCC_NV16,
            MWFOURCC_NV61, MWFOURCC_P010, MWFOURCC_P210 };
        std::atomic<int> rgbWireRange{MWCAP_VIDEO_QUANTIZATION_LIMITED};
        std::atomic<int> inputRangeSetting{MWCAP_VIDEO_QUANTIZATION_UNKNOWN};
        std::atomic<int> restoreWriteFailures{0};
        std::atomic<int> rangeFailure{0}; // save failure, partial write failure, ignored write
        MW_RESULT FakeGetRange(HCHANNEL, MWCAP_VIDEO_QUANTIZATION_RANGE* range)
        {
            if (rangeFailure == 1) return MW_FAILED;
            *range = static_cast<MWCAP_VIDEO_QUANTIZATION_RANGE>(inputRangeSetting.load());
            if (rangeFailure == 3 && *range == MWCAP_VIDEO_QUANTIZATION_FULL)
                *range = MWCAP_VIDEO_QUANTIZATION_LIMITED;
            return MW_SUCCEEDED;
        }
        MW_RESULT FakeSetRange(HCHANNEL, MWCAP_VIDEO_QUANTIZATION_RANGE range)
        {
            if (range != MWCAP_VIDEO_QUANTIZATION_FULL && restoreWriteFailures > 0)
            { --restoreWriteFailures; return MW_FAILED; }
            inputRangeSetting = range;
            return rangeFailure == 2 && range == MWCAP_VIDEO_QUANTIZATION_FULL ? MW_FAILED : MW_SUCCEEDED;
        }
        MW_RESULT FakeAviPacket(HCHANNEL, MWCAP_HDMI_INFOFRAME_ID id, HDMI_INFOFRAME_PACKET* packet)
        {
            if (id != MWCAP_HDMI_INFOFRAME_ID_AVI || signalMode != 5) return MW_FAILED;
            *packet = {};
            packet->aviInfoFramePayload.byRGB_YCbCr = HDMI_ENCODING_RGB_444;
            packet->aviInfoFramePayload.byVIC = 16;
            packet->aviInfoFramePayload.byRGBQuantizationRange =
                rgbWireRange == MWCAP_VIDEO_QUANTIZATION_LIMITED ? 1 :
                rgbWireRange == MWCAP_VIDEO_QUANTIZATION_FULL ? 2 : 3;
            return MW_SUCCEEDED;
        }
		std::atomic_bool failCopies{ false };
		HANDLE fakeNotifyEvent = nullptr;
		HANDLE fakeCaptureEvent = nullptr;
		MWCAP_VIDEO_COLOR_FORMAT requestedColor = MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN;
		MWCAP_VIDEO_QUANTIZATION_RANGE requestedRange =
			MWCAP_VIDEO_QUANTIZATION_UNKNOWN;

		struct RepackTimingSummary
		{
			double meanMs = 0.0;
			double p50Ms = 0.0;
			double p95Ms = 0.0;
			double maxMs = 0.0;
		};

		template<typename Repack>
		RepackTimingSummary MeasureRepack(Repack&& repack, size_t bufferCount)
		{
			constexpr size_t warmupCount = 8;
			constexpr size_t sampleCount = 120;
			for (size_t i = 0; i < warmupCount; ++i)
				repack(i % bufferCount);

			std::vector<double> samples;
			samples.reserve(sampleCount);
			for (size_t i = 0; i < sampleCount; ++i)
			{
				const auto start = std::chrono::steady_clock::now();
				repack(i % bufferCount);
				const auto stop = std::chrono::steady_clock::now();
				samples.push_back(std::chrono::duration<double, std::milli>(
					stop - start).count());
			}

			std::sort(samples.begin(), samples.end());
			RepackTimingSummary result;
			for (const double sample : samples)
				result.meanMs += sample;
			result.meanMs /= samples.size();
			result.p50Ms = samples[samples.size() / 2];
			result.p95Ms = samples[(samples.size() * 95 + 99) / 100 - 1];
			result.maxMs = samples.back();
			return result;
		}

		std::wstring FormatRepackTimings(const wchar_t* format,
			const wchar_t* workingSet, const RepackTimingSummary& timing)
		{
			std::wostringstream message;
			message << L"Magewell "
#ifdef NDEBUG
				<< L"Release ";
#else
				<< L"Debug ";
#endif
			message << L"4K60 " << format << L" " << workingSet
				<< L" samples=120 mean_ms=" << std::fixed << std::setprecision(3)
				<< timing.meanMs << L" p50_ms=" << timing.p50Ms
				<< L" p95_ms=" << timing.p95Ms << L" max_ms=" << timing.maxMs
				<< L" frame_budget_ms=16.683";
			return message.str();
		}

		void FillP210Frame(std::vector<uint8_t>& frame, uint32_t stride,
			uint32_t width, uint32_t height, uint32_t seed)
		{
			const size_t lumaPlaneBytes = static_cast<size_t>(stride) * height;
			for (uint32_t row = 0; row < height; ++row)
			{
				auto* luma = reinterpret_cast<uint16_t*>(
					frame.data() + static_cast<size_t>(row) * stride);
				auto* chroma = reinterpret_cast<uint16_t*>(
					frame.data() + lumaPlaneBytes + static_cast<size_t>(row) * stride);
				for (uint32_t i = 0; i < width; ++i)
				{
					luma[i] = static_cast<uint16_t>(
						((i * 13 + row * 17 + seed * 97) & 0x3FF) << 6);
					chroma[i] = static_cast<uint16_t>(
						((i * 19 + row * 7 + seed * 31) & 0x3FF) << 6);
				}
			}
		}

		bool VerifyP210ToV210(const std::vector<uint8_t>& source,
			uint32_t sourceStride, const std::vector<uint8_t>& destination,
			uint32_t destinationStride, uint32_t width, uint32_t height)
		{
			const size_t lumaPlaneBytes = static_cast<size_t>(sourceStride) * height;
			for (uint32_t row = 0; row < height; ++row)
			{
				const auto* luma = reinterpret_cast<const uint16_t*>(
					source.data() + static_cast<size_t>(row) * sourceStride);
				const auto* chroma = reinterpret_cast<const uint16_t*>(
					source.data() + lumaPlaneBytes + static_cast<size_t>(row) * sourceStride);
				const auto* words = reinterpret_cast<const uint32_t*>(
					destination.data() + static_cast<size_t>(row) * destinationStride);
				for (uint32_t group = 0; group * 6 < width; ++group)
				{
					const uint32_t pixel = group * 6;
					const uint32_t actual[12] = {
						words[group * 4] & 0x3FF,
						(words[group * 4] >> 10) & 0x3FF,
						(words[group * 4] >> 20) & 0x3FF,
						words[group * 4 + 1] & 0x3FF,
						(words[group * 4 + 1] >> 10) & 0x3FF,
						(words[group * 4 + 1] >> 20) & 0x3FF,
						words[group * 4 + 2] & 0x3FF,
						(words[group * 4 + 2] >> 10) & 0x3FF,
						(words[group * 4 + 2] >> 20) & 0x3FF,
						words[group * 4 + 3] & 0x3FF,
						(words[group * 4 + 3] >> 10) & 0x3FF,
						(words[group * 4 + 3] >> 20) & 0x3FF };
					const uint32_t sourceIndex[12] = {
						pixel, pixel, pixel + 1, pixel + 1, pixel + 2, pixel + 2,
						pixel + 3, pixel + 3, pixel + 4, pixel + 4, pixel + 5, pixel + 5 };
					const bool isChroma[12] = {
						true, false, true, false, true, false,
						true, false, true, false, true, false };
					for (size_t component = 0; component < 12; ++component)
					{
						const uint32_t index = std::min(sourceIndex[component], width - 1);
					const uint32_t expected = (isChroma[component]
						? chroma[index] : luma[index]) >> 6;
					if (actual[component] != expected)
						return false;
					}
				}
			}
			return true;
		}

		void FillRgb10Frame(std::vector<uint8_t>& frame, uint32_t stride,
			uint32_t width, uint32_t height, uint32_t seed)
		{
			for (uint32_t row = 0; row < height; ++row)
			{
				auto* pixels = reinterpret_cast<uint32_t*>(
					frame.data() + static_cast<size_t>(row) * stride);
				for (uint32_t x = 0; x < width; ++x)
				{
					const uint32_t r = (x * 5 + row * 3 + seed * 11) & 0x3FF;
					const uint32_t g = (x * 7 + row * 13 + seed * 17) & 0x3FF;
					const uint32_t b = (x * 19 + row * 23 + seed * 29) & 0x3FF;
					pixels[x] = r | (g << 10) | (b << 20) | (3u << 30);
				}
			}
		}

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
			*source = selectedInput.load() ? selectedInput.load() :
				INPUT_SOURCE(MWCAP_VIDEO_INPUT_TYPE_HDMI, 0);
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeGetScan(HCHANNEL, BOOLEAN* enabled)
		{
			*enabled = scanEnabled.load() ? TRUE : FALSE;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeSetScan(HCHANNEL, BOOLEAN enabled)
		{
			scanEnabled = enabled != FALSE;
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
			if (failDeviceTime.load()) return MW_FAILED;
			*value = fakeDeviceTime.load();
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeSelectInput(HCHANNEL, DWORD source)
		{
			if (scanEnabled.load()) return MW_FAILED;
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
			if (signalMode == 1 || signalMode == 3 || signalMode == 4 || signalMode == 5)
				signal->colorFormat = MWCAP_VIDEO_COLOR_FORMAT_RGB;
            if (signalMode == 5)
            {
                signal->cx = 130; signal->cy = 128;
                signal->quantRange = static_cast<MWCAP_VIDEO_QUANTIZATION_RANGE>(
                    inputRangeSetting == MWCAP_VIDEO_QUANTIZATION_UNKNOWN ? rgbWireRange.load() : inputRangeSetting.load());
            }
			if (signalMode == 2) signal->cx = 0;
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeInputSpecific(HCHANNEL, MWCAP_INPUT_SPECIFIC_STATUS* status)
		{
			if (signalMode == 1 || signalMode == 4 || signalMode == 5)
			{
				*status = {};
				status->bValid = TRUE;
				status->dwVideoInputType = MWCAP_VIDEO_INPUT_TYPE_HDMI;
				status->hdmiStatus.byBitDepth = signalMode == 5 ? 8 : signalMode == 1 ? 12 : 16;
				return MW_SUCCEEDED;
			}
			if (signalMode == 3)
			{
				*status = {};
				status->bValid = TRUE;
				status->dwVideoInputType = MWCAP_VIDEO_INPUT_TYPE_SDI;
				status->sdiStatus.sdiBitDepth = static_cast<SDI_BIT_DEPTH>(sdiDepth.load());
				status->sdiStatus.sdiSamplingStruct = SDI_SAMPLING_444_RGB;
				return MW_SUCCEEDED;
			}
			return MW_FAILED;
		}
		MW_RESULT FakeHdrValid(HCHANNEL, DWORD* flags)
        {
            if (signalMode != 5) return MW_FAILED;
            *flags = MWCAP_HDMI_INFOFRAME_MASK_AVI;
            return MW_SUCCEEDED;
        }
		MW_RESULT FakeFamily(HCHANNEL, void*, DWORD) { return MW_FAILED; }
		MW_RESULT FakeFormats(HCHANNEL, DWORD* formats, int* count)
		{
			const std::vector<DWORD> values = signalMode == 5 ? rgbCaptureFormats :
				std::vector<DWORD>{ MWFOURCC_P210 };
			if (formats) std::copy(values.begin(), values.end(), formats);
			*count = static_cast<int>(values.size());
			return MW_SUCCEEDED;
		}
		MW_RESULT FakeCaptureStart(HCHANNEL, HANDLE eventHandle)
		{
			fakeCaptureEvent = eventHandle;
			if ((++startCalls == 2 || signalMode == 5) && fakeNotifyEvent)
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
			if (formatEventPending.exchange(false)) *bits |= MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE;
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
			if (failCopies.load())
			{
				SetEvent(fakeNotifyEvent);
				return MW_FAILED;
			}
            if (signalMode == 5)
            {
                if (!rgbSenderBytes || fourcc != rgbCaptureFourcc || stride != 130*4 ||
                    frameBytes != rgbSenderBytes->size() || width != 130 || height != 128 ||
                    color != MWCAP_VIDEO_COLOR_FORMAT_RGB || range != MWCAP_VIDEO_QUANTIZATION_FULL)
                    return MW_FAILED;
                memcpy(frame, rgbSenderBytes->data(), frameBytes);
                const int effectiveInputRange = inputRangeSetting == MWCAP_VIDEO_QUANTIZATION_UNKNOWN ?
                    rgbWireRange.load() : inputRangeSetting.load();
                if (effectiveInputRange == MWCAP_VIDEO_QUANTIZATION_LIMITED)
                    for (size_t pixel = 0; pixel < frameBytes; pixel += 4)
                        for (size_t c = fourcc == MWFOURCC_ARGB ? 1 : 0,
                            end = c + 3; c < end; ++c)
                            frame[pixel+c] = BYTE(std::max(0, std::min(255, (int(frame[pixel+c])-16)*255/219)));
                SetEvent(fakeCaptureEvent);
                if (streamFrames.load()) SetEvent(fakeNotifyEvent);
                return MW_SUCCEEDED;
            }
			if (fourcc != MWFOURCC_P210 || frameBytes != 40800 ||
				stride != 204 || width != 102 || height != 100)
				return MW_FAILED;
			memset(frame, 0, frameBytes);
			const uint16_t luma[] = { 64, 128, 192, 256, 320, 384 };
			const uint16_t chroma[] = { 704, 768, 832, 896, 960, 1024 };
			memcpy(frame, luma, sizeof(luma));
			memcpy(frame + stride * height, chroma, sizeof(chroma));
			SetEvent(fakeCaptureEvent);
			if (streamFrames.load()) SetEvent(fakeNotifyEvent);
			return MW_SUCCEEDED;
		}

		MagewellSdkApi CaptureApi()
		{
			MagewellSdkApi api;
			api.MWOpenChannelByPath = &FakeOpen;
			api.MWCloseChannel = &FakeClose;
			api.MWGetVideoInputSourceArray = &FakeTwoInputs;
			api.MWGetVideoInputSource = &FakeCurrentInput;
			api.MWSetVideoInputSource = &FakeSelectInput;
			api.MWGetInputSourceScan = &FakeGetScan;
			api.MWSetInputSourceScan = &FakeSetScan;
			api.MWGetDeviceTime = &FakeDeviceTime;
			api.MWStartVideoCapture = &FakeCaptureStart;
			api.MWStopVideoCapture = &FakeStop;
			api.MWRegisterNotify = &FakeCaptureRegister;
			api.MWUnregisterNotify = &FakeUnregister;
			api.MWGetNotifyStatus = &FakeNotify;
			api.MWGetVideoSignalStatus = &FakeSignal;
            api.MWGetVideoInputQuantizationRange = &FakeGetRange;
            api.MWSetVideoInputQuantizationRange = &FakeSetRange;
			api.MWGetInputSpecificStatus = &FakeInputSpecific;
			api.MWGetHDMIInfoFrameValidFlag = &FakeHdrValid;
            api.MWGetHDMIInfoFramePacket = &FakeAviPacket;
			api.MWGetFamilyInfo = &FakeFamily;
			api.MWGetVideoCaptureSupportColorFormat = &FakeFormats;
			api.MWGetVideoBufferInfo = &FakeBufferInfo;
			api.MWCaptureVideoFrameToVirtualAddressEx = &FakeCopy;
			api.MWGetVideoCaptureStatus = &FakeFrameStatus;
			api.MWGetVideoFrameInfo = &FakeFrameInfo;
			return api;
		}

		class QueueCallback : public ICaptureDeviceCallback
		{
		public:
			std::mutex mutex;
			std::condition_variable changed;
			std::vector<std::unique_ptr<VideoFrame>> frames;
			std::vector<CString> errors;
			int validStates = 0, invalidStates = 0;
			size_t goal = 34;
			bool failClockAfterFirstFrame = false;
			~QueueCallback()
			{
				for (auto& frame : frames) frame->SourceBufferRelease();
			}
			VideoStateComPtr latestValidState;
			void OnCaptureDeviceState(CaptureDeviceState) override {}
			void OnCaptureDeviceCardStateChange(CaptureDeviceCardStateComPtr) override {}
			void OnCaptureDeviceVideoStateChange(
				ACaptureDevice*, CaptureRunToken, VideoStateComPtr state) override
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (state->valid) { ++validStates; latestValidState = state; } else ++invalidStates;
				changed.notify_all();
			}
			void OnCaptureDeviceVideoFrame(
				ACaptureDevice*, CaptureRunToken, VideoFrame& frame) override
			{
				std::lock_guard<std::mutex> lock(mutex);
				frames.emplace_back(new VideoFrame(frame));
				frames.back()->SourceBufferAddRef();
				if (frames.size() >= goal) streamFrames = false;
				if (failClockAfterFirstFrame) failDeviceTime = true;
				changed.notify_all();
			}
			void OnCaptureDeviceError(const CString& error) override
			{
				std::lock_guard<std::mutex> lock(mutex);
				errors.push_back(error);
				changed.notify_all();
			}
			template<class Predicate> bool Wait(Predicate predicate)
			{
				std::unique_lock<std::mutex> lock(mutex);
				return changed.wait_for(lock, std::chrono::seconds(4), predicate);
			}
		};

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
		TEST_METHOD_INITIALIZE(ResetFakeCard)
		{
			stopCalls = unregisterCalls = notifyReads = startCalls = closeCalls = 0;
			fakeDeviceTime = 1000000;
			failDeviceTime = failOpen = streamFrames = formatEventPending = failCopies = false;
			selectedInput = 0;
			scanEnabled = true;
			signalMode = 0;
			sdiDepth = SDI_BIT_DEPTH_10BIT;
            inputRangeSetting = MWCAP_VIDEO_QUANTIZATION_UNKNOWN;
            rgbWireRange = MWCAP_VIDEO_QUANTIZATION_LIMITED;
            rangeFailure = 0; restoreWriteFailures = 0; rgbSenderBytes = nullptr;
            rgbCaptureFourcc = MWFOURCC_ARGB; rgbCaptureFormats = { MWFOURCC_ARGB };
			fakeNotifyEvent = fakeCaptureEvent = nullptr;
		}


        TEST_METHOD(RawRangeRestorationRetriesTransientSdkFailure)
        {
            inputRangeSetting = MWCAP_VIDEO_QUANTIZATION_LIMITED;
            const auto api = CaptureApi();
            MagewellRgbTunnelRange lease(api, reinterpret_cast<HCHANNEL>(1));
            lease.Enable();
            Assert::AreEqual(int(MWCAP_VIDEO_QUANTIZATION_FULL), inputRangeSetting.load());
            restoreWriteFailures = 1;
            Assert::IsTrue(lease.Restore());
            Assert::AreEqual(int(MWCAP_VIDEO_QUANTIZATION_LIMITED), inputRangeSetting.load());
            Assert::IsFalse(lease.Active());
        }

        TEST_METHOD(PersistentRestoreFailureIsReportedInsteadOfClaimingOriginalSettings)
        {
            using namespace DolbyVisionTestSender;
            signalMode = 5;
            SenderPicture picture(false, 130, 128, VideoFrameEncoding::ARGB_8BIT);
            picture.Embed(Packetize(DmBody())); rgbSenderBytes = &picture.input;
            const auto sdk = MagewellSdkTestAccess::Create(CaptureApi()); MWCAP_CHANNEL_INFO info = {};
            QueueCallback callback; callback.goal = 1;
            MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
            device.SetCallbackHandler(&callback); device.SetRawRgbTunnelCapture(true); device.StartCapture(1);
            Assert::IsTrue(callback.Wait([&]{ return !callback.frames.empty(); }));
            restoreWriteFailures = 3;
            device.StopCapture();
            Assert::AreEqual(int(MWCAP_VIDEO_QUANTIZATION_FULL), inputRangeSetting.load());
            Assert::IsTrue(std::any_of(callback.errors.begin(), callback.errors.end(),
                [](const CString& error){ return error.Find(L"restore the original input range") >= 0; }));
            inputRangeSetting = MWCAP_VIDEO_QUANTIZATION_UNKNOWN; // reset the fake device only
        }

        TEST_METHOD(CaptureCopyFailureRestoresRawInputRangeAndPublishesInvalidState)
        {
            signalMode = 5; failCopies = true;
            const auto sdk = MagewellSdkTestAccess::Create(CaptureApi()); MWCAP_CHANNEL_INFO info = {};
            QueueCallback callback;
            MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
            device.SetCallbackHandler(&callback); device.SetRawRgbTunnelCapture(true); device.StartCapture(1);
            Assert::IsTrue(callback.Wait([&]{ return !callback.errors.empty(); })); device.StopCapture();
            Assert::IsTrue(callback.frames.empty());
            Assert::IsTrue(callback.invalidStates >= 1);
            Assert::AreEqual(int(MWCAP_VIDEO_QUANTIZATION_UNKNOWN), inputRangeSetting.load());
        }

        TEST_METHOD(RawMagewellCapturePreservesDvForFullLimitedAndUnknownWireRanges)
        {
            using namespace DolbyVisionTestSender;
            for (const auto format : { MWFOURCC_ARGB, MWFOURCC_BGRA })
            for (const auto wire : { MWCAP_VIDEO_QUANTIZATION_FULL,
                MWCAP_VIDEO_QUANTIZATION_LIMITED, MWCAP_VIDEO_QUANTIZATION_UNKNOWN })
            for (const auto original : { MWCAP_VIDEO_QUANTIZATION_UNKNOWN,
                MWCAP_VIDEO_QUANTIZATION_LIMITED, MWCAP_VIDEO_QUANTIZATION_FULL })
            {
                ResetFakeCard(); signalMode = 5; rgbWireRange = wire; inputRangeSetting = original;
                rgbCaptureFourcc = format;
                rgbCaptureFormats = format == MWFOURCC_BGRA ? proCapture4kPlusFormats :
                    std::vector<DWORD>{ MWFOURCC_ARGB };
                const auto encoding = format == MWFOURCC_ARGB ?
                    VideoFrameEncoding::ARGB_8BIT : VideoFrameEncoding::BGRA_8BIT;
                SenderPicture picture(false, 130, 128, encoding);
                picture.Embed(Packetize(DmBody())); rgbSenderBytes = &picture.input;
                const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
                MWCAP_CHANNEL_INFO info = {};
                QueueCallback callback; callback.goal = 1;
                MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
                device.SetCallbackHandler(&callback); device.SetRawRgbTunnelCapture(true);
                device.StartCapture(1);
                Assert::IsTrue(callback.Wait([&]{ return !callback.frames.empty() || !callback.errors.empty(); }));
                device.StopCapture();
                Assert::IsTrue(callback.errors.empty());
                Assert::AreEqual(int(original), inputRangeSetting.load(), L"User input-range override was not restored");
                Assert::IsTrue(!callback.frames.empty() && callback.latestValidState);
                Assert::IsTrue(callback.latestValidState->videoFrameEncoding == encoding);
                Assert::IsTrue(requestedColor == MWCAP_VIDEO_COLOR_FORMAT_RGB);
                Assert::IsTrue(requestedRange == MWCAP_VIDEO_QUANTIZATION_FULL);
                Assert::IsTrue(DolbyVisionTunnelCanEnable(true, true, *callback.latestValidState));
                const auto expectedSdrRange = original == MWCAP_VIDEO_QUANTIZATION_UNKNOWN ? wire : original;
                Assert::IsTrue(callback.latestValidState->tunnelSdrRgbRange ==
                    (expectedSdrRange == MWCAP_VIDEO_QUANTIZATION_LIMITED ? TunnelSdrRgbRange::Limited :
                     expectedSdrRange == MWCAP_VIDEO_QUANTIZATION_FULL ? TunnelSdrRgbRange::Full : TunnelSdrRgbRange::Unknown));
                Assert::IsTrue(std::equal(picture.input.begin(), picture.input.end(),
                    static_cast<const BYTE*>(callback.frames.front()->GetData())));
                auto state = callback.latestValidState; state->dolbyVisionTunnel = true;
                Formatter formatter; formatter.OnVideoState(state);
                std::vector<BYTE> decoded(formatter.GetOutFrameSize());
                Assert::IsTrue(formatter.FormatVideoFrame(*callback.frames.front(), decoded.data()));
                AssertPeak(formatter.GetDolbyVisionTunnelMetadata(), 3072);
                AssertExactSamples(picture, decoded);
            }
        }

        TEST_METHOD(OrdinaryLimitedRgbConversionCorruptsTunnelAndCannotBeAdmitted)
        {
            using namespace DolbyVisionTestSender;
            signalMode = 5;
            SenderPicture picture(false, 130, 128, VideoFrameEncoding::ARGB_8BIT);
            picture.Embed(Packetize(DmBody())); rgbSenderBytes = &picture.input;
            const auto sdk = MagewellSdkTestAccess::Create(CaptureApi()); MWCAP_CHANNEL_INFO info = {};
            QueueCallback callback; callback.goal = 1;
            MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
            device.SetCallbackHandler(&callback); device.StartCapture(1);
            Assert::IsTrue(callback.Wait([&]{ return !callback.frames.empty(); })); device.StopCapture();
            Assert::IsFalse(DolbyVisionTunnelCanEnable(true, true, *callback.latestValidState));
            Assert::IsFalse(std::equal(picture.input.begin(), picture.input.end(),
                static_cast<const BYTE*>(callback.frames.front()->GetData())));
            // Negative control: forcing those converted bytes through the decoder cannot pass CRC.
            auto state = callback.latestValidState; state->rgbTunnelBytesPreserved = true; state->dolbyVisionTunnel = true;
            Formatter formatter; formatter.OnVideoState(state);
            std::vector<BYTE> output(formatter.GetOutFrameSize());
            formatter.FormatVideoFrame(*callback.frames.front(), output.data());
            Assert::IsFalse(formatter.GetDolbyVisionTunnelMetadata().valid);
        }

        TEST_METHOD(RawModeSwitchRestoresRangeAndPublishesNewStateBeforeOrdinaryFrames)
        {
            using namespace DolbyVisionTestSender;
            signalMode = 5;
            SenderPicture picture(false, 130, 128, VideoFrameEncoding::ARGB_8BIT);
            picture.Embed(Packetize(DmBody())); rgbSenderBytes = &picture.input;
            const auto sdk = MagewellSdkTestAccess::Create(CaptureApi()); MWCAP_CHANNEL_INFO info = {};
            QueueCallback callback; callback.goal = 1;
            MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
            device.SetCallbackHandler(&callback); device.SetRawRgbTunnelCapture(true); device.StartCapture(1);
            Assert::IsTrue(callback.Wait([&]{ return !callback.frames.empty(); }));
            device.SetRawRgbTunnelCapture(false);
            Assert::IsTrue(callback.Wait([&]{ return callback.frames.size() >= 2 || !callback.errors.empty(); }));
            device.StopCapture(); Assert::IsTrue(callback.errors.empty());
            Assert::AreEqual(int(MWCAP_VIDEO_QUANTIZATION_UNKNOWN), inputRangeSetting.load());
            Assert::IsFalse(callback.latestValidState->rgbTunnelBytesPreserved);
            Assert::IsTrue(callback.validStates >= 2 && callback.invalidStates >= 1);
            Assert::IsFalse(std::equal(picture.input.begin(), picture.input.end(),
                static_cast<const BYTE*>(callback.frames.back()->GetData())));
        }

        TEST_METHOD(RawRangeSetupFailuresNeverDeliverFramesAndRestorePartialWrites)
        {
            for (const int failure : { 1, 2, 3 })
            {
                ResetFakeCard(); signalMode = 5; rangeFailure = failure;
                const auto sdk = MagewellSdkTestAccess::Create(CaptureApi()); MWCAP_CHANNEL_INFO info = {};
                QueueCallback callback;
                MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
                device.SetCallbackHandler(&callback); device.SetRawRgbTunnelCapture(true); device.StartCapture(1);
                Assert::IsTrue(callback.Wait([&]{ return !callback.errors.empty(); })); device.StopCapture();
                Assert::IsTrue(callback.frames.empty());
                Assert::AreEqual(int(MWCAP_VIDEO_QUANTIZATION_UNKNOWN), inputRangeSetting.load());
            }
        }

        TEST_METHOD(OrdinaryMagewellRgb8RejectsRemappedTunnelBytes)
        {
            const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
            MWCAP_CHANNEL_INFO info = {};
            MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
            HoldingCallback callback;
            device.SetCallbackHandler(&callback);
            for (const auto encoding : { VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
            for (const auto range : { MWCAP_VIDEO_QUANTIZATION_FULL,
                MWCAP_VIDEO_QUANTIZATION_LIMITED, MWCAP_VIDEO_QUANTIZATION_UNKNOWN })
            {
                MagewellCaptureTestAccess::PublishRgb8(device, range, encoding);
                Assert::IsNotNull(callback.state.p);
                Assert::IsTrue(callback.state->valid);
                Assert::IsTrue(callback.state->videoFrameEncoding == encoding);
                Assert::AreEqual(range == MWCAP_VIDEO_QUANTIZATION_FULL,
                    callback.state->rgbTunnelBytesPreserved);
                Assert::AreEqual(range == MWCAP_VIDEO_QUANTIZATION_FULL,
                    DolbyVisionTunnelCanEnable(true, true, *callback.state));
            }
        }

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

		TEST_METHOD(MemoryBudgetCountsRetiredFramesUntilLastRelease)
		{
			auto budget = std::make_shared<MagewellCaptureMemoryBudget>(48);
			auto oldBuffer = std::make_shared<MagewellCaptureBuffer>(32, budget);
			auto* retained = oldBuffer.get();
			retained->AddRef();
			oldBuffer.reset(); // Old capture pool was cleared during a format change.
			Assert::ExpectException<std::runtime_error>([&]() {
				auto rejected = std::make_shared<MagewellCaptureBuffer>(32, budget);
			});
			auto newBuffer = std::make_shared<MagewellCaptureBuffer>(16, budget);
			retained->Release();
			auto replacement = std::make_shared<MagewellCaptureBuffer>(32, budget);
			Assert::AreEqual<uint32_t>(32, replacement->Size());
		}

		TEST_METHOD(CaptureFillsDeepQueueAndEightFrameAnalysisLookahead)
		{
			QueueCallback callback;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			device.SetCallbackHandler(&callback);
			streamFrames = true;
			device.StartCapture(1);
			const bool reached = callback.Wait([&]() {
				return callback.frames.size() >= 34 || !callback.errors.empty();
			});
			device.StopCapture();
			device.SetCallbackHandler(nullptr);
			Assert::IsTrue(reached && callback.frames.size() >= 34);
			Assert::IsTrue(callback.errors.empty());
			for (size_t i = 0; i < callback.frames.size(); ++i)
				for (size_t j = 0; j < i; ++j)
					Assert::IsTrue(callback.frames[i]->GetData() != callback.frames[j]->GetData());
			Assert::IsTrue(AlphaQueuePolicy::CanDequeue(callback.frames.size(), 10, true));
			struct Entry { bool cadenceRepeat = false; };
			const std::vector<Entry> queue(callback.frames.size());
			const auto preview = AlphaQueuePolicy::SelectActivePicturePreview(queue, 8, 8);
			Assert::AreEqual<size_t>(8, preview.effectiveFutureFrames);
			Assert::AreEqual<uint32_t>(11u | (1u << 10) | (12u << 20),
				reinterpret_cast<const uint32_t*>(callback.frames.front()->GetData())[0]);
		}

		TEST_METHOD(PoolBudgetExhaustionInvalidatesInsteadOfSilentlyStarving)
		{
			QueueCallback callback;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			// 102x100 V210 is 384 bytes/row. Exercise the boundary using 300 KiB.
			MagewellCaptureTestAccess::SetBudget(device, 8 * 384 * 100);
			device.SetCallbackHandler(&callback);
			streamFrames = true;
			device.StartCapture(1);
			const bool failed = callback.Wait([&]() { return !callback.errors.empty(); });
			device.StopCapture();
			device.SetCallbackHandler(nullptr);
			Assert::IsTrue(failed);
			Assert::AreEqual<size_t>(8, callback.frames.size());
			Assert::IsTrue(callback.invalidStates > 0);
			Assert::IsTrue(callback.errors.front().Find(TEXT("memory budget")) >= 0);
		}

		TEST_METHOD(SdiRgbDepthSelectsNativeFormatsAndConvertsTwelveBit)
		{
			signalMode = 3;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			BYTE depth = 0;
			int sampling = -1;
			VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
			Assert::IsTrue(MagewellCaptureTestAccess::ReadFormat(device, depth, sampling, encoding));
			Assert::AreEqual<int>(10, depth);
			Assert::AreEqual(0, sampling);
			Assert::IsTrue(encoding == VideoFrameEncoding::R10l);
			sdiDepth = SDI_BIT_DEPTH_8BIT;
			Assert::IsTrue(MagewellCaptureTestAccess::ReadFormat(device, depth, sampling, encoding));
			Assert::IsTrue(encoding == VideoFrameEncoding::ARGB_8BIT);
			sdiDepth = SDI_BIT_DEPTH_12BIT;
			Assert::IsTrue(MagewellCaptureTestAccess::ReadFormat(device, depth, sampling, encoding));
			Assert::AreEqual<int>(12, depth);
			Assert::IsTrue(encoding == VideoFrameEncoding::V210);
		}

		TEST_METHOD(HdmiRgb12DeliversConvertedYuvFrames)
		{
			signalMode = 1;
			QueueCallback callback;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			device.SetCallbackHandler(&callback);
			device.StartCapture(1);
			const bool delivered = callback.Wait([&]() { return !callback.frames.empty(); });
			device.StopCapture();
			device.SetCallbackHandler(nullptr);
			Assert::IsTrue(delivered);
			Assert::IsTrue(callback.errors.empty());
			Assert::IsTrue(requestedColor == MWCAP_VIDEO_COLOR_FORMAT_YUV601);
			Assert::IsTrue(requestedRange == MWCAP_VIDEO_QUANTIZATION_LIMITED);
		}

		TEST_METHOD(UnsupportedSignalRecoversWithoutRestartOrDriverNotification)
		{
			QueueCallback callback;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			device.SetCallbackHandler(&callback);
			device.StartCapture(1);
			const bool first = callback.Wait([&]() { return !callback.frames.empty(); });
			bool invalidated = false, recovered = false, deliveredAgain = false;
			if (first)
			{
				signalMode = 4;
				formatEventPending = true;
				SetEvent(fakeNotifyEvent);
				invalidated = callback.Wait([&]() { return callback.invalidStates > 0; });
				signalMode = 0; // No notification: adapter must poll while unsupported.
				recovered = callback.Wait([&]() { return callback.validStates >= 2; });
				if (recovered)
				{
					SetEvent(fakeNotifyEvent);
					deliveredAgain = callback.Wait([&]() { return callback.frames.size() >= 2; });
				}
			}
			device.StopCapture();
			device.SetCallbackHandler(nullptr);
			Assert::IsTrue(first && invalidated && recovered && deliveredAgain);
			Assert::AreEqual<size_t>(1, callback.errors.size());
			Assert::IsTrue(scanEnabled.load());
		}

		TEST_METHOD(PersistentClockFailureInvalidatesOnCaptureThread)
		{
			QueueCallback callback;
			callback.failClockAfterFirstFrame = true;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			device.SetCallbackHandler(&callback);
			streamFrames = true;
			device.StartCapture(1);
			const bool failed = callback.Wait([&]() { return !callback.errors.empty(); });
			const auto fallback = device.TimingClockNow();
			device.StopCapture();
			device.SetCallbackHandler(nullptr);
			Assert::IsTrue(failed && callback.invalidStates > 0);
			Assert::AreEqual<size_t>(1, callback.errors.size());
			Assert::IsTrue(callback.errors.front().Find(TEXT("clock")) >= 0);
			Assert::IsTrue(device.TimingClockNow() >= fallback);
		}

		TEST_METHOD(InvalidInitialGeometryCanRecoverByPolling)
		{
			QueueCallback callback;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			device.SetCallbackHandler(&callback);
			signalMode = 2;
			device.StartCapture(1);
			const bool rejected = callback.Wait([&]() { return !callback.errors.empty(); });
			signalMode = 0;
			const bool recovered = callback.Wait([&]() { return !callback.frames.empty(); });
			device.StopCapture();
			device.SetCallbackHandler(nullptr);
			Assert::IsTrue(rejected && recovered);
			Assert::AreEqual<size_t>(1, callback.errors.size());
		}

		TEST_METHOD(LostFrameNotificationsInvalidateInsteadOfKeepingStaleVideo)
		{
			QueueCallback callback;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			device.SetCallbackHandler(&callback);
			device.StartCapture(1); // The fake emits one frame, then no notifications.
			const bool failed = callback.Wait([&]() { return !callback.errors.empty(); });
			device.StopCapture();
			device.SetCallbackHandler(nullptr);
			Assert::IsTrue(failed && callback.invalidStates > 0);
			Assert::AreEqual<size_t>(1, callback.frames.size());
			Assert::IsTrue(callback.errors.front().Find(TEXT("buffered frames")) >= 0);
		}

		TEST_METHOD(PersistentCopyFailureInvalidatesAndReportsOnlyOnce)
		{
			QueueCallback callback;
			const auto sdk = MagewellSdkTestAccess::Create(CaptureApi());
			MWCAP_CHANNEL_INFO info = {};
			MagewellCaptureDevice device(sdk, L"fake", info, TEXT("fake"));
			device.SetCallbackHandler(&callback);
			failCopies = true;
			device.StartCapture(1);
			const bool failed = callback.Wait([&]() { return !callback.errors.empty(); });
			device.StopCapture();
			device.SetCallbackHandler(nullptr);
			Assert::IsTrue(failed && callback.invalidStates > 0);
			Assert::AreEqual<size_t>(1, callback.errors.size());
			Assert::IsTrue(callback.frames.empty());
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
			device.SetFrameOffsetMs(90);
			fakeDeviceTime = 1020000;
			Assert::AreEqual<timingclocktime_t>(20000, device.TimingClockNow());
			fakeDeviceTime = 1050000;
			Assert::AreEqual<timingclocktime_t>(50000, device.TimingClockNow());
			fakeDeviceTime = 1040000;
			Assert::AreEqual<timingclocktime_t>(50000, device.TimingClockNow());
			failDeviceTime = true;
			Assert::AreEqual<timingclocktime_t>(50000, device.TimingClockNow());
			failDeviceTime = false;
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
			api.MWGetInputSourceScan = &FakeGetScan;
			api.MWSetInputSourceScan = &FakeSetScan;
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
			Assert::IsTrue(scanEnabled.load());
			device.SetCallbackHandler(nullptr);
		}

		TEST_METHOD(FakeCardCapturesBt601AndRetainsFrameAcrossStop)
		{
			selectedInput = 0;
			scanEnabled = true;
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
			api.MWGetInputSourceScan = &FakeGetScan;
			api.MWSetInputSourceScan = &FakeSetScan;
			api.MWGetDeviceTime = &FakeDeviceTime;
			api.MWStartVideoCapture = &FakeCaptureStart;
			api.MWStopVideoCapture = &FakeStop;
			api.MWRegisterNotify = &FakeCaptureRegister;
			api.MWUnregisterNotify = &FakeUnregister;
			api.MWGetNotifyStatus = &FakeNotify;
			api.MWGetVideoSignalStatus = &FakeSignal;
            api.MWGetVideoInputQuantizationRange = &FakeGetRange;
            api.MWSetVideoInputQuantizationRange = &FakeSetRange;
			api.MWGetInputSpecificStatus = &FakeInputSpecific;
			api.MWGetHDMIInfoFrameValidFlag = &FakeHdrValid;
            api.MWGetHDMIInfoFramePacket = &FakeAviPacket;
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
			device.SetFrameOffsetMs(90);
			device.StartCapture(1);
			bool completed = false;
			{
				std::unique_lock<std::mutex> lock(callback.mutex);
				completed = callback.arrived.wait_for(lock,
					std::chrono::seconds(2), [&]() {
						return callback.heldFrame != nullptr || !callback.error.IsEmpty();
					});
			}
			const bool scanDisabledDuringCapture = !scanEnabled.load();
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
			Assert::IsTrue(scanDisabledDuringCapture);
			Assert::IsTrue(scanEnabled.load());
			Assert::IsTrue(requestedColor == MWCAP_VIDEO_COLOR_FORMAT_YUV601);
			Assert::IsTrue(requestedRange == MWCAP_VIDEO_QUANTIZATION_LIMITED);
			Assert::IsTrue(callback.state->colorspace == ColorSpace::REC_601_525);
			Assert::IsTrue(callback.state->videoFrameEncoding == VideoFrameEncoding::V210);
			const uint32_t* words = reinterpret_cast<const uint32_t*>(
				callback.heldFrame->GetData());
			Assert::AreEqual<uint32_t>(11u | (1u << 10) | (12u << 20), words[0]);
			Assert::AreEqual<timingclocktime_t>(1000,
				callback.heldFrame->GetCaptureTimingTimestamp());
			Assert::AreEqual<timingclocktime_t>(901000,
				callback.heldFrame->GetTimingTimestamp());
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
			Assert::IsTrue(encoding == VideoFrameEncoding::R10l);
			MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
				12, formats, fourcc, encoding);
			Assert::AreEqual<DWORD>(MWFOURCC_P210, fourcc);
			Assert::IsTrue(encoding == VideoFrameEncoding::V210);
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

        TEST_METHOD(ProCapture4kPlusAdvertisedFormatsSelectBgraByteOrder)
        {
            DWORD fourcc = 0;
            VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
            MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
                8, proCapture4kPlusFormats, fourcc, encoding);
            Assert::AreEqual<DWORD>(MWFOURCC_BGRA, fourcc);
            Assert::IsTrue(encoding == VideoFrameEncoding::BGRA_8BIT);
            // Independent SDK memory layout: B,G,R,A, not A,R,G,B.
            const uint8_t pixel[] = { 0x12, 0x34, 0x56, 0xE7 };
            Assert::AreEqual(uint32_t((0x56u << 20) | (0x34u << 10) | 0x12u),
                ReadDolbyVisionTunnelWord(pixel, encoding));
            Assert::ExpectException<std::runtime_error>([&] {
                MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
                    8, { MWFOURCC_P210, MWFOURCC_BGR24 }, fourcc, encoding);
            });
            MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
                8, { MWFOURCC_ARGB, MWFOURCC_BGRA }, fourcc, encoding);
            Assert::AreEqual<DWORD>(MWFOURCC_ARGB, fourcc);
        }

		TEST_METHOD(Rgb12CompatibilityRequiresP210AndEvenWidth)
		{
			DWORD fourcc = 0;
			VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
			MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
				12, { MWFOURCC_P210 }, fourcc, encoding);
			Assert::AreEqual<DWORD>(MWFOURCC_P210, fourcc);
			Assert::IsTrue(encoding == VideoFrameEncoding::V210);
			Assert::ExpectException<std::runtime_error>([&]() {
				MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
					12, { MWFOURCC_ARGB, MWFOURCC_RGB10 }, fourcc, encoding);
			});
			Assert::ExpectException<std::runtime_error>([&]() {
				MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
					12, { MWFOURCC_P210 }, fourcc, encoding, 1919);
			});
			for (BYTE depth : { BYTE(0), BYTE(10), BYTE(16) })
				Assert::ExpectException<std::runtime_error>([&]() {
					MagewellCaptureTestAccess::Choose(MWCAP_VIDEO_COLOR_FORMAT_RGB,
						depth, { MWFOURCC_P210 }, fourcc, encoding);
				});
		}

		TEST_METHOD(Rgb10ToR10lPreservesChannelCodesAndRowPadding)
		{
			// Two rows, three pixels each. R10l rows are padded to 256 bytes.
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
				0x0c, 0x20, 0x40, 0x00,
				0x00, 0x08, 0xc0, 0xff,
				0x00, 0xc1, 0x3a, 0x10 };
			for (size_t i = 0; i < firstRow.size(); ++i)
				Assert::AreEqual(firstRow[i], result[i]);
			Assert::AreEqual<uint8_t>(0, result[255]);
			Assert::AreEqual<uint8_t>(0x18, result[256]);
			Assert::AreEqual<uint8_t>(0x50, result[257]);
			Assert::IsTrue(AlphaCanUseNativeRgbUpload(VideoFrameEncoding::R10l));
		}

		TEST_METHOD(Rgb10RepackFeedsNativeRgbAnalysisAtLimitedEndpoints)
		{
			const uint32_t black = 64u | (64u << 10) | (64u << 20);
			const uint32_t white = 940u | (940u << 10) | (940u << 20);
			const std::array<uint32_t, 2> source = { black, white };
			std::array<uint8_t, 256> packed = {};
			MagewellCaptureTestAccess::RepackRgb(
				reinterpret_cast<const uint8_t*>(source.data()), 8,
				packed.data(), 256, 2, 1);
			const AnalysisLumaSource analysis = {
				packed.data(), packed.size(), 2, 1, 256, 0,
				AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::R10l,
				ColorSpace::REC_709, 1 };
			AnalysisLumaSample blackSample, whiteSample;
			Assert::IsTrue(analysis.Sample(0, 0, blackSample));
			Assert::IsTrue(analysis.Sample(1, 0, whiteSample));
			Assert::AreEqual(64, static_cast<int>(blackSample.luma));
			Assert::AreEqual(940, static_cast<int>(whiteSample.luma));
			Assert::AreEqual(512, static_cast<int>(blackSample.chromaU));
			Assert::AreEqual(512, static_cast<int>(whiteSample.chromaV));
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

		TEST_METHOD(P210ToV210FourKRepackProfileAndReference)
		{
			constexpr uint32_t width = 3840;
			constexpr uint32_t height = 2160;
			constexpr uint32_t sourceStride = width * 2;
			constexpr uint32_t destinationStride = ((width + 47) / 48) * 128;
			constexpr size_t bufferCount = 4;
			std::vector<std::vector<uint8_t>> sources(bufferCount,
				std::vector<uint8_t>(static_cast<size_t>(sourceStride) * height * 2));
			std::vector<std::vector<uint8_t>> destinations(bufferCount,
				std::vector<uint8_t>(static_cast<size_t>(destinationStride) * height));
			for (size_t i = 0; i < bufferCount; ++i)
				FillP210Frame(sources[i], sourceStride, width, height,
					static_cast<uint32_t>(i + 1));

			MagewellCaptureTestAccess::RepackYuv(sources[0].data(), sourceStride,
				destinations[0].data(), destinationStride, width, height);
			Assert::IsTrue(VerifyP210ToV210(sources[0], sourceStride, destinations[0],
				destinationStride, width, height),
				L"The full-frame V210 words must decode to the source P210 codes.");

			const auto singleBuffer = MeasureRepack([&](size_t) {
				MagewellCaptureTestAccess::RepackYuv(sources[0].data(), sourceStride,
					destinations[0].data(), destinationStride, width, height);
			}, 1);
			const auto rotatingBuffers = MeasureRepack([&](size_t index) {
				MagewellCaptureTestAccess::RepackYuv(sources[index].data(), sourceStride,
					destinations[index].data(), destinationStride, width, height);
			}, bufferCount);
			for (size_t i = 0; i < bufferCount; ++i)
				Assert::IsTrue(VerifyP210ToV210(sources[i], sourceStride,
					destinations[i], destinationStride, width, height),
					L"Each rotating P210 output must match its corresponding source frame.");
			const auto captureLayout = MeasureRepack([&](size_t index) {
				MagewellCaptureTestAccess::RepackYuv(sources[0].data(), sourceStride,
					destinations[index].data(), destinationStride, width, height);
			}, bufferCount);
			for (size_t i = 0; i < bufferCount; ++i)
				Assert::IsTrue(VerifyP210ToV210(sources[0], sourceStride,
					destinations[i], destinationStride, width, height),
					L"Every timed P210 output buffer must contain a valid full-frame repack.");
			Logger::WriteMessage(FormatRepackTimings(L"P210-to-V210",
				L"single-buffer", singleBuffer).c_str());
			Logger::WriteMessage(FormatRepackTimings(L"P210-to-V210",
				L"rotating-4", rotatingBuffers).c_str());
			Logger::WriteMessage(FormatRepackTimings(L"P210-to-V210",
				L"capture-layout", captureLayout).c_str());
		}

		TEST_METHOD(P210TailWidthsAndPaddedRowsMatchDecodedCodes)
		{
			constexpr uint32_t height = 2;
			for (const uint32_t width : { 8u, 10u })
			{
				const uint32_t sourceStride = width * 2 + 8;
				const uint32_t destinationStride = ((width + 47) / 48) * 128;
				std::vector<uint8_t> source(static_cast<size_t>(sourceStride) * height * 2, 0xA5);
				FillP210Frame(source, sourceStride, width, height, width);
				std::vector<uint8_t> destination(
					static_cast<size_t>(destinationStride) * height, 0xCD);
				MagewellCaptureTestAccess::RepackYuv(source.data(), sourceStride,
					destination.data(), destinationStride, width, height);
				Assert::IsTrue(VerifyP210ToV210(source, sourceStride, destination,
					destinationStride, width, height),
					L"Partial V210 groups must repeat the final P210 sample for width remainder 2 or 4.");
				for (uint32_t row = 0; row < height; ++row)
					for (uint32_t byte = 32; byte < destinationStride; ++byte)
						Assert::AreEqual<uint8_t>(0xCD, destination[
							static_cast<size_t>(row) * destinationStride + byte],
							L"Repacking must leave aligned V210 row padding untouched.");
			}
		}

		TEST_METHOD(Rgb10ToR10lFourKRepackProfileAndReference)
		{
			constexpr uint32_t width = 3840;
			constexpr uint32_t height = 2160;
			constexpr uint32_t sourceStride = width * 4;
			constexpr uint32_t destinationStride = ((width + 63) / 64) * 256;
			constexpr size_t bufferCount = 4;
			std::vector<std::vector<uint8_t>> sources(bufferCount,
				std::vector<uint8_t>(static_cast<size_t>(sourceStride) * height));
			std::vector<std::vector<uint8_t>> destinations(bufferCount,
				std::vector<uint8_t>(static_cast<size_t>(destinationStride) * height));
			for (size_t i = 0; i < bufferCount; ++i)
				FillRgb10Frame(sources[i], sourceStride, width, height,
					static_cast<uint32_t>(i + 1));

			MagewellCaptureTestAccess::RepackRgb(sources[0].data(), sourceStride,
				destinations[0].data(), destinationStride, width, height);
			auto verifyRgbOutput = [&](size_t index) {
				for (uint32_t row = 0; row < height; ++row)
				{
					const auto* source = reinterpret_cast<const uint32_t*>(
						sources[index].data() + static_cast<size_t>(row) * sourceStride);
					const auto* output = reinterpret_cast<const uint32_t*>(
						destinations[index].data() + static_cast<size_t>(row) * destinationStride);
					for (uint32_t x = 0; x < width; ++x)
					{
						const uint32_t packed = source[x];
						if (((output[x] >> 22) & 0x3FF) != (packed & 0x3FF) ||
							((output[x] >> 12) & 0x3FF) != ((packed >> 10) & 0x3FF) ||
							((output[x] >> 2) & 0x3FF) != ((packed >> 20) & 0x3FF) ||
							(output[x] & 0x3) != 0)
							return false;
					}
				}
				return true;
			};
			Assert::IsTrue(verifyRgbOutput(0),
				L"The full-frame R10l channel codes must match the source RGB10 values.");

			const auto singleBuffer = MeasureRepack([&](size_t) {
				MagewellCaptureTestAccess::RepackRgb(sources[0].data(), sourceStride,
					destinations[0].data(), destinationStride, width, height);
			}, 1);
			const auto rotatingBuffers = MeasureRepack([&](size_t index) {
				MagewellCaptureTestAccess::RepackRgb(sources[index].data(), sourceStride,
					destinations[index].data(), destinationStride, width, height);
			}, bufferCount);
			for (size_t i = 0; i < bufferCount; ++i)
				Assert::IsTrue(verifyRgbOutput(i),
					L"Every timed RGB10 output buffer must preserve all 10-bit channel codes.");
			Logger::WriteMessage(FormatRepackTimings(L"RGB10-to-R10l",
				L"single-buffer", singleBuffer).c_str());
			Logger::WriteMessage(FormatRepackTimings(L"RGB10-to-R10l",
				L"rotating-4", rotatingBuffers).c_str());
		}
	};
}
