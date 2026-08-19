#include <pch.h>

#include "LibplaceboVideoRenderer.h"

#include <ConfigFile.h>
#include <EventActionLauncher.h>
#include <ActivePictureTransitionModel.h>
#include <AspectRatio.h>
#include <ActivePictureEvidence.h>
#include <RendererConfigView.h>
#include <RendererProfileConfig.h>
#include <UnifiedProfileRuntime.h>
#include <DebugLog.h>
#include <DisplayRuleExpression.h>
#include <DisplayRefreshRatePolicy.h>
#include <microsoft_directshow/MadVRShaderLoader.h>
#include <vprenderer/AlphaCadenceCorrectionPolicy.h>
#include <vprenderer/AlphaQueuePolicy.h>
#include <vprenderer/LibplaceboDisplayLut.h>
#include <vprenderer/AlphaPresentationTelemetry.h>
#include <vprenderer/AlphaNativeRgbIngress.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/NativeStatsOverlayPlacement.h>
#include <SceneDetector.h>
#include <vprenderer/LibplaceboOutputPolicy.h>
#include <vprenderer/LibplaceboRenderParameters.h>
#include <video_frame_formatter/CARGBtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CDeckLinkRGBToP010VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP210VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP210VideoFrameFormatter.h>

#pragma warning(push)
#pragma warning(disable: 4244) // conversion warning in an upstream inline helper
#include <libplacebo/cache.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/utils/upload.h>
#pragma warning(pop)

#include <dxgi1_6.h>
#include <nvapi.h>
#include <wincodec.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{
	using SteadyClock = std::chrono::steady_clock;
	constexpr size_t MAX_USER_SHADER_BYTES = 4 * 1024 * 1024;
	uint64_t AlphaSourceFormatKey(const VideoState& state);

	int64_t SteadyClockNowNs()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(
			SteadyClock::now().time_since_epoch()).count();
	}

	int64_t PerformanceCounterNow()
	{
		LARGE_INTEGER value{};
		return QueryPerformanceCounter(&value) ? value.QuadPart : 0;
	}

	std::string CurrentExecutablePath()
	{
		std::vector<wchar_t> buffer(32768);
		const DWORD length = GetModuleFileNameW(
			nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size())
			return {};
		CW2A utf8(buffer.data(), CP_UTF8);
		return std::string(static_cast<const char*>(utf8));
	}

	std::wstring ScreenshotDirectory()
	{
		std::vector<wchar_t> buffer(32768);
		const DWORD length = GetModuleFileNameW(
			nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size())
			return L"screenshots";
		std::wstring path(buffer.data(), length);
		const size_t separator = path.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
			return L"screenshots";
		path.resize(separator);
		return path + L"\\screenshots";
	}

	std::string JsonEscape(const std::string& value)
	{
		std::ostringstream result;
		for (const unsigned char character : value)
		{
			switch (character)
			{
			case '\\': result << "\\\\"; break;
			case '"': result << "\\\""; break;
			case '\n': result << "\\n"; break;
			case '\r': result << "\\r"; break;
			case '\t': result << "\\t"; break;
			default:
				if (character < 0x20)
					result << "\\u" << std::hex << std::setw(4) <<
						std::setfill('0') << static_cast<unsigned int>(character) <<
						std::dec;
				else
					result << character;
			}
		}
		return result.str();
	}

	std::string WideToUtf8(const std::wstring& value)
	{
		if (value.empty()) return {};
		const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
			static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		if (length <= 0) return {};
		std::string result(static_cast<size_t>(length), '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
			static_cast<int>(value.size()), &result[0], length, nullptr, nullptr);
		return result;
	}

	std::wstring CaptureTimestampStem(uint64_t sourceSequence)
	{
		SYSTEMTIME time{};
		GetLocalTime(&time);
		wchar_t stem[128] = {};
		swprintf_s(stem,
			L"VP-rendered-%04u%02u%02u-%02u%02u%02u-%03u-frame-%llu",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
			time.wSecond, time.wMilliseconds,
			static_cast<unsigned long long>(sourceSequence));
		return stem;
	}

	std::string Sha256Hex(const void* data, size_t size)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		DWORD objectBytes = 0;
		DWORD hashBytes = 0;
		DWORD resultBytes = 0;
		std::vector<uint8_t> object;
		std::vector<uint8_t> digest;
		NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm,
			BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (status >= 0) status = BCryptGetProperty(algorithm,
			BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
			sizeof(objectBytes), &resultBytes, 0);
		if (status >= 0) status = BCryptGetProperty(algorithm,
			BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashBytes),
			sizeof(hashBytes), &resultBytes, 0);
		if (status >= 0)
		{
			object.resize(objectBytes);
			digest.resize(hashBytes);
			status = BCryptCreateHash(algorithm, &hash, object.data(),
				static_cast<ULONG>(object.size()), nullptr, 0, 0);
		}
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		while (status >= 0 && size > 0)
		{
			const ULONG chunk = static_cast<ULONG>(std::min<size_t>(
				size, std::numeric_limits<ULONG>::max()));
			status = BCryptHashData(hash, const_cast<PUCHAR>(bytes), chunk, 0);
			bytes += chunk;
			size -= chunk;
		}
		if (status >= 0) status = BCryptFinishHash(hash, digest.data(),
			static_cast<ULONG>(digest.size()), 0);
		if (hash) BCryptDestroyHash(hash);
		if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
		if (status < 0) return {};
		std::ostringstream result;
		result << std::hex << std::setfill('0');
		for (const uint8_t byte : digest)
			result << std::setw(2) << static_cast<unsigned int>(byte);
		return result.str();
	}

	bool WriteR10Png16(const std::wstring& filename,
		const std::vector<uint32_t>& pixels, unsigned int width,
		unsigned int height, std::string& error)
	{
		const HRESULT initialize = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool uninitialize = SUCCEEDED(initialize);
		CComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		CComPtr<IWICStream> stream;
		CComPtr<IWICBitmapEncoder> encoder;
		CComPtr<IWICBitmapFrameEncode> frame;
		CComPtr<IPropertyBag2> properties;
		if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
		if (SUCCEEDED(result)) result = stream->InitializeFromFilename(
			filename.c_str(), GENERIC_WRITE);
		if (SUCCEEDED(result)) result = factory->CreateEncoder(
			GUID_ContainerFormatPng, nullptr, &encoder);
		if (SUCCEEDED(result)) result = encoder->Initialize(stream,
			WICBitmapEncoderNoCache);
		if (SUCCEEDED(result)) result = encoder->CreateNewFrame(
			&frame, &properties);
		if (SUCCEEDED(result)) result = frame->Initialize(properties);
		if (SUCCEEDED(result)) result = frame->SetSize(width, height);
		WICPixelFormatGUID format = GUID_WICPixelFormat64bppRGBA;
		if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
		if (SUCCEEDED(result) && format != GUID_WICPixelFormat64bppRGBA)
			result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;

		std::vector<uint16_t> rgba;
		if (SUCCEEDED(result))
		{
			rgba.resize(pixels.size() * 4);
			for (size_t index = 0; index < pixels.size(); ++index)
			{
				const uint32_t packed = pixels[index];
				const uint16_t red = static_cast<uint16_t>(packed & 0x3ffu);
				const uint16_t green = static_cast<uint16_t>((packed >> 10) & 0x3ffu);
				const uint16_t blue = static_cast<uint16_t>((packed >> 20) & 0x3ffu);
				const uint16_t alpha = static_cast<uint16_t>((packed >> 30) & 0x3u);
				// Bit replication preserves every original 10-bit code in the
				// high ten bits and expands the endpoints exactly to 16-bit.
				rgba[index * 4 + 0] = LibplaceboOutput::ExpandR10ToR16(red);
				rgba[index * 4 + 1] = LibplaceboOutput::ExpandR10ToR16(green);
				rgba[index * 4 + 2] = LibplaceboOutput::ExpandR10ToR16(blue);
				rgba[index * 4 + 3] = static_cast<uint16_t>(alpha * 21845u);
			}
			const UINT stride = width * 4u * sizeof(uint16_t);
			result = frame->WritePixels(height, stride,
				static_cast<UINT>(rgba.size() * sizeof(uint16_t)),
				reinterpret_cast<BYTE*>(rgba.data()));
		}
		if (SUCCEEDED(result)) result = frame->Commit();
		if (SUCCEEDED(result)) result = encoder->Commit();
		properties.Release();
		frame.Release();
		encoder.Release();
		stream.Release();
		factory.Release();
		if (uninitialize) CoUninitialize();
		if (FAILED(result))
		{
			char message[64] = {};
			sprintf_s(message, "WIC result=0x%08lX",
				static_cast<unsigned long>(result));
			error = message;
			return false;
		}
		return true;
	}

	bool ReadUserShader(const std::string& filename, std::string& source,
		std::string& resolvedPath, std::string& reason)
	{
		source.clear();
		resolvedPath.clear();
		reason.clear();
		if (!MadVRShaderLoader::ResolveShaderFilename(
			filename, CurrentExecutablePath(), resolvedPath, reason))
			return false;

		std::ifstream input(
			resolvedPath, std::ios::binary | std::ios::ate);
		if (!input.is_open())
		{
			reason = "cannot open shader file";
			return false;
		}
		const std::streamoff length = input.tellg();
		if (length <= 0 ||
			static_cast<uint64_t>(length) > MAX_USER_SHADER_BYTES)
		{
			reason = "shader file is empty or exceeds 4 MiB";
			return false;
		}
		source.resize(static_cast<size_t>(length));
		input.seekg(0, std::ios::beg);
		if (!input.read(&source[0],
			static_cast<std::streamsize>(source.size())))
		{
			reason = "cannot read shader file";
			source.clear();
			return false;
		}
		return true;
	}

	std::string FileNameFromPath(const std::string& path)
	{
		const size_t separator = path.find_last_of("\\/");
		return separator == std::string::npos
			? path
			: path.substr(separator + 1);
	}

	bool ApplyUserShaderParameters(std::string& source,
		const std::map<std::string, std::string>& parameters,
		std::string& reason)
	{
		for (const auto& parameter : parameters)
		{
			const std::string token = "{{" + parameter.first + "}}";
			size_t position = 0;
			while ((position = source.find(token, position)) !=
				std::string::npos)
			{
				source.replace(position, token.size(), parameter.second);
				position += parameter.second.size();
			}
		}
		if (source.find("{{") != std::string::npos ||
			source.find("}}") != std::string::npos)
		{
			reason = "shader contains an unresolved {{parameter}} token";
			return false;
		}
		return true;
	}

	const char* AlphaCadenceActionText(AlphaCadenceAction action)
	{
		switch (action)
		{
		case AlphaCadenceAction::Drop:
			return "drop";
		case AlphaCadenceAction::Repeat:
			return "repeat";
		default:
			return "none";
		}
	}

	const char* AlphaCadenceBlockReasonText(
		AlphaCadenceBlockReason reason)
	{
		switch (reason)
		{
		case AlphaCadenceBlockReason::None:
			return "none";
		case AlphaCadenceBlockReason::Disabled:
			return "disabled";
		case AlphaCadenceBlockReason::PresentationEvidenceUnavailable:
			return "presentation_evidence_unavailable";
		case AlphaCadenceBlockReason::InvalidRates:
			return "invalid_rates";
		case AlphaCadenceBlockReason::RateMismatchTooLarge:
			return "rate_mismatch_too_large";
		case AlphaCadenceBlockReason::StabilizingRates:
			return "stabilizing_rates";
		case AlphaCadenceBlockReason::NoActionableMismatch:
			return "no_actionable_mismatch";
		case AlphaCadenceBlockReason::BeforeDeadline:
			return "before_deadline";
		case AlphaCadenceBlockReason::Cooldown:
			return "cooldown";
		case AlphaCadenceBlockReason::VerificationPending:
			return "verification_pending";
		case AlphaCadenceBlockReason::DropQueueNotAboveDesired:
			return "drop_queue_not_above_desired";
		case AlphaCadenceBlockReason::RepeatQueueNotBelowDesired:
			return "repeat_queue_not_below_desired";
		case AlphaCadenceBlockReason::DropPresentationDebtMissing:
			return "drop_presentation_debt_missing";
		case AlphaCadenceBlockReason::RepeatPresentationDebtPresent:
			return "repeat_presentation_debt_present";
		case AlphaCadenceBlockReason::WaitingForFreshScene:
			return "waiting_for_fresh_scene";
		case AlphaCadenceBlockReason::FallbackNotMature:
			return "fallback_not_mature";
		case AlphaCadenceBlockReason::DropFallbackQueueTooYoung:
			return "drop_fallback_queue_too_young";
		default:
			return "unknown";
		}
	}

	const char* AlphaPresentationEvidenceText(
		AlphaPresentationEvidence evidence)
	{
		switch (evidence)
		{
		case AlphaPresentationEvidence::Stable:
			return "stable";
		case AlphaPresentationEvidence::Warming:
			return "warming";
		case AlphaPresentationEvidence::Disjoint:
			return "disjoint";
		default:
			return "unavailable";
		}
	}

	const char* AlphaPresentationTimingStatusText(
		AlphaPresentationTimingStatus status)
	{
		switch (status)
		{
		case AlphaPresentationTimingStatus::Available:
			return "available";
		case AlphaPresentationTimingStatus::Disjoint:
			return "disjoint";
		case AlphaPresentationTimingStatus::FrameStatisticsUnavailable:
			return "frame_statistics_unavailable";
		case AlphaPresentationTimingStatus::FrameStatisticsFailed:
			return "frame_statistics_failed";
		default:
			return "no_swapchain";
		}
	}

	constexpr const char* SHADER_CACHE_RELATIVE_PATH =
		"vprenderer\\VideoProcessorShaderCache.bin";
	constexpr const char* SHADER_CACHE_CLEAR_RELATIVE_PATH =
		"vprenderer\\VideoProcessorShaderCache.clear";
	constexpr size_t MAX_SHADER_CACHE_FILE_SIZE =
		256u * 1024u * 1024u;
	std::mutex g_runtimeDisplayRuleMutex;
	std::string g_runtimeManualDisplayRule;

	std::string NvApiStatusText(NvAPI_Status status)
	{
		NvAPI_ShortString text{};
		if (NvAPI_GetErrorMessage(status, text) == NVAPI_OK)
			return text;
		return std::to_string(static_cast<int>(status));
	}

	std::string NarrowDisplayName(const wchar_t* value)
	{
		if (!value || !*value)
			return {};
		const int size = WideCharToMultiByte(CP_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr);
		if (size <= 1)
			return {};
		std::string result(static_cast<size_t>(size), '\0');
		WideCharToMultiByte(CP_ACP, 0, value, -1, &result[0], size, nullptr, nullptr);
		result.pop_back();
		return result;
	}

	class NvidiaBt2020Reporter
	{
	public:
		~NvidiaBt2020Reporter()
		{
			if (!Restore())
			{
				DebugLog::Log(
					"NVIDIA BT.2020 report: final AVI InfoFrame restore "
					"failed for %s; residual driver state is unverified",
					m_displayName.c_str());
				Shutdown();
			}
		}

		bool IsActive() const { return m_active; }
		bool IsReadbackVerified() const { return m_readbackVerified; }

		bool Enable(const wchar_t* displayName)
		{
			const std::string name = NarrowDisplayName(displayName);
			if (name.empty())
			{
				DebugLog::Log("NVIDIA BT.2020 report: display name unavailable; BT.2020 rendering continues without NVIDIA signaling");
				return false;
			}
			if (m_active && name == m_displayName)
				return true;
			if (!Restore())
			{
				DebugLog::Log(
					"NVIDIA BT.2020 report: refusing a new target while "
					"the previous AVI InfoFrame restore remains pending");
				return false;
			}

			NvAPI_Status status = NvAPI_Initialize();
			if (status != NVAPI_OK)
			{
				DebugLog::Log("NVIDIA BT.2020 report: NvAPI_Initialize failed: %s; BT.2020 rendering continues", NvApiStatusText(status).c_str());
				return false;
			}
			m_initialized = true;
			status = NvAPI_DISP_GetDisplayIdByDisplayName(name.c_str(), &m_displayId);
			if (status != NVAPI_OK)
			{
				DebugLog::Log("NVIDIA BT.2020 report: display %s lookup failed: %s; BT.2020 rendering continues", name.c_str(), NvApiStatusText(status).c_str());
				Shutdown();
				return false;
			}

			m_originalInfoFrame = {};
			m_originalInfoFrame.version = NV_INFOFRAME_DATA_VER;
			m_originalInfoFrame.size = sizeof(m_originalInfoFrame);
			m_originalInfoFrame.cmd = NV_INFOFRAME_CMD_GET;
			m_originalInfoFrame.type = INFOFRAME_TYPE_AVI;
			status = NvAPI_Disp_InfoFrameControl(m_displayId, &m_originalInfoFrame);
			if (status != NVAPI_OK)
			{
				DebugLog::Log("NVIDIA BT.2020 report: AVI InfoFrame read failed for %s: %s; BT.2020 rendering continues", name.c_str(), NvApiStatusText(status).c_str());
				Shutdown();
				return false;
			}

			NV_INFOFRAME_DATA requested = m_originalInfoFrame;
			requested.cmd = NV_INFOFRAME_CMD_SET;
			requested.type = INFOFRAME_TYPE_AVI;
			requested.infoframe.video.colorimetry =
				NV_INFOFRAME_FIELD_VALUE_AVI_COLORIMETRY_USE_EXTENDED_COLORIMETRY;
			// CTA-861 extended-colorimetry value 6 identifies BT.2020
			// RGB/Y'C'bC'r. The NVAPI header retains its historical RESERVED06
			// name even though this is the value used by the NVIDIA InfoFrame
			// path that madVR relies upon.
			requested.infoframe.video.extendedColorimetry =
				NV_INFOFRAME_FIELD_VALUE_AVI_EXTENDEDCOLORIMETRY_RESERVED06;
			status = NvAPI_Disp_InfoFrameControl(m_displayId, &requested);
			if (status != NVAPI_OK)
			{
				DebugLog::Log("NVIDIA BT.2020 report: AVI InfoFrame SET failed for %s: %s; BT.2020 rendering continues", name.c_str(), NvApiStatusText(status).c_str());
				Shutdown();
				return false;
			}

			NV_INFOFRAME_DATA verified{};
			verified.version = NV_INFOFRAME_DATA_VER;
			verified.size = sizeof(verified);
			verified.cmd = NV_INFOFRAME_CMD_GET;
			verified.type = INFOFRAME_TYPE_AVI;
			status = NvAPI_Disp_InfoFrameControl(m_displayId, &verified);
			const bool readbackMatches =
				status == NVAPI_OK &&
				verified.infoframe.video.colorimetry ==
					NV_INFOFRAME_FIELD_VALUE_AVI_COLORIMETRY_USE_EXTENDED_COLORIMETRY &&
				verified.infoframe.video.extendedColorimetry ==
					NV_INFOFRAME_FIELD_VALUE_AVI_EXTENDEDCOLORIMETRY_RESERVED06;
			const LibplaceboOutput::OneShotSignalAcceptance acceptance =
				LibplaceboOutput::ClassifyOneShotSignal(
					true, status == NVAPI_OK, readbackMatches);
			if (acceptance !=
				LibplaceboOutput::OneShotSignalAcceptance::READBACK_VERIFIED)
			{
				// SET is a one-shot InfoFrame flushed to the display. A later GET
				// is valuable evidence, but some driver versions report their
				// automatic state rather than the one-shot value. Do not undo a
				// successful physical transmission solely for that reason.
				m_active = true;
				m_readbackVerified = false;
				m_displayName = name;
				DebugLog::Log("NVIDIA BT.2020 report: AVI InfoFrame SET accepted for %s but GET did not echo the one-shot value status=%s colorimetry=%u extended=%u; retaining BT.2020 signal with unverified readback", name.c_str(), NvApiStatusText(status).c_str(), static_cast<unsigned int>(verified.infoframe.video.colorimetry), static_cast<unsigned int>(verified.infoframe.video.extendedColorimetry));
				return true;
			}

			m_active = true;
			m_readbackVerified = true;
			m_displayName = name;
			DebugLog::Log("NVIDIA BT.2020 report: AVI InfoFrame enabled on %s display_id=0x%08X previous_colorimetry=%u previous_extended=%u verified_colorimetry=%u verified_extended=%u", name.c_str(), m_displayId, static_cast<unsigned int>(m_originalInfoFrame.infoframe.video.colorimetry), static_cast<unsigned int>(m_originalInfoFrame.infoframe.video.extendedColorimetry), static_cast<unsigned int>(verified.infoframe.video.colorimetry), static_cast<unsigned int>(verified.infoframe.video.extendedColorimetry));
			return true;
		}

		bool Restore()
		{
			if (!m_active)
			{
				Shutdown();
				return true;
			}

			// A modeset can invalidate the NvAPI display ID. Resolve the saved
			// display name again immediately before the restoration SET.
			NvU32 currentDisplayId = 0;
			NvAPI_Status status = NvAPI_DISP_GetDisplayIdByDisplayName(
				m_displayName.c_str(), &currentDisplayId);
			if (status != NVAPI_OK)
			{
				DebugLog::Log(
					"NVIDIA BT.2020 report: AVI InfoFrame restore pending "
					"for %s because display lookup failed: %s",
					m_displayName.c_str(), NvApiStatusText(status).c_str());
				return false;
			}
			m_displayId = currentDisplayId;

			NV_INFOFRAME_DATA restore = m_originalInfoFrame;
			restore.cmd = NV_INFOFRAME_CMD_SET;
			restore.type = INFOFRAME_TYPE_AVI;
			status = NvAPI_Disp_InfoFrameControl(m_displayId, &restore);
			DebugLog::Log("NVIDIA BT.2020 report: AVI InfoFrame restore on %s display_id=0x%08X colorimetry=%u extended=%u result=%s", m_displayName.c_str(), m_displayId, static_cast<unsigned int>(restore.infoframe.video.colorimetry), static_cast<unsigned int>(restore.infoframe.video.extendedColorimetry), NvApiStatusText(status).c_str());
			if (status != NVAPI_OK)
			{
				DebugLog::Log(
					"NVIDIA BT.2020 report: restore remains pending; "
					"ownership is retained for retry");
				return false;
			}

			m_active = false;
			m_readbackVerified = false;
			m_displayName.clear();
			Shutdown();
			return true;
		}

	private:
		void Shutdown()
		{
			if (m_initialized)
				NvAPI_Unload();
			m_initialized = false;
		}

		bool m_initialized = false;
		bool m_active = false;
		bool m_readbackVerified = false;
		NvU32 m_displayId = 0;
		std::string m_displayName;
		NV_INFOFRAME_DATA m_originalInfoFrame{};
		static constexpr NvU8 INFOFRAME_TYPE_AVI = 2;
	};

	std::string ShaderCachePath()
	{
		char modulePath[MAX_PATH] = {};
		const DWORD length =
			GetModuleFileNameA(nullptr, modulePath, ARRAYSIZE(modulePath));
		if (length == 0 || length >= ARRAYSIZE(modulePath))
			return SHADER_CACHE_RELATIVE_PATH;

		std::string path(modulePath, length);
		const size_t separator = path.find_last_of("\\/");
		if (separator == std::string::npos)
			return SHADER_CACHE_RELATIVE_PATH;
		path.resize(separator + 1);
		path += SHADER_CACHE_RELATIVE_PATH;
		return path;
	}

	std::string ShaderCacheClearRequestPath()
	{
		char modulePath[MAX_PATH] = {};
		const DWORD length =
			GetModuleFileNameA(nullptr, modulePath, ARRAYSIZE(modulePath));
		if (length == 0 || length >= ARRAYSIZE(modulePath))
			return SHADER_CACHE_CLEAR_RELATIVE_PATH;

		std::string path(modulePath, length);
		const size_t separator = path.find_last_of("\\/");
		if (separator == std::string::npos)
			return SHADER_CACHE_CLEAR_RELATIVE_PATH;
		path.resize(separator + 1);
		path += SHADER_CACHE_CLEAR_RELATIVE_PATH;
		return path;
	}

	template<typename T>
	const T& LibplaceboExportedData(const char* exportName)
	{
		HMODULE module = GetModuleHandleW(L"libplacebo-360.dll");
		if (!module)
			throw std::runtime_error("libplacebo runtime was not preloaded");

		const T* value = reinterpret_cast<const T*>(GetProcAddress(module, exportName));
		if (!value)
			throw std::runtime_error("libplacebo runtime is missing a required data export");
		return *value;
	}

	struct LibplaceboCompileSnapshot
	{
		double glslMs = 0.0;
		double spirvCrossMs = 0.0;
		double hlslMs = 0.0;

		bool Compiled() const
		{
			return glslMs > 0.0 || spirvCrossMs > 0.0 || hlslMs > 0.0;
		}
	};

	class LibplaceboCompileTelemetry
	{
	public:
		void BeginRender()
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			m_active = true;
			m_snapshot = {};
		}

		LibplaceboCompileSnapshot EndRender()
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			m_active = false;
			return m_snapshot;
		}

		void Observe(const char* message)
		{
			const double glslMs = ParseDuration(
				message, "translating GLSL to SPIR-V");
			const double spirvCrossMs = ParseDuration(
				message, "translating SPIR-V to HLSL");
			const double hlslMs = ParseDuration(
				message, "translating HLSL to DXBC");
			if (glslMs <= 0.0 && spirvCrossMs <= 0.0 && hlslMs <= 0.0)
				return;

			std::lock_guard<std::mutex> guard(m_mutex);
			if (!m_active)
				return;
			m_snapshot.glslMs += glslMs;
			m_snapshot.spirvCrossMs += spirvCrossMs;
			m_snapshot.hlslMs += hlslMs;
		}

	private:
		static double ParseDuration(const char* message, const char* operation)
		{
			if (!message || !operation || !std::strstr(message, operation))
				return 0.0;
			const char* spent = std::strstr(message, "Spent ");
			if (!spent)
				return 0.0;
			char* end = nullptr;
			const double duration = std::strtod(spent + 6, &end);
			return end != spent + 6 && duration > 0.0 ? duration : 0.0;
		}

		std::mutex m_mutex;
		bool m_active = false;
		LibplaceboCompileSnapshot m_snapshot;
	};

	void LibplaceboLog(void* privateContext, enum pl_log_level level,
		const char* message)
	{
		if (!message)
			return;
		if (privateContext)
		{
			static_cast<LibplaceboCompileTelemetry*>(privateContext)->Observe(
				message);
		}

		const char* label = "info";
		switch (level)
		{
		case PL_LOG_FATAL: label = "fatal"; break;
		case PL_LOG_ERR: label = "error"; break;
		case PL_LOG_WARN: label = "warning"; break;
		case PL_LOG_DEBUG: label = "debug"; break;
		case PL_LOG_TRACE: label = "trace"; break;
		default: break;
		}

		DebugLog::Log("libplacebo [%s]: %s", label, message);
	}

	enum pl_color_primaries TranslatePrimaries(ColorSpace colorspace)
	{
		switch (colorspace)
		{
		case ColorSpace::REC_601_525: return PL_COLOR_PRIM_BT_601_525;
		case ColorSpace::REC_601_576:
		case ColorSpace::REC_601_625: return PL_COLOR_PRIM_BT_601_625;
		case ColorSpace::REC_709: return PL_COLOR_PRIM_BT_709;
		case ColorSpace::P3_D65: return PL_COLOR_PRIM_DISPLAY_P3;
		case ColorSpace::P3_DCI: return PL_COLOR_PRIM_DCI_P3;
		case ColorSpace::P3_D60: return PL_COLOR_PRIM_DISPLAY_P3;
		case ColorSpace::BT_2020: return PL_COLOR_PRIM_BT_2020;
		default: return PL_COLOR_PRIM_UNKNOWN;
		}
	}

	enum pl_color_system TranslateSystem(ColorSpace colorspace)
	{
		switch (colorspace)
		{
		case ColorSpace::REC_601_525:
		case ColorSpace::REC_601_576:
		case ColorSpace::REC_601_625:
			return PL_COLOR_SYSTEM_BT_601;
		case ColorSpace::BT_2020:
			return PL_COLOR_SYSTEM_BT_2020_NC;
		default:
			return PL_COLOR_SYSTEM_BT_709;
		}
	}

	enum pl_color_transfer TranslateTransfer(EOTF eotf)
	{
		switch (eotf)
		{
		case EOTF::PQ: return PL_COLOR_TRC_PQ;
		case EOTF::HLG: return PL_COLOR_TRC_HLG;
		case EOTF::HDR: return PL_COLOR_TRC_GAMMA22;
		case EOTF::SDR: return PL_COLOR_TRC_BT_1886;
		default: return PL_COLOR_TRC_UNKNOWN;
		}
	}

	void SetCiePoint(struct pl_cie_xy& point, double x, double y)
	{
		point.x = static_cast<float>(x);
		point.y = static_cast<float>(y);
	}

	struct pl_color_space TranslateColorSpace(const VideoState& state)
	{
		struct pl_color_space result{};
		result.primaries = TranslatePrimaries(state.colorspace);
		result.transfer = TranslateTransfer(state.eotf);

		// Leave HDR metadata unset when the capture source supplied only the
		// mandatory empty HDRData object. libplacebo can infer safe defaults from
		// the transfer function and primaries; zero mastering primaries/luminance
		// would instead describe an invalid mastering display.
		if (state.hdrData && state.hdrData->IsValid())
		{
			const HDRData& hdr = *state.hdrData;
			SetCiePoint(result.hdr.prim.red, hdr.displayPrimaryRedX, hdr.displayPrimaryRedY);
			SetCiePoint(result.hdr.prim.green, hdr.displayPrimaryGreenX, hdr.displayPrimaryGreenY);
			SetCiePoint(result.hdr.prim.blue, hdr.displayPrimaryBlueX, hdr.displayPrimaryBlueY);
			SetCiePoint(result.hdr.prim.white, hdr.whitePointX, hdr.whitePointY);
			result.hdr.min_luma = static_cast<float>(hdr.masteringDisplayMinLuminance);
			result.hdr.max_luma = static_cast<float>(hdr.masteringDisplayMaxLuminance);
			result.hdr.max_cll = static_cast<float>(hdr.maxCll);
			result.hdr.max_fall = static_cast<float>(hdr.maxFall);
		}

		return result;
	}

	bool IsNativeRgbUpload(VideoFrameEncoding encoding,
		VideoConversionOverride videoConversionOverride)
	{
		return videoConversionOverride ==
			VideoConversionOverride::VIDEOCONVERSION_NONE &&
			AlphaCanUseNativeRgbUpload(encoding);
	}

	std::unique_ptr<IVideoFrameFormatter> CreateAlphaFormatter(
		VideoFrameEncoding encoding,
		VideoConversionOverride videoConversionOverride)
	{
		switch (encoding)
		{
		case VideoFrameEncoding::V210:
			// v210 is 10-bit 4:2:2. When P010 has not been explicitly
			// requested, retain its vertical chroma resolution in P210 rather
			// than silently reducing it to P010's 4:2:0 representation.
			if (AlphaUsesP210Ingress(encoding, videoConversionOverride))
			{
				return std::unique_ptr<IVideoFrameFormatter>(
					new CV210toP210VideoFrameFormatter());
			}
			return std::unique_ptr<IVideoFrameFormatter>(new CV210toP010VideoFrameFormatter());
		case VideoFrameEncoding::UYVY:
		case VideoFrameEncoding::HDYC:
			// UYVY/HDYC are 8-bit 4:2:2. Keep every captured chroma row in
			// P210 unless the user explicitly selects the P010 conversion.
			if (AlphaUsesP210Ingress(encoding, videoConversionOverride))
			{
				return std::unique_ptr<IVideoFrameFormatter>(
					new CUYVYtoP210VideoFrameFormatter());
			}
			return std::unique_ptr<IVideoFrameFormatter>(
				new CUYVYtoP010VideoFrameFormatter());
		case VideoFrameEncoding::ARGB_8BIT:
		case VideoFrameEncoding::BGRA_8BIT:
			return std::unique_ptr<IVideoFrameFormatter>(new CARGBtoP010VideoFrameFormatter());
		case VideoFrameEncoding::R10b:
		case VideoFrameEncoding::R10l:
		case VideoFrameEncoding::R210:
		case VideoFrameEncoding::R12B:
		case VideoFrameEncoding::R12L:
			return std::unique_ptr<IVideoFrameFormatter>(new CDeckLinkRGBToP010VideoFrameFormatter());
		default:
			throw std::runtime_error(
				"libplacebo does not support this capture format");
		}
	}

	enum class AutoToggle
	{
		AUTO,
		ON,
		OFF
	};

	enum class ProfileUpdateMode
	{
		REBUILD,
		LIVE,
		NEVER
	};

	const char* ProfileUpdateModeName(ProfileUpdateMode mode)
	{
		switch (mode)
		{
		case ProfileUpdateMode::LIVE: return "live";
		case ProfileUpdateMode::NEVER: return "never";
		default: return "rebuild";
		}
	}

	bool ParseProfileUpdateMode(const std::string& raw, ProfileUpdateMode& mode)
	{
		const std::string normalized = ConfigFile::NormalizeName(raw);
		if (normalized == "rebuild") mode = ProfileUpdateMode::REBUILD;
		else if (normalized == "live") mode = ProfileUpdateMode::LIVE;
		else if (normalized == "never") mode = ProfileUpdateMode::NEVER;
		else return false;
		return true;
	}

	struct RefreshRateCommandRule
	{
		int minimumRate = 0;
		int maximumRate = 0;
		std::string commandLine;
	};

	struct RendererSettings
	{
		double sdrTargetNits = PL_COLOR_SDR_WHITE;
		double sdrBlackNits = PL_COLOR_SDR_WHITE / PL_COLOR_SDR_CONTRAST;
		// File-only rollout policy. Rebuild remains the compatibility default;
		// live preserves the renderer for safe changes, while never retains the
		// current program instead of requesting a new variant.
		ProfileUpdateMode profileUpdateMode = ProfileUpdateMode::REBUILD;
		bool switchRefreshRate = true;
		std::string quality = "high";
		std::string toneMapping = "auto";
		std::string gamutMapping = "auto";
		AutoToggle peakDetection = AutoToggle::AUTO;
		bool hasContrastRecovery = false;
		float contrastRecovery = 0.0f;
		std::string upscaler = "auto";
		std::string downscaler = "auto";
		std::string debandStrength = "auto";
		AutoToggle deband = AutoToggle::AUTO;
		AutoToggle sigmoid = AutoToggle::AUTO;
		AutoToggle dithering = AutoToggle::AUTO;
		std::string displayBitDepth = "auto";
		std::string outputPresentation = "auto";
		std::string outputRange = "auto";
		// Limited-range DXGI declarations are output-path policy, independent of
		// the calibrated display transfer selected by a rendering profile.
		std::string outputTransportGamma = "auto";
		std::string outputGamma = "auto";
		std::string sdrTargetPrimaries = "rec709";
		bool reportBt2020ToDisplay = false;
		std::string sdrInputTransfer = "auto";
		// Missing keys retain the historical VP behavior: honor the declared SDR
		// source transfer and color-manage it to the accepted output transfer.
		std::string sdrAdjustGamma = "on";
		bool outputDiagnostics = false;
		bool diagnosticDisableShaderCache = false;
		// Developer-only probes. These are deliberately named experiments rather
		// than a generic D3D11/DXGI flag escape hatch.
		bool diagnosticDisableCompute = false;
		bool diagnosticForce8BitSdrSwapchain = false;
		bool diagnosticAllowLimitedG22 = false;
		bool diagnosticAllowFullG22 = false;
		bool diagnosticVpOwnedDxgiPresenter = false;
		double configuredScreenAspect = 1.0;
		bool configuredScreenTarget = false;
		std::string verticalAlignment = "center";
		double anamorphicScale = 1.0;
		bool automaticSourceCrop = false;
		bool scopeSubtitleFit = false;
		uint64_t scopeSubtitleHoldMs = 2000;
		uint64_t scopeSubtitleEngageDriftMs = 0;
		uint64_t scopeSubtitleReleaseDriftMs = 0;
		int scopeSubtitlePaddingPixels = 20;
		int scopeSubtitleTargetBufferPixels =
			RendererProfileConfig::DEFAULT_SUBTITLE_TARGET_BUFFER_PIXELS;
		uint64_t refreshRateCommandDelayMs = 5000;
		std::vector<RefreshRateCommandRule> refreshRateCommandRules;
		std::string lutPath;
		bool lutPathRejected = false;
		std::string lutConstrainedBaseDirectory;
		std::string lutReferencePrimaries = "auto";
		std::string lutReferenceTransfer = "auto";
		std::string lutReferenceRange = "auto";
		double lutReferenceNits = 0.0;
	};

	AlphaSourceCrop::VerticalPictureAlignment ResolveVerticalPictureAlignment(
		const std::string& alignment)
	{
		if (alignment == "top")
			return AlphaSourceCrop::VerticalPictureAlignment::TOP;
		if (alignment == "bottom")
			return AlphaSourceCrop::VerticalPictureAlignment::BOTTOM;
		return AlphaSourceCrop::VerticalPictureAlignment::CENTER;
	}

	std::string EffectiveSettingsFingerprint(
		const RendererSettings& settings,
		bool includeViewportSettings = true)
	{
		// Stable, locale-independent representation of every renderer-affecting
		// value. Profile labels deliberately do not participate.
		std::ostringstream stream;
		stream.imbue(std::locale::classic());
		stream.precision(17);
		stream
			<< settings.sdrTargetNits << '|' << settings.sdrBlackNits << '|'
			<< settings.switchRefreshRate << '|' << settings.quality << '|'
			<< settings.toneMapping << '|' << settings.gamutMapping << '|'
			<< static_cast<int>(settings.peakDetection) << '|'
			<< settings.hasContrastRecovery << '|' << settings.contrastRecovery << '|'
			<< settings.upscaler << '|' << settings.downscaler << '|'
			<< settings.debandStrength << '|'
			<< static_cast<int>(settings.deband) << '|'
			<< static_cast<int>(settings.sigmoid) << '|'
			<< static_cast<int>(settings.dithering) << '|'
			<< settings.displayBitDepth << '|'
			<< settings.outputPresentation << '|' << settings.outputRange << '|'
			<< settings.outputTransportGamma << '|' << settings.outputGamma << '|'
			<< settings.sdrTargetPrimaries << '|'
			<< settings.reportBt2020ToDisplay << '|' << settings.sdrInputTransfer << '|'
			<< settings.sdrAdjustGamma << '|'
			<< settings.outputDiagnostics << '|' << settings.diagnosticDisableShaderCache << '|'
			<< settings.diagnosticDisableCompute << '|'
			<< settings.diagnosticForce8BitSdrSwapchain << '|'
			<< settings.diagnosticAllowLimitedG22 << '|'
			<< settings.diagnosticAllowFullG22 << '|'
			<< settings.diagnosticVpOwnedDxgiPresenter << '|';
		if (includeViewportSettings)
		{
			stream
				<< settings.configuredScreenAspect << '|' << settings.configuredScreenTarget << '|'
				<< settings.verticalAlignment << '|'
				<< settings.anamorphicScale << '|'
				<< settings.automaticSourceCrop << '|'
				<< settings.scopeSubtitleFit << '|' << settings.scopeSubtitleHoldMs << '|'
				<< settings.scopeSubtitleEngageDriftMs << '|'
				<< settings.scopeSubtitleReleaseDriftMs << '|'
				<< settings.scopeSubtitlePaddingPixels << '|'
				<< settings.scopeSubtitleTargetBufferPixels << '|';
		}
		stream
			<< settings.lutPath << '|'
			<< settings.lutPathRejected << '|' << settings.lutReferencePrimaries << '|'
			<< settings.lutReferenceTransfer << '|' << settings.lutReferenceRange << '|'
			<< settings.lutReferenceNits;
		return stream.str();
	}

	struct DisplayRule
	{
		std::string name;
		std::string section;
		int priority = 0;
		int specificity = 0;
	};
	void ApplyDisplayRuleOverrides(const ConfigFile& config, const DisplayRule& rule,
		RendererSettings& settings);

	bool ParseRefreshRateRuleKey(const std::string& key, int& minimumRate, int& maximumRate);

	bool LookupUnifiedSourceValue(const VideoState& state,
		const std::string& variable, std::string& value)
	{
		return StateVariables::LookupVideoState(state, variable, value);
	}

	bool MatchesDisplayRule(const std::string& expression, const VideoState& state, int& specificity)
	{
		std::string parseError;
		return DisplayRuleExpression::Matches(expression,
			[&state](const std::string& variable, std::string& value)
			{
				if (variable == "eotf" || variable == "transfer") value = CStringA(ToString(state.eotf)).GetString();
				else if (variable == "colorspace") value = CStringA(ToString(state.colorspace)).GetString();
				else if (variable == "primaries") value = CStringA(ToString(state.colorspace)).GetString();
				else if (variable == "format") value = CStringA(ToString(state.videoFrameEncoding)).GetString();
				else if (variable == "key") value = "NONE";
				else if (variable == "range" || variable == "scan") return false; // Reserved vocabulary until the capture API exposes it.
				else if (variable == "hdr_metadata") value = state.hdrData && state.hdrData->IsValid() ? "true" : "false";
				else if (variable == "interlaced") value = state.displayMode && state.displayMode->IsInterlaced() ? "true" : "false";
				else if (!state.displayMode) return false;
				else if (variable == "source_rate") value = std::to_string(static_cast<int>(std::floor(state.displayMode->RefreshRateHz())));
				else if (variable == "width") value = std::to_string(state.displayMode->FrameWidth());
				else if (variable == "height") value = std::to_string(state.displayMode->FrameHeight());
				else if (variable == "resolution") value = std::to_string(state.displayMode->FrameWidth()) + "x" + std::to_string(state.displayMode->FrameHeight());
				else return false;
				return true;
			}, specificity, parseError);
	}

	DisplayRule SelectDisplayRule(const ConfigFile& config, const VideoState& state)
	{
		DisplayRule selected;
		std::string ruleNames;
		if (!config.TryGetString("display_rules", "rules", ruleNames))
			return selected;
		std::istringstream names(ruleNames);
		std::string name;
		while (std::getline(names, name, ','))
		{
			name = ConfigFile::NormalizeName(name);
			if (name.empty()) continue;
			const std::string section = "display_rules." + name;
			std::string expression;
			if (!config.TryGetString(section, "rule", expression))
				continue;
			int specificity = 0;
			if (!MatchesDisplayRule(expression, state, specificity))
				continue;
			int priority = 0;
			std::string rawPriority;
			if (config.TryGetString(section, "priority", rawPriority))
			{
				try { priority = std::stoi(ConfigFile::Trim(rawPriority)); }
				catch (const std::exception&) { DebugLog::Log("display: invalid priority '%s' for rule '%s'", rawPriority.c_str(), name.c_str()); }
			}
			if (selected.name.empty() || priority > selected.priority ||
				(priority == selected.priority && specificity > selected.specificity))
				selected = { name, section, priority, specificity };
		}
		return selected;
	}

	DisplayRule FindDisplayRule(const ConfigFile& config, const std::string& requestedName)
	{
		const std::string name = ConfigFile::NormalizeName(requestedName);
		std::string ruleNames;
		if (name.empty() || !config.TryGetString("display_rules", "rules", ruleNames))
			return DisplayRule();

		std::istringstream names(ruleNames);
		std::string configuredName;
		while (std::getline(names, configuredName, ','))
		{
			if (ConfigFile::NormalizeName(configuredName) != name)
				continue;
			const std::string section = "display_rules." + name;
			std::string expression;
			if (!config.TryGetString(section, "rule", expression))
				return DisplayRule();
			return { name, section, 0, 0 };
		}
		return DisplayRule();
	}

	std::vector<std::string> SplitConfiguredList(const std::string& text)
	{
		std::vector<std::string> result;
		std::istringstream stream(text);
		std::string item;
		while (std::getline(stream, item, ','))
		{
			item = ConfigFile::NormalizeName(item);
			if (!item.empty()) result.push_back(item);
		}
		return result;
	}

	// Proposed profile groups intentionally use the same expression evaluator and
	// override machinery as legacy display rules.  A profile selected by a shortcut
	// is addressed as group/profile, while automatic profiles are independently
	// selected for every declared group.
	DisplayRule FindProfile(const ConfigFile& config, const std::string& name)
	{
		const size_t slash = name.find('/');
		if (slash == std::string::npos) return {};
		const std::string group = ConfigFile::NormalizeName(name.substr(0, slash));
		const std::string profile = ConfigFile::NormalizeName(name.substr(slash + 1));
		const std::string section = "profiles." + group + "." + profile;
		return group.empty() || profile.empty() || !config.HasSection(section) ?
			DisplayRule() : DisplayRule{ group + "/" + profile, section, 0, 0 };
	}

	void ApplyAutomaticProfiles(const ConfigFile& config, const VideoState& state,
		const std::map<std::string, std::string>& manualProfiles,
		RendererSettings& settings, std::string& activeProfiles)
	{
		RendererProfileConfig::Model model;
		std::string modelError;
		if (RendererProfileConfig::IsUnified(config))
		{
			if (!RendererProfileConfig::Read(config, model, modelError))
			{
				DebugLog::Log("unified renderer configuration ignored: %s", modelError.c_str());
				return;
			}
			std::vector<RendererProfileConfig::AutomaticSelection> selections;
			if (!RendererProfileConfig::SelectAutomatic(model,
				[&state](const std::string& variable, std::string& value)
					{ return LookupUnifiedSourceValue(state, variable, value); },
				selections, modelError))
			{
				DebugLog::Log("unified renderer profile selection failed: %s", modelError.c_str());
				return;
			}
			std::map<std::string, RendererProfileConfig::AutomaticSelection> selectedByGroup;
			for (const RendererProfileConfig::AutomaticSelection& selection : selections)
				selectedByGroup[selection.group] = selection;
			for (const RendererProfileConfig::Group& group : model.groups)
			{
				std::string profileName;
				bool manual = false;
				const auto manualSelection = manualProfiles.find(group.name);
				if (manualSelection != manualProfiles.end())
				{
					profileName = manualSelection->second;
					manual = true;
				}
				else
				{
					const auto automaticSelection = selectedByGroup.find(group.name);
					if (automaticSelection == selectedByGroup.end())
						continue;
					profileName = automaticSelection->second.profile;
				}
				const auto profile = model.profiles.find(group.name + "." + profileName);
				if (profile == model.profiles.end()) continue;
				std::string section = "profiles." + group.name + "." + profileName;
				if (RendererProfileConfig::IsTargetModel(config))
				{
					const std::string root = group.name == "display" ?
						"vprenderer" :
						(group.name == "input" ? "vprenderer.input" :
							(group.name == "scaling" ? "vprenderer.scaling" :
								(group.name == "output" ? "vprenderer.output" :
									(group.name == "viewport" ? "vprenderer.viewport" :
										group.name))));
					if (!config.HasSection(root) &&
						group.defaultSelection != "base")
					{
						const DisplayRule baselineRule = {
							group.name + "/" + group.defaultSelection,
							root + "." + group.defaultSelection, 0, 0 };
						ApplyDisplayRuleOverrides(config, baselineRule, settings);
					}
					section = profileName == "base" ? root : root + "." + profileName;
				}
				const DisplayRule rule = { group.name + "/" + profileName,
					section, profile->second.priority, 0 };
				ApplyDisplayRuleOverrides(config, rule, settings);
				if (!activeProfiles.empty()) activeProfiles += ", ";
				activeProfiles += rule.name;
				if (manual) activeProfiles += " (manual)";
			}
			return;
		}

		std::string groups;
		if (!config.TryGetString("profile_groups", "groups", groups)) return;
		for (const std::string& group : SplitConfiguredList(groups))
		{
			DisplayRule selected;
			const std::string prefix = "profiles." + group + ".";
			for (const std::string& section : config.GetSectionNames())
			{
				if (section.rfind(prefix, 0) != 0) continue;
				const DisplayRule candidate = FindProfile(config, group + "/" + section.substr(prefix.size()));
				std::string when;
				if (candidate.name.empty() || !config.TryGetString(candidate.section, "when", when)) continue;
				int specificity = 0;
				if (!MatchesDisplayRule(when, state, specificity)) continue;
				int priority = 0; std::string rawPriority;
				if (config.TryGetString(candidate.section, "priority", rawPriority))
					try { priority = std::stoi(ConfigFile::Trim(rawPriority)); } catch (...) {}
				if (selected.name.empty() || priority > selected.priority ||
					(priority == selected.priority && specificity > selected.specificity))
					selected = { candidate.name, candidate.section, priority, specificity };
			}
			if (!selected.name.empty())
			{
				ApplyDisplayRuleOverrides(config, selected, settings);
				if (!activeProfiles.empty()) activeProfiles += ", ";
				activeProfiles += selected.name;
			}
		}
	}

	std::string ResolveDisplayRuleName(const VideoState& state)
	{
		ConfigFile config;
		if (!config.Load(ConfigFile::RENDERER_FILENAME))
			return "";
		return SelectDisplayRule(config, state).name;
	}

	bool ParseDouble(const std::string& value, double& parsed)
	{
		try
		{
			size_t consumed = 0;
			parsed = std::stod(ConfigFile::Trim(value), &consumed);
			return consumed == ConfigFile::Trim(value).size() && std::isfinite(parsed);
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	bool ParseInteger(const std::string& value, int minimum, int maximum,
		int& parsed)
	{
		try
		{
			const std::string trimmed = ConfigFile::Trim(value);
			size_t consumed = 0;
			const int candidate = std::stoi(trimmed, &consumed);
			if (consumed != trimmed.size() || candidate < minimum ||
				candidate > maximum)
				return false;
			parsed = candidate;
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	bool ParseAspectRatio(const std::string& value, double& parsed)
	{
		AspectRatio aspect;
		std::string error;
		if (!AspectRatioParser::Parse(
			value, 1.0, 4.0, aspect, error))
			return false;
		parsed = aspect.value;
		return true;
	}

	bool ParseRefreshRateRuleKey(
		const std::string& key,
		int& minimumRate,
		int& maximumRate)
	{
		const std::string trimmed = ConfigFile::Trim(key);
		const size_t separator = trimmed.find('-');
		try
		{
			size_t consumed = 0;
			minimumRate = std::stoi(trimmed.substr(0, separator), &consumed);
			if (consumed != (separator == std::string::npos
				? trimmed.size()
				: separator))
			{
				return false;
			}

			maximumRate = minimumRate;
			if (separator != std::string::npos)
			{
				const std::string maximumText = trimmed.substr(separator + 1);
				consumed = 0;
				maximumRate = std::stoi(maximumText, &consumed);
				if (consumed != maximumText.size())
					return false;
			}
		}
		catch (const std::exception&)
		{
			return false;
		}

		return minimumRate >= 1 && maximumRate >= minimumRate && maximumRate <= 1000;
	}

	bool TryGetDisplayString(
		const ConfigFile& config,
		const char* key,
		std::string& value)
	{
		return RendererConfigView(config).TryGetDisplayString(key, value);
	}

	bool TryGetDisplayBool(
		const ConfigFile& config,
		const char* key,
		bool& value)
	{
		return RendererConfigView(config).TryGetDisplayBool(key, value);
	}

	bool IsAbsolutePath(const std::string& path)
	{
		return (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
			path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
			(path.size() >= 2 && path[0] == '\\' && path[1] == '\\');
	}

	std::string CanonicalFullPath(const std::string& path)
	{
		const DWORD required = GetFullPathNameA(path.c_str(), 0, nullptr, nullptr);
		if (required == 0)
			return std::string();
		std::vector<char> buffer(static_cast<size_t>(required));
		if (GetFullPathNameA(path.c_str(), required, buffer.data(), nullptr) == 0)
			return std::string();
		return std::string(buffer.data());
	}

	std::string NormalizePathForComparison(std::string value)
	{
		std::replace(value.begin(), value.end(), '/', '\\');
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	bool IsPathWithin(const std::string& base, const std::string& candidate)
	{
		std::string basePrefix = NormalizePathForComparison(base);
		if (basePrefix.empty())
			return false;
		if (basePrefix.back() != '\\')
			basePrefix.push_back('\\');
		const std::string normalizedCandidate =
			NormalizePathForComparison(candidate);
		return normalizedCandidate.compare(
			0, basePrefix.size(), basePrefix) == 0;
	}

	std::string ResolveConfigRelativePath(
		const ConfigFile& config,
		const std::string& path,
		bool* rejected = nullptr,
		std::string* constrainedBaseDirectory = nullptr)
	{
		if (rejected)
			*rejected = false;
		if (constrainedBaseDirectory)
			constrainedBaseDirectory->clear();
		const std::string trimmed = ConfigFile::Trim(path);
		if (trimmed.empty() || IsAbsolutePath(trimmed) || config.GetLoadedPath().empty())
			return trimmed;

		const size_t separator = config.GetLoadedPath().find_last_of("\\/");
		if (separator == std::string::npos)
			return trimmed;

		const std::string base =
			CanonicalFullPath(config.GetLoadedPath().substr(0, separator + 1));
		const std::string candidate = CanonicalFullPath(
			config.GetLoadedPath().substr(0, separator + 1) + trimmed);
		if (base.empty() || candidate.empty())
		{
			if (rejected)
				*rejected = true;
			return std::string();
		}

		if (!IsPathWithin(base, candidate))
		{
			DebugLog::Log(
				"display: relative LUT path escapes the configuration directory and was rejected: %s",
				trimmed.c_str());
			if (rejected)
				*rejected = true;
			return std::string();
		}

		// Reparse-point containment is verified by the loader against the same
		// handle it reads, which avoids a validate-then-reopen race.
		if (constrainedBaseDirectory)
			*constrainedBaseDirectory = base;
		return candidate;
	}

	std::string ReadChoice(
		const ConfigFile& config,
		const char* key,
		const char* defaultValue,
		std::initializer_list<const char*> choices)
	{
		std::string rawValue;
		if (!TryGetDisplayString(config, key, rawValue))
			return defaultValue;

		const std::string value = ConfigFile::NormalizeName(rawValue);
		for (const char* choice : choices)
		{
			if (value == choice)
				return value;
		}

		DebugLog::Log(
			"libplacebo: invalid %s value '%s'; using %s",
			key,
			rawValue.c_str(),
			defaultValue);
		return defaultValue;
	}

	AutoToggle ReadAutoToggle(
		const ConfigFile& config,
		const char* key,
		AutoToggle defaultValue = AutoToggle::AUTO)
	{
		std::string rawValue;
		if (!TryGetDisplayString(config, key, rawValue))
			return defaultValue;
		if (ConfigFile::NormalizeName(rawValue) == "auto")
			return AutoToggle::AUTO;

		bool enabled = false;
		if (TryGetDisplayBool(config, key, enabled))
			return enabled ? AutoToggle::ON : AutoToggle::OFF;

		DebugLog::Log(
			"libplacebo: invalid %s value '%s'; using AUTO",
			key,
			rawValue.c_str());
		return defaultValue;
	}

	bool ApplyDebandStrength(const std::string& rawValue,
		RendererSettings& settings)
	{
		const std::string value = ConfigFile::NormalizeName(rawValue);
		if (value == "auto")
		{
			settings.debandStrength = value;
			settings.deband = AutoToggle::AUTO;
			return true;
		}
		if (value == "off")
		{
			settings.debandStrength = value;
			settings.deband = AutoToggle::OFF;
			return true;
		}
		if (value == "light" || value == "default")
		{
			settings.debandStrength = value;
			settings.deband = AutoToggle::ON;
			return true;
		}
		return false;
	}

	void NormalizeSdrBlackLevel(RendererSettings& settings)
	{
		if (std::isfinite(settings.sdrBlackNits) && settings.sdrBlackNits >= 0.0 &&
			settings.sdrBlackNits < settings.sdrTargetNits)
		{
			return;
		}
		settings.sdrBlackNits = settings.sdrTargetNits / PL_COLOR_SDR_CONTRAST;
		DebugLog::Log(
			"libplacebo: resolved sdr_black_nits conflicts with sdr_target_nits; using Auto (%.3f)",
			settings.sdrBlackNits);
	}

	void ApplyDisplayRuleOverrides(
		const ConfigFile& config,
		const DisplayRule& rule,
		RendererSettings& settings)
	{
		if (rule.name.empty())
			return;

		auto readChoice = [&config, &rule](const char* key, std::string& target,
			std::initializer_list<const char*> choices)
		{
			std::string raw;
			if (!config.TryGetString(rule.section, key, raw)) return;
			const std::string value = ConfigFile::NormalizeName(raw);
			for (const char* choice : choices)
				if (value == choice) { target = value; return; }
			DebugLog::Log("display rule '%s': invalid %s value '%s'; retaining base setting", rule.name.c_str(), key, raw.c_str());
		};
		auto readToggle = [&config, &rule](const char* key, AutoToggle& target)
		{
			std::string raw;
			if (!config.TryGetString(rule.section, key, raw)) return;
			if (ConfigFile::NormalizeName(raw) == "auto") { target = AutoToggle::AUTO; return; }
			bool enabled = false;
			if (config.TryGetBool(rule.section, key, enabled)) { target = enabled ? AutoToggle::ON : AutoToggle::OFF; return; }
			DebugLog::Log("display rule '%s': invalid %s value '%s'; retaining base setting", rule.name.c_str(), key, raw.c_str());
		};
		std::string raw;
		if (config.TryGetString(rule.section, "sdr_target_nits", raw))
		{
			double value = 0.0;
			if (ParseDouble(raw, value) && value >= 40.0 && value <= 500.0) settings.sdrTargetNits = value;
		}
		// Rules inherit the base black level unless they explicitly override it.
		// Resetting this unconditionally turned a base sdr_black_nits=0 back into
		// AUTO whenever the ordinary SDR rule was selected.
		if (config.TryGetString(rule.section, "sdr_black_nits", raw))
		{
			if (ConfigFile::NormalizeName(raw) == "auto")
			{
				settings.sdrBlackNits =
					settings.sdrTargetNits / PL_COLOR_SDR_CONTRAST;
			}
			else
			{
				double value = 0.0;
				if (ParseDouble(raw, value) && value >= 0.0 && value < settings.sdrTargetNits)
					settings.sdrBlackNits = value;
			}
		}
		if (config.TryGetString(rule.section, "switch_refresh_rate", raw))
		{
			bool value = false;
			if (config.TryGetBool(rule.section, "switch_refresh_rate", value)) settings.switchRefreshRate = value;
		}
		readChoice("quality", settings.quality, { "fast", "balanced", "high" });
		readChoice("tone_mapping", settings.toneMapping, { "auto", "spline", "bt2390", "st2094-40", "reinhard" });
		readChoice("gamut_mapping", settings.gamutMapping, { "auto", "perceptual", "softclip", "relative", "desaturate" });
		readToggle("peak_detection", settings.peakDetection);
		if (config.TryGetString(rule.section, "peak_detection", raw))
		{
			const std::string named = ConfigFile::NormalizeName(raw);
			if (named == "default") settings.peakDetection = AutoToggle::AUTO;
			else if (named == "high_quality") settings.peakDetection = AutoToggle::ON;
		}
		readChoice("upscaler", settings.upscaler, { "auto", "ewa_lanczossharp", "ewa_lanczos", "bicubic", "bilinear" });
		readChoice("downscaler", settings.downscaler, { "auto", "ewa_lanczos", "bicubic", "bilinear" });
		readToggle("deband", settings.deband);
		if (config.TryGetString(rule.section, "deband_strength", raw))
		{
			if (!ApplyDebandStrength(raw, settings))
				DebugLog::Log("display rule '%s': invalid deband_strength value '%s'; retaining base setting",
					rule.name.c_str(), raw.c_str());
		}
		readToggle("sigmoid", settings.sigmoid);
		readToggle("dithering", settings.dithering);
		readChoice("display_bit_depth", settings.displayBitDepth,
			{ "auto", "8", "10" });
		readChoice("output_presentation", settings.outputPresentation,
			{ "auto", "composed", "direct" });
		readChoice("output_range", settings.outputRange, { "auto", "full", "limited" });
		readChoice("output_transport_gamma", settings.outputTransportGamma,
			{ "auto", "2.2", "2.4" });
		readChoice("output_gamma", settings.outputGamma, { "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
		// Output transport and experiments live in their own selected profile.
		// Leaving these booleans out meant that the UI
		// persisted them but the renderer silently used its false defaults.  In
		// particular, Proposed never activated its VP-owned presenter or its
		// Limited/G22 diagnostic path.
		auto readBoolean = [&config, &rule](const char* key, bool& target)
		{
			std::string raw;
			if (!config.TryGetString(rule.section, key, raw)) return;
			bool value = false;
			if (config.TryGetBool(rule.section, key, value))
			{
				target = value;
				return;
			}
			DebugLog::Log("display rule '%s': invalid %s value '%s'; retaining base setting",
				rule.name.c_str(), key, raw.c_str());
		};
		bool legacyLiveProfileUpdates =
			settings.profileUpdateMode == ProfileUpdateMode::LIVE;
		readBoolean("live_profile_updates", legacyLiveProfileUpdates);
		settings.profileUpdateMode = legacyLiveProfileUpdates ?
			ProfileUpdateMode::LIVE : ProfileUpdateMode::REBUILD;
		readBoolean("output_diagnostics", settings.outputDiagnostics);
		readBoolean("diagnostic_disable_shader_cache",
			settings.diagnosticDisableShaderCache);
		readBoolean("diagnostic_disable_compute", settings.diagnosticDisableCompute);
		readBoolean("diagnostic_force_8bit_sdr_swapchain",
			settings.diagnosticForce8BitSdrSwapchain);
		readBoolean("diagnostic_allow_limited_g22",
			settings.diagnosticAllowLimitedG22);
		readBoolean("diagnostic_allow_full_g22",
			settings.diagnosticAllowFullG22);
		readBoolean("diagnostic_vp_owned_dxgi_presenter",
			settings.diagnosticVpOwnedDxgiPresenter);
		readChoice("sdr_target_primaries", settings.sdrTargetPrimaries, { "rec709", "bt2020" });
		if (!config.TryGetBool(rule.section, "report_bt2020_to_display",
			settings.reportBt2020ToDisplay) &&
			config.TryGetString(rule.section, "report_bt2020_to_display", raw))
		{
			DebugLog::Log("display rule '%s': invalid report_bt2020_to_display value '%s'; retaining base setting", rule.name.c_str(), raw.c_str());
		}
		readChoice("sdr_input_transfer", settings.sdrInputTransfer, { "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
		readChoice("sdr_adjust_gamma", settings.sdrAdjustGamma,
			{ "auto", "on", "off" });
		if (config.TryGetString(rule.section, "contrast_recovery", raw))
		{
			settings.hasContrastRecovery = false;
			if (ConfigFile::NormalizeName(raw) != "auto")
			{
				double value = 0.0;
				if (ParseDouble(raw, value) && value >= 0.0 && value <= 1.0)
				{
					settings.hasContrastRecovery = true;
					settings.contrastRecovery = static_cast<float>(value);
				}
			}
		}
		if (config.TryGetString(rule.section, "lut", raw))
			settings.lutPath = ResolveConfigRelativePath(
				config, raw, &settings.lutPathRejected,
				&settings.lutConstrainedBaseDirectory);
		readChoice("lut_reference_primaries", settings.lutReferencePrimaries,
			{ "auto", "rec709", "p3_d65", "bt2020" });
		readChoice("lut_reference_transfer", settings.lutReferenceTransfer,
			{ "auto", "srgb", "bt1886", "2.2", "2.4" });
		readChoice("lut_reference_range", settings.lutReferenceRange,
			{ "auto", "full", "limited" });
		if (config.TryGetString(rule.section, "lut_reference_nits", raw))
		{
			if (ConfigFile::NormalizeName(raw) == "auto")
			{
				settings.lutReferenceNits = 0.0;
			}
			else
			{
				double value = 0.0;
				if (ParseDouble(raw, value) && value >= 40.0 && value <= 500.0)
					settings.lutReferenceNits = value;
				else
					DebugLog::Log(
						"display rule '%s': invalid lut_reference_nits '%s'; retaining base setting",
						rule.name.c_str(),
						raw.c_str());
			}
		}
		const auto readViewportString = [&](const char* genericKey,
			std::string& value)
		{
			return config.TryGetString(rule.section, genericKey, value);
		};
		if (readViewportString("screen_aspect", raw))
		{
			double value = 0.0;
			if (ParseAspectRatio(raw, value) && value >= 1.0 && value <= 4.0)
			{
				settings.configuredScreenAspect = value;
				settings.configuredScreenTarget = true;
			}
		}
		if (readViewportString("vertical_alignment", raw))
		{
			const std::string alignment = ConfigFile::NormalizeName(raw);
			if (alignment == "top" || alignment == "center" ||
				alignment == "bottom")
				settings.verticalAlignment = alignment;
		}
		if (config.TryGetString(rule.section, "anamorphic_scale", raw))
		{
			double value = 0.0;
			if (ParseAspectRatio(raw, value) && value >= 0.5 && value <= 2.0)
				settings.anamorphicScale = value;
			else
				DebugLog::Log("profile '%s': invalid anamorphic_scale '%s'",
					rule.name.c_str(), raw.c_str());
		}
		const auto readViewportBool = [&](const char* genericKey, bool& value)
		{
			return config.TryGetBool(rule.section, genericKey, value);
		};
		if (!readViewportBool("automatic_crop",
			settings.automaticSourceCrop) &&
			readViewportString("automatic_crop", raw))
			DebugLog::Log("profile '%s': invalid automatic_crop '%s'",
				rule.name.c_str(), raw.c_str());
		if (!readViewportBool("subtitle_fit", settings.scopeSubtitleFit) &&
			readViewportString("subtitle_fit", raw))
			DebugLog::Log("profile '%s': invalid subtitle_fit '%s'",
				rule.name.c_str(), raw.c_str());
		if (readViewportString("subtitle_hold_seconds", raw))
		{
			double seconds = 0.0;
			if (ParseDouble(raw, seconds) &&
				seconds >= RendererProfileConfig::MIN_SUBTITLE_HOLD_SECONDS &&
				seconds <= RendererProfileConfig::MAX_SUBTITLE_HOLD_SECONDS)
				settings.scopeSubtitleHoldMs = static_cast<uint64_t>(std::llround(seconds * 1000.0));
		}
		if (readViewportString("subtitle_engage_drift_ms", raw))
		{
			int milliseconds = 0;
			if (ParseInteger(raw, 0, 30000, milliseconds))
				settings.scopeSubtitleEngageDriftMs =
					static_cast<uint64_t>(milliseconds);
		}
		if (readViewportString("subtitle_release_drift_ms", raw))
		{
			int milliseconds = 0;
			if (ParseInteger(raw, 0, 30000, milliseconds))
				settings.scopeSubtitleReleaseDriftMs =
					static_cast<uint64_t>(milliseconds);
		}
		if (readViewportString("subtitle_padding_pixels", raw))
		{
			double pixels = 0.0;
			if (ParseDouble(raw, pixels) && pixels >= 0.0 && pixels <= 500.0)
				settings.scopeSubtitlePaddingPixels = static_cast<int>(std::llround(pixels));
		}
		if (readViewportString("subtitle_target_buffer_pixels", raw))
		{
			double pixels = 0.0;
			if (ParseDouble(raw, pixels) && pixels >= 0.0 &&
				pixels <= RendererProfileConfig::MAX_SUBTITLE_TARGET_BUFFER_PIXELS)
			{
				settings.scopeSubtitleTargetBufferPixels =
					static_cast<int>(std::llround(pixels));
			}
		}
	}

	RendererSettings LoadRendererSettings(const VideoState& state, std::string& activeRule,
		const std::string& manualRule = "",
		const std::map<std::string, std::string>& manualUnifiedProfiles = {})
	{
		RendererSettings settings;
		activeRule.clear();
		ConfigFile config;
		if (!config.Load(ConfigFile::RENDERER_FILENAME))
			return settings;
		DebugLog::Log(
			"libplacebo configuration loaded from %s",
			config.GetLoadedPath().c_str());
		for (const std::string& warning : config.GetWarnings())
			DebugLog::Log(
				"libplacebo configuration warning: %s",
				warning.c_str());

		std::string rawValue;
		bool legacyLiveProfileUpdates = false;
		if (TryGetDisplayString(config, "live_profile_updates", rawValue) &&
			!TryGetDisplayBool(config, "live_profile_updates",
				legacyLiveProfileUpdates))
		{
			DebugLog::Log(
				"libplacebo: invalid live_profile_updates value '%s'; using false",
				rawValue.c_str());
			legacyLiveProfileUpdates = false;
		}
		settings.profileUpdateMode = legacyLiveProfileUpdates ?
			ProfileUpdateMode::LIVE : ProfileUpdateMode::REBUILD;
		if (TryGetDisplayString(config, "sdr_target_nits", rawValue))
		{
			double parsed = 0.0;
			if (ParseDouble(rawValue, parsed) && parsed >= 40.0 && parsed <= 500.0)
				settings.sdrTargetNits = parsed;
			else
				DebugLog::Log(
					"libplacebo: sdr_target_nits must be between 40 and 500; using %.0f",
					PL_COLOR_SDR_WHITE);
		}

		settings.sdrBlackNits = settings.sdrTargetNits / PL_COLOR_SDR_CONTRAST;
		if (TryGetDisplayString(config, "sdr_black_nits", rawValue) &&
			ConfigFile::NormalizeName(rawValue) != "auto")
		{
			double parsed = 0.0;
			if (ParseDouble(rawValue, parsed) && parsed >= 0.0 &&
				parsed < settings.sdrTargetNits)
			{
				settings.sdrBlackNits = parsed;
			}
			else
			{
				DebugLog::Log(
					"libplacebo: sdr_black_nits must be non-negative and below sdr_target_nits; using AUTO (%.3f)",
					settings.sdrBlackNits);
			}
		}

		if (TryGetDisplayString(config, "switch_refresh_rate", rawValue) &&
			!TryGetDisplayBool(config, "switch_refresh_rate", settings.switchRefreshRate))
		{
			DebugLog::Log(
				"libplacebo: invalid switch_refresh_rate value '%s'; using true",
				rawValue.c_str());
			settings.switchRefreshRate = true;
		}

		settings.quality = ReadChoice(
			config, "quality", "high", { "fast", "balanced", "high" });
		settings.toneMapping = ReadChoice(
			config, "tone_mapping", "auto",
			{ "auto", "spline", "bt2390", "st2094-40", "reinhard" });
		settings.gamutMapping = ReadChoice(
			config, "gamut_mapping", "auto",
			{ "auto", "perceptual", "softclip", "relative", "desaturate" });
		settings.peakDetection = ReadAutoToggle(config, "peak_detection");
		if (TryGetDisplayString(config, "peak_detection", rawValue))
		{
			const std::string named = ConfigFile::NormalizeName(rawValue);
			if (named == "default")
				settings.peakDetection = AutoToggle::AUTO;
			else if (named == "high_quality")
				settings.peakDetection = AutoToggle::ON;
		}
		settings.upscaler = ReadChoice(
			config, "upscaler", "auto",
			{ "auto", "ewa_lanczossharp", "ewa_lanczos", "bicubic", "bilinear" });
		settings.downscaler = ReadChoice(
			config, "downscaler", "auto",
			{ "auto", "ewa_lanczos", "bicubic", "bilinear" });
		settings.deband = ReadAutoToggle(config, "deband");
		if (TryGetDisplayString(config, "deband_strength", rawValue))
		{
			if (!ApplyDebandStrength(rawValue, settings))
				DebugLog::Log(
					"libplacebo: deband_strength must be Auto, off, light, or default; retaining the debanding setting");
		}
		settings.sigmoid = ReadAutoToggle(config, "sigmoid");
		settings.dithering = ReadAutoToggle(config, "dithering");
		settings.displayBitDepth = ReadChoice(
			config, "display_bit_depth", "auto", { "auto", "8", "10" });
		settings.outputPresentation = ReadChoice(
			config, "output_presentation", "auto",
			{ "auto", "composed", "direct" });
		settings.outputRange = ReadChoice(
			config, "output_range", "auto", { "auto", "full", "limited" });
		settings.outputTransportGamma = ReadChoice(
			config, "output_transport_gamma", "auto",
			{ "auto", "2.2", "2.4" });
		settings.outputGamma = ReadChoice(
			config, "output_gamma", "auto",
			{ "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
		settings.sdrTargetPrimaries = ReadChoice(
			config, "sdr_target_primaries", "rec709", { "rec709", "bt2020" });
		if (TryGetDisplayString(config, "report_bt2020_to_display", rawValue) &&
			!TryGetDisplayBool(config, "report_bt2020_to_display", settings.reportBt2020ToDisplay))
		{
			DebugLog::Log("libplacebo: invalid report_bt2020_to_display value '%s'; using false", rawValue.c_str());
			settings.reportBt2020ToDisplay = false;
		}
		settings.sdrInputTransfer = ReadChoice(
			config, "sdr_input_transfer", "auto",
			{ "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
		settings.sdrAdjustGamma = ReadChoice(
			config, "sdr_adjust_gamma", "on",
			{ "auto", "on", "off" });
		if (TryGetDisplayString(config, "output_diagnostics", rawValue) &&
			!TryGetDisplayBool(config, "output_diagnostics", settings.outputDiagnostics))
		{
			DebugLog::Log(
				"libplacebo: invalid output_diagnostics value '%s'; using false",
				rawValue.c_str());
			settings.outputDiagnostics = false;
		}
		if (TryGetDisplayString(config, "diagnostic_disable_shader_cache", rawValue) &&
			!TryGetDisplayBool(
				config,
				"diagnostic_disable_shader_cache",
				settings.diagnosticDisableShaderCache))
		{
			DebugLog::Log(
				"libplacebo: invalid diagnostic_disable_shader_cache value '%s'; using false",
				rawValue.c_str());
			settings.diagnosticDisableShaderCache = false;
		}
		if (TryGetDisplayString(config, "diagnostic_disable_compute", rawValue) &&
			!TryGetDisplayBool(config, "diagnostic_disable_compute",
				settings.diagnosticDisableCompute))
		{
			DebugLog::Log(
				"libplacebo: invalid diagnostic_disable_compute value '%s'; using false",
				rawValue.c_str());
			settings.diagnosticDisableCompute = false;
		}
		if (TryGetDisplayString(config, "diagnostic_force_8bit_sdr_swapchain", rawValue) &&
			!TryGetDisplayBool(config, "diagnostic_force_8bit_sdr_swapchain",
				settings.diagnosticForce8BitSdrSwapchain))
		{
			DebugLog::Log(
				"libplacebo: invalid diagnostic_force_8bit_sdr_swapchain value '%s'; using false",
				rawValue.c_str());
			settings.diagnosticForce8BitSdrSwapchain = false;
		}
		if (TryGetDisplayString(config, "diagnostic_allow_limited_g22", rawValue) &&
			!TryGetDisplayBool(config, "diagnostic_allow_limited_g22",
				settings.diagnosticAllowLimitedG22))
		{
			DebugLog::Log(
				"libplacebo: invalid diagnostic_allow_limited_g22 value '%s'; using false",
				rawValue.c_str());
			settings.diagnosticAllowLimitedG22 = false;
		}
		if (TryGetDisplayString(config, "diagnostic_allow_full_g22", rawValue) &&
			!TryGetDisplayBool(config, "diagnostic_allow_full_g22",
				settings.diagnosticAllowFullG22))
		{
			DebugLog::Log(
				"libplacebo: invalid diagnostic_allow_full_g22 value '%s'; using false",
				rawValue.c_str());
			settings.diagnosticAllowFullG22 = false;
		}
		if (TryGetDisplayString(config, "diagnostic_vp_owned_dxgi_presenter", rawValue) &&
			!TryGetDisplayBool(config, "diagnostic_vp_owned_dxgi_presenter",
				settings.diagnosticVpOwnedDxgiPresenter))
		{
			DebugLog::Log(
				"libplacebo: invalid diagnostic_vp_owned_dxgi_presenter value '%s'; using false",
				rawValue.c_str());
			settings.diagnosticVpOwnedDxgiPresenter = false;
		}

		if (TryGetDisplayString(config, "screen_aspect", rawValue))
		{
			double parsed = 0.0;
			if (ParseAspectRatio(rawValue, parsed) && parsed >= 1.0 && parsed <= 4.0)
			{
				settings.configuredScreenAspect = parsed;
				settings.configuredScreenTarget = true;
			}
			else
				DebugLog::Log(
					"libplacebo: screen_aspect must be a ratio or decimal between 1.0 and 4.0; using output panel aspect");
		}
		settings.verticalAlignment = ReadChoice(config,
			"vertical_alignment", "center", { "top", "center", "bottom" });
		if (TryGetDisplayString(config, "lut", rawValue))
			settings.lutPath = ResolveConfigRelativePath(
				config, rawValue, &settings.lutPathRejected,
				&settings.lutConstrainedBaseDirectory);
		settings.lutReferencePrimaries = ReadChoice(
			config, "lut_reference_primaries", "auto",
			{ "auto", "rec709", "p3_d65", "bt2020" });
		settings.lutReferenceTransfer = ReadChoice(
			config, "lut_reference_transfer", "auto",
			{ "auto", "srgb", "bt1886", "2.2", "2.4" });
		settings.lutReferenceRange = ReadChoice(
			config, "lut_reference_range", "auto",
			{ "auto", "full", "limited" });
		if (TryGetDisplayString(config, "lut_reference_nits", rawValue) &&
			ConfigFile::NormalizeName(rawValue) != "auto")
		{
			double parsed = 0.0;
			if (ParseDouble(rawValue, parsed) && parsed >= 40.0 && parsed <= 500.0)
				settings.lutReferenceNits = parsed;
			else
				DebugLog::Log(
					"display: lut_reference_nits must be AUTO or between 40 and 500; using AUTO");
		}
		if (TryGetDisplayString(config, "subtitle_fit", rawValue) &&
			!TryGetDisplayBool(config, "subtitle_fit", settings.scopeSubtitleFit))
		{
			DebugLog::Log(
				"libplacebo: invalid subtitle_fit value '%s'; using false",
				rawValue.c_str());
			settings.scopeSubtitleFit = false;
		}
		if (TryGetDisplayString(config, "automatic_crop", rawValue) &&
			!TryGetDisplayBool(config, "automatic_crop",
				settings.automaticSourceCrop))
		{
			DebugLog::Log(
				"libplacebo: invalid automatic_crop value '%s'; using false",
				rawValue.c_str());
			settings.automaticSourceCrop = false;
		}
		if (TryGetDisplayString(config, "subtitle_hold_seconds", rawValue))
		{
			double seconds = 0.0;
			if (ParseDouble(rawValue, seconds) &&
				seconds >= RendererProfileConfig::MIN_SUBTITLE_HOLD_SECONDS &&
				seconds <= RendererProfileConfig::MAX_SUBTITLE_HOLD_SECONDS)
				settings.scopeSubtitleHoldMs = static_cast<uint64_t>(std::llround(seconds * 1000.0));
			else
				DebugLog::Log("libplacebo: subtitle_hold_seconds must be between 0.25 and 30; using 2.0");
		}
		if (TryGetDisplayString(config, "subtitle_engage_drift_ms", rawValue))
		{
			int milliseconds = 0;
			if (ParseInteger(rawValue, 0, 30000, milliseconds))
				settings.scopeSubtitleEngageDriftMs =
					static_cast<uint64_t>(milliseconds);
			else
				DebugLog::Log("libplacebo: subtitle_engage_drift_ms must be between 0 and 30000; using 0");
		}
		if (TryGetDisplayString(config, "subtitle_release_drift_ms", rawValue))
		{
			int milliseconds = 0;
			if (ParseInteger(rawValue, 0, 30000, milliseconds))
				settings.scopeSubtitleReleaseDriftMs =
					static_cast<uint64_t>(milliseconds);
			else
				DebugLog::Log("libplacebo: subtitle_release_drift_ms must be between 0 and 30000; using 0");
		}
		if (TryGetDisplayString(config, "subtitle_padding_pixels", rawValue))
		{
			double pixels = 0.0;
			if (ParseDouble(rawValue, pixels) && pixels >= 0.0 && pixels <= 500.0)
				settings.scopeSubtitlePaddingPixels = static_cast<int>(std::llround(pixels));
			else
				DebugLog::Log("libplacebo: subtitle_padding_pixels must be between 0 and 500; using 20");
		}
		if (TryGetDisplayString(config, "subtitle_target_buffer_pixels", rawValue))
		{
			double pixels = 0.0;
			if (ParseDouble(rawValue, pixels) && pixels >= 0.0 &&
				pixels <= RendererProfileConfig::MAX_SUBTITLE_TARGET_BUFFER_PIXELS)
			{
				settings.scopeSubtitleTargetBufferPixels =
					static_cast<int>(std::llround(pixels));
			}
			else
			{
				DebugLog::Log(
					"libplacebo: subtitle_target_buffer_pixels must be between 0 and 50; using 10");
			}
		}

		if (TryGetDisplayString(config, "contrast_recovery", rawValue) &&
			ConfigFile::NormalizeName(rawValue) != "auto")
		{
			double parsed = 0.0;
			if (ParseDouble(rawValue, parsed) && parsed >= 0.0 && parsed <= 1.0)
			{
				settings.hasContrastRecovery = true;
				settings.contrastRecovery = static_cast<float>(parsed);
			}
			else
			{
				DebugLog::Log(
					"libplacebo: contrast_recovery must be AUTO or between 0.0 and 1.0; using AUTO");
			}
		}

		std::string activeProfiles;
		ApplyAutomaticProfiles(config, state, manualUnifiedProfiles, settings, activeProfiles);
		const bool unifiedConfig = RendererProfileConfig::IsUnified(config);
		const DisplayRule selectedRule = unifiedConfig ? DisplayRule() :
			(manualRule.empty() ? SelectDisplayRule(config, state) :
				(FindProfile(config, manualRule).name.empty() ?
					FindDisplayRule(config, manualRule) : FindProfile(config, manualRule)));
		if (!selectedRule.name.empty())
		{
			ApplyDisplayRuleOverrides(config, selectedRule, settings);
			activeRule = selectedRule.name;
			DebugLog::Log("display: selected %s rule '%s' (priority %d)",
				manualRule.empty() ? "automatic" : "manual", activeRule.c_str(), selectedRule.priority);
		}

		// [vpvr.general] owns cross-profile renderer behavior. Deprecated
		// [general], [display], and [libplacebo] locations remain readable
		// through the centralized compatibility view.
		const RendererConfigView rendererConfig(config);
		if (rendererConfig.TryGetPolicyString(
			"profile_update_mode", rawValue) &&
			!ParseProfileUpdateMode(rawValue, settings.profileUpdateMode))
		{
			DebugLog::Log(
				"libplacebo: invalid profile_update_mode policy '%s'; using rebuild",
				rawValue.c_str());
			settings.profileUpdateMode = ProfileUpdateMode::REBUILD;
		}
		if (rendererConfig.TryGetPolicyString(
			"live_profile_updates", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"live_profile_updates", legacyLiveProfileUpdates))
		{
			DebugLog::Log("libplacebo: invalid live_profile_updates policy '%s'; retaining display setting", rawValue.c_str());
		}
		else if (!rendererConfig.TryGetPolicyString(
			"profile_update_mode", rawValue) &&
			rendererConfig.TryGetPolicyString("live_profile_updates", rawValue))
		{
			settings.profileUpdateMode = legacyLiveProfileUpdates ?
				ProfileUpdateMode::LIVE : ProfileUpdateMode::REBUILD;
		}
		if (rendererConfig.TryGetPolicyString(
			"switch_refresh_rate", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"switch_refresh_rate", settings.switchRefreshRate))
		{
			DebugLog::Log("libplacebo: invalid switch_refresh_rate policy '%s'; retaining display setting", rawValue.c_str());
		}
		if (rendererConfig.TryGetPolicyString(
			"output_diagnostics", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"output_diagnostics", settings.outputDiagnostics))
		{
			DebugLog::Log("libplacebo: invalid output_diagnostics policy '%s'; retaining display setting", rawValue.c_str());
		}
		if (rendererConfig.TryGetPolicyString(
			"diagnostic_disable_shader_cache", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"diagnostic_disable_shader_cache",
				settings.diagnosticDisableShaderCache))
		{
			DebugLog::Log("libplacebo: invalid diagnostic_disable_shader_cache policy '%s'; retaining display setting", rawValue.c_str());
		}
		if (rendererConfig.TryGetPolicyString(
			"diagnostic_disable_compute", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"diagnostic_disable_compute", settings.diagnosticDisableCompute))
		{
			DebugLog::Log("libplacebo: invalid diagnostic_disable_compute policy '%s'; retaining display setting", rawValue.c_str());
		}
		if (rendererConfig.TryGetPolicyString(
			"diagnostic_force_8bit_sdr_swapchain", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"diagnostic_force_8bit_sdr_swapchain",
				settings.diagnosticForce8BitSdrSwapchain))
		{
			DebugLog::Log("libplacebo: invalid diagnostic_force_8bit_sdr_swapchain policy '%s'; retaining display setting", rawValue.c_str());
		}
		if (rendererConfig.TryGetPolicyString(
			"diagnostic_allow_limited_g22", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"diagnostic_allow_limited_g22", settings.diagnosticAllowLimitedG22))
		{
			DebugLog::Log("libplacebo: invalid diagnostic_allow_limited_g22 policy '%s'; retaining display setting", rawValue.c_str());
		}
		if (rendererConfig.TryGetPolicyString(
			"diagnostic_allow_full_g22", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"diagnostic_allow_full_g22", settings.diagnosticAllowFullG22))
		{
			DebugLog::Log("libplacebo: invalid diagnostic_allow_full_g22 policy '%s'; retaining display setting", rawValue.c_str());
		}
		if (rendererConfig.TryGetPolicyString(
			"diagnostic_vp_owned_dxgi_presenter", rawValue) &&
			!rendererConfig.TryGetPolicyBool(
				"diagnostic_vp_owned_dxgi_presenter",
				settings.diagnosticVpOwnedDxgiPresenter))
		{
			DebugLog::Log("libplacebo: invalid diagnostic_vp_owned_dxgi_presenter policy '%s'; retaining display setting", rawValue.c_str());
		}
		if (!activeProfiles.empty())
		{
			activeRule = activeProfiles;
			DebugLog::Log("profiles: automatic selections: %s", activeProfiles.c_str());
		}

		const auto* refreshCommands = config.GetSectionValues("refresh_rate_commands");
		if (refreshCommands)
		{
			auto delaySeconds = refreshCommands->find("delay_seconds");
			if (delaySeconds != refreshCommands->end())
			{
				double seconds = 0.0;
				if (ParseDouble(delaySeconds->second, seconds) &&
					seconds >= 0.0 && seconds <= 30.0 &&
					std::floor(seconds) == seconds)
				{
					settings.refreshRateCommandDelayMs =
						static_cast<uint64_t>(std::llround(seconds * 1000.0));
				}
				else
				{
					DebugLog::Log(
						"display: refresh_rate_commands delay_seconds must be a whole number from 0 to 30; using 5");
				}
			}

			// Compatibility with the initial two-part format. New configurations put
			// the complete command line directly in each rate entry.
			std::string legacyCommand;
			auto command = refreshCommands->find("command");
			if (command != refreshCommands->end())
				legacyCommand = ConfigFile::Trim(command->second);

			for (const auto& entry : *refreshCommands)
			{
				if (entry.first == "command" || entry.first == "delay_seconds")
					continue;

				RefreshRateCommandRule rule;
				if (!ParseRefreshRateRuleKey(
					entry.first, rule.minimumRate, rule.maximumRate))
				{
					DebugLog::Log(
						"display: invalid refresh-rate command key '%s'; expected RATE or MIN-MAX",
						entry.first.c_str());
					continue;
				}
				rule.commandLine = ConfigFile::Trim(entry.second);
				if (!legacyCommand.empty())
				{
					std::string executable = legacyCommand;
					if (executable.find_first_of(" \t") != std::string::npos &&
						(executable.size() < 2 || executable.front() != '"'))
					{
						executable = "\"" + executable + "\"";
					}
					rule.commandLine = executable +
						(rule.commandLine.empty() ? "" : " " + rule.commandLine);
				}
				if (rule.commandLine.empty())
				{
					DebugLog::Log(
						"display: empty refresh-rate command for key '%s' ignored",
						entry.first.c_str());
					continue;
				}
				settings.refreshRateCommandRules.push_back(std::move(rule));
			}
		}

		// A selected profile can legitimately change the white target without
		// repeating an inherited explicit black level. Resolve that cross-field
		// dependency once all profiles have been applied, so it never reaches the
		// output path in an invalid state.
		NormalizeSdrBlackLevel(settings);
		return settings;
	}

	enum pl_color_transfer TranslateOutputGamma(const std::string& gamma)
	{
		if (gamma == "bt1886") return PL_COLOR_TRC_BT_1886;
		if (gamma == "srgb") return PL_COLOR_TRC_SRGB;
		if (gamma == "1.8") return PL_COLOR_TRC_GAMMA18;
		if (gamma == "2.0") return PL_COLOR_TRC_GAMMA20;
		if (gamma == "2.2") return PL_COLOR_TRC_GAMMA22;
		if (gamma == "2.4") return PL_COLOR_TRC_GAMMA24;
		if (gamma == "2.6") return PL_COLOR_TRC_GAMMA26;
		if (gamma == "2.8") return PL_COLOR_TRC_GAMMA28;
		return PL_COLOR_TRC_UNKNOWN;
	}

	double RefreshRateHz(const DISPLAYCONFIG_RATIONAL& rate)
	{
		return rate.Numerator > 0 && rate.Denominator > 0
			? static_cast<double>(rate.Numerator) / static_cast<double>(rate.Denominator)
			: 0.0;
	}

	std::wstring Utf8ToWide(const std::string& value)
	{
		if (value.empty())
			return std::wstring();
		const int length = MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
		if (length <= 0)
			return std::wstring();
		std::wstring result(static_cast<size_t>(length), L'\0');
		if (MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, &result[0], length) <= 0)
		{
			return std::wstring();
		}
		result.resize(static_cast<size_t>(length - 1));
		return result;
	}

	void RunRefreshRateCommand(
		const RendererSettings& settings,
		double refreshRate)
	{
		if (settings.refreshRateCommandRules.empty() || refreshRate <= 0.0)
		{
			return;
		}

		// Match the established VP refresh-key convention: 23.976 -> 23 and
		// 59.94 -> 59. An exact key takes precedence over a wider range.
		const int refreshKey = static_cast<int>(std::floor(refreshRate + 0.000001));
		const RefreshRateCommandRule* selected = nullptr;
		for (const RefreshRateCommandRule& rule : settings.refreshRateCommandRules)
		{
			if (refreshKey < rule.minimumRate || refreshKey > rule.maximumRate)
				continue;
			if (!selected ||
				(rule.maximumRate - rule.minimumRate) <
				(selected->maximumRate - selected->minimumRate))
			{
				selected = &rule;
			}
		}
		if (!selected)
			return;

		const std::wstring configuredCommand = Utf8ToWide(selected->commandLine);
		if (configuredCommand.empty())
		{
			DebugLog::Log("display: refresh-rate command line is not valid UTF-8");
			return;
		}

		wchar_t commandProcessor[MAX_PATH]{};
		DWORD commandProcessorLength = GetEnvironmentVariableW(
			L"ComSpec", commandProcessor, static_cast<DWORD>(_countof(commandProcessor)));
		if (commandProcessorLength == 0 ||
			commandProcessorLength >= static_cast<DWORD>(_countof(commandProcessor)))
		{
			const UINT systemLength = GetSystemDirectoryW(
				commandProcessor, static_cast<UINT>(_countof(commandProcessor)));
			if (systemLength == 0 ||
				systemLength + 8 >= static_cast<UINT>(_countof(commandProcessor)))
			{
				DebugLog::Log("display: unable to locate the Windows command processor");
				return;
			}
			wcscat_s(commandProcessor, L"\\cmd.exe");
		}

		// The renderer and its fullscreen/window state may still be being
		// established here. The configured delay lets scripts inspect the final
		// render window or send shortcuts. ping.exe provides a quiet,
		// console-independent whole-second delay (timeout.exe fails without an
		// interactive stdin).
		const uint64_t delaySeconds =
			(settings.refreshRateCommandDelayMs + 999) / 1000;
		std::wstring delayedCommand;
		if (delaySeconds > 0)
		{
			delayedCommand = L"ping.exe 127.0.0.1 -n ";
			delayedCommand += std::to_wstring(delaySeconds + 1);
			delayedCommand += L" >nul & ";
		}
		delayedCommand += configuredCommand;

		std::wstring processCommandLine = L"\"";
		processCommandLine += commandProcessor;
		processCommandLine += L"\" /d /s /c \"";
		processCommandLine += delayedCommand;
		processCommandLine += L"\"";

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESHOWWINDOW;
		startupInfo.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION processInfo{};
		if (!CreateProcessW(
			commandProcessor,
			&processCommandLine[0],
			nullptr,
			nullptr,
			FALSE,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&startupInfo,
			&processInfo))
		{
			DebugLog::Log(
				"display: refresh-rate command failed: rate=%.6f key=%d error=%lu",
				refreshRate,
				refreshKey,
				GetLastError());
			return;
		}
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);

		DebugLog::Log(
			"display: refresh-rate command scheduled in %.3f seconds: rate=%.6f key=%d command='%s'",
			static_cast<double>(settings.refreshRateCommandDelayMs) / 1000.0,
			refreshRate,
			refreshKey,
			selected->commandLine.c_str());
	}

	bool RefreshRatesEqual(
		const DISPLAYCONFIG_RATIONAL& first,
		const DISPLAYCONFIG_RATIONAL& second)
	{
		return DisplayRefreshRatesExactlyEqual(
			{ first.Numerator, first.Denominator },
			{ second.Numerator, second.Denominator });
	}

	std::wstring DisplayDeviceNameForWindow(HWND hwnd)
	{
		const HMONITOR monitor = hwnd
			? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
			: nullptr;
		if (!monitor)
			return std::wstring();

		MONITORINFOEXW monitorInfo{};
		monitorInfo.cbSize = sizeof(monitorInfo);
		return GetMonitorInfoW(monitor, &monitorInfo)
			? std::wstring(monitorInfo.szDevice)
			: std::wstring();
	}

	bool QueryDisplayPath(
		const std::wstring& displayDeviceName,
		std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
		std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
		UINT32& pathCount,
		UINT32& modeCount,
		size_t& matchingPath)
	{
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			pathCount = 0;
			modeCount = 0;
			if (GetDisplayConfigBufferSizes(
				QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS ||
				pathCount == 0)
			{
				return false;
			}

			paths.resize(pathCount);
			modes.resize(modeCount);
			const LONG queryResult = QueryDisplayConfig(
				QDC_ONLY_ACTIVE_PATHS,
				&pathCount,
				paths.data(),
				&modeCount,
				modes.data(),
				nullptr);
			if (queryResult == ERROR_INSUFFICIENT_BUFFER)
				continue;
			if (queryResult != ERROR_SUCCESS)
				return false;

			for (UINT32 pathIndex = 0; pathIndex < pathCount; ++pathIndex)
			{
				DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
				sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
				sourceName.header.size = sizeof(sourceName);
				sourceName.header.adapterId = paths[pathIndex].sourceInfo.adapterId;
				sourceName.header.id = paths[pathIndex].sourceInfo.id;
				if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS &&
					displayDeviceName == sourceName.viewGdiDeviceName)
				{
					matchingPath = pathIndex;
					return true;
				}
			}
			return false;
		}
		return false;
	}

	LONG ApplyDisplayRefreshRate(
		std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
		std::vector<DISPLAYCONFIG_MODE_INFO>& modes,
		UINT32 pathCount,
		UINT32 modeCount,
		size_t pathIndex,
		const DISPLAYCONFIG_RATIONAL& refreshRate)
	{
		DISPLAYCONFIG_PATH_INFO& path = paths[pathIndex];
		path.targetInfo.refreshRate = refreshRate;

		// QueryDisplayConfig supplies the current target mode. If that mode stays
		// referenced, SetDisplayConfig ignores targetInfo.refreshRate and uses the
		// mode's existing vSyncFreq. Keep the source mode (and therefore desktop
		// resolution) fixed, but ask Windows to select a target timing for the new
		// rational refresh rate.
		if ((path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0)
			path.targetInfo.targetModeInfoIdx = DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID;
		else
			path.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;

		return SetDisplayConfig(
			pathCount,
			paths.data(),
			modeCount,
			modes.data(),
			SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);
	}

	class ScopedDisplayRefreshRate
	{
	public:
		ScopedDisplayRefreshRate()
			: m_cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
		{
		}

		~ScopedDisplayRefreshRate()
		{
			if (!m_finalRestoreAttempted && !RestoreNow())
				DebugLog::Log(
					"libplacebo refresh-rate final restore remains unverified");
			for (std::thread& worker : m_actionWorkers)
				if (worker.joinable()) worker.join();
			if (m_cancelEvent) CloseHandle(m_cancelEvent);
		}

		bool RestoreNow()
		{
			m_finalRestoreAttempted = true;
			if (m_cancelEvent) SetEvent(m_cancelEvent);
			for (std::thread& worker : m_actionWorkers)
				if (worker.joinable()) worker.join();
			m_actionWorkers.clear();
			return Restore();
		}

		bool HasPendingRestore() const { return m_changed; }

		void Switch(HWND hwnd, const VideoState& state, const RendererSettings& settings)
		{
			if (!settings.switchRefreshRate || !state.displayMode)
				return;
			m_refreshCommandSettings = settings;

			m_displayDeviceName = DisplayDeviceNameForWindow(hwnd);
			if (m_displayDeviceName.empty())
				return;

			std::vector<DISPLAYCONFIG_PATH_INFO> paths;
			std::vector<DISPLAYCONFIG_MODE_INFO> modes;
			UINT32 pathCount = 0;
			UINT32 modeCount = 0;
			size_t pathIndex = 0;
			if (!QueryDisplayPath(
				m_displayDeviceName, paths, modes, pathCount, modeCount, pathIndex))
			{
				DebugLog::Log("libplacebo refresh-rate switch: active display path not found");
				return;
			}

			m_originalRefreshRate = paths[pathIndex].targetInfo.refreshRate;
			DISPLAYCONFIG_RATIONAL targetRefreshRate{};
			targetRefreshRate.Numerator = state.displayMode->TimeScale();
			targetRefreshRate.Denominator = state.displayMode->FrameDuration();

			const double contentRate = state.displayMode->RefreshRateHz();
			const bool useDoubleRate =
				state.displayMode->IsInterlaced() ||
				(contentRate > 24.1 && contentRate < 31.0);
			if (useDoubleRate)
				targetRefreshRate.Numerator *= 2;

			if (RefreshRatesEqual(m_originalRefreshRate, targetRefreshRate))
			{
				DebugLog::Log(
					"libplacebo refresh-rate switch: display already %.6f Hz for %.6f Hz input",
					RefreshRateHz(m_originalRefreshRate),
					contentRate);
				RunRefreshRateCommand(settings, RefreshRateHz(m_originalRefreshRate));
				PublishEvent("refresh.confirmed",
					RefreshRateHz(m_originalRefreshRate),
					RefreshRateHz(targetRefreshRate),
					RefreshRateHz(m_originalRefreshRate));
				return;
			}

			const LONG switchResult = ApplyDisplayRefreshRate(
				paths,
				modes,
				pathCount,
				modeCount,
				pathIndex,
				targetRefreshRate);
			if (switchResult != ERROR_SUCCESS)
			{
				DebugLog::Log(
					"libplacebo refresh-rate switch failed: input=%.6f Hz target=%.6f Hz error=%ld; continuing with current display mode",
					contentRate,
					RefreshRateHz(targetRefreshRate),
					switchResult);
				return;
			}

			m_changed = true;
			DISPLAYCONFIG_RATIONAL actualRefreshRate{};
			const bool actualRateAvailable = GetCurrentRefreshRate(actualRefreshRate);
			if (actualRateAvailable)
			{
				DebugLog::Log(
					"libplacebo refresh-rate switch applied: input=%.6f Hz target=%.6f Hz previous=%.6f Hz actual=%.6f Hz",
					contentRate,
					RefreshRateHz(targetRefreshRate),
					RefreshRateHz(m_originalRefreshRate),
					RefreshRateHz(actualRefreshRate));
			}
			else
			{
				DebugLog::Log(
					"libplacebo refresh-rate switch applied: input=%.6f Hz target=%.6f Hz previous=%.6f Hz",
					contentRate,
					RefreshRateHz(targetRefreshRate),
					RefreshRateHz(m_originalRefreshRate));
			}
			RunRefreshRateCommand(
				settings,
				actualRateAvailable
					? RefreshRateHz(actualRefreshRate)
					: RefreshRateHz(targetRefreshRate));
			PublishEvent("refresh.applied",
				actualRateAvailable ? RefreshRateHz(actualRefreshRate) : RefreshRateHz(targetRefreshRate),
				RefreshRateHz(targetRefreshRate), RefreshRateHz(m_originalRefreshRate));
		}

	private:
		bool GetCurrentRefreshRate(DISPLAYCONFIG_RATIONAL& refreshRate) const
		{
			std::vector<DISPLAYCONFIG_PATH_INFO> paths;
			std::vector<DISPLAYCONFIG_MODE_INFO> modes;
			UINT32 pathCount = 0;
			UINT32 modeCount = 0;
			size_t pathIndex = 0;
			if (!QueryDisplayPath(
				m_displayDeviceName, paths, modes, pathCount, modeCount, pathIndex))
			{
				return false;
			}
			refreshRate = paths[pathIndex].targetInfo.refreshRate;
			return true;
		}

		bool Restore()
		{
			if (!m_changed)
				return true;

			std::vector<DISPLAYCONFIG_PATH_INFO> paths;
			std::vector<DISPLAYCONFIG_MODE_INFO> modes;
			UINT32 pathCount = 0;
			UINT32 modeCount = 0;
			size_t pathIndex = 0;
			if (!QueryDisplayPath(
				m_displayDeviceName, paths, modes, pathCount, modeCount, pathIndex))
			{
				DebugLog::Log("libplacebo refresh-rate restore failed: active display path not found");
				return false;
			}

			const LONG restoreResult = ApplyDisplayRefreshRate(
				paths,
				modes,
				pathCount,
				modeCount,
				pathIndex,
				m_originalRefreshRate);
			if (restoreResult != ERROR_SUCCESS)
			{
				DebugLog::Log(
					"libplacebo refresh-rate restore failed: %.6f Hz error=%ld; "
					"restore remains pending",
					RefreshRateHz(m_originalRefreshRate), restoreResult);
				return false;
			}

			// SetDisplayConfig success means the request was accepted, not that
			// the active path has converged. Require two consecutive exact
			// rational observations within a bounded two-second window.
			DISPLAYCONFIG_RATIONAL actualRefreshRate{};
			DisplayRefreshRestoreVerifier verifier(
				{ m_originalRefreshRate.Numerator,
				  m_originalRefreshRate.Denominator });
			const ULONGLONG verificationDeadline = GetTickCount64() + 2000;
			do
			{
				const bool querySucceeded =
					GetCurrentRefreshRate(actualRefreshRate);
				if (verifier.Observe(
					querySucceeded,
					{ actualRefreshRate.Numerator,
					  actualRefreshRate.Denominator }))
					break;
				Sleep(50);
			}
			while (GetTickCount64() < verificationDeadline);

			if (verifier.ConsecutiveMatches() < 2)
			{
				DebugLog::Log(
					"libplacebo refresh-rate restore unverified: requested=%.6f Hz "
					"last_observed=%.6f Hz; restore remains pending",
					RefreshRateHz(m_originalRefreshRate),
					RefreshRateHz(actualRefreshRate));
				return false;
			}

			DebugLog::Log(
				"libplacebo refresh-rate restore verified: %.6f Hz "
				"rational=%u/%u",
				RefreshRateHz(m_originalRefreshRate),
				m_originalRefreshRate.Numerator,
				m_originalRefreshRate.Denominator);
			RunRefreshRateCommand(
				m_refreshCommandSettings,
				RefreshRateHz(m_originalRefreshRate));
			PublishEvent("refresh.restored", RefreshRateHz(m_originalRefreshRate),
				RefreshRateHz(m_originalRefreshRate), RefreshRateHz(m_originalRefreshRate));
			m_changed = false;
			return true;
		}

		void PublishEvent(const std::string& event, double actualRefresh,
			double requestedRefresh, double previousRefresh)
		{
			ConfigFile config;
			RendererProfileConfig::Model model;
			std::string error;
			if (!config.Load(ConfigFile::RENDERER_FILENAME) ||
				!RendererProfileConfig::IsUnified(config) ||
				!RendererProfileConfig::Read(config, model, error))
				return;
			std::ostringstream identity;
			identity.imbue(std::locale::classic());
			identity.precision(9);
			identity << event << '|' << actualRefresh;
			if (!m_publishedTransitions.insert(identity.str()).second)
			{
				DebugLog::Log("event transition '%s' suppressed as duplicate",
					identity.str().c_str());
				return;
			}
			for (const auto& action : model.actions)
			{
				if (action.renderer != "vprenderer" && action.renderer != "*")
					continue;
				if (std::find(action.events.begin(), action.events.end(), event) ==
					action.events.end()) continue;
				const EventActionLauncher::ActionValueLookup values =
					[&event, actualRefresh, requestedRefresh, previousRefresh](
						const std::string& variable, std::string& value)
					{
						double number = 0.0;
						if (variable == "event")
						{
							value = event;
							return true;
						}
						if (variable == "event_reason")
						{
							value = "refresh";
							return true;
						}
						if (variable == "actual_refresh") number = actualRefresh;
						else if (variable == "requested_refresh") number = requestedRefresh;
						else if (variable == "previous_refresh") number = previousRefresh;
						else return false;
						for (const double canonical : {
							24000.0 / 1001.0, 30000.0 / 1001.0, 60000.0 / 1001.0 })
							if (std::abs(number - canonical) < 0.01)
							{
								number = canonical < 24.5 ? 23.976 :
									(canonical < 40.0 ? 29.97 : 59.94);
								break;
							}
						std::ostringstream text;
						text.imbue(std::locale::classic());
						text.precision(9);
						text << number;
						value = text.str();
						return true;
					};
				int specificity = 0;
				std::string matchError;
				const bool matches = action.when.empty() ||
					action.whenExpression.Matches(values, specificity, matchError);
				if (!matches)
				{
					if (!matchError.empty())
						DebugLog::Log("event action '%s' evaluation failed: %s",
							action.name.c_str(), matchError.c_str());
					continue;
				}
				RendererProfileConfig::Model::EventAction resolvedAction;
				std::string expansionError;
				if (!EventActionLauncher::ExpandArgumentVariables(action, values,
					resolvedAction, expansionError))
				{
					DebugLog::Log("event action '%s' expansion failed: %s",
						action.name.c_str(), expansionError.c_str());
					continue;
				}
				const std::string configPath = config.GetLoadedPath();
				const DWORD delayMs = static_cast<DWORD>(
					resolvedAction.delaySeconds * 1000);
				DebugLog::Log("event action '%s' scheduled for %s in %d seconds",
					resolvedAction.name.c_str(), event.c_str(),
					resolvedAction.delaySeconds);
				m_actionWorkers.emplace_back([this, resolvedAction, configPath, delayMs]()
					{
						if (!m_cancelEvent ||
							WaitForSingleObject(m_cancelEvent, delayMs) == WAIT_TIMEOUT)
							EventActionLauncher::Launch(resolvedAction, configPath);
						else
							DebugLog::Log("event action '%s' cancelled with renderer generation",
								resolvedAction.name.c_str());
					});
			}
		}

		std::wstring m_displayDeviceName;
		DISPLAYCONFIG_RATIONAL m_originalRefreshRate{};
		bool m_changed = false;
		bool m_finalRestoreAttempted = false;
		RendererSettings m_refreshCommandSettings;
		HANDLE m_cancelEvent = nullptr;
		std::vector<std::thread> m_actionWorkers;
		std::set<std::string> m_publishedTransitions;
	};
}


struct LibplaceboVideoRenderer::Impl
{
	SceneDetector sceneDetector;
	AlphaCadenceCorrectionPolicy cadenceCorrectionPolicy;
	AlphaPresentationTelemetry presentationTelemetry;
	ScopedDisplayRefreshRate displayRefreshRate;
	pl_log log = nullptr;
	pl_d3d11 d3d11 = nullptr;
	pl_cache cache = nullptr;
	pl_swapchain swapchain = nullptr;
	// When enabled, VP creates and retains this DXGI object. The pl_swapchain
	// wrapper remains only as a resize/configuration compatibility handle; VP
	// acquires the backbuffer, supplies it to libplacebo, and calls Present.
	CComPtr<IDXGISwapChain1> vpOwnedSwapchain;
	uint64_t vpOwnedSwapchainGeneration = 0;
	uint64_t vpOwnedTargetValidationLoggedGeneration = 0;
	uint64_t vpOwnedTargetFailureLoggedGeneration = 0;
	uint64_t vpOwnedPresentFailureLoggedGeneration = 0;
	uint64_t successfulPresentCount = 0;
	uint64_t vpOwnedClearLoggedGeneration = 0;
	std::string vpOwnedAppliedEncoding = "unapplied";
	HRESULT vpOwnedColorSpaceResult = E_PENDING;
	bool vpOwnedColorSpaceVerified = false;
	DXGI_FORMAT negotiatedSwapchainFormat = DXGI_FORMAT_UNKNOWN;
	pl_renderer renderer = nullptr;
	LibplaceboCompileTelemetry compileTelemetry;
	pl_tex textures[2] = { nullptr, nullptr };
	pl_tex statsOverlayTexture = nullptr;
	pl_tex sweepOverlayTexture = nullptr;
	std::mutex statsOverlayMutex;
	std::vector<uint8_t> statsOverlayPixels;
	int statsOverlayWidth = 0;
	int statsOverlayHeight = 0;
	int statsOverlayStride = 0;
	uint64_t statsOverlaySerial = 0;
	uint64_t appliedStatsOverlaySerial = 0;
	std::vector<uint8_t> sweepOverlayPixels;
	int sweepOverlayWidth = 0;
	int sweepOverlayHeight = 0;
	int sweepOverlayStride = 0;
	uint64_t sweepOverlaySerial = 0;
	uint64_t appliedSweepOverlaySerial = 0;
	NativeStatsOverlayPlacement::Result lastStatsOverlayPlacement;
	bool hasStatsOverlayPlacement = false;
	NativeStatsOverlayPlacement::Result lastSweepOverlayPlacement;
	bool hasSweepOverlayPlacement = false;
	pl_custom_lut* displayLut = nullptr;
	// Kept deliberately short for the Ctrl+I OSD: "Disabled",
	// "Loaded: validating", "Active: name (65^3)", or "Rejected: reason".
	std::string displayLutStatus = "Disabled";
	std::string displayLutPath;
	std::string displayLutConstrainedBaseDirectory;
	std::string lutReferencePrimaries = "auto";
	std::string lutReferenceTransfer = "auto";
	std::string lutReferenceRange = "auto";
	double lutReferenceNits = 0.0;
	bool displayLutParsed = false;
	std::unique_ptr<IVideoFrameFormatter> formatter;
	VideoStateComPtr formatterState;
	std::vector<BYTE> convertedFrame;
	bool formatterContractLogged = false;
	std::string ingressStatus = "P010 (initializing)";
	struct pl_render_params renderParams{};
	ActivePictureTransitionModel nlsTransition;
	ConfiguredShaderRule nlsRule;
	std::vector<ConfiguredShaderRule> startupNlsPrewarmRules;
	bool startupNlsPrewarmComplete = false;
	std::string requestedShaderSelector;
	std::string activeShaderStatus = "None";
	uint64_t activeShaderStatusSerial = 0;
	uint64_t nlsRendererGeneration = 0;
	uint64_t nlsGeometryGeneration = 0;
	bool nlsRequested = false;
	uint64_t nativeRgbAnalysisLoggedGeneration = 0;
	VideoFrameEncoding nativeRgbAnalysisLoggedEncoding =
		VideoFrameEncoding::UNKNOWN;
	ColorSpace nativeRgbAnalysisLoggedColorspace = ColorSpace::UNKNOWN;
	int nativeRgbAnalysisLoggedWidth = 0;
	int nativeRgbAnalysisLoggedHeight = 0;
	bool nlsGeometryAvailable = false;
	bool nlsTransitionWithdrawn = false;
	ActivePictureBounds nlsGeometry;
	ActivePictureClassification nlsGeometryClassification =
		ActivePictureClassification::UNAVAILABLE;
	bool latestActivePictureObservationSupportsCrop = false;
	uint64_t nlsGeometrySourceGeneration = 0;
	uint64_t activePictureAnalysisSourceGeneration = 0;
	NlsMappingDecision nlsDecision;
	const struct pl_hook* nlsHook = nullptr;
	std::string nlsHookSignature;
	std::string rejectedNlsHookSignature;
	std::string activeNlsShaderPath;
	std::string lastNlsPipelineVariant;
	std::string lastNlsHookBindingPolicy;
	bool nlsPipelineWasActive = false;
	struct pl_color_map_params colorMapParams{};
	struct pl_peak_detect_params peakDetectParams{};
	struct pl_sigmoid_params sigmoidParams{};
	struct pl_deband_params debandParams{};
	struct pl_dither_params ditherParams{};
	double sdrTargetNits = PL_COLOR_SDR_WHITE;
	double sdrBlackNits = PL_COLOR_SDR_WHITE / PL_COLOR_SDR_CONTRAST;
	struct pl_color_space configuredOutputColor{};
	LibplaceboOutput::Plan requestedOutputPlan;
	LibplaceboOutput::Plan outputPlan;
	LibplaceboOutput::Actual actualOutput;
	bool targetBt2020 = false;
	bool reportBt2020ToDisplay = false;
	bool bt2020SignalingFailed = false;
	std::wstring negotiatedDisplayDeviceName;
	NvidiaBt2020Reporter nvidiaBt2020Reporter;
	bool swapchainBlit = true;
	bool suppressLimitedNegotiation = false;
	uint64_t nextOutputRecoveryTick = 0;
	enum pl_color_transfer sdrInputTransfer = PL_COLOR_TRC_UNKNOWN;
	LibplaceboOutput::SdrAdjustGamma sdrAdjustGamma =
		LibplaceboOutput::SdrAdjustGamma::ON;
	LibplaceboOutput::SdrGammaDecision lastSdrGammaDecision;
	std::string lastSdrGammaDecisionSignature;
	double configuredScreenAspect = 1.0;
	bool configuredScreenTarget = false;
	std::string verticalAlignment = "center";
	double anamorphicScale = 1.0;
	bool automaticSourceCrop = false;
	bool scopeSubtitleFit = false;
	uint64_t scopeSubtitleHoldMs = 2000;
	uint64_t scopeSubtitleEngageDriftMs = 0;
	uint64_t scopeSubtitleReleaseDriftMs = 0;
	int scopeSubtitlePaddingPixels = 20;
	int scopeSubtitleTargetBufferPixels =
		RendererProfileConfig::DEFAULT_SUBTITLE_TARGET_BUFFER_PIXELS;
	uint64_t scopeSubtitleAnalysisFrame = 0;
	int scopeSubtitlePictureLeft = 0;
	int scopeSubtitlePictureTop = 0;
	int scopeSubtitlePictureRight = 0;
	int scopeSubtitlePictureBottom = 0;
	int scopeSubtitleDetectedLeft = 0;
	int scopeSubtitleDetectedRight = 0;
	uint64_t scopeSubtitleLeftLastDetectionTick = 0;
	uint64_t scopeSubtitleRightLastDetectionTick = 0;
	uint64_t scopeSubtitleEvidenceSourceGeneration = 0;
	AlphaSourceCrop::VerticalBarPresentationState
		scopeVerticalBarPresentation;
	AlphaSourceCrop::VerticalTranslationConfirmationState
		scopeSubtitleTranslationConfirmation;
	AlphaSourceCrop::VerticalFitConfirmationState
		scopeSubtitleFitConfirmation;
	AlphaSourceCrop::OutwardPictureConfirmationState
		outwardPictureConfirmation;
	AlphaSourceCrop::VerticalTranslationDrift scopeSubtitleDrift;
	bool scopeSubtitleDriftWasActive = false;
	bool scopeSubtitleAuthorityGapHeld = false;
	bool scopeSubtitleRetentionWasUnsafe = false;
	uint64_t scopeSubtitleRetentionGeneration = 0;
	bool scopeSubtitleWasActive = false;
	bool scopeSubtitleWasTopActive = false;
	std::string lastScopeVerticalOverlayPolicy;
	// The detector supplies a coarse four-edge envelope while the denser bar
	// pass below catches smaller source-baked UI. Outward evidence is renewable
	// for as long as it remains visible; the configured hold is used only as a
	// slow-in release delay, never as a maximum lifetime.
	ActivePictureBounds scopePresentationEvidenceBase;
	ActivePictureBounds scopePresentationEvidenceBounds;
	ActivePictureBounds scopePresentationCurrentBounds;
	uint64_t scopePresentationEvidenceLastTick = 0;
	uint64_t scopePresentationEvidenceSourceGeneration = 0;
	uint64_t scopePresentationEvidenceSourceSequence = 0;
	uint64_t scopePresentationCurrentSourceGeneration = 0;
	uint64_t scopePresentationCurrentSourceSequence = 0;
	static constexpr uint64_t ACTIVE_PICTURE_AMBIGUITY_HOLD_MS = 2000;
	// A recognized cut and an ordinary ambiguous fade are the same presentation
	// problem once a last-known-good crop exists. Use one bounded interval so
	// scene detection cannot shorten crop/NLS retention back to 500 ms.
	static constexpr uint64_t ACTIVE_PICTURE_SCENE_VERIFICATION_MS =
		ACTIVE_PICTURE_AMBIGUITY_HOLD_MS;
	uint64_t activePictureSceneVerificationDeadlineTick = 0;
	AlphaSourceCrop::AmbiguityHold activePictureAmbiguityHold;
	bool sceneVerificationGeometryAvailable = false;
	ActivePictureBounds sceneVerificationGeometry;
	uint64_t sceneVerificationGeometrySourceGeneration = 0;
	bool sceneVerificationLatestSupportsCrop = false;
	bool latestActivePictureEvidenceAvailable = false;
	ActivePictureClassification latestActivePictureEvidenceClassification =
		ActivePictureClassification::UNAVAILABLE;
	ActivePictureBounds latestActivePictureEvidenceBounds;
	uint64_t latestActivePictureEvidenceFrame = 0;
	bool latestActivePictureEvidenceWasStartupHypothesis = false;
	bool presentationOwnedGeometryTransitionDeferred = false;
	bool latestActivePicturePresentationRetentionSafe = false;
	bool latestActivePicturePresentationRetentionEvaluated = false;
	std::string latestActivePicturePresentationRetentionReason;
	bool fullRasterPresentationAuthorityAvailable = false;
	uint64_t fullRasterPresentationAuthoritySourceGeneration = 0;
	std::string lastSourceCropPolicy;
	std::string lastFinalPresentationPolicy;
	std::string lastFinalLayoutPolicy;
	std::string activeDisplayRule;
	std::string effectiveSettingsFingerprint;
	std::string restartSettingsFingerprint;
	RendererSettings activeSettings;
	HWND videoHwnd = nullptr;
	HMONITOR negotiatedMonitor = nullptr;
	bool hasPresentedFrame = false;
	uint64_t nextPresentationTelemetryLogTick = 0;
	uint64_t lastSubmittedScreenProfileRequest = 0;
	uint64_t activePictureScreenProfileRequestSerial = 0;
	std::mutex renderMutex;
	EOTF lastRenderedEotf = EOTF::UNKNOWN;
	ColorSpace lastRenderedColorspace = ColorSpace::UNKNOWN;
	std::string shaderCachePath;
	uint64_t loadedShaderCacheSignature = 0;
	bool shaderCacheEnabled = true;
	bool outputDiagnostics = false;
	bool outputContractLogged = false;
	unsigned int diagnosticReadbackFramesRemaining = 30;
	bool diagnosticReadbackComplete = false;
	unsigned int diagnosticReadbackAttempts = 0;
	bool diagnosticReadbackNonBlack = false;
	std::atomic_bool renderedOutputCaptureRequested{false};
	std::vector<std::thread> captureWorkers;
	bool cadenceLogInitialized = false;
	uint64_t cadenceLoggedPolicyGeneration = 0;
	bool cadenceLoggedDue = false;
	AlphaCadenceBlockReason cadenceLoggedBlockReason =
		AlphaCadenceBlockReason::None;
	uint64_t cadenceNextDueSummaryTick = 0;
	uint64_t cadenceNextActionId = 0;
	uint64_t cadencePendingActionId = 0;
	AlphaCadenceAction cadencePendingAction =
		AlphaCadenceAction::None;

	void LogCadencePolicyEvent(
		const char* event,
		const AlphaCadenceCorrectionDecision& decision,
		uint64_t queueGeneration,
		uint64_t actionId = 0) const
	{
		const AlphaCadenceDiagnosticSnapshot& snapshot =
			decision.diagnostic;
		const AlphaCadenceAction requestedAction =
			decision.action != AlphaCadenceAction::None
				? decision.action : decision.predictedAction;
		DebugLog::Log(
			"Alpha cadence policy: event=%s action_id=%llu policy_generation=%llu detector_generation=%llu presentation_generation=%llu queue_generation=%llu source=%llu present_id=%u evidence=%s rate_samples=%u action=%s authorized=%d reason=%s phase=%.9f deadline_s=%+.6f raw_ppm=%+.3f filtered_ppm=%.3f capture_hz=%.6f display_hz=%.6f queue=%zu/%zu oldest_ms=%.3f debt=%llu scene_event=%llu scene_safe=%d scene_fresh=%d scene_authorized=%d planned_frames=%u fallback_frames=%u fallback_mature=%d fallback_eligible=%d cooldown_frames=%u verification_pending=%d",
			event,
			static_cast<unsigned long long>(actionId),
			static_cast<unsigned long long>(snapshot.policyGeneration),
			static_cast<unsigned long long>(snapshot.detectorGeneration),
			static_cast<unsigned long long>(
				snapshot.presentationGeneration),
			static_cast<unsigned long long>(queueGeneration),
			static_cast<unsigned long long>(snapshot.sourceSequence),
			snapshot.lastPresentId,
			AlphaPresentationEvidenceText(snapshot.presentationEvidence),
			decision.rateFilterSamples,
			AlphaCadenceActionText(requestedAction),
			decision.action != AlphaCadenceAction::None ? 1 : 0,
			AlphaCadenceBlockReasonText(decision.blockReason),
			snapshot.phaseFrames,
			decision.secondsUntilCorrection,
			snapshot.rawMismatchPpm,
			snapshot.filteredMismatchPpm,
			snapshot.captureRateHz,
			snapshot.displayRateHz,
			snapshot.queueDepth,
			snapshot.desiredQueueDepth,
			snapshot.oldestQueuedAgeMs,
			static_cast<unsigned long long>(snapshot.presentationDebt),
			static_cast<unsigned long long>(snapshot.sceneEventId),
			snapshot.safeSceneBoundary ? 1 : 0,
			snapshot.sceneEventFresh ? 1 : 0,
			snapshot.sceneAuthorized ? 1 : 0,
			snapshot.plannedFrames,
			snapshot.fallbackFrames,
			snapshot.fallbackMature ? 1 : 0,
			snapshot.fallbackEligible ? 1 : 0,
			snapshot.cooldownFrames,
			decision.verificationPending ? 1 : 0);
	}

	void UpdateCadenceDiagnostics(
		AlphaCadenceCorrectionDecision& decision,
		uint64_t queueGeneration)
	{
		const uint64_t nowTick = GetTickCount64();
		const uint64_t policyGeneration =
			decision.diagnostic.policyGeneration;
		if (!cadenceLogInitialized ||
			policyGeneration != cadenceLoggedPolicyGeneration)
		{
			if (cadencePendingActionId != 0)
			{
				DebugLog::Log(
					"Alpha cadence verification: event=generation_replaced action_id=%llu action=%s old_policy_generation=%llu new_policy_generation=%llu source=%llu present_id=%u",
					static_cast<unsigned long long>(
						cadencePendingActionId),
					AlphaCadenceActionText(cadencePendingAction),
					static_cast<unsigned long long>(
						cadenceLoggedPolicyGeneration),
					static_cast<unsigned long long>(policyGeneration),
					static_cast<unsigned long long>(
						decision.diagnostic.sourceSequence),
					decision.diagnostic.lastPresentId);
				cadencePendingActionId = 0;
				cadencePendingAction = AlphaCadenceAction::None;
			}
			cadenceLoggedPolicyGeneration = policyGeneration;
			cadenceLogInitialized = true;
			cadenceLoggedDue = false;
			cadenceLoggedBlockReason =
				AlphaCadenceBlockReason::None;
			cadenceNextDueSummaryTick = 0;
			LogCadencePolicyEvent(
				"generation_changed", decision, queueGeneration);
		}

		if (decision.verificationCompleted)
		{
			DebugLog::Log(
				"Alpha cadence verification: event=completed action_id=%llu action=%s policy_generation=%llu source=%llu present_id=%u debt=%llu result=%s",
				static_cast<unsigned long long>(
					cadencePendingActionId),
				AlphaCadenceActionText(decision.verificationAction),
				static_cast<unsigned long long>(policyGeneration),
				static_cast<unsigned long long>(
					decision.diagnostic.sourceSequence),
				decision.diagnostic.lastPresentId,
				static_cast<unsigned long long>(
					decision.diagnostic.presentationDebt),
				decision.lastVerificationSucceeded
					? "verified" : "ambiguous");
			cadencePendingActionId = 0;
			cadencePendingAction = AlphaCadenceAction::None;
		}

		const bool blockedDue =
			decision.due &&
			decision.action == AlphaCadenceAction::None;
		if (blockedDue)
		{
			const bool entered = !cadenceLoggedDue;
			const bool reasonChanged =
				cadenceLoggedDue &&
				decision.blockReason != cadenceLoggedBlockReason;
			const bool prolonged =
				!entered && !reasonChanged &&
				nowTick >= cadenceNextDueSummaryTick;
			if (entered || reasonChanged || prolonged)
			{
				LogCadencePolicyEvent(
					entered ? "due_entered" :
					(reasonChanged ? "due_reason_changed" :
						"due_prolonged"),
					decision, queueGeneration);
				cadenceNextDueSummaryTick = nowTick + 30000;
			}
			cadenceLoggedDue = true;
			cadenceLoggedBlockReason = decision.blockReason;
		}
		else if (cadenceLoggedDue)
		{
			LogCadencePolicyEvent(
				decision.action != AlphaCadenceAction::None
					? "due_authorized" : "due_cleared",
				decision, queueGeneration);
			cadenceLoggedDue = false;
			cadenceLoggedBlockReason =
				AlphaCadenceBlockReason::None;
			cadenceNextDueSummaryTick = 0;
		}

		if (decision.action != AlphaCadenceAction::None)
		{
			if (++cadenceNextActionId == 0)
				++cadenceNextActionId;
			decision.actionId = cadenceNextActionId;
			cadencePendingActionId = decision.actionId;
			cadencePendingAction = decision.action;
			LogCadencePolicyEvent(
				"authorized", decision, queueGeneration,
				decision.actionId);
		}
	}

	// The caller holds renderMutex. A failed native outcome restores the phase
	// reservation exactly once and clears the diagnostic verification link.
	void RecordCadenceNativeOutcome(
		const AlphaCadenceCorrectionDecision& decision,
		uint64_t queueGeneration,
		const char* outcome,
		bool succeeded,
		const char* ownership,
		size_t nativeQueueDepth,
		uint64_t actionCount,
		const char* detail = "none")
	{
		if (!succeeded)
		{
			cadenceCorrectionPolicy.CancelPendingAction();
			if (cadencePendingActionId == decision.actionId)
			{
				cadencePendingActionId = 0;
				cadencePendingAction = AlphaCadenceAction::None;
			}
		}
		DebugLog::Log(
			"Alpha cadence native: action_id=%llu action=%s outcome=%s success=%d policy_generation=%llu detector_generation=%llu queue_generation=%llu source=%llu present_id=%u queue=%zu/%zu native_queue_depth=%zu debt=%llu counter=%llu rollback=%d ownership=%s detail=%s",
			static_cast<unsigned long long>(decision.actionId),
			AlphaCadenceActionText(decision.action),
			outcome,
			succeeded ? 1 : 0,
			static_cast<unsigned long long>(
				decision.diagnostic.policyGeneration),
			static_cast<unsigned long long>(
				decision.diagnostic.detectorGeneration),
			static_cast<unsigned long long>(queueGeneration),
			static_cast<unsigned long long>(
				decision.diagnostic.sourceSequence),
			decision.diagnostic.lastPresentId,
			decision.diagnostic.queueDepth,
			decision.diagnostic.desiredQueueDepth,
			nativeQueueDepth,
			static_cast<unsigned long long>(
				decision.diagnostic.presentationDebt),
			static_cast<unsigned long long>(actionCount),
			succeeded ? 0 : 1,
			ownership,
			detail && *detail ? detail : "none");
	}

	// The caller holds renderMutex. A repeat becomes a counted native action
	// only when the retained frame completes its second render.
	void RecordCadenceRepeatConsumption(
		uint64_t actionId,
		uint64_t policyGeneration,
		uint64_t detectorGeneration,
		uint64_t queueGeneration,
		uint64_t sourceSequence,
		uint32_t presentId,
		uint64_t presentationDebt,
		double deadlineSeconds,
		bool succeeded,
		size_t queueDepth,
		uint64_t actionCount)
	{
		if (!succeeded)
		{
			cadenceCorrectionPolicy.CancelPendingAction();
			if (cadencePendingActionId == actionId)
			{
				cadencePendingActionId = 0;
				cadencePendingAction = AlphaCadenceAction::None;
			}
		}
		DebugLog::Log(
			"Alpha cadence native: action_id=%llu action=repeat outcome=%s success=%d policy_generation=%llu detector_generation=%llu queue_generation=%llu source=%llu present_id=%u deadline_s=%+.6f native_queue_depth=%zu debt=%llu counter=%llu rollback=%d ownership=%s",
			static_cast<unsigned long long>(actionId),
			succeeded ? "repeat_consumed" :
				"repeat_consume_render_failed",
			succeeded ? 1 : 0,
			static_cast<unsigned long long>(policyGeneration),
			static_cast<unsigned long long>(detectorGeneration),
			static_cast<unsigned long long>(queueGeneration),
			static_cast<unsigned long long>(sourceSequence),
			presentId,
			deadlineSeconds,
			queueDepth,
			static_cast<unsigned long long>(presentationDebt),
			static_cast<unsigned long long>(actionCount),
			succeeded ? 0 : 1,
			succeeded ? "released_after_second_present" :
				"caller_release_pending");
	}

	void ResetTimingAfterBacklogRecovery(uint64_t queueGeneration)
	{
		cadenceCorrectionPolicy.Reset(queueGeneration);
		presentationTelemetry.Reset(queueGeneration);
		cadencePendingActionId = 0;
		cadencePendingAction = AlphaCadenceAction::None;
		cadenceLogInitialized = false;
		cadenceLoggedDue = false;
		cadenceLoggedBlockReason = AlphaCadenceBlockReason::None;
		cadenceNextDueSummaryTick = 0;
	}

	bool RetireOutput()
	{
		if (!outputResourcesRetired)
		{
			for (std::thread& worker : captureWorkers)
				if (worker.joinable()) worker.join();
			captureWorkers.clear();
			pl_mpv_user_shader_destroy(&nlsHook);
			pl_renderer_destroy(&renderer);
			pl_lut_free(&displayLut);
			if (d3d11)
			{
				pl_tex_destroy(d3d11->gpu, &statsOverlayTexture);
				pl_tex_destroy(d3d11->gpu, &sweepOverlayTexture);
				for (pl_tex& texture : textures)
					pl_tex_destroy(d3d11->gpu, &texture);
			}
			pl_swapchain_destroy(&swapchain);
			vpOwnedSwapchain.Release();
			SaveShaderCache();
			pl_d3d11_destroy(&d3d11);
			pl_cache_destroy(&cache);
			pl_log_destroy(&log);
			outputResourcesRetired = true;
			DebugLog::Log(
				"VP Renderer retirement: swapchain and D3D resources released "
				"before display-global restoration");
		}

		const bool refreshRestored = displayRefreshRate.RestoreNow();
		if (!refreshRestored)
		{
			DebugLog::Log(
				"VP Renderer retirement: display refresh restoration is "
				"pending; NVIDIA restoration deferred");
			return false;
		}
		const bool nvidiaRestored = nvidiaBt2020Reporter.Restore();
		DebugLog::Log(
			"VP Renderer retirement: external state refresh=%s nvidia=%s",
			refreshRestored ? "restored" : "pending",
			nvidiaRestored ? "restored" : "pending");
		return nvidiaRestored;
	}

	~Impl()
	{
		if (!RetireOutput())
			DebugLog::Log(
				"VP Renderer retirement: final external-state restoration "
				"remains unverified");
	}

	bool outputResourcesRetired = false;

	bool PresentBlackFrame()
	{
		if (!d3d11 || !d3d11->gpu || !swapchain)
			return false;

		if (vpOwnedSwapchain)
		{
			CComPtr<ID3D11Texture2D> backbuffer;
			const HRESULT getBufferResult = vpOwnedSwapchain->GetBuffer(
				0, IID_PPV_ARGS(&backbuffer));
			if (FAILED(getBufferResult) || !backbuffer)
			{
				DebugLog::Log(
					"VP-owned DXGI terminal black rejected: generation=%llu stage=GetBuffer result=0x%08lX",
					static_cast<unsigned long long>(vpOwnedSwapchainGeneration),
					static_cast<unsigned long>(getBufferResult));
				return false;
			}
			pl_d3d11_wrap_params wrapParams{};
			wrapParams.tex = static_cast<ID3D11Resource*>(backbuffer.p);
			pl_tex targetTexture = pl_d3d11_wrap(d3d11->gpu, &wrapParams);
			if (!targetTexture || !targetTexture->params.renderable ||
				!targetTexture->params.blit_dst)
			{
				DebugLog::Log(
					"VP-owned DXGI terminal black rejected: generation=%llu stage=capability renderable=%d blit_dst=%d",
					static_cast<unsigned long long>(vpOwnedSwapchainGeneration),
					targetTexture && targetTexture->params.renderable ? 1 : 0,
					targetTexture && targetTexture->params.blit_dst ? 1 : 0);
				pl_tex_destroy(d3d11->gpu, &targetTexture);
				return false;
			}
			pl_swapchain_frame frame{};
			frame.fbo = targetTexture;
			pl_frame target{};
			pl_frame_from_swapchain(&target, &frame);
			const float black[] = { 0.0f, 0.0f, 0.0f };
			pl_frame_clear(d3d11->gpu, &target, black);
			pl_gpu_flush(d3d11->gpu);
			pl_tex_destroy(d3d11->gpu, &targetTexture);
			backbuffer.Release();
			const HRESULT presentResult = vpOwnedSwapchain->Present(1, 0);
			const bool presented = SUCCEEDED(presentResult);
			DebugLog::Log(
				"VP-owned DXGI terminal black: generation=%llu clear=whole_surface present=%d result=0x%08lX",
				static_cast<unsigned long long>(vpOwnedSwapchainGeneration),
				presented ? 1 : 0, static_cast<unsigned long>(presentResult));
			return presented;
		}

		struct pl_swapchain_frame swapchainFrame{};
		if (!pl_swapchain_start_frame(swapchain, &swapchainFrame))
			return false;

		struct pl_frame target{};
		pl_frame_from_swapchain(&target, &swapchainFrame);
		const float black[] = { 0.0f, 0.0f, 0.0f };
		pl_frame_clear(d3d11->gpu, &target, black);
		if (!pl_swapchain_submit_frame(swapchain))
			return false;

		pl_swapchain_swap_buffers(swapchain);
		return true;
	}

	void LoadShaderCache()
	{
		shaderCachePath = ShaderCachePath();
		const std::string clearRequestPath = ShaderCacheClearRequestPath();
		if (GetFileAttributesA(clearRequestPath.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			DeleteFileA(shaderCachePath.c_str());
			DeleteFileA((shaderCachePath + ".tmp").c_str());
			DeleteFileA(clearRequestPath.c_str());
			pl_cache_reset(cache);
			loadedShaderCacheSignature = 0;
			DebugLog::Log(
				"libplacebo persistent shader cache cleared by user request");
			return;
		}
		std::ifstream input(
			shaderCachePath,
			std::ios::binary | std::ios::ate);
		if (!input.is_open())
		{
			DebugLog::Log(
				"libplacebo persistent shader cache: no existing file at %s",
				shaderCachePath.c_str());
			return;
		}

		const std::streamoff length = input.tellg();
		if (length <= 0 ||
			static_cast<uint64_t>(length) > MAX_SHADER_CACHE_FILE_SIZE)
		{
			DebugLog::Log(
				"libplacebo persistent shader cache ignored: invalid size %lld bytes",
				static_cast<long long>(length));
			return;
		}

		std::vector<uint8_t> data(static_cast<size_t>(length));
		input.seekg(0, std::ios::beg);
		if (!input.read(
				reinterpret_cast<char*>(data.data()),
				static_cast<std::streamsize>(data.size())))
		{
			DebugLog::Log(
				"libplacebo persistent shader cache ignored: read failed");
			return;
		}

		const int loaded = pl_cache_load(cache, data.data(), data.size());
		if (loaded < 0)
		{
			DebugLog::Log(
				"libplacebo persistent shader cache ignored: corrupt or incompatible file");
			pl_cache_reset(cache);
			return;
		}

		loadedShaderCacheSignature = pl_cache_signature(cache);
		DebugLog::Log(
			"libplacebo persistent shader cache loaded: %d objects, %zu bytes",
			loaded,
			data.size());
	}

	void SaveShaderCache()
	{
		if (shaderCachePath.empty())
			shaderCachePath = ShaderCachePath();
		const std::string clearRequestPath = ShaderCacheClearRequestPath();
		if (GetFileAttributesA(clearRequestPath.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			DeleteFileA(shaderCachePath.c_str());
			DeleteFileA((shaderCachePath + ".tmp").c_str());
			DeleteFileA(clearRequestPath.c_str());
			if (cache)
				pl_cache_reset(cache);
			loadedShaderCacheSignature = 0;
			DebugLog::Log(
				"libplacebo persistent shader cache clear completed; current cache was not saved");
			return;
		}
		if (!shaderCacheEnabled ||
			!cache || shaderCachePath.empty() || pl_cache_objects(cache) <= 0)
			return;

		const uint64_t signature = pl_cache_signature(cache);
		if (signature == loadedShaderCacheSignature)
			return;

		const size_t required = pl_cache_save(cache, nullptr, 0);
		if (required == 0 || required > MAX_SHADER_CACHE_FILE_SIZE)
		{
			DebugLog::Log(
				"libplacebo persistent shader cache not saved: invalid size %zu bytes",
				required);
			return;
		}

		std::vector<uint8_t> data(required);
		const size_t written =
			pl_cache_save(cache, data.data(), data.size());
		if (written != required)
		{
			DebugLog::Log(
				"libplacebo persistent shader cache not saved: serialization failed");
			return;
		}

		const std::string temporaryPath = shaderCachePath + ".tmp";
		{
			std::ofstream output(
				temporaryPath,
				std::ios::binary | std::ios::trunc);
			if (!output.is_open() ||
				!output.write(
					reinterpret_cast<const char*>(data.data()),
					static_cast<std::streamsize>(data.size())))
			{
				DebugLog::Log(
					"libplacebo persistent shader cache not saved: write failed");
				return;
			}
		}

		if (!MoveFileExA(
			temporaryPath.c_str(),
			shaderCachePath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			DeleteFileA(temporaryPath.c_str());
			DebugLog::Log(
				"libplacebo persistent shader cache not saved: atomic replace failed (%lu)",
				GetLastError());
			return;
		}

		loadedShaderCacheSignature = signature;
		DebugLog::Log(
			"libplacebo persistent shader cache saved: %d objects, %zu bytes",
			pl_cache_objects(cache),
			data.size());
	}

	void LogFormatterInputDiagnostics(
		const uint16_t* luma, const uint16_t* chroma, int width, int height,
		int chromaHeight, const VideoFrameFormatterOutputContract& contract) const
	{
		uint16_t minimumLuma = std::numeric_limits<uint16_t>::max();
		uint16_t maximumLuma = 0;
		uint16_t minimumChroma = std::numeric_limits<uint16_t>::max();
		uint16_t maximumChroma = 0;
		uint64_t lumaSamples = 0;
		uint64_t chromaSamples = 0;
		for (int y = 0; y < height; ++y)
		{
			const uint16_t* const row = luma + static_cast<size_t>(y) * width;
			for (int x = 0; x < width; ++x)
			{
				const uint16_t sample = static_cast<uint16_t>(row[x] >> contract.bitShift);
				minimumLuma = std::min(minimumLuma, sample);
				maximumLuma = std::max(maximumLuma, sample);
				++lumaSamples;
			}
		}
		for (int y = 0; y < chromaHeight; ++y)
		{
			const uint16_t* const row = chroma + static_cast<size_t>(y) * width;
			for (int x = 0; x < width; ++x)
			{
				const uint16_t sample = static_cast<uint16_t>(row[x] >> contract.bitShift);
				minimumChroma = std::min(minimumChroma, sample);
				maximumChroma = std::max(maximumChroma, sample);
				++chromaSamples;
			}
		}
		DebugLog::Log(
			"Alpha formatter input diagnostic: luma=%u..%u (%llu samples) chroma=%u..%u (%llu samples)",
			minimumLuma, maximumLuma,
			static_cast<unsigned long long>(lumaSamples),
			minimumChroma, maximumChroma,
			static_cast<unsigned long long>(chromaSamples));
	}

	bool ReadR10Texture(ID3D11Texture2D* authoritativeBackbuffer,
		std::vector<uint32_t>& pixels, D3D11_TEXTURE2D_DESC& desc,
		std::string& error)
	{
		CComPtr<ID3D11Texture2D> backBuffer = authoritativeBackbuffer;
		if (!backBuffer)
		{
			CComPtr<IDXGISwapChain> nativeSwapchain;
			nativeSwapchain.Attach(pl_d3d11_swapchain_unwrap(swapchain));
			if (!nativeSwapchain)
			{
				error = "cannot unwrap libplacebo swapchain";
				return false;
			}
			const HRESULT getBuffer = nativeSwapchain->GetBuffer(
				0, IID_PPV_ARGS(&backBuffer));
			if (FAILED(getBuffer) || !backBuffer)
			{
				char message[64] = {};
				sprintf_s(message, "GetBuffer=0x%08lX",
					static_cast<unsigned long>(getBuffer));
				error = message;
				return false;
			}
		}
		backBuffer->GetDesc(&desc);
		if (desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM)
		{
			error = "authoritative backbuffer is not R10G10B10A2_UNORM";
			return false;
		}

		D3D11_TEXTURE2D_DESC stagingDesc = desc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;
		CComPtr<ID3D11Texture2D> staging;
		HRESULT result = d3d11->device->CreateTexture2D(
			&stagingDesc, nullptr, &staging);
		if (FAILED(result) || !staging)
		{
			char message[64] = {};
			sprintf_s(message, "CreateTexture2D=0x%08lX",
				static_cast<unsigned long>(result));
			error = message;
			return false;
		}

		CComPtr<ID3D11DeviceContext> context;
		d3d11->device->GetImmediateContext(&context);
		context->CopyResource(staging, backBuffer);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		result = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(result))
		{
			char message[64] = {};
			sprintf_s(message, "Map=0x%08lX",
				static_cast<unsigned long>(result));
			error = message;
			return false;
		}
		pixels.resize(static_cast<size_t>(desc.Width) * desc.Height);
		for (unsigned int y = 0; y < desc.Height; ++y)
			memcpy(pixels.data() + static_cast<size_t>(y) * desc.Width,
				static_cast<const uint8_t*>(mapped.pData) +
					static_cast<size_t>(y) * mapped.RowPitch,
				static_cast<size_t>(desc.Width) * sizeof(uint32_t));
		context->Unmap(staging, 0);
		return true;
	}

	void LogOutputReadback(ID3D11Texture2D* authoritativeBackbuffer,
		const char* owner)
	{
		using namespace LibplaceboOutput;
		++diagnosticReadbackAttempts;
		std::vector<uint32_t> pixels;
		D3D11_TEXTURE2D_DESC desc{};
		std::string error;
		if (!ReadR10Texture(authoritativeBackbuffer, pixels, desc, error))
		{
			DebugLog::Log(
				"libplacebo output diagnostic readback failed: owner=%s reason=%s",
				owner, error.c_str());
			return;
		}

		const PackedR10Stats stats = AnalyzePackedR10(
			pixels.data(), desc.Width,
			desc.Width,
			desc.Height,
			8);
		const bool allZero = stats.sampledPixels > 0 &&
			stats.maximum[0] == 0 && stats.maximum[1] == 0 &&
			stats.maximum[2] == 0;
		if (allZero && diagnosticReadbackAttempts < 10)
		{
			DebugLog::Log(
				"libplacebo output diagnostic readback: status=not-ready all_zero=1 attempt=%u/10; retrying after GPU flush",
				diagnosticReadbackAttempts);
			return;
		}
		diagnosticReadbackComplete = true;
		diagnosticReadbackNonBlack = !allZero;
		const double divisor =
			stats.sampledPixels > 0 ? static_cast<double>(stats.sampledPixels) : 1.0;
		DebugLog::Log(
			"libplacebo output diagnostic readback: source=authoritative-present-backbuffer owner=%s format=R10G10B10A2 size=%ux%u sampled=%llu step=8 encoding=%s transfer=%s min_rgb=%u/%u/%u max_rgb=%u/%u/%u mean_rgb=%.2f/%.2f/%.2f channels_below_64=%llu channels_above_940=%llu near_black_codes=0:%llu,1-3:%llu,4-15:%llu,16-31:%llu,32-63:%llu,64-79:%llu,80-127:%llu,128+:%llu cache=%s objects=%d signature=%016llX",
			owner,
			desc.Width,
			desc.Height,
			static_cast<unsigned long long>(stats.sampledPixels),
			ToString(actualOutput.encoding),
			LibplaceboOutput::ToString(actualOutput.targetTransfer),
			stats.minimum[0],
			stats.minimum[1],
			stats.minimum[2],
			stats.maximum[0],
			stats.maximum[1],
			stats.maximum[2],
			stats.sum[0] / divisor,
			stats.sum[1] / divisor,
			stats.sum[2] / divisor,
			static_cast<unsigned long long>(stats.channelsBelowStudioBlack),
			static_cast<unsigned long long>(stats.channelsAboveStudioWhite),
			static_cast<unsigned long long>(stats.nearBlackBuckets[0]),
			static_cast<unsigned long long>(stats.nearBlackBuckets[1]),
			static_cast<unsigned long long>(stats.nearBlackBuckets[2]),
			static_cast<unsigned long long>(stats.nearBlackBuckets[3]),
			static_cast<unsigned long long>(stats.nearBlackBuckets[4]),
			static_cast<unsigned long long>(stats.nearBlackBuckets[5]),
			static_cast<unsigned long long>(stats.nearBlackBuckets[6]),
			static_cast<unsigned long long>(stats.nearBlackBuckets[7]),
			shaderCacheEnabled ? "enabled" : "disabled",
			cache ? pl_cache_objects(cache) : 0,
			static_cast<unsigned long long>(cache ? pl_cache_signature(cache) : 0));
	}

	void CaptureRenderedOutput(ID3D11Texture2D* authoritativeBackbuffer,
		const char* owner, uint64_t frameGeneration, uint64_t sourceSequence,
		const struct pl_frame& sourceImage)
	{
		std::vector<uint32_t> pixels;
		D3D11_TEXTURE2D_DESC desc{};
		std::string error;
		if (!ReadR10Texture(authoritativeBackbuffer, pixels, desc, error))
		{
			DebugLog::Log(
				"Rendered-output capture failed: stage=readback owner=%s source=%llu reason=%s",
				owner, static_cast<unsigned long long>(sourceSequence), error.c_str());
			return;
		}
		const LibplaceboOutput::PackedR10Stats stats =
			LibplaceboOutput::AnalyzePackedR10(
				pixels.data(), desc.Width, desc.Width, desc.Height, 1);
		const std::wstring directory = ScreenshotDirectory();
		const std::wstring stem = CaptureTimestampStem(sourceSequence);
		uint64_t contentSignature = 1469598103934665603ull;
		for (const uint32_t pixel : pixels)
		{
			for (unsigned int byte = 0; byte < sizeof(pixel); ++byte)
			{
				contentSignature ^= static_cast<uint8_t>(pixel >> (byte * 8));
				contentSignature *= 1099511628211ull;
			}
		}
		const std::string rawSha256 = Sha256Hex(pixels.data(),
			pixels.size() * sizeof(uint32_t));
		const std::wstring pngPath = directory + L"\\" + stem + L".png";
		const std::wstring jsonPath = directory + L"\\" + stem + L".json";
		std::ostringstream metadata;
		metadata.imbue(std::locale::classic());
		metadata << std::setprecision(10)
			<< "{\n  \"schema\": \"vp-rendered-output-capture-v1\",\n"
			<< "  \"capture_id\": \"" << JsonEscape(WideToUtf8(stem)) << "\",\n"
			<< "  \"source_sequence\": " << sourceSequence << ",\n"
			<< "  \"renderer_generation\": " << frameGeneration << ",\n"
			<< "  \"presenter_owner\": \"" << JsonEscape(owner) << "\",\n"
			<< "  \"swapchain_generation\": " << vpOwnedSwapchainGeneration << ",\n"
			<< "  \"display_device\": \"" << JsonEscape(
				WideToUtf8(negotiatedDisplayDeviceName)) << "\",\n"
			<< "  \"authoritative_stage\": \"after-render-before-present\",\n"
			<< "  \"pixel_format\": \"R10G10B10A2_UNORM\",\n"
			<< "  \"png_mapping\": \"R10 code c stored as 16-bit (c<<6)|(c>>4); recover with value>>6\",\n"
			<< "  \"width\": " << desc.Width << ",\n"
			<< "  \"height\": " << desc.Height << ",\n"
			<< "  \"content_signature_fnv1a64\": \"" << std::hex <<
				std::setw(16) << std::setfill('0') << contentSignature << std::dec <<
				"\",\n"
			<< "  \"raw_r10_sha256\": \"" << rawSha256 << "\",\n"
			<< "  \"ingress\": \"" << JsonEscape(ingressStatus) << "\",\n"
			<< "  \"sdr_adjust_gamma_requested\": \"" <<
				LibplaceboOutput::ToString(lastSdrGammaDecision.requested) << "\",\n"
			<< "  \"sdr_gamma_action\": \"" <<
				LibplaceboOutput::ToString(lastSdrGammaDecision.action) << "\",\n"
			<< "  \"sdr_gamma_applicable\": " <<
				(lastSdrGammaDecision.action ==
					LibplaceboOutput::SdrGammaAction::NOT_APPLICABLE ? "false" : "true") << ",\n"
			<< "  \"sdr_gamma_reason\": \"" << JsonEscape(
				lastSdrGammaDecision.reason) << "\",\n"
			<< "  \"sdr_input_transfer_configured\": \"" << JsonEscape(
				activeSettings.sdrInputTransfer) << "\",\n"
			<< "  \"source_transfer_declared\": \"" <<
				LibplaceboOutput::ToString(lastSdrGammaDecision.declaredSource) << "\",\n"
			<< "  \"source_transfer_effective\": \"" <<
				LibplaceboOutput::ToString(lastSdrGammaDecision.effectiveSource) << "\",\n"
			<< "  \"target_transfer_effective\": \"" <<
				LibplaceboOutput::ToString(lastSdrGammaDecision.actualTarget) << "\",\n"
			<< "  \"transfer_only_caveats\": \"YUV/RGB matrix, range, primaries, scaling, LUT, shaders, dithering, quantization, and display response may still alter output\",\n"
			<< "  \"source_levels\": \"" <<
				(sourceImage.repr.levels == PL_COLOR_LEVELS_LIMITED ?
					"limited" : "full") << "\",\n"
			<< "  \"source_transfer\": \"" << JsonEscape(
				pl_color_transfer_name(sourceImage.color.transfer)) << "\",\n"
			<< "  \"source_primaries\": \"" << JsonEscape(
				pl_color_primaries_name(sourceImage.color.primaries)) << "\",\n"
			<< "  \"source_eotf\": \"" << JsonEscape(
				CStringA(ToString(lastRenderedEotf)).GetString()) << "\",\n"
			<< "  \"source_colorspace\": \"" << JsonEscape(
				CStringA(ToString(lastRenderedColorspace)).GetString()) << "\",\n"
			<< "  \"requested_presentation\": \"" <<
				JsonEscape(activeSettings.outputPresentation) << "\",\n"
			<< "  \"requested_range\": \"" <<
				JsonEscape(activeSettings.outputRange) << "\",\n"
			<< "  \"requested_gamma\": \"" <<
				JsonEscape(activeSettings.outputGamma) << "\",\n"
			<< "  \"actual_presentation\": \"" <<
				LibplaceboOutput::ToString(actualOutput.presentationModel) << "\",\n"
			<< "  \"actual_dxgi_encoding\": \"" <<
				LibplaceboOutput::ToString(actualOutput.encoding) << "\",\n"
			<< "  \"actual_pixel_transfer\": \"" <<
				LibplaceboOutput::ToString(actualOutput.targetTransfer) << "\",\n"
			<< "  \"target_primaries\": \"" <<
				(targetBt2020 ? "BT.2020" : "Rec.709") << "\",\n"
			<< "  \"report_bt2020_to_display\": " <<
				(reportBt2020ToDisplay ? "true" : "false") << ",\n"
			<< "  \"dxgi_applied_encoding\": \"" <<
				JsonEscape(vpOwnedAppliedEncoding) << "\",\n"
			<< "  \"dxgi_set_result\": " <<
				static_cast<long>(vpOwnedColorSpaceResult) << ",\n"
			<< "  \"dxgi_postcheck_verified\": " <<
				(vpOwnedColorSpaceVerified ? "true" : "false") << ",\n"
			<< "  \"requested_encoding_active\": " <<
				(actualOutput.requestedEncodingActive ? "true" : "false") << ",\n"
			<< "  \"safe_to_render\": " <<
				(actualOutput.safeToRender ? "true" : "false") << ",\n"
			<< "  \"output_reason\": \"" <<
				JsonEscape(actualOutput.reason) << "\",\n"
			<< "  \"target_nits\": " << sdrTargetNits << ",\n"
			<< "  \"black_nits\": " << sdrBlackNits << ",\n"
			<< "  \"tone_mapping\": \"" <<
				JsonEscape(activeSettings.toneMapping) << "\",\n"
			<< "  \"gamut_mapping\": \"" <<
				JsonEscape(activeSettings.gamutMapping) << "\",\n"
			<< "  \"min_rgb_code\": [" << stats.minimum[0] << ", " <<
				stats.minimum[1] << ", " << stats.minimum[2] << "],\n"
			<< "  \"max_rgb_code\": [" << stats.maximum[0] << ", " <<
				stats.maximum[1] << ", " << stats.maximum[2] << "],\n"
			<< "  \"channels_below_code_64\": " <<
				stats.channelsBelowStudioBlack << ",\n"
			<< "  \"channels_above_code_940\": " <<
				stats.channelsAboveStudioWhite << ",\n"
			<< "  \"near_black_channel_buckets\": {\"0\": " <<
				stats.nearBlackBuckets[0] << ", \"1-3\": " <<
				stats.nearBlackBuckets[1] << ", \"4-15\": " <<
				stats.nearBlackBuckets[2] << ", \"16-31\": " <<
				stats.nearBlackBuckets[3] << ", \"32-63\": " <<
				stats.nearBlackBuckets[4] << ", \"64-79\": " <<
				stats.nearBlackBuckets[5] << ", \"80-127\": " <<
				stats.nearBlackBuckets[6] << ", \"128+\": " <<
				stats.nearBlackBuckets[7] << "}\n}\n";
		const SteadyClock::time_point captureStarted = SteadyClock::now();
		captureWorkers.emplace_back([
			pixels = std::move(pixels), width = desc.Width, height = desc.Height,
			pngPath, jsonPath, directory, json = metadata.str(), sourceSequence,
			captureStarted]()
		{
			if (!CreateDirectoryW(directory.c_str(), nullptr) &&
				GetLastError() != ERROR_ALREADY_EXISTS)
			{
				DebugLog::Log(
					"Rendered-output capture failed: stage=create-directory source=%llu error=%lu",
					static_cast<unsigned long long>(sourceSequence),
					static_cast<unsigned long>(GetLastError()));
				return;
			}
			std::string writeError;
			if (!WriteR10Png16(pngPath, pixels, width, height, writeError))
			{
				DebugLog::Log(
					"Rendered-output capture failed: stage=png source=%llu reason=%s path=%ls",
					static_cast<unsigned long long>(sourceSequence),
					writeError.c_str(), pngPath.c_str());
				return;
			}
			std::ofstream sidecar(jsonPath, std::ios::binary | std::ios::trunc);
			if (!sidecar || !sidecar.write(json.data(),
				static_cast<std::streamsize>(json.size())))
			{
				DebugLog::Log(
					"Rendered-output capture failed: stage=json source=%llu path=%ls",
					static_cast<unsigned long long>(sourceSequence), jsonPath.c_str());
				return;
			}
			DebugLog::Log(
				"Rendered-output capture complete: source=%llu png=%ls metadata=%ls raw_bytes=%llu elapsed_ms=%.2f",
				static_cast<unsigned long long>(sourceSequence), pngPath.c_str(),
				jsonPath.c_str(),
				static_cast<unsigned long long>(pixels.size() * sizeof(uint32_t)),
				std::chrono::duration<double, std::milli>(
					SteadyClock::now() - captureStarted).count());
		});
	}

	void ConfigureRenderParams(const RendererSettings& settings)
	{
		LibplaceboRenderParameters::Settings parameterSettings;
		parameterSettings.quality = settings.quality;
		parameterSettings.toneMapping = settings.toneMapping;
		parameterSettings.gamutMapping = settings.gamutMapping;
		parameterSettings.peakDetection =
			static_cast<LibplaceboRenderParameters::Toggle>(settings.peakDetection);
		parameterSettings.hasContrastRecovery = settings.hasContrastRecovery;
		parameterSettings.contrastRecovery = settings.contrastRecovery;
		parameterSettings.upscaler = settings.upscaler;
		parameterSettings.downscaler = settings.downscaler;
		parameterSettings.debandStrength = settings.debandStrength;
		parameterSettings.deband =
			static_cast<LibplaceboRenderParameters::Toggle>(settings.deband);
		parameterSettings.sigmoid =
			static_cast<LibplaceboRenderParameters::Toggle>(settings.sigmoid);
		parameterSettings.dithering =
			static_cast<LibplaceboRenderParameters::Toggle>(settings.dithering);

		LibplaceboRenderParameters::Projection projection;
		std::string projectionError;
		if (!LibplaceboRenderParameters::Build(parameterSettings,
			displayLutParsed && displayLut, projection, projectionError))
		{
			throw std::runtime_error(
				"could not configure libplacebo render parameters: " + projectionError);
		}
		if (displayLutParsed && displayLut && projection.renderParams.error_diffusion == nullptr)
		{
			DebugLog::Log(
				"display: disabled error-diffusion dithering while a 3D LUT is active; using the compatible target-LUT render path");
		}

		renderParams = projection.renderParams;
		colorMapParams = projection.colorMapParams;
		peakDetectParams = projection.peakDetectParams;
		sigmoidParams = projection.sigmoidParams;
		debandParams = projection.debandParams;
		ditherParams = projection.ditherParams;
		renderParams.color_map_params = &colorMapParams;
		renderParams.peak_detect_params =
			projection.renderParams.peak_detect_params ? &peakDetectParams : nullptr;
		renderParams.sigmoid_params =
			projection.renderParams.sigmoid_params ? &sigmoidParams : nullptr;
		renderParams.deband_params =
			projection.renderParams.deband_params ? &debandParams : nullptr;
		renderParams.dither_params =
			projection.renderParams.dither_params ? &ditherParams : nullptr;

		DebugLog::Log(
			"libplacebo settings: quality=%s tone_mapping=%s gamut_mapping=%s peak_detection=%s contrast_recovery=%.2f upscaler=%s downscaler=%s deband=%s dithering=%s display_bit_depth=%s output_presentation=%s output_range=%s output_transport_gamma=%s output_gamma=%s sdr_input_transfer=%s sdr_adjust_gamma=%s target=%.1f nits black=%.3f profile_update_mode=%s output_diagnostics=%d diagnostic_disable_shader_cache=%d diagnostic_disable_compute=%d diagnostic_force_8bit_sdr_swapchain=%d diagnostic_allow_limited_g22=%d diagnostic_allow_full_g22=%d diagnostic_vp_owned_dxgi_presenter=%d refresh_switch=%d refresh_command_delay=%llus refresh_commands=%u viewport_target=%s screen_aspect=%.4f automatic_crop=%d subtitle_fit=%d subtitle_hold=%llums subtitle_engage_drift=%llums subtitle_release_drift=%llums subtitle_padding=%dpx subtitle_target_buffer=%dpx",
			settings.quality.c_str(),
			colorMapParams.tone_mapping_function
				? colorMapParams.tone_mapping_function->name : "none",
			colorMapParams.gamut_mapping ? colorMapParams.gamut_mapping->name : "none",
			renderParams.peak_detect_params ? "on" : "off",
			colorMapParams.contrast_recovery,
			renderParams.upscaler && renderParams.upscaler->name
				? renderParams.upscaler->name : "built-in",
			renderParams.downscaler && renderParams.downscaler->name
				? renderParams.downscaler->name : "built-in",
			settings.debandStrength == "auto" ?
				(renderParams.deband_params ? "auto/on" : "auto/off") :
				settings.debandStrength.c_str(),
			renderParams.error_diffusion ? "auto/error-diffusion" :
				(renderParams.dither_params ? "on" : "off"),
			settings.displayBitDepth.c_str(),
			settings.outputPresentation.c_str(),
			settings.outputRange.c_str(),
			settings.outputTransportGamma.c_str(),
			settings.outputGamma.c_str(),
			settings.sdrInputTransfer.c_str(),
			settings.sdrAdjustGamma.c_str(),
			sdrTargetNits,
			sdrBlackNits,
			ProfileUpdateModeName(settings.profileUpdateMode),
			settings.outputDiagnostics ? 1 : 0,
			settings.diagnosticDisableShaderCache ? 1 : 0,
			settings.diagnosticDisableCompute ? 1 : 0,
			settings.diagnosticForce8BitSdrSwapchain ? 1 : 0,
			settings.diagnosticAllowLimitedG22 ? 1 : 0,
			settings.diagnosticAllowFullG22 ? 1 : 0,
			settings.diagnosticVpOwnedDxgiPresenter ? 1 : 0,
			settings.switchRefreshRate ? 1 : 0,
			static_cast<unsigned long long>(settings.refreshRateCommandDelayMs / 1000),
			static_cast<unsigned int>(settings.refreshRateCommandRules.size()),
			configuredScreenTarget ? "configured" : "output-panel",
			configuredScreenTarget ? configuredScreenAspect : 0.0,
			automaticSourceCrop ? 1 : 0,
			scopeSubtitleFit ? 1 : 0,
			static_cast<unsigned long long>(scopeSubtitleHoldMs),
			static_cast<unsigned long long>(scopeSubtitleEngageDriftMs),
			static_cast<unsigned long long>(scopeSubtitleReleaseDriftMs),
			scopeSubtitlePaddingPixels,
			scopeSubtitleTargetBufferPixels);
	}

	void ClearScopePresentationEvidence()
	{
		scopePresentationEvidenceBase = {};
		scopePresentationEvidenceBounds = {};
		scopePresentationCurrentBounds = {};
		scopePresentationEvidenceLastTick = 0;
		scopePresentationEvidenceSourceGeneration = 0;
		scopePresentationEvidenceSourceSequence = 0;
		scopePresentationCurrentSourceGeneration = 0;
		scopePresentationCurrentSourceSequence = 0;
	}

	void ClearScopeSubtitleEvidence()
	{
		scopeSubtitleAnalysisFrame = 0;
		scopeVerticalBarPresentation = {};
		scopeSubtitleTranslationConfirmation = {};
		scopeSubtitleFitConfirmation = {};
		scopeSubtitleDrift.Reset();
		scopeSubtitleDriftWasActive = false;
		scopeSubtitleAuthorityGapHeld = false;
		scopeSubtitleWasActive = false;
		scopeSubtitleWasTopActive = false;
		lastScopeVerticalOverlayPolicy.clear();
		scopeSubtitlePictureLeft = 0;
		scopeSubtitlePictureTop = 0;
		scopeSubtitlePictureRight = 0;
		scopeSubtitlePictureBottom = 0;
		scopeSubtitleDetectedLeft = 0;
		scopeSubtitleDetectedRight = 0;
		scopeSubtitleLeftLastDetectionTick = 0;
		scopeSubtitleRightLastDetectionTick = 0;
		scopeSubtitleEvidenceSourceGeneration = 0;
	}

	float UpdateScopeSubtitleShift(
		const AnalysisLumaSource* source,
		int width,
		int height,
		bool configuredScreenActive,
		const ActivePictureBounds* barAuthority,
		uint64_t sourceSequence,
		bool forceAnalysis = false,
		bool heldBarAnalysisAuthority = false)
	{
		const uint64_t now = GetTickCount64();
		const bool sourceIsCurrent = source && source->IsValid() &&
			source->width == width && source->height == height &&
			width >= 320 && height >= 180;
		const bool authorityIsCurrentBars =
			barAuthority &&
			barAuthority->rasterWidth == width &&
			barAuthority->rasterHeight == height &&
			barAuthority->left >= 0 && barAuthority->right <= width &&
			barAuthority->top >= 0 && barAuthority->bottom <= height &&
			barAuthority->left < barAuthority->right &&
			barAuthority->top < barAuthority->bottom &&
			(barAuthority->left > 0 || barAuthority->top > 0 ||
			 barAuthority->right < width || barAuthority->bottom < height);
		if (!configuredScreenActive || !sourceIsCurrent)
		{
			// Receiver controls and ordinary picture detail are in-picture UI unless
			// shared authority proves one or more encoded bars around the picture.
			ClearScopeSubtitleEvidence();
			return 0.0f;
		}
		const bool retainAcrossAuthorityGap =
			AlphaSourceCrop::CanRetainVerticalBarPresentationAcrossAuthorityGap(
				scopeVerticalBarPresentation,
				scopeSubtitleEvidenceSourceGeneration, source->generation,
				now, scopeSubtitleHoldMs, sourceSequence);
		if (!authorityIsCurrentBars && !retainAcrossAuthorityGap)
		{
			// No current bar authority and no same-generation presentation action
			// left to release. Do not let an ordinary in-picture UI inherit stale
			// subtitle placement.
			if (scopeSubtitleAuthorityGapHeld)
			{
				DebugLog::Log(
					"libplacebo scope subtitle fit: provisional bar-authority hold released; source_generation=%llu evidence_generation=%llu",
					static_cast<unsigned long long>(source->generation),
					static_cast<unsigned long long>(
						scopeSubtitleEvidenceSourceGeneration));
			}
			ClearScopeSubtitleEvidence();
			return 0.0f;
		}
		if (heldBarAnalysisAuthority)
		{
			if (!scopeSubtitleAuthorityGapHeld)
			{
				DebugLog::Log(
					"libplacebo scope subtitle fit: continuing dense analysis on same-generation trusted base during provisional authority gap");
				scopeSubtitleAuthorityGapHeld = true;
			}
		}
		else if (!authorityIsCurrentBars)
		{
			if (!scopeSubtitleAuthorityGapHeld)
			{
				DebugLog::Log(
					"libplacebo scope subtitle fit: retaining same-generation action through provisional bar-authority gap; action=%d shift=%.1f px",
					static_cast<int>(scopeVerticalBarPresentation.action),
					scopeVerticalBarPresentation.translationPixels);
				scopeSubtitleAuthorityGapHeld = true;
			}
		}
		else
		{
			if (scopeSubtitleAuthorityGapHeld)
			{
				DebugLog::Log(
					"libplacebo scope subtitle fit: bar authority reacquired; resuming dense analysis");
			}
			scopeSubtitleAuthorityGapHeld = false;
		}
		if (authorityIsCurrentBars &&
			(scopeSubtitleEvidenceSourceGeneration != source->generation ||
			 scopeSubtitlePictureLeft != barAuthority->left ||
			 scopeSubtitlePictureTop != barAuthority->top ||
			 scopeSubtitlePictureRight != barAuthority->right ||
			 scopeSubtitlePictureBottom != barAuthority->bottom))
		{
			// Held bar content belongs to one exact authority rectangle and source.
			ClearScopeSubtitleEvidence();
			scopeSubtitleEvidenceSourceGeneration = source->generation;
			scopeSubtitlePictureLeft = barAuthority->left;
			scopeSubtitlePictureTop = barAuthority->top;
			scopeSubtitlePictureRight = barAuthority->right;
			scopeSubtitlePictureBottom = barAuthority->bottom;
		}

		// This inexpensive pass deliberately avoids OCR and models. Shared crop
		// authority supplies the bar edges; this code only finds meaningful
		// non-black content inside those already-proven encoded bars.
		// Sampling every third rendered frame keeps the 4K CPU cost negligible
		// while still reacting in roughly 50-125 ms.
		const bool subtitleAnalysisScheduled = authorityIsCurrentBars &&
			++scopeSubtitleAnalysisFrame % 3 == 0;
		if (authorityIsCurrentBars &&
			(forceAnalysis || subtitleAnalysisScheduled))
		{
			if (forceAnalysis && !subtitleAnalysisScheduled)
			{
				DebugLog::Log(
					"libplacebo scope subtitle fit: forcing immediate bar scan after retained crop became pixel-unsafe");
			}
			// This is intentionally fixed-capacity: no full-frame surface and no
			// recurring heap allocation is needed to establish the black floor.
			std::array<uint16_t, 8192> blackSamples{};
			size_t blackSampleCount = 0;
			auto lumaCode = [source](int x, int y)
			{
				AnalysisLumaSample sample;
				return source->Sample(x, y, sample) ?
					static_cast<int>(sample.luma) : 0;
			};
			auto appendBlackSample = [&blackSamples, &blackSampleCount](int value)
			{
				if (blackSampleCount < blackSamples.size())
					blackSamples[blackSampleCount++] = static_cast<uint16_t>(value);
			};
			const int sampleStep = std::max(2, width / 256);
			const int edgeWidth = std::max(16, width / 5);
			const int edgeRows = std::max(3, std::min(10, height / 60));
			for (int y = height - edgeRows; y < height; ++y)
			{
				for (int x = 0; x < edgeWidth; x += sampleStep)
					appendBlackSample(lumaCode(x, y));
				for (int x = width - edgeWidth; x < width; x += sampleStep)
					appendBlackSample(lumaCode(x, y));
			}

			if (blackSampleCount == 0)
				return 0.0f;
			const size_t median = blackSampleCount / 2;
			std::nth_element(
				blackSamples.begin(),
				blackSamples.begin() + median,
				blackSamples.begin() + blackSampleCount);
			const int blackCode = blackSamples[median];
			const int rowStep = std::max(1, height / 1080);

			// Subtitle fitting deliberately uses a simple, renderer-local rule:
			// meaningful non-black content inside a confirmed encoded black bar
			// must remain visible. It does not try to recognize glyphs, language,
			// alignment, or subtitle style.
			if ((scopeSubtitleFit || automaticSourceCrop) &&
				scopeSubtitlePictureTop > 0 &&
				scopeSubtitlePictureBottom < height)
			{
				const int x0 = width / 32;
				const int x1 = width - x0;
				const int xStep = std::max(1, width / 1920);
				const int contentLimit = std::min(1023, blackCode + 32);
				const int sampledColumns = std::max(1, (x1 - x0) / xStep);
				// A few bright pixels are normally edge noise, receiver overlays, or
				// compression shimmer. Captions occupy a meaningful central horizontal
				// span even when the text itself is short, so require that before a bar
				// can move the picture.
				const int minimumContentSamples =
					std::max(8, sampledColumns / 192);
				const int minimumContentSpan = std::max(24, width / 48);
				const int minimumContentRows = 2;
				const int barInset = std::max(1, rowStep);

				auto findBarContent =
					[&](int searchTop, int searchBottom,
						int& detectedTop, int& detectedBottom,
						int& maximumContentSamples)
					{
						detectedTop = height;
						detectedBottom = 0;
						maximumContentSamples = 0;
						int contentRows = 0;
						for (int y = searchTop; y < searchBottom; y += rowStep)
						{
							int contentSamples = 0;
							int left = width;
							int right = 0;
							for (int x = x0; x < x1; x += xStep)
							{
								if (lumaCode(x, y) <= contentLimit)
									continue;
								++contentSamples;
								left = std::min(left, x);
								right = std::max(right, x);
							}
							if (contentSamples < minimumContentSamples ||
								right - left < minimumContentSpan)
							{
								continue;
							}
							++contentRows;
							maximumContentSamples = std::max(
								maximumContentSamples, contentSamples);
							detectedTop = std::min(detectedTop, y);
							detectedBottom =
								std::max(detectedBottom, y + rowStep);
						}
						if (contentRows < minimumContentRows)
						{
							detectedTop = 0;
							detectedBottom = 0;
							return false;
						}
						return true;
					};

				int upperContentTop = 0;
				int upperContentBottom = 0;
				int lowerContentTop = 0;
				int lowerContentBottom = 0;
				int upperMaximumContentSamples = 0;
				int lowerMaximumContentSamples = 0;
				const bool upperContent =
					scopeSubtitlePictureTop > barInset * 2 &&
					findBarContent(
						barInset,
						scopeSubtitlePictureTop - barInset,
						upperContentTop,
						upperContentBottom,
						upperMaximumContentSamples);
				const bool lowerContent =
					scopeSubtitlePictureBottom + barInset * 2 < height &&
					findBarContent(
						scopeSubtitlePictureBottom + barInset,
						height - barInset,
						lowerContentTop,
						lowerContentBottom,
						lowerMaximumContentSamples);
				// Source overlay extent is owned entirely by the trusted active
				// picture and current bar pixels. In particular, screenAspect must
				// never influence whether source pixels require accommodation.
				const ActivePictureBounds trustedPicture = {
					scopeSubtitlePictureLeft, scopeSubtitlePictureTop,
					scopeSubtitlePictureRight, scopeSubtitlePictureBottom,
					width, height,
					scopeSubtitlePictureBottom > scopeSubtitlePictureTop
						? static_cast<double>(scopeSubtitlePictureRight -
							scopeSubtitlePictureLeft) /
							(scopeSubtitlePictureBottom - scopeSubtitlePictureTop)
						: 0.0,
					ActivePictureBounds::BarAxes::TOP_BOTTOM };
				AlphaSourceCrop::PresentationEnvelopeGeometryInput envelopeInput;
				envelopeInput.trustedPicture = trustedPicture;
				envelopeInput.observedContent = trustedPicture;
				envelopeInput.observedContentAvailable =
					upperContent || lowerContent;
				envelopeInput.expandTop = upperContent;
				envelopeInput.expandBottom = lowerContent;
				envelopeInput.verticalPadding =
					std::max(8, height / 90) + scopeSubtitlePaddingPixels;
				if (upperContent)
					envelopeInput.observedContent.top = upperContentTop;
				if (lowerContent)
					envelopeInput.observedContent.bottom = lowerContentBottom;
				const AlphaSourceCrop::PresentationEnvelopeGeometryDecision
					envelope = AlphaSourceCrop::BuildPresentationEnvelope(envelopeInput);
				const float upperRequiredShift = upperContent && envelope.valid
					? static_cast<float>(trustedPicture.top - envelope.bounds.top)
					: 0.0f;
				const float lowerRequiredShift = lowerContent && envelope.valid
					? static_cast<float>(envelope.bounds.bottom -
						trustedPicture.bottom)
					: 0.0f;

				// Translation is reserved for one overlay-like vertical edge. A shallow
				// full-width receiver OSD is still overlay-like. Content broad in both
				// dimensions, two current edges, or evidence opposite a held translation
				// is one FIT decision, never a competing translate-and-fit pair.
				const bool previousOwnsCurrentAnalysis =
					scopeSubtitleEvidenceSourceGeneration == source->generation &&
					scopeVerticalBarPresentation.action !=
						AlphaSourceCrop::VerticalBarPresentationAction::NONE;
				const bool verticalHoldActive = previousOwnsCurrentAnalysis ||
					AlphaSourceCrop::IsVerticalBarPresentationActive(
						scopeVerticalBarPresentation, now, scopeSubtitleHoldMs,
						sourceSequence);
				const bool topHoldActive = verticalHoldActive &&
					scopeVerticalBarPresentation.action ==
						AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE &&
					scopeVerticalBarPresentation.translationPixels < -0.5f;
				const bool lowerHoldActive = verticalHoldActive &&
					scopeVerticalBarPresentation.action ==
						AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE &&
					scopeVerticalBarPresentation.translationPixels > 0.5f;
				AlphaSourceCrop::VerticalBarContentInput verticalInput;
				verticalInput.upperContent = upperContent;
				verticalInput.lowerContent = lowerContent;
				verticalInput.upperOccupiedDepth =
					upperContentBottom - upperContentTop;
				verticalInput.lowerOccupiedDepth =
					lowerContentBottom - lowerContentTop;
				verticalInput.upperPeakSamples = upperMaximumContentSamples;
				verticalInput.lowerPeakSamples = lowerMaximumContentSamples;
				verticalInput.upperBarPixels = scopeSubtitlePictureTop;
				verticalInput.lowerBarPixels =
					height - scopeSubtitlePictureBottom;
				verticalInput.sampledColumns = sampledColumns;
				verticalInput.topTranslationHeld = topHoldActive;
				verticalInput.bottomTranslationHeld = lowerHoldActive;
				verticalInput.upperRequiredShift = upperRequiredShift;
				verticalInput.lowerRequiredShift = lowerRequiredShift;
				AlphaSourceCrop::VerticalBarContentDecision verticalDecision =
					AlphaSourceCrop::EvaluateVerticalBarContent(verticalInput);
				const bool upperOverlayLike = verticalDecision.upperOverlayLike;
				const bool lowerOverlayLike = verticalDecision.lowerOverlayLike;
				AlphaSourceCrop::VerticalTranslationConfirmationInput
					confirmationInput;
				confirmationInput.previous =
					scopeSubtitleTranslationConfirmation;
				confirmationInput.observed = verticalDecision;
				confirmationInput.acceptedTranslationActive =
					previousOwnsCurrentAnalysis &&
					scopeVerticalBarPresentation.action ==
						AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE;
				confirmationInput.acceptedTranslationPixels =
					scopeVerticalBarPresentation.translationPixels;
				confirmationInput.targetBufferPixels = static_cast<float>(
					scopeSubtitleTargetBufferPixels);
				confirmationInput.maximumTranslationMagnitudePixels =
					verticalDecision.translationPixels < 0.0f
					? static_cast<float>(trustedPicture.top)
					: static_cast<float>(height - trustedPicture.bottom);
				const auto confirmation =
					AlphaSourceCrop::ConfirmVerticalTranslation(confirmationInput);
				scopeSubtitleTranslationConfirmation = confirmation.state;
				if (confirmation.pending)
				{
					DebugLog::Log(
						"Alpha vertical bar translation: candidate pending confirmation %u/%u target=%.1f px retained=%.1f px",
						confirmation.state.confirmations,
						AlphaSourceCrop::
							VERTICAL_TRANSLATION_CONFIRMATIONS_REQUIRED,
						confirmation.state.candidateTranslationPixels,
						confirmationInput.acceptedTranslationActive
							? confirmationInput.acceptedTranslationPixels : 0.0f);
				}
				else if (confirmation.newlyAccepted)
				{
					DebugLog::Log(
						"Alpha vertical bar translation: candidate accepted target=%.1f px buffer=%d px after %u stable samples",
						confirmation.effective.translationPixels,
						scopeSubtitleTargetBufferPixels,
						AlphaSourceCrop::
							VERTICAL_TRANSLATION_CONFIRMATIONS_REQUIRED);
				}
				verticalDecision = confirmation.effective;
				const auto fitConfirmation =
					AlphaSourceCrop::ConfirmVerticalFit(
						scopeSubtitleFitConfirmation, verticalDecision);
				scopeSubtitleFitConfirmation = fitConfirmation.state;
				if (fitConfirmation.pending)
				{
					DebugLog::Log(
						"Alpha vertical bar fit: candidate pending confirmation %u/%u; retaining trusted presentation",
						fitConfirmation.state.confirmations,
						AlphaSourceCrop::
							VERTICAL_FIT_CONFIRMATIONS_REQUIRED);
				}
				else if (fitConfirmation.newlyAccepted)
				{
					DebugLog::Log(
						"Alpha vertical bar fit: candidate accepted after %u consecutive dense samples",
						AlphaSourceCrop::
							VERTICAL_FIT_CONFIRMATIONS_REQUIRED);
				}
				verticalDecision = fitConfirmation.effective;
				AlphaSourceCrop::VerticalBarPresentationUpdateInput updateInput;
				updateInput.previous = scopeVerticalBarPresentation;
				updateInput.current = verticalDecision;
				updateInput.upperContent = upperContent;
				updateInput.lowerContent = lowerContent;
				updateInput.upperContentTop = upperContentTop;
				updateInput.lowerContentBottom = lowerContentBottom;
				updateInput.currentTick = now;
				updateInput.currentSourceSequence = sourceSequence;
				updateInput.holdMs = scopeSubtitleHoldMs;
				updateInput.translationEnabled = scopeSubtitleFit;
				updateInput.previousOwnsCurrentAnalysis =
					previousOwnsCurrentAnalysis;
				scopeVerticalBarPresentation =
					AlphaSourceCrop::UpdateVerticalBarPresentation(updateInput);
				const auto action = verticalDecision.action ==
					AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE &&
					!scopeSubtitleFit
					? AlphaSourceCrop::VerticalBarPresentationAction::FIT
					: verticalDecision.action;
				const char* actionLabel =
					action == AlphaSourceCrop::VerticalBarPresentationAction::FIT
					? "fit" : (action ==
						AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE
						? (verticalDecision.translationPixels < 0.0f
							? "translate-top" : "translate-bottom")
						: (action ==
							AlphaSourceCrop::VerticalBarPresentationAction::FAIL_OPEN
							? "fail-open" : "none"));
				{
					std::ostringstream policy;
					policy << upperContent << upperOverlayLike << '|'
						<< lowerContent << lowerOverlayLike << '|'
						<< static_cast<int>(action) << '|'
						<< upperContentTop << ',' << upperContentBottom << ','
						<< upperMaximumContentSamples << '|'
						<< lowerContentTop << ',' << lowerContentBottom << ','
						<< lowerMaximumContentSamples;
					if (policy.str() != lastScopeVerticalOverlayPolicy)
					{
						lastScopeVerticalOverlayPolicy = policy.str();
						DebugLog::Log(
							"Alpha vertical bar content: top=%d overlay_top=%d top_extent=%d..%d top_peak=%d bottom=%d overlay_bottom=%d bottom_extent=%d..%d bottom_peak=%d decision=%s",
							upperContent ? 1 : 0, upperOverlayLike ? 1 : 0,
							upperContentTop, upperContentBottom,
							upperMaximumContentSamples,
							lowerContent ? 1 : 0, lowerOverlayLike ? 1 : 0,
							lowerContentTop, lowerContentBottom,
							lowerMaximumContentSamples,
							actionLabel);
					}
				}
			}

			if (automaticSourceCrop &&
				(scopeSubtitlePictureLeft > 0 ||
				 scopeSubtitlePictureRight < width))
			{
				const int y0 = height / 32;
				const int y1 = height - y0;
				const int yStep = std::max(1, height / 1080);
				const int columnStep = std::max(1, width / 1920);
				const int contentLimit = std::min(1023, blackCode + 32);
				const int sampledRows = std::max(1, (y1 - y0) / yStep);
				const int minimumContentSamples =
					std::max(8, sampledRows / 192);
				const int minimumContentSpan = std::max(16, height / 48);
				const int minimumContentColumns = 2;
				const int barInset = std::max(1, columnStep);

				auto findSideContent =
					[&](int searchLeft, int searchRight,
						int& detectedLeft, int& detectedRight)
					{
						detectedLeft = width;
						detectedRight = 0;
						int contentColumns = 0;
						for (int x = searchLeft; x < searchRight;
							x += columnStep)
						{
							int contentSamples = 0;
							int top = height;
							int bottom = 0;
							for (int y = y0; y < y1; y += yStep)
							{
								if (lumaCode(x, y) <= contentLimit)
									continue;
								++contentSamples;
								top = std::min(top, y);
								bottom = std::max(bottom, y);
							}
							if (contentSamples < minimumContentSamples ||
								bottom - top < minimumContentSpan)
								continue;
							++contentColumns;
							detectedLeft = std::min(detectedLeft, x);
							detectedRight =
								std::max(detectedRight, x + columnStep);
						}
						if (contentColumns < minimumContentColumns)
						{
							detectedLeft = 0;
							detectedRight = 0;
							return false;
						}
						return true;
					};

				int leftContentLeft = 0;
				int leftContentRight = 0;
				int rightContentLeft = 0;
				int rightContentRight = 0;
				const bool leftContent =
					scopeSubtitlePictureLeft > barInset * 2 &&
					findSideContent(barInset,
						scopeSubtitlePictureLeft - barInset,
						leftContentLeft, leftContentRight);
				const bool rightContent =
					scopeSubtitlePictureRight + barInset * 2 < width &&
					findSideContent(scopeSubtitlePictureRight + barInset,
						width - barInset,
						rightContentLeft, rightContentRight);
				if (leftContent)
				{
					scopeSubtitleDetectedLeft = leftContentLeft;
					scopeSubtitleLeftLastDetectionTick = now;
				}
				if (rightContent)
				{
					scopeSubtitleDetectedRight = rightContentRight;
					scopeSubtitleRightLastDetectionTick = now;
				}
			}
		}

		const bool subtitleAnalysisEvaluated = authorityIsCurrentBars &&
			(forceAnalysis || subtitleAnalysisScheduled);
		const bool verticalPresentationActive =
			AlphaSourceCrop::IsVerticalBarPresentationActiveForFrame(
				scopeVerticalBarPresentation, now, scopeSubtitleHoldMs,
				sourceSequence, subtitleAnalysisEvaluated,
				authorityIsCurrentBars,
				scopeSubtitleEvidenceSourceGeneration, source->generation);
		if (!verticalPresentationActive)
			scopeVerticalBarPresentation = {};
		if (!scopeSubtitleFit && verticalPresentationActive &&
			scopeVerticalBarPresentation.action ==
				AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE)
		{
			AlphaSourceCrop::VerticalBarPresentationUpdateInput updateInput;
			updateInput.previous = scopeVerticalBarPresentation;
			updateInput.currentTick = now;
			updateInput.currentSourceSequence = sourceSequence;
			updateInput.holdMs = scopeSubtitleHoldMs;
			updateInput.translationEnabled = false;
			scopeVerticalBarPresentation =
				AlphaSourceCrop::UpdateVerticalBarPresentation(updateInput);
		}
		const bool translationActive = scopeSubtitleFit &&
			verticalPresentationActive &&
			scopeVerticalBarPresentation.action ==
				AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE;
		const bool topTranslationActive = translationActive &&
			scopeVerticalBarPresentation.translationPixels < -0.5f;
		const float requestedShift = translationActive
			? scopeVerticalBarPresentation.translationPixels : 0.0f;

		if (translationActive != scopeSubtitleWasActive)
		{
			DebugLog::Log(
				"libplacebo scope subtitle fit: %s picture=%d..%d subtitle_bottom=%d requested_shift=%.1f px",
				translationActive ? "engaged" : "released",
				scopeSubtitlePictureTop,
				scopeSubtitlePictureBottom,
				scopeVerticalBarPresentation.detectedBottom,
				requestedShift);
			scopeSubtitleWasActive = translationActive;
		}
		if (topTranslationActive != scopeSubtitleWasTopActive)
		{
			DebugLog::Log("libplacebo scope subtitle fit: upper-edge placement %s; shift=%.1f px",
				topTranslationActive ? "active" : "released",
				requestedShift);
			scopeSubtitleWasTopActive = topTranslationActive;
		}
		return requestedShift;
	}

	static bool EncodingUsesBt2020(LibplaceboOutput::DxgiEncoding encoding)
	{
		using LibplaceboOutput::DxgiEncoding;
		return encoding == DxgiEncoding::FULL_G22_P2020 ||
			encoding == DxgiEncoding::STUDIO_G22_P2020 ||
			encoding == DxgiEncoding::STUDIO_G24_P2020;
	}

	static enum pl_color_levels EncodingLevels(
		LibplaceboOutput::DxgiEncoding encoding)
	{
		using LibplaceboOutput::DxgiEncoding;
		return encoding == DxgiEncoding::FULL_G22_P709 ||
			encoding == DxgiEncoding::FULL_G22_P2020
				? PL_COLOR_LEVELS_FULL
				: PL_COLOR_LEVELS_LIMITED;
	}

	static enum pl_color_transfer EncodingTransfer(
		LibplaceboOutput::DxgiEncoding encoding)
	{
		using LibplaceboOutput::DxgiEncoding;
		if (encoding == DxgiEncoding::FULL_G22_P709 ||
			encoding == DxgiEncoding::FULL_G22_P2020)
			return PL_COLOR_TRC_SRGB;
		if (encoding == DxgiEncoding::STUDIO_G24_P709 ||
			encoding == DxgiEncoding::STUDIO_G24_P2020)
			return PL_COLOR_TRC_GAMMA24;
		if (encoding == DxgiEncoding::STUDIO_G22_P709 ||
			encoding == DxgiEncoding::STUDIO_G22_P2020)
			return PL_COLOR_TRC_GAMMA22;
		return PL_COLOR_TRC_UNKNOWN;
	}

	static enum pl_color_transfer ResolvedPixelTransfer(
		LibplaceboOutput::DxgiEncoding encoding,
		LibplaceboOutput::TargetTransfer targetTransfer)
	{
		using LibplaceboOutput::TargetTransfer;
		switch (targetTransfer)
		{
		case TargetTransfer::BT1886: return PL_COLOR_TRC_BT_1886;
		case TargetTransfer::GAMMA18: return PL_COLOR_TRC_GAMMA18;
		case TargetTransfer::GAMMA20: return PL_COLOR_TRC_GAMMA20;
		case TargetTransfer::GAMMA22: return PL_COLOR_TRC_GAMMA22;
		case TargetTransfer::GAMMA24: return PL_COLOR_TRC_GAMMA24;
		case TargetTransfer::GAMMA26: return PL_COLOR_TRC_GAMMA26;
		case TargetTransfer::GAMMA28: return PL_COLOR_TRC_GAMMA28;
		default: break;
		}
		return EncodingTransfer(encoding);
	}

	static LibplaceboOutput::SdrTransfer ToSdrTransfer(
		enum pl_color_transfer transfer)
	{
		using LibplaceboOutput::SdrTransfer;
		switch (transfer)
		{
		case PL_COLOR_TRC_BT_1886: return SdrTransfer::BT1886;
		case PL_COLOR_TRC_SRGB: return SdrTransfer::SRGB;
		case PL_COLOR_TRC_GAMMA18: return SdrTransfer::GAMMA18;
		case PL_COLOR_TRC_GAMMA20: return SdrTransfer::GAMMA20;
		case PL_COLOR_TRC_GAMMA22: return SdrTransfer::GAMMA22;
		case PL_COLOR_TRC_GAMMA24: return SdrTransfer::GAMMA24;
		case PL_COLOR_TRC_GAMMA26: return SdrTransfer::GAMMA26;
		case PL_COLOR_TRC_GAMMA28: return SdrTransfer::GAMMA28;
		case PL_COLOR_TRC_UNKNOWN: return SdrTransfer::UNKNOWN;
		default: return SdrTransfer::OTHER;
		}
	}

	static enum pl_color_transfer FromSdrTransfer(
		LibplaceboOutput::SdrTransfer transfer)
	{
		using LibplaceboOutput::SdrTransfer;
		switch (transfer)
		{
		case SdrTransfer::BT1886: return PL_COLOR_TRC_BT_1886;
		case SdrTransfer::SRGB: return PL_COLOR_TRC_SRGB;
		case SdrTransfer::GAMMA18: return PL_COLOR_TRC_GAMMA18;
		case SdrTransfer::GAMMA20: return PL_COLOR_TRC_GAMMA20;
		case SdrTransfer::GAMMA22: return PL_COLOR_TRC_GAMMA22;
		case SdrTransfer::GAMMA24: return PL_COLOR_TRC_GAMMA24;
		case SdrTransfer::GAMMA26: return PL_COLOR_TRC_GAMMA26;
		case SdrTransfer::GAMMA28: return PL_COLOR_TRC_GAMMA28;
		default: return PL_COLOR_TRC_UNKNOWN;
		}
	}

	void LogPresentationTarget(const char* trigger) const
	{
		const LONG_PTR style = GetWindowLongPtr(videoHwnd, GWL_STYLE);
		const LONG_PTR exStyle = GetWindowLongPtr(videoHwnd, GWL_EXSTYLE);
		const HWND parent = GetParent(videoHwnd);
		const HWND root = GetAncestor(videoHwnd, GA_ROOT);
		const HWND owner = GetWindow(videoHwnd, GW_OWNER);
		const bool isChild = (style & WS_CHILD) != 0;
		const bool visible = IsWindowVisible(videoHwnd) != FALSE;
		const bool iconic = IsIconic(videoHwnd) != FALSE;
		DWORD cloaked = 0;
		using DwmGetWindowAttributeFn = HRESULT(WINAPI*)(
			HWND, DWORD, PVOID, DWORD);
		HMODULE dwmApi = LoadLibraryW(L"dwmapi.dll");
		const auto getWindowAttribute = dwmApi
			? reinterpret_cast<DwmGetWindowAttributeFn>(
				GetProcAddress(dwmApi, "DwmGetWindowAttribute"))
			: nullptr;
		const HRESULT cloakedResult = getWindowAttribute
			? getWindowAttribute(
				videoHwnd,
				static_cast<DWORD>(DWMWA_CLOAKED),
				&cloaked,
				static_cast<DWORD>(sizeof(cloaked)))
			: E_NOINTERFACE;
		if (dwmApi)
			FreeLibrary(dwmApi);
		RECT client{};
		RECT screen{};
		GetClientRect(videoHwnd, &client);
		GetWindowRect(videoHwnd, &screen);
		MONITORINFO monitorInfo{ sizeof(monitorInfo) };
		const HMONITOR monitor = MonitorFromWindow(
			videoHwnd, MONITOR_DEFAULTTONEAREST);
		GetMonitorInfo(monitor, &monitorInfo);

		DebugLog::Log(
			"Alpha presentation target: trigger=%s hwnd=%p parent=%p root=%p owner=%p style=0x%08lX exstyle=0x%08lX is_child=%d visible=%d iconic=%d cloaked=%d cloak_result=0x%08lX client=%ld,%ld-%ld,%ld screen=%ld,%ld-%ld,%ld monitor=%p monitor_rect=%ld,%ld-%ld,%ld requested=%s/%s/%s/%s effective=%s/%s/%s/%s fallback=%s",
			trigger,
			videoHwnd,
			parent,
			root,
			owner,
			static_cast<unsigned long>(style),
			static_cast<unsigned long>(exStyle),
			isChild ? 1 : 0,
			visible ? 1 : 0,
			iconic ? 1 : 0,
			SUCCEEDED(cloakedResult) ? static_cast<int>(cloaked) : -1,
			static_cast<unsigned long>(cloakedResult),
			client.left, client.top, client.right, client.bottom,
			screen.left, screen.top, screen.right, screen.bottom,
			monitor,
			monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
			monitorInfo.rcMonitor.right, monitorInfo.rcMonitor.bottom,
			LibplaceboOutput::ToString(requestedOutputPlan.request.presentation),
			LibplaceboOutput::ToString(requestedOutputPlan.request.range),
			LibplaceboOutput::ToString(requestedOutputPlan.request.gamma),
			LibplaceboOutput::ToString(requestedOutputPlan.request.primaries),
			LibplaceboOutput::ToString(outputPlan.request.presentation),
			LibplaceboOutput::ToString(outputPlan.request.range),
			LibplaceboOutput::ToString(outputPlan.request.gamma),
			LibplaceboOutput::ToString(outputPlan.request.primaries),
			isChild ? "embedded child preview" : "none");
	}

	void SetSwapchainColorHint(LibplaceboOutput::DxgiEncoding encoding,
		LibplaceboOutput::TargetTransfer targetTransfer)
	{
		configuredOutputColor =
			LibplaceboExportedData<pl_color_space>("pl_color_space_bt709");
		configuredOutputColor.primaries = EncodingUsesBt2020(encoding)
			? PL_COLOR_PRIM_BT_2020 : PL_COLOR_PRIM_BT_709;
		const enum pl_color_transfer transfer =
			ResolvedPixelTransfer(encoding, targetTransfer);
		configuredOutputColor.transfer =
			transfer == PL_COLOR_TRC_UNKNOWN ? PL_COLOR_TRC_SRGB : transfer;
		configuredOutputColor.hdr.min_luma = static_cast<float>(sdrBlackNits);
		configuredOutputColor.hdr.max_luma = static_cast<float>(sdrTargetNits);
		// libplacebo applies a colour-space hint lazily while starting a frame.
		// Do not give it that authority for the VP-owned path: VP applies the
		// DXGI state synchronously and tracks that applied transition below.
		if (swapchain && !vpOwnedSwapchain)
			pl_swapchain_colorspace_hint(swapchain, &configuredOutputColor);
	}

	bool ReturnedTargetMatchesActualOutput(
		const struct pl_color_repr& repr,
		const struct pl_color_space& color) const
	{
		return actualOutput.safeToRender &&
			LibplaceboDisplayLut::TargetMatchesSignal(
				color.primaries,
				color.transfer,
				repr.levels,
				EncodingUsesBt2020(actualOutput.encoding)
					? PL_COLOR_PRIM_BT_2020 : PL_COLOR_PRIM_BT_709,
				ResolvedPixelTransfer(actualOutput.encoding,
					actualOutput.targetTransfer),
				EncodingLevels(actualOutput.encoding));
	}

	void ReleaseVpOwnedSwapchain()
	{
		if (!vpOwnedSwapchain) return;
		DebugLog::Log(
			"VP-owned DXGI swapchain: releasing generation=%llu applied_record=%s verified=%d present_owner=VP",
			static_cast<unsigned long long>(vpOwnedSwapchainGeneration),
			vpOwnedAppliedEncoding.c_str(),
			vpOwnedColorSpaceVerified ? 1 : 0);
		vpOwnedSwapchain.Release();
		vpOwnedAppliedEncoding = "unapplied";
		vpOwnedColorSpaceResult = E_PENDING;
		vpOwnedColorSpaceVerified = false;
		vpOwnedTargetValidationLoggedGeneration = 0;
		vpOwnedTargetFailureLoggedGeneration = 0;
		vpOwnedPresentFailureLoggedGeneration = 0;
		successfulPresentCount = 0;
		vpOwnedClearLoggedGeneration = 0;
	}

	bool CreateVpOwnedSwapchain(bool blit, const char* trigger)
	{
		if (!d3d11 || !d3d11->device)
			return false;
		RECT client{};
		if (!GetClientRect(videoHwnd, &client))
			return false;
		CComQIPtr<IDXGIDevice> dxgiDevice(d3d11->device);
		CComPtr<IDXGIAdapter> adapter;
		CComPtr<IDXGIFactory2> factory;
		if (!dxgiDevice || FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter ||
			FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))) || !factory)
		{
			DebugLog::Log(
				"VP-owned DXGI swapchain: cannot resolve an IDXGIFactory2 (trigger=%s)",
				trigger);
			return false;
		}

		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
		desc.Height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));
		// Match libplacebo's own D3D11 8-bit picker. Supplying BGRA here while
		// asking libplacebo to disable 10-bit makes its first resize replace the
		// externally created format, defeating the authoritative VP request.
		desc.Format = activeSettings.diagnosticForce8BitSdrSwapchain
			? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R10G10B10A2_UNORM;
		desc.Stereo = FALSE;
		desc.SampleDesc.Count = 1;
		// Match the D3D11 target contract used by libplacebo itself. At feature
		// level 11, its wrapped-texture backend requires the SRV/UAV combination
		// to expose blit_dst. Render-target usage alone produces a texture that is
		// renderable but invalid as a pl_render_image destination.
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT |
			DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_UNORDERED_ACCESS;
		desc.BufferCount = blit ? 1 : 3;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.SwapEffect = blit ? DXGI_SWAP_EFFECT_DISCARD :
			DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		desc.Flags = 0;
		CComPtr<IDXGISwapChain1> created;
		const HRESULT result = factory->CreateSwapChainForHwnd(
			d3d11->device, videoHwnd, &desc, nullptr, nullptr, &created);
		if (FAILED(result) || !created)
		{
			DebugLog::Log(
				"VP-owned DXGI swapchain: CreateSwapChainForHwnd failed trigger=%s result=0x%08lX format=%u effect=%u buffers=%u size=%ux%u",
				trigger, static_cast<unsigned long>(result),
				static_cast<unsigned int>(desc.Format),
				static_cast<unsigned int>(desc.SwapEffect), desc.BufferCount,
				desc.Width, desc.Height);
			return false;
		}
		vpOwnedSwapchain = created;
		++vpOwnedSwapchainGeneration;
		vpOwnedAppliedEncoding = "unapplied";
		vpOwnedColorSpaceResult = E_PENDING;
		vpOwnedColorSpaceVerified = false;
		vpOwnedTargetValidationLoggedGeneration = 0;
		vpOwnedTargetFailureLoggedGeneration = 0;
		vpOwnedPresentFailureLoggedGeneration = 0;
		successfulPresentCount = 0;
		vpOwnedClearLoggedGeneration = 0;
		DebugLog::Log(
			"VP-owned DXGI swapchain: created generation=%llu trigger=%s format=%u effect=%u buffers=%u usage=0x%08X size=%ux%u present_owner=VP",
			static_cast<unsigned long long>(vpOwnedSwapchainGeneration), trigger,
			static_cast<unsigned int>(desc.Format),
			static_cast<unsigned int>(desc.SwapEffect), desc.BufferCount,
			desc.BufferUsage, desc.Width, desc.Height);
		return true;
	}

	void ConfigureSwapchainOutput(const char* trigger)
	{
		using namespace LibplaceboOutput;
		LogPresentationTarget(trigger);

		const bool previousStateMayBeStudio =
			actualOutput.encoding != DxgiEncoding::FULL_G22_P709 ||
			!actualOutput.safeToRender;
		negotiatedMonitor = MonitorFromWindow(
			videoHwnd,
			MONITOR_DEFAULTTONEAREST);
		negotiatedSwapchainFormat = DXGI_FORMAT_UNKNOWN;
		if (!swapchain)
		{
			actualOutput = {};
			actualOutput.safeToRender = false;
			actualOutput.reason = "DXGI swapchain is unavailable";
			DebugLog::Log(
				"libplacebo output negotiation (%s): no swapchain; rendering blocked pending recovery",
				trigger);
			return;
		}
		Evidence evidence;
		const bool vpOwnsPresentation = vpOwnedSwapchain != nullptr;
		evidence.vpOwnsPresentation = vpOwnsPresentation;
		auto recordVpOwnedColorState = [&](DxgiEncoding encoding,
			HRESULT result, bool verified, const char* stage)
		{
			if (!vpOwnsPresentation) return;
			vpOwnedAppliedEncoding = ToString(encoding);
			vpOwnedColorSpaceResult = result;
			vpOwnedColorSpaceVerified = verified;
			DebugLog::Log(
				"VP-owned DXGI swapchain: colour-space applied record generation=%llu stage=%s requested=%s set=0x%08lX capability_recheck=%d present_owner=VP wire_state=unverified",
				static_cast<unsigned long long>(vpOwnedSwapchainGeneration), stage,
				vpOwnedAppliedEncoding.c_str(), static_cast<unsigned long>(result),
				verified ? 1 : 0);
		};
		IDXGISwapChain* nativeSwapchain = pl_d3d11_swapchain_unwrap(swapchain);
		if (!nativeSwapchain)
		{
			evidence.fullRestoreRequired = previousStateMayBeStudio;
			actualOutput = Finalize(outputPlan, evidence);
			SetSwapchainColorHint(actualOutput.encoding,
				actualOutput.targetTransfer);
			DebugLog::Log(
				"libplacebo output negotiation (%s): effective_request=%s/%s/%s/%s actual=UNKNOWN/FULL/sRGB/Rec.709 reason=cannot unwrap DXGI swapchain",
				trigger,
				ToString(outputPlan.request.presentation),
				ToString(outputPlan.request.range),
				ToString(outputPlan.request.gamma),
				ToString(outputPlan.request.primaries));
			return;
		}

		DXGI_SWAP_CHAIN_DESC swapchainDesc{};
		negotiatedSwapchainFormat = DXGI_FORMAT_UNKNOWN;
		const HRESULT descResult = nativeSwapchain->GetDesc(&swapchainDesc);
		if (SUCCEEDED(descResult))
		{
			negotiatedSwapchainFormat = swapchainDesc.BufferDesc.Format;
			const bool flip =
				swapchainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ||
				swapchainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
			evidence.presentationModel =
				flip ? PresentationModel::FLIP : PresentationModel::BITBLT;
			DebugLog::Log(
				"libplacebo DXGI swapchain: trigger=%s format=%u swap_effect=%u flags=0x%08X buffers=%u windowed=%d model=%s direct_state=unverified",
				trigger,
				static_cast<unsigned int>(swapchainDesc.BufferDesc.Format),
				static_cast<unsigned int>(swapchainDesc.SwapEffect),
				swapchainDesc.Flags,
				swapchainDesc.BufferCount,
				swapchainDesc.Windowed ? 1 : 0,
				ToString(evidence.presentationModel));
		}
		else
		{
			DebugLog::Log(
				"libplacebo DXGI swapchain descriptor failed: trigger=%s result=0x%08lX",
				trigger,
				static_cast<unsigned long>(descResult));
		}

		CComQIPtr<IDXGIDevice> dxgiDevice(d3d11->device);
		CComPtr<IDXGIAdapter> adapter;
		if (dxgiDevice && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter)
		{
			DXGI_ADAPTER_DESC adapterDesc{};
			LARGE_INTEGER driverVersion{};
			const HRESULT driverResult = adapter->CheckInterfaceSupport(
				__uuidof(IDXGIDevice),
				&driverVersion);
			if (SUCCEEDED(adapter->GetDesc(&adapterDesc)))
			{
				DebugLog::Log(
					"libplacebo DXGI adapter: name=%ls luid=%08lX:%08lX vendor=0x%04X device=0x%04X driver_check=0x%08lX driver=%08lX:%08lX",
					adapterDesc.Description,
					static_cast<unsigned long>(adapterDesc.AdapterLuid.HighPart),
					static_cast<unsigned long>(adapterDesc.AdapterLuid.LowPart),
					adapterDesc.VendorId,
					adapterDesc.DeviceId,
					static_cast<unsigned long>(driverResult),
					static_cast<unsigned long>(driverVersion.HighPart),
					static_cast<unsigned long>(driverVersion.LowPart));
			}
		}

		CComPtr<IDXGIOutput> output;
		if (SUCCEEDED(nativeSwapchain->GetContainingOutput(&output)) && output)
		{
			DXGI_OUTPUT_DESC outputDesc{};
			if (SUCCEEDED(output->GetDesc(&outputDesc)))
			{
				negotiatedMonitor = outputDesc.Monitor;
				negotiatedDisplayDeviceName = outputDesc.DeviceName;
				DEVMODEW mode{};
				mode.dmSize = sizeof(mode);
				const BOOL haveMode = EnumDisplaySettingsW(
					outputDesc.DeviceName,
					ENUM_CURRENT_SETTINGS,
					&mode);
				DebugLog::Log(
					"libplacebo DXGI output: device=%ls monitor=0x%p desktop=%ld,%ld-%ld,%ld rotation=%u mode=%lux%lu@%luHz bits=%lu",
					outputDesc.DeviceName,
					outputDesc.Monitor,
					outputDesc.DesktopCoordinates.left,
					outputDesc.DesktopCoordinates.top,
					outputDesc.DesktopCoordinates.right,
					outputDesc.DesktopCoordinates.bottom,
					static_cast<unsigned int>(outputDesc.Rotation),
					haveMode ? mode.dmPelsWidth : 0,
					haveMode ? mode.dmPelsHeight : 0,
					haveMode ? mode.dmDisplayFrequency : 0,
					haveMode ? mode.dmBitsPerPel : 0);
			}
			CComQIPtr<IDXGIOutput6> output6(output);
			DXGI_OUTPUT_DESC1 outputDesc1{};
			const HRESULT output6Result = output6 ? output6->GetDesc1(&outputDesc1) :
				E_NOINTERFACE;
			if (SUCCEEDED(output6Result))
			{
				DebugLog::Log(
					"libplacebo DXGI output capabilities: color_space=%u bits_per_color=%u min_luminance=%.4f max_luminance=%.1f max_full_frame_luminance=%.1f",
					static_cast<unsigned int>(outputDesc1.ColorSpace),
					static_cast<unsigned int>(outputDesc1.BitsPerColor),
					outputDesc1.MinLuminance,
					outputDesc1.MaxLuminance,
					outputDesc1.MaxFullFrameLuminance);
			}
			else
				DebugLog::Log(
					"libplacebo DXGI output capabilities unavailable: result=0x%08lX",
					static_cast<unsigned long>(output6Result));
		}

		CComQIPtr<IDXGISwapChain3> swapchain3(nativeSwapchain);
		nativeSwapchain->Release();
		evidence.hasSwapchain3 = swapchain3 != nullptr;

		struct ColorSpaceProbe
		{
			DXGI_COLOR_SPACE_TYPE colorSpace;
			const char* description;
			HRESULT checkResult = E_FAIL;
			UINT support = 0;
		};
		ColorSpaceProbe probes[] =
		{
			{ DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, "RGB full G22 P709" },
			{ DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709, "RGB studio G22 P709" },
			{ DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709, "RGB studio G24 P709" },
			{ DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020, "RGB full G22 P2020" },
			{ DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020, "RGB studio G22 P2020" },
			{ DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020, "RGB studio G24 P2020" },
			{ DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, "RGB full linear P709" }
		};

		auto restoreFullAndVerify = [&]()
		{
			UINT preSupport = 0;
			UINT postSupport = 0;
			HRESULT preCheck = E_FAIL;
			HRESULT setResult = E_FAIL;
			HRESULT postCheck = E_FAIL;
			unsigned int checkCount = 0;
			const VerifiedTransition transition = ExecuteVerifiedTransition(
				[&]()
				{
					UINT& support = checkCount++ == 0 ? preSupport : postSupport;
					HRESULT& result = checkCount == 1 ? preCheck : postCheck;
					result = swapchain3->CheckColorSpaceSupport(
						DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
						&support);
					return SUCCEEDED(result) &&
						(support &
							DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
				},
				[&]()
				{
					setResult = swapchain3->SetColorSpace1(
						DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
					return SUCCEEDED(setResult);
				});
			evidence.fullRestorePresentSupportedBeforeSet =
				transition.presentSupportedBeforeSet;
			evidence.fullRestoreSetSucceeded = transition.setSucceeded;
			evidence.fullRestorePresentSupportedAfterSet =
				transition.presentSupportedAfterSet;
			if (!transition.presentSupportedBeforeSet)
			{
				setResult = E_FAIL;
				postCheck = E_FAIL;
			}
			DebugLog::Log(
				"libplacebo DXGI safe Full/sRGB restore: pre_capability_check=0x%08lX pre_flags=0x%08X set=0x%08lX post_capability_check=0x%08lX post_flags=0x%08X api_sequence_succeeded=%d wire_state=unverified",
				static_cast<unsigned long>(preCheck),
				preSupport,
				static_cast<unsigned long>(setResult),
				static_cast<unsigned long>(postCheck),
				postSupport,
				(evidence.fullRestorePresentSupportedBeforeSet &&
					evidence.fullRestoreSetSucceeded &&
					evidence.fullRestorePresentSupportedAfterSet) ? 1 : 0);
			recordVpOwnedColorState(DxgiEncoding::FULL_G22_P709, setResult,
				evidence.fullRestorePresentSupportedBeforeSet &&
				evidence.fullRestoreSetSucceeded &&
				evidence.fullRestorePresentSupportedAfterSet,
				"safe-full-restore");
		};

		if (previousStateMayBeStudio)
		{
			evidence.fullRestoreRequired = true;
			if (swapchain3)
				restoreFullAndVerify();
		}
		const bool safeFullBaseline =
			!evidence.fullRestoreRequired ||
			(evidence.fullRestorePresentSupportedBeforeSet &&
				evidence.fullRestoreSetSucceeded &&
				evidence.fullRestorePresentSupportedAfterSet);

		if (swapchain3 && safeFullBaseline)
		{
			for (ColorSpaceProbe& probe : probes)
			{
				probe.checkResult = swapchain3->CheckColorSpaceSupport(
					probe.colorSpace,
					&probe.support);
				DebugLog::Log(
					"libplacebo DXGI color-space support: %s check=0x%08lX flags=0x%08X present=%d overlay=%d",
					probe.description,
					static_cast<unsigned long>(probe.checkResult),
					probe.support,
					(probe.support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ? 1 : 0,
					(probe.support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_OVERLAY_PRESENT) ? 1 : 0);
			}
		}

		// VP owns this swapchain specifically so its applied state is
		// authoritative. Apply and recheck even the nominal Full/sRGB baseline;
		// merely relying on the creation default would leave the record unknown.
		if (vpOwnsPresentation && swapchain3 && safeFullBaseline &&
			outputPlan.valid && !outputPlan.requiresDxgiOverride)
		{
			evidence.fullRestoreRequired = true;
			restoreFullAndVerify();
		}

		auto toDxgi = [](DxgiEncoding encoding)
		{
			switch (encoding)
			{
			case DxgiEncoding::STUDIO_G22_P709:
				return DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
			case DxgiEncoding::STUDIO_G24_P709:
				return DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709;
			case DxgiEncoding::FULL_G22_P2020:
				return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020;
			case DxgiEncoding::STUDIO_G22_P2020:
				return DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020;
			case DxgiEncoding::STUDIO_G24_P2020:
				return DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020;
			default:
				return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
			}
		};

		if (swapchain3 &&
			safeFullBaseline &&
			!suppressLimitedNegotiation &&
			outputPlan.valid &&
			outputPlan.requiresDxgiOverride)
		{
			const DXGI_COLOR_SPACE_TYPE desired = toDxgi(outputPlan.desiredEncoding);
			ColorSpaceProbe* desiredProbe = nullptr;
			for (ColorSpaceProbe& probe : probes)
				if (probe.colorSpace == desired)
					desiredProbe = &probe;

			evidence.presentSupportedBeforeSet =
				desiredProbe &&
				SUCCEEDED(desiredProbe->checkResult) &&
				(desiredProbe->support &
					DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
			if (evidence.presentSupportedBeforeSet)
			{
				UINT postSupport = 0;
				HRESULT setResult = E_FAIL;
				HRESULT postCheck = E_FAIL;
				unsigned int checkCount = 0;
				const VerifiedTransition transition = ExecuteVerifiedTransition(
					[&]()
					{
						if (checkCount++ == 0)
							return evidence.presentSupportedBeforeSet;
						postCheck = swapchain3->CheckColorSpaceSupport(
							desired,
							&postSupport);
						return SUCCEEDED(postCheck) &&
							(postSupport &
								DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
					},
					[&]()
					{
						setResult = swapchain3->SetColorSpace1(desired);
						return SUCCEEDED(setResult);
					});
				evidence.setSucceeded = transition.setSucceeded;
				evidence.presentSupportedAfterSet =
					transition.presentSupportedAfterSet;
				DebugLog::Log(
					"libplacebo DXGI color-space set: selected=%s set=0x%08lX post_capability_check=0x%08lX post_flags=0x%08X wire_state=unverified",
					ToString(outputPlan.desiredEncoding),
					static_cast<unsigned long>(setResult),
					static_cast<unsigned long>(postCheck),
					postSupport);
				recordVpOwnedColorState(outputPlan.desiredEncoding, setResult,
					transition.presentSupportedBeforeSet && transition.setSucceeded &&
					transition.presentSupportedAfterSet,
					"requested-colour-space");
			}
		}

		const bool studioAccepted =
			outputPlan.valid &&
			outputPlan.requiresDxgiOverride &&
			evidence.hasSwapchain3 &&
			evidence.presentSupportedBeforeSet &&
			evidence.setSucceeded &&
			evidence.presentSupportedAfterSet;
		if (!studioAccepted &&
			evidence.setSucceeded &&
			swapchain3)
		{
			evidence.fullRestoreRequired = true;
			restoreFullAndVerify();
		}

		actualOutput = Finalize(outputPlan, evidence);
		// A DXGI failure falls back to Full/Rec.709 in policy. Replace the
		// original requested hint immediately so future frames cannot inherit
		// an unaccepted BT.2020/limited target contract.
		SetSwapchainColorHint(actualOutput.encoding,
			actualOutput.targetTransfer);
		DebugLog::Log(
			"libplacebo DXGI color-space result: trigger=%s requested=%s accepted=%d applied_record=%s wire_state=unverified",
			trigger,
			ToString(actualOutput.encoding),
			actualOutput.requestedEncodingActive ? 1 : 0,
			vpOwnsPresentation ? (vpOwnedColorSpaceVerified
				? vpOwnedAppliedEncoding.c_str() : "vp-record-unverified") :
				"unavailable (IDXGISwapChain3 has no getter)");
		DebugLog::Log(
			"libplacebo output applied state: pixel_transfer=%s dxgi_declaration=%s declaration_semantics=%s presenter_owner=%s strict=%d wire_state=unverified",
			LibplaceboOutput::ToString(actualOutput.targetTransfer),
			ToString(actualOutput.encoding),
			actualOutput.encoding == DxgiEncoding::FULL_G22_P709
				? "sRGB_nominal_no_pure22_DXGI_enum" : "matches_nominal_transfer",
			vpOwnsPresentation ? "VP" : "libplacebo",
			outputPlan.strictContract ? 1 : 0);
		DebugLog::Log(
			"libplacebo output negotiation (%s): effective_request=%s/%s/%s/%s actual_contract=%s/%s/%s/%s dxgi=%s accepted=%d safe=%d wire_state=unverified reason=%s",
			trigger,
			ToString(outputPlan.request.presentation),
			ToString(outputPlan.request.range),
			ToString(outputPlan.request.gamma),
			ToString(outputPlan.request.primaries),
			ToString(actualOutput.presentationModel),
			ToRangeString(actualOutput.encoding),
			actualOutput.targetTransfer == TargetTransfer::SWAPCHAIN
				? ToGammaString(actualOutput.encoding)
				: LibplaceboOutput::ToString(actualOutput.targetTransfer),
			EncodingUsesBt2020(actualOutput.encoding) ? "BT.2020" : "Rec.709",
			ToString(actualOutput.encoding),
			actualOutput.requestedEncodingActive ? 1 : 0,
			actualOutput.safeToRender ? 1 : 0,
			actualOutput.reason.c_str());

		if (targetBt2020 && reportBt2020ToDisplay)
		{
			if (!nvidiaBt2020Reporter.Enable(negotiatedDisplayDeviceName.c_str()))
			{
				// HDMI reporting is optional metadata. Preserve the selected BT.2020
				// render target and the proven P709 transport if it is unavailable.
				DebugLog::Log(
					"libplacebo: NVIDIA BT.2020 InfoFrame SET failed; retaining the BT.2020 target with P709 transport and marking HDMI signaling unavailable");
				reportBt2020ToDisplay = false;
				bt2020SignalingFailed = true;
			}
			else
				bt2020SignalingFailed = false;
		}
		else
		{
			nvidiaBt2020Reporter.Restore();
			bt2020SignalingFailed = false;
		}
	}

	bool RecreateSwapchain(
		bool blit,
		bool suppressLimited,
		const char* trigger)
	{
		pl_swapchain_destroy(&swapchain);
		ReleaseVpOwnedSwapchain();
		swapchainBlit = blit;
		suppressLimitedNegotiation = suppressLimited;
		auto markUnavailable = [&](const char* reason)
		{
			pl_swapchain_destroy(&swapchain);
			actualOutput = {};
			actualOutput.safeToRender = false;
			actualOutput.reason = reason;
		};
		struct pl_d3d11_swapchain_params swapchainParams{};
		// libplacebo retains these behavioral fields even when it wraps an
		// externally supplied swapchain. Set them on both ownership paths so its
		// initial color configuration cannot silently upgrade an 8-bit VP chain.
		swapchainParams.color_bits =
			activeSettings.diagnosticForce8BitSdrSwapchain ? 8 : 10;
		swapchainParams.disable_10bit_sdr =
			activeSettings.diagnosticForce8BitSdrSwapchain;
		// The VP-owned path is an authoritative flip-model experiment. Its
		// externally wrapped bitblt FBO does not meet the same capability
		// guarantees as a libplacebo-created composed swapchain (the teardown
		// clear already demonstrated that mismatch). Keep Composed on the proven
		// libplacebo-owned path instead of presenting a black experimental surface.
		const bool useVpOwnedDxgiPresenter =
			activeSettings.diagnosticVpOwnedDxgiPresenter && !blit;
		if (activeSettings.diagnosticVpOwnedDxgiPresenter && blit)
		{
			DebugLog::Log(
				"VP-owned DXGI swapchain: requested=1 applied=0 trigger=%s reason=the VP-owned beta presenter supports flip/direct only; using libplacebo-owned composed swapchain",
				trigger);
		}
		if (useVpOwnedDxgiPresenter)
		{
			if (!CreateVpOwnedSwapchain(blit, trigger))
			{
				markUnavailable("failed to create the VP-owned DXGI swapchain");
				return false;
			}
			swapchainParams.swapchain = vpOwnedSwapchain;
		}
		else
		{
			swapchainParams.window = videoHwnd;
			swapchainParams.blit = blit;
		}
		swapchain = pl_d3d11_create_swapchain(d3d11, &swapchainParams);
		if (!swapchain)
		{
			markUnavailable("failed to recreate the DXGI swapchain");
			DebugLog::Log(
				"libplacebo swapchain recreation failed: trigger=%s model=%s",
				trigger,
				blit ? "COMPOSED/BITBLT" : "FLIP/DIRECT-ELIGIBLE");
			return false;
		}

		if (!vpOwnedSwapchain)
			pl_swapchain_colorspace_hint(swapchain, &configuredOutputColor);
		RECT client{};
		if (!GetClientRect(videoHwnd, &client))
		{
			markUnavailable("failed to query the render window during swapchain recreation");
			return false;
		}
		int width = std::max<LONG>(1, client.right - client.left);
		int height = std::max<LONG>(1, client.bottom - client.top);
		if (!pl_swapchain_resize(swapchain, &width, &height))
		{
			markUnavailable("failed to resize the recreated DXGI swapchain");
			DebugLog::Log(
				"libplacebo swapchain recreation resize failed: trigger=%s size=%dx%d",
				trigger,
				width,
				height);
			return false;
		}
		ConfigureSwapchainOutput(trigger);
		return true;
	}

	void EnsureAutoLimitedFallback(const char* trigger)
	{
		if (swapchainBlit ||
			!LibplaceboOutput::ShouldFallbackToComposed(outputPlan, actualOutput))
			return;

		DebugLog::Log(
			"libplacebo output negotiation (%s): AUTO Limited unavailable; recreating the stable composed Full/sRGB swapchain",
			trigger);
		if (!RecreateSwapchain(
			true,
			true,
			"AUTO Limited composed fallback"))
		{
			actualOutput.safeToRender = false;
			actualOutput.reason =
				"failed to recreate the stable composed Full/sRGB swapchain";
		}
	}

	void ConfigureAndFallback(const char* trigger)
	{
		if (!swapchain)
		{
			if (!RecreateSwapchain(
				outputPlan.useBlit,
				false,
				trigger))
				return;
			EnsureAutoLimitedFallback(trigger);
			return;
		}
		ConfigureSwapchainOutput(trigger);
		EnsureAutoLimitedFallback(trigger);
	}

	void RetryAutoLimitedCandidate(const char* trigger)
	{
		if (suppressLimitedNegotiation &&
			outputPlan.request.presentation ==
				LibplaceboOutput::PresentationRequest::AUTO &&
			outputPlan.request.range == LibplaceboOutput::RangeRequest::LIMITED)
		{
			if (!RecreateSwapchain(false, false, trigger))
			{
				RecreateSwapchain(
					true,
					true,
					"AUTO Limited candidate recreation failure");
			}
			EnsureAutoLimitedFallback(trigger);
			return;
		}
		if (!swapchain)
		{
			if (!RecreateSwapchain(
				outputPlan.useBlit,
				false,
				trigger))
				return;
			EnsureAutoLimitedFallback(trigger);
			return;
		}
		ConfigureAndFallback(trigger);
	}

	static std::string LutFileName(const std::string& path)
	{
		const size_t separator = path.find_last_of("\\/");
		return separator == std::string::npos ? path : path.substr(separator + 1);
	}

	void LoadDisplayLut(const RendererSettings& settings)
	{
		displayLutPath = settings.lutPath;
		displayLutConstrainedBaseDirectory = settings.lutConstrainedBaseDirectory;
		lutReferencePrimaries = settings.lutReferencePrimaries;
		lutReferenceTransfer = settings.lutReferenceTransfer;
		lutReferenceRange = settings.lutReferenceRange;
		lutReferenceNits = settings.lutReferenceNits;

		if (settings.lutPathRejected)
		{
			displayLutStatus = "Rejected: bad path";
			DebugLog::Log(
				"display: LUT rejected because its relative path escaped the configuration directory; rendering without a LUT");
			return;
		}

		const LibplaceboDisplayLut::LoadResult result =
			LibplaceboDisplayLut::Load(
				log, displayLutPath, displayLutConstrainedBaseDirectory);
		displayLut = result.lut;
		displayLutParsed =
			result.status == LibplaceboDisplayLut::Status::ACTIVE;
		if (result.status == LibplaceboDisplayLut::Status::DISABLED)
		{
			displayLutStatus = "Disabled";
			return;
		}
		if (result.status == LibplaceboDisplayLut::Status::REJECTED)
		{
			const char* reason =
				LibplaceboDisplayLut::ShortReason(result.rejection);
			displayLutStatus = std::string("Rejected: ") + reason;
			DebugLog::Log(
				"display: LUT rejected (%s); rendering without a LUT: %s",
				reason,
				displayLutPath.c_str());
			return;
		}

		DebugLog::Log(
			"display: parsed 3D LUT %s (%d x %d x %d, %zu bytes, signature=%016llX); target contract validation pending",
			displayLutPath.c_str(),
			displayLut->size[0],
			displayLut->size[1],
			displayLut->size[2],
			result.fileBytes,
			static_cast<unsigned long long>(displayLut->signature));
		displayLutStatus = "Loaded: validating";
	}

	void RejectDisplayLutAfterRenderFailure()
	{
		if (!displayLutParsed || !displayLut)
			return;

		DebugLog::Log(
			"display: LUT render failed; disabling %s for this renderer instance and continuing without a LUT",
			displayLutPath.c_str());
		displayLutStatus = "Rejected: render error";
		displayLutParsed = false;
		pl_lut_free(&displayLut);
		if (renderer)
			pl_renderer_flush_cache(renderer);
	}

	const char* DisplayLutContractMismatch(
		const struct pl_frame& target,
		bool returnedTargetMatchesActualOutput) const
	{
		if (!displayLutParsed || !displayLut)
			return nullptr;
		if (!actualOutput.safeToRender)
			return "output not signaled";

		enum pl_color_primaries expectedPrimaries = PL_COLOR_PRIM_UNKNOWN;
		if (lutReferencePrimaries == "rec709")
			expectedPrimaries = PL_COLOR_PRIM_BT_709;
		else if (lutReferencePrimaries == "p3_d65")
			expectedPrimaries = PL_COLOR_PRIM_DISPLAY_P3;
		else if (lutReferencePrimaries == "bt2020")
			expectedPrimaries = PL_COLOR_PRIM_BT_2020;

		const enum pl_color_transfer expectedTransfer =
			lutReferenceTransfer == "auto"
				? PL_COLOR_TRC_UNKNOWN
				: TranslateOutputGamma(lutReferenceTransfer);
		const enum pl_color_levels expectedRange =
			lutReferenceRange == "auto"
				? PL_COLOR_LEVELS_UNKNOWN
				: lutReferenceRange == "limited"
					? PL_COLOR_LEVELS_LIMITED
					: PL_COLOR_LEVELS_FULL;

		const LibplaceboDisplayLut::ContractRejection rejection =
			LibplaceboDisplayLut::ValidateContract(
				expectedPrimaries,
				expectedTransfer,
				expectedRange,
				lutReferenceNits,
				target.color.primaries,
				target.color.transfer,
				target.repr.levels,
				target.color.hdr.max_luma,
				returnedTargetMatchesActualOutput);
		return rejection == LibplaceboDisplayLut::ContractRejection::NONE
			? nullptr
			: LibplaceboDisplayLut::ShortReason(rejection);
	}

	void ConfigureDisplayLutForTarget(
		struct pl_frame& target,
		bool returnedTargetMatchesActualOutput)
	{
		target.lut = nullptr;
		target.lut_type = PL_LUT_UNKNOWN;
		if (!displayLutParsed || !displayLut)
			return;

		const char* mismatch = DisplayLutContractMismatch(
			target, returnedTargetMatchesActualOutput);
		if (mismatch)
		{
			const std::string status = std::string("Rejected: ") + mismatch;
			if (displayLutStatus != status)
			{
				displayLutStatus = status;
				DebugLog::Log(
					"display: LUT target contract rejected (%s): file=%s requested=%s/%s/%s/%.1f actual=%s/%s/%s/%.1f dxgi=%s; rendering without a LUT",
					mismatch,
					displayLutPath.c_str(),
					lutReferencePrimaries.c_str(),
					lutReferenceTransfer.c_str(),
					lutReferenceRange.c_str(),
					lutReferenceNits,
					pl_color_primaries_name(target.color.primaries),
					pl_color_transfer_name(target.color.transfer),
					target.repr.levels == PL_COLOR_LEVELS_LIMITED
						? "limited" : "full",
					target.color.hdr.max_luma,
					LibplaceboOutput::ToString(actualOutput.encoding));
			}
			return;
		}

		target.lut = displayLut;
		target.lut_type = PL_LUT_NATIVE;
		std::ostringstream label;
		label << "Active: " << LutFileName(displayLutPath)
			<< " (" << displayLut->size[0] << "^3)";
		if (displayLutStatus != label.str())
		{
			displayLutStatus = label.str();
			DebugLog::Log(
				"display: LUT active: file=%s dimensions=%d^3 signature=%016llX stage=target/native/post-encode reference=%s/%s/%s/%.1f gamut_mapping=%s dxgi=%s",
				displayLutPath.c_str(),
				displayLut->size[0],
				static_cast<unsigned long long>(displayLut->signature),
				pl_color_primaries_name(target.color.primaries),
				pl_color_transfer_name(target.color.transfer),
				target.repr.levels == PL_COLOR_LEVELS_LIMITED
					? "limited" : "full",
				target.color.hdr.max_luma,
				colorMapParams.gamut_mapping &&
					colorMapParams.gamut_mapping->name
					? colorMapParams.gamut_mapping->name : "auto",
				LibplaceboOutput::ToString(actualOutput.encoding));
		}
	}

	void Initialize(HWND videoHwnd, VideoStateComPtr& state, const std::string& manualRule,
		const std::map<std::string, std::string>& manualUnifiedProfiles,
		VideoConversionOverride videoConversionOverride,
		bool nonCapturingPreparationMode)
	{
		this->videoHwnd = videoHwnd;
		const RendererSettings settings = LoadRendererSettings(
			*state, activeDisplayRule, manualRule, manualUnifiedProfiles);
		effectiveSettingsFingerprint = EffectiveSettingsFingerprint(settings);
		restartSettingsFingerprint = EffectiveSettingsFingerprint(settings, false);
		activeSettings = settings;
		outputDiagnostics = settings.outputDiagnostics;
		shaderCacheEnabled = !settings.diagnosticDisableShaderCache;

		struct pl_log_params logParams{};
		logParams.log_cb = LibplaceboLog;
		logParams.log_priv = &compileTelemetry;
		logParams.log_level = PL_LOG_INFO;
		log = pl_log_create(PL_API_VER, &logParams);
		if (!log)
			throw std::runtime_error("Failed to create libplacebo log context");

		struct pl_d3d11_params deviceParams =
			LibplaceboExportedData<pl_d3d11_params>("pl_d3d11_default_params");
		deviceParams.allow_software = false;
		deviceParams.min_feature_level = D3D_FEATURE_LEVEL_10_0;
		deviceParams.max_frame_latency = 2;
		deviceParams.no_compute = settings.diagnosticDisableCompute;
		d3d11 = pl_d3d11_create(log, &deviceParams);
		if (!d3d11)
			throw std::runtime_error("Failed to create libplacebo D3D11 device");
		DebugLog::Log(
			"libplacebo D3D11 device: feature_level=0x%04X software=%d allow_software=%d no_compute=%d max_frame_latency=%d",
			static_cast<unsigned int>(d3d11->device->GetFeatureLevel()),
			d3d11->software ? 1 : 0,
			deviceParams.allow_software ? 1 : 0,
			deviceParams.no_compute ? 1 : 0,
			deviceParams.max_frame_latency);

		struct pl_cache_params cacheParams =
			LibplaceboExportedData<pl_cache_params>("pl_cache_default_params");
		cacheParams.log = log;
		cacheParams.max_object_size = 64u * 1024u * 1024u;
		cacheParams.max_total_size = 256u * 1024u * 1024u;
		cache = pl_cache_create(&cacheParams);
		if (shaderCacheEnabled)
		{
			LoadShaderCache();
			pl_gpu_set_cache(d3d11->gpu, cache);
			DebugLog::Log(
				"libplacebo GPU shader cache enabled: memory limit=%u MiB object limit=%u MiB path=%s",
				256u,
				64u,
				shaderCachePath.c_str());
		}
		else
		{
			shaderCachePath = ShaderCachePath();
			DebugLog::Log(
				"libplacebo GPU shader cache disabled by diagnostic setting; persistent file left unchanged at %s",
				shaderCachePath.c_str());
		}

		ConfigFile shaderConfig;
		std::string nlsPrewarmReason;
		if (shaderConfig.Load(ConfigFile::RENDERER_FILENAME) &&
			MadVRShaderLoader::ResolveConfiguredNlsPrewarmRules(shaderConfig,
				startupNlsPrewarmRules, nlsPrewarmReason))
		{
			std::ostringstream names;
			for (size_t index = 0; index < startupNlsPrewarmRules.size(); ++index)
			{
				if (index != 0)
					names << ',';
				names << startupNlsPrewarmRules[index].name;
			}
			DebugLog::Log(
				"Alpha shader startup prewarm armed: rules=%zu names=%s",
				startupNlsPrewarmRules.size(), names.str().c_str());
		}
		else
		{
			startupNlsPrewarmComplete = true;
			DebugLog::Log(
				"Alpha shader startup prewarm skipped: %s",
				nlsPrewarmReason.c_str());
		}

		LibplaceboOutput::Request outputRequest;
		outputRequest.presentation = LibplaceboOutput::ParsePresentation(
			settings.outputPresentation);
		outputRequest.range = LibplaceboOutput::ParseRange(settings.outputRange);
		outputRequest.gamma = LibplaceboOutput::ParseGamma(
			settings.outputRange == "limited" ?
				settings.outputTransportGamma : settings.outputGamma);
		outputRequest.allowLimitedG22Experiment = settings.diagnosticAllowLimitedG22;
		outputRequest.allowFullG22Experiment = settings.diagnosticAllowFullG22;
		outputRequest.vpOwnedPresenter = settings.diagnosticVpOwnedDxgiPresenter;
		const LibplaceboOutput::SdrTargetPrimaries requestedTarget =
			settings.sdrTargetPrimaries == "bt2020"
				? LibplaceboOutput::SdrTargetPrimaries::BT2020
				: LibplaceboOutput::SdrTargetPrimaries::REC709;
		bt2020SignalingFailed = false;
		const LibplaceboOutput::SdrOutputContract outputContract =
			LibplaceboOutput::MakeSdrOutputContract(
				outputRequest, requestedTarget, settings.reportBt2020ToDisplay);
		outputRequest = outputContract.transport;
		targetBt2020 = outputContract.target ==
			LibplaceboOutput::SdrTargetPrimaries::BT2020;
		reportBt2020ToDisplay = outputContract.reportBt2020ToDisplay;
		if (targetBt2020)
			DebugLog::Log("libplacebo output contract: target=BT.2020 transform=BT.709-to-BT.2020 DXGI_transport=P709/sRGB NVIDIA_AVI=%s",
				reportBt2020ToDisplay ? "requested" : "disabled");
		else if (reportBt2020ToDisplay)
			DebugLog::Log("libplacebo: report_bt2020_to_display ignored because target primaries are Rec.709");
		requestedOutputPlan = LibplaceboOutput::MakePlan(outputRequest);
		if (outputRequest.gamma == LibplaceboOutput::GammaRequest::UNSUPPORTED)
		{
			DebugLog::Log(
				"libplacebo output request unsupported: configured_output_gamma=%s parsed=UNSUPPORTED effect=none fallback_policy=Full/sRGB reason=%s",
				settings.outputGamma.c_str(),
				requestedOutputPlan.reason.c_str());
		}
		else if (!requestedOutputPlan.valid)
		{
			DebugLog::Log(
				"libplacebo output request rejected: configured=%s/%s/%s reason=%s fallback_policy=Full/sRGB",
				settings.outputPresentation.c_str(),
				settings.outputRange.c_str(), settings.outputGamma.c_str(),
				requestedOutputPlan.reason.c_str());
		}
		LibplaceboOutput::Request effectiveOutputRequest = outputRequest;
		// The embedded preview is a WS_CHILD control. Its historically stable
		// presentation path is DWM-composed BitBlt; it cannot safely inherit a
		// fullscreen direct/BT.2020 request. Keep the configured request intact
		// for a later top-level fullscreen renderer generation.
		const LONG_PTR targetStyle = GetWindowLongPtr(videoHwnd, GWL_STYLE);
		if ((targetStyle & WS_CHILD) != 0)
		{
			effectiveOutputRequest.presentation =
				LibplaceboOutput::PresentationRequest::COMPOSED;
			effectiveOutputRequest.range =
				LibplaceboOutput::RangeRequest::FULL;
			effectiveOutputRequest.gamma =
				LibplaceboOutput::GammaRequest::SRGB;
			effectiveOutputRequest.primaries =
				LibplaceboOutput::PrimariesRequest::REC709;
		}
		outputPlan = LibplaceboOutput::MakePlan(effectiveOutputRequest);
		LogPresentationTarget("initialize");
		// COMPOSED retains the legacy bitblt/DWM path. AUTO and DIRECT ask for a
		// flip candidate on top-level hosts; the WS_CHILD preview above is still
		// explicitly composed. Windows decides whether a flip chain is composed,
		// DirectFlip, MPO, or independent flip.
		swapchainBlit = outputPlan.useBlit;
		struct pl_d3d11_swapchain_params swapchainParams{};
		swapchainParams.color_bits =
			settings.diagnosticForce8BitSdrSwapchain ? 8 : 10;
		swapchainParams.disable_10bit_sdr =
			settings.diagnosticForce8BitSdrSwapchain;
		const bool initializeVpOwnedPresenter =
			settings.diagnosticVpOwnedDxgiPresenter && !outputPlan.useBlit;
		if (settings.diagnosticVpOwnedDxgiPresenter && outputPlan.useBlit)
		{
			DebugLog::Log(
				"VP-owned DXGI swapchain: requested=1 applied=0 trigger=initialize reason=the VP-owned beta presenter supports flip/direct only; using libplacebo-owned composed swapchain");
		}
		if (initializeVpOwnedPresenter)
		{
			if (!CreateVpOwnedSwapchain(outputPlan.useBlit, "initialize"))
				throw std::runtime_error("Failed to create VP-owned DXGI swapchain");
			swapchainParams.swapchain = vpOwnedSwapchain;
		}
		else
		{
			swapchainParams.window = videoHwnd;
			swapchainParams.blit = outputPlan.useBlit;
		}
		swapchain = pl_d3d11_create_swapchain(d3d11, &swapchainParams);
		if (!swapchain)
			throw std::runtime_error("Failed to create libplacebo D3D11 swapchain");

		sdrTargetNits = settings.sdrTargetNits;
		sdrBlackNits = settings.sdrBlackNits;
		sdrInputTransfer = TranslateOutputGamma(settings.sdrInputTransfer);
		sdrAdjustGamma = LibplaceboOutput::ParseSdrAdjustGamma(
			settings.sdrAdjustGamma);
		lastSdrGammaDecision = {};
		lastSdrGammaDecisionSignature.clear();
		configuredScreenAspect = settings.configuredScreenAspect;
		configuredScreenTarget = settings.configuredScreenTarget;
		verticalAlignment = settings.verticalAlignment;
		anamorphicScale = settings.anamorphicScale;
		automaticSourceCrop = settings.automaticSourceCrop;
		scopeSubtitleFit = settings.scopeSubtitleFit;
		scopeSubtitleHoldMs = settings.scopeSubtitleHoldMs;
		scopeSubtitleEngageDriftMs = settings.scopeSubtitleEngageDriftMs;
		scopeSubtitleReleaseDriftMs = settings.scopeSubtitleReleaseDriftMs;
		scopeSubtitlePaddingPixels = settings.scopeSubtitlePaddingPixels;
		scopeSubtitleTargetBufferPixels =
			settings.scopeSubtitleTargetBufferPixels;
		SetSwapchainColorHint(
			outputPlan.valid ? outputPlan.desiredEncoding :
				LibplaceboOutput::DxgiEncoding::FULL_G22_P709,
			outputPlan.valid ? outputPlan.targetTransfer :
				LibplaceboOutput::TargetTransfer::SWAPCHAIN);

		RECT client{};
		if (!GetClientRect(videoHwnd, &client))
			throw std::runtime_error("Failed to query libplacebo render window size");
		int width = std::max<LONG>(1, client.right - client.left);
		int height = std::max<LONG>(1, client.bottom - client.top);
		if (!pl_swapchain_resize(swapchain, &width, &height))
			throw std::runtime_error("Failed to initialize libplacebo swapchain size");
		// Creating the D3D11 device/swapchain can itself cause Windows to restore
		// the desktop timing. Query and select the content rate only after that
		// transition has completed; an earlier verified no-op could otherwise
		// leave this newly initialized renderer running at the restored rate.
		if (!nonCapturingPreparationMode)
			displayRefreshRate.Switch(videoHwnd, *state, settings);
		else
			DebugLog::Log(
				"libplacebo non-capturing preparation: display refresh switching suppressed");
		// Negotiate only after libplacebo has applied its hint and completed the
		// initial ResizeBuffers operation; either may otherwise replace DXGI state.
		ConfigureAndFallback("initialize");

		renderer = pl_renderer_create(log, d3d11->gpu);
		if (!renderer)
			throw std::runtime_error("Failed to create libplacebo renderer");

		if (!IsNativeRgbUpload(state->videoFrameEncoding, videoConversionOverride))
		{
			formatter = CreateAlphaFormatter(state->videoFrameEncoding,
				videoConversionOverride);
			formatter->OnVideoState(state);
			formatterState = state;
			convertedFrame.resize(static_cast<size_t>(formatter->GetOutFrameSize()));
		}
		LoadDisplayLut(settings);
		ConfigureRenderParams(settings);

		DebugLog::Log(
			"libplacebo initialized: D3D11, %s upload, SDR target request=%s %.1f nits",
			IsNativeRgbUpload(state->videoFrameEncoding, videoConversionOverride) ?
				"native RGB" :
			((state->videoFrameEncoding == VideoFrameEncoding::V210 ||
				state->videoFrameEncoding == VideoFrameEncoding::UYVY ||
				state->videoFrameEncoding == VideoFrameEncoding::HDYC) &&
				videoConversionOverride == VideoConversionOverride::VIDEOCONVERSION_NONE ?
					"lossless P210" : "P010"),
			targetBt2020 ? "BT.2020" : "Rec.709",
			sdrTargetNits);
	}

	void ApplyViewportSettings(const RendererSettings& settings)
	{
		std::lock_guard<std::mutex> guard(renderMutex);
		const bool renderingBehaviorChanged =
			configuredScreenAspect != settings.configuredScreenAspect ||
			configuredScreenTarget != settings.configuredScreenTarget ||
			verticalAlignment != settings.verticalAlignment ||
			anamorphicScale != settings.anamorphicScale ||
			automaticSourceCrop != settings.automaticSourceCrop ||
			scopeSubtitleFit != settings.scopeSubtitleFit ||
			scopeSubtitleHoldMs != settings.scopeSubtitleHoldMs ||
			scopeSubtitleEngageDriftMs != settings.scopeSubtitleEngageDriftMs ||
			scopeSubtitleReleaseDriftMs != settings.scopeSubtitleReleaseDriftMs ||
			scopeSubtitlePaddingPixels != settings.scopeSubtitlePaddingPixels ||
			scopeSubtitleTargetBufferPixels !=
				settings.scopeSubtitleTargetBufferPixels;
		configuredScreenAspect = settings.configuredScreenAspect;
		configuredScreenTarget = settings.configuredScreenTarget;
		verticalAlignment = settings.verticalAlignment;
		anamorphicScale = settings.anamorphicScale;
		automaticSourceCrop = settings.automaticSourceCrop;
		scopeSubtitleFit = settings.scopeSubtitleFit;
		scopeSubtitleHoldMs = settings.scopeSubtitleHoldMs;
		scopeSubtitleEngageDriftMs = settings.scopeSubtitleEngageDriftMs;
		scopeSubtitleReleaseDriftMs = settings.scopeSubtitleReleaseDriftMs;
		scopeSubtitlePaddingPixels = settings.scopeSubtitlePaddingPixels;
		scopeSubtitleTargetBufferPixels =
			settings.scopeSubtitleTargetBufferPixels;
		effectiveSettingsFingerprint = EffectiveSettingsFingerprint(settings);
		restartSettingsFingerprint = EffectiveSettingsFingerprint(settings, false);
		if (renderingBehaviorChanged)
		{
			// A viewport/profile epoch cannot inherit crop proof. Reacquire from
			// current-frame shared evidence even when the source generation did
			// not change.
			nlsTransition.Reset();
			outwardPictureConfirmation = {};
			nlsGeometryAvailable = false;
			nlsTransitionWithdrawn = false;
			nlsGeometry = {};
			nlsGeometryClassification =
				ActivePictureClassification::UNAVAILABLE;
			latestActivePictureObservationSupportsCrop = false;
			nlsGeometrySourceGeneration = 0;
			activePictureAnalysisSourceGeneration = 0;
			ClearSceneVerificationSnapshot();
			ClearScopeSubtitleEvidence();
			ClearScopePresentationEvidence();
			ClearLatestActivePictureEvidence();
			nlsDecision = {};
			renderParams.hooks = nullptr;
			renderParams.num_hooks = 0;
			// The alternate profile was prewarmed with the previous crop/subtitle
			// parameters. Re-prime it lazily without dropping the live swapchain.
			ClearScopeSubtitleEvidence();
			ClearScopePresentationEvidence();
			lastSourceCropPolicy.clear();
			lastFinalPresentationPolicy.clear();
			lastFinalLayoutPolicy.clear();
		}
	}

	// A profile can change libplacebo's render description without changing the
	// D3D11 device or swapchain contract. Preserve the renderer and its program
	// cache for those changes when the file-only compatibility switch is enabled.
	// Any transport, device, or cache-policy difference remains a hard rebuild
	// boundary. Refresh-rate policy is orthogonal to shader math and may change
	// without replacing the renderer.
	bool ApplyProfileSettingsLive(const RendererSettings& settings)
	{
		std::lock_guard<std::mutex> guard(renderMutex);
		RendererSettings currentTransport = activeSettings;
		RendererSettings nextTransport = settings;
		currentTransport.switchRefreshRate = settings.switchRefreshRate;
		currentTransport.sdrTargetNits = settings.sdrTargetNits;
		currentTransport.sdrBlackNits = settings.sdrBlackNits;
		currentTransport.quality = settings.quality;
		currentTransport.toneMapping = settings.toneMapping;
		currentTransport.gamutMapping = settings.gamutMapping;
		currentTransport.peakDetection = settings.peakDetection;
		currentTransport.hasContrastRecovery = settings.hasContrastRecovery;
		currentTransport.contrastRecovery = settings.contrastRecovery;
		currentTransport.upscaler = settings.upscaler;
		currentTransport.downscaler = settings.downscaler;
		currentTransport.debandStrength = settings.debandStrength;
		currentTransport.deband = settings.deband;
		currentTransport.sigmoid = settings.sigmoid;
		currentTransport.dithering = settings.dithering;
		currentTransport.displayBitDepth = settings.displayBitDepth;
		currentTransport.outputGamma = settings.outputGamma;
		currentTransport.sdrTargetPrimaries = "rec709";
		currentTransport.reportBt2020ToDisplay = false;
		currentTransport.sdrInputTransfer = settings.sdrInputTransfer;
		currentTransport.sdrAdjustGamma = settings.sdrAdjustGamma;
		currentTransport.outputDiagnostics = settings.outputDiagnostics;
		currentTransport.lutPath = settings.lutPath;
		currentTransport.lutPathRejected = settings.lutPathRejected;
		currentTransport.lutConstrainedBaseDirectory =
			settings.lutConstrainedBaseDirectory;
		currentTransport.lutReferencePrimaries = settings.lutReferencePrimaries;
		currentTransport.lutReferenceTransfer = settings.lutReferenceTransfer;
		currentTransport.lutReferenceRange = settings.lutReferenceRange;
		currentTransport.lutReferenceNits = settings.lutReferenceNits;
		nextTransport.sdrTargetPrimaries = "rec709";
		nextTransport.reportBt2020ToDisplay = false;
		if (EffectiveSettingsFingerprint(currentTransport, false) !=
			EffectiveSettingsFingerprint(nextTransport, false))
		{
			DebugLog::Log(
				"libplacebo live profile update rejected: transport, device, or shader-cache policy differs");
			return false;
		}

		const bool lutChanged =
			activeSettings.lutPath != settings.lutPath ||
			activeSettings.lutPathRejected != settings.lutPathRejected ||
			activeSettings.lutConstrainedBaseDirectory !=
				settings.lutConstrainedBaseDirectory;
		const bool diagnosticsEnabled =
			!outputDiagnostics && settings.outputDiagnostics;
		if (lutChanged)
		{
			// libplacebo keys custom LUT state by the LUT's content signature and
			// invalidates only the outdated resource itself. Its renderer API
			// explicitly supports image/color/target changes without a cache flush,
			// so preserve compiled programs while replacing the parsed LUT.
			pl_lut_free(&displayLut);
			displayLutParsed = false;
			displayLutStatus = "Disabled";
			LoadDisplayLut(settings);
		}
		// Reference metadata describes how the already loaded LUT is interpreted;
		// changing it does not require reparsing the LUT file itself.
		lutReferencePrimaries = settings.lutReferencePrimaries;
		lutReferenceTransfer = settings.lutReferenceTransfer;
		lutReferenceRange = settings.lutReferenceRange;
		lutReferenceNits = settings.lutReferenceNits;

		LibplaceboOutput::Request requestedTransport;
		requestedTransport.presentation = LibplaceboOutput::ParsePresentation(
			settings.outputPresentation);
		requestedTransport.range = LibplaceboOutput::ParseRange(settings.outputRange);
		requestedTransport.gamma = LibplaceboOutput::ParseGamma(
			settings.outputRange == "limited" ?
				settings.outputTransportGamma : settings.outputGamma);
		requestedTransport.allowLimitedG22Experiment =
			settings.diagnosticAllowLimitedG22;
		requestedTransport.allowFullG22Experiment =
			settings.diagnosticAllowFullG22;
		requestedTransport.vpOwnedPresenter =
			settings.diagnosticVpOwnedDxgiPresenter;
		const auto target = settings.sdrTargetPrimaries == "bt2020"
			? LibplaceboOutput::SdrTargetPrimaries::BT2020
			: LibplaceboOutput::SdrTargetPrimaries::REC709;
		const auto contract = LibplaceboOutput::MakeSdrOutputContract(
			requestedTransport, target, settings.reportBt2020ToDisplay);
		const LibplaceboOutput::Plan nextOutputPlan =
			LibplaceboOutput::MakePlan(contract.transport);
		activeSettings = settings;
		sdrTargetNits = settings.sdrTargetNits;
		sdrBlackNits = settings.sdrBlackNits;
		sdrInputTransfer = TranslateOutputGamma(settings.sdrInputTransfer);
		sdrAdjustGamma = LibplaceboOutput::ParseSdrAdjustGamma(
			settings.sdrAdjustGamma);
		outputDiagnostics = settings.outputDiagnostics;
		targetBt2020 = contract.target ==
			LibplaceboOutput::SdrTargetPrimaries::BT2020;
		reportBt2020ToDisplay = contract.reportBt2020ToDisplay;
		bt2020SignalingFailed = false;
		actualOutput.targetTransfer = nextOutputPlan.targetTransfer;
		SetSwapchainColorHint(actualOutput.encoding, actualOutput.targetTransfer);
		lastSdrGammaDecision = {};
		lastSdrGammaDecisionSignature.clear();
		outputContractLogged = false;
		if (diagnosticsEnabled)
		{
			diagnosticReadbackFramesRemaining = 30;
			diagnosticReadbackComplete = false;
			diagnosticReadbackAttempts = 0;
			diagnosticReadbackNonBlack = false;
		}
		ConfigureRenderParams(settings);
		restartSettingsFingerprint = EffectiveSettingsFingerprint(settings, false);
		effectiveSettingsFingerprint = EffectiveSettingsFingerprint(settings);

		if (targetBt2020 && reportBt2020ToDisplay)
		{
			if (!nvidiaBt2020Reporter.Enable(negotiatedDisplayDeviceName.c_str()))
			{
				reportBt2020ToDisplay = false;
				bt2020SignalingFailed = true;
			}
		}
		else
		{
			nvidiaBt2020Reporter.Restore();
		}
		DebugLog::Log(
			"libplacebo profile settings applied live: target=%s luminance=%.1f nits black=%.4f nits LUT=%s processing=updated DXGI_transport=P709/sRGB NVIDIA_AVI=%s swapchain_recreated=0",
			targetBt2020 ? "BT.2020" : "Rec.709",
			sdrTargetNits,
			sdrBlackNits,
			displayLutParsed && displayLut ? "active" : "disabled",
			reportBt2020ToDisplay ? "requested" : "disabled");
		return true;
	}

	void SetShaderStatus(const std::string& status)
	{
		if (activeShaderStatus == status)
			return;
		activeShaderStatus = status;
		++activeShaderStatusSerial;
	}

	void SetConfiguredShaderSelection(const std::string& selector,
		const std::vector<ConfiguredShaderRule>& selection,
		uint64_t rendererGeneration)
	{
		requestedShaderSelector =
			MadVRShaderLoader::CanonicalizeRuleSelector(selector);
		nlsRendererGeneration = rendererGeneration;
		nlsRequested = false;
		nlsRule = {};
		ActivePictureTransitionModel::SetRuntimeStableGeometryDeadbandPercent(
			ActivePictureTransitionModel::
				DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT);
		// Shader selection changes presentation, not the captured source. Keep
		// generation-current active-picture authority and its transition history
		// so toggling NLS cannot flash through full-raster/pillarbox geometry.
		nlsDecision = {};
		nlsHookSignature.clear();
		rejectedNlsHookSignature.clear();
		activeNlsShaderPath.clear();
		lastNlsPipelineVariant.clear();
		lastNlsHookBindingPolicy.clear();
		nlsPipelineWasActive = false;
		renderParams.hooks = nullptr;
		renderParams.num_hooks = 0;
		pl_mpv_user_shader_destroy(&nlsHook);

		const auto none = std::find_if(selection.begin(), selection.end(),
			[](const ConfiguredShaderRule& rule) { return rule.none; });
		if (none != selection.end())
		{
			SetShaderStatus("NLS: Off");
			MadVRShaderLoader::SetRuntimeShaderSelection(
				requestedShaderSelector, requestedShaderSelector,
				NlsMappingMode::OFF);
			DebugLog::Log(
				"Alpha shaders: selected \"%s\" (off)",
				requestedShaderSelector.c_str());
			return;
		}

		const auto nls = std::find_if(selection.begin(), selection.end(),
			[](const ConfiguredShaderRule& rule) { return rule.nls; });
		if (nls == selection.end() || nls->filename.empty())
		{
			SetShaderStatus("Rejected: Alpha NLS rule required");
			DebugLog::Log(
				"Alpha shaders: selector \"%s\" has no applicable GLSL NLS rule",
				requestedShaderSelector.c_str());
			return;
		}

		nlsRule = *nls;
		nlsTransition.SetStableGeometryDeadbandPercent(
			nlsRule.stableGeometryDeadbandPercent);
		ActivePictureTransitionModel::SetRuntimeStableGeometryDeadbandPercent(
			nlsRule.stableGeometryDeadbandPercent);
		nlsRequested = true;
		SetShaderStatus("NLS: Waiting");
		MadVRShaderLoader::SetRuntimeShaderSelection(
			requestedShaderSelector, requestedShaderSelector,
			NlsMappingMode::WAITING);
		DebugLog::Log(
			"Alpha shaders: armed \"%s\" with applicable rule \"%s\" file=%s renderer_generation=%llu",
			requestedShaderSelector.c_str(), nlsRule.name.c_str(),
			nlsRule.filename.c_str(),
			static_cast<unsigned long long>(nlsRendererGeneration));
	}

	static std::map<std::string, std::string> FixedNlsParameters(
		const ConfiguredShaderRule& rule)
	{
		std::map<std::string, std::string> parameters = rule.parameters;
		// These values are runtime inputs (or legacy no-ops), not shader-source
		// constants. Keeping them out of the source signature prevents active-
		// picture or aspect changes from manufacturing new compiler variants.
		parameters.erase("stretch_ratio");
		parameters.erase("warp_axis");
		parameters.erase("safe_fit");
		parameters.erase("safe_fit_axis");
		parameters.erase("safe_fit_fraction");
		return parameters;
	}

	static std::string NlsHookKey(const ConfiguredShaderRule& rule,
		const std::map<std::string, std::string>& parameters)
	{
		std::ostringstream keyBuilder;
		keyBuilder << rule.filename;
		for (const auto& parameter : parameters)
			keyBuilder << '|' << parameter.first << '=' << parameter.second;
		return keyBuilder.str();
	}

	bool CreateNlsHook(const ConfiguredShaderRule& rule,
		const struct pl_hook*& hook, std::string& hookKey,
		std::string& resolvedPath, std::string& reason)
	{
		const std::map<std::string, std::string> parameters =
			FixedNlsParameters(rule);
		hookKey = NlsHookKey(rule, parameters);
		std::string source;
		if (!ReadUserShader(rule.filename, source, resolvedPath, reason))
			return false;
		if (!ApplyUserShaderParameters(source, parameters, reason))
			return false;
		hook = pl_mpv_user_shader_parse(
			d3d11->gpu, source.data(), source.size());
		if (!hook)
		{
			reason = "libplacebo could not parse shader";
			return false;
		}
		return true;
	}

	struct NlsHookMappingState
	{
		bool stretchAvailable = false;
		bool axisAvailable = false;
		float stretchRatio = 0.0f;
		float warpAxis = 0.0f;
		int parameterCount = 0;
		std::string floatParameterNames;
	};

	static NlsHookMappingState ReadNlsHookMapping(const struct pl_hook* hook)
	{
		NlsHookMappingState state;
		if (!hook)
			return state;
		state.parameterCount = hook->num_parameters;
		for (int index = 0; index < hook->num_parameters; ++index)
		{
			const struct pl_hook_par& parameter = hook->parameters[index];
			if (!parameter.name || !parameter.data ||
				parameter.type != PL_VAR_FLOAT)
			{
				continue;
			}
			if (!state.floatParameterNames.empty())
				state.floatParameterNames += ',';
			state.floatParameterNames += parameter.name;
			if (strcmp(parameter.name, "stretch_ratio") == 0)
			{
				state.stretchAvailable = true;
				state.stretchRatio = parameter.data->f;
			}
			else if (strcmp(parameter.name, "warp_axis") == 0)
			{
				state.axisAvailable = true;
				state.warpAxis = parameter.data->f;
			}
		}
		return state;
	}

	static bool SetNlsHookMapping(const struct pl_hook* hook,
		const NlsMappingDecision& decision,
		NlsHookMappingState* boundState = nullptr)
	{
		bool stretchUpdated = false;
		bool axisUpdated = false;
		if (!hook)
			return false;
		for (int index = 0; index < hook->num_parameters; ++index)
		{
			const struct pl_hook_par& parameter = hook->parameters[index];
			if (!parameter.name || !parameter.data ||
				parameter.type != PL_VAR_FLOAT)
			{
				continue;
			}
			if (strcmp(parameter.name, "stretch_ratio") == 0)
			{
				parameter.data->f = static_cast<float>(decision.stretchRatio);
				stretchUpdated = true;
			}
			else if (strcmp(parameter.name, "warp_axis") == 0)
			{
				parameter.data->f = decision.verticalWarp ? 1.0f : 0.0f;
				axisUpdated = true;
			}
		}
		if (boundState)
			*boundState = ReadNlsHookMapping(hook);
		return stretchUpdated && axisUpdated;
	}

	void LogNlsHookBinding(const NlsMappingDecision& decision,
		const NlsHookMappingState& binding)
	{
		double strength = 1.0;
		const auto configuredStrength = nlsRule.parameters.find("strength");
		if (configuredStrength != nlsRule.parameters.end())
			ParseDouble(configuredStrength->second, strength);
		strength = std::max(0.0, std::min(1.0, strength));

		double xCenterScale = 1.0;
		double yCenterScale = 1.0;
		double requestedBalance = 0.0;
		double effectiveBalance = 0.0;
		double maximumCenterZoom = 1.08;
		const auto configuredBalance = nlsRule.parameters.find("axis_balance");
		if (configuredBalance != nlsRule.parameters.end())
		{
			double balance = 0.5;
			ParseDouble(configuredBalance->second, balance);
			balance = std::max(0.0, std::min(1.0, balance));
			const auto configuredMaximumCenterZoom =
				nlsRule.parameters.find("max_center_zoom");
			if (configuredMaximumCenterZoom != nlsRule.parameters.end())
				ParseDouble(configuredMaximumCenterZoom->second,
					maximumCenterZoom);
			maximumCenterZoom = std::max(1.0,
				std::min(1.25, maximumCenterZoom));
			requestedBalance = balance * strength;
			effectiveBalance = requestedBalance;
			if (decision.stretchRatio > 1.000001)
			{
				const double balanceLimit = std::log(maximumCenterZoom) /
					std::log(decision.stretchRatio);
				effectiveBalance = std::min(effectiveBalance,
					std::max(0.0, std::min(1.0, balanceLimit)));
			}
			const double q = decision.verticalWarp
				? 1.0 / decision.stretchRatio : decision.stretchRatio;
			const double xExponent = decision.verticalWarp
				? effectiveBalance : 1.0 - effectiveBalance;
			const double yExponent = decision.verticalWarp
				? effectiveBalance - 1.0 : -effectiveBalance;
			xCenterScale = std::pow(q, xExponent);
			yCenterScale = std::pow(q, yExponent);
		}
		else
		{
			const double selectedAxisScale =
				1.0 + (decision.stretchRatio - 1.0) * strength;
			if (decision.verticalWarp)
				yCenterScale = selectedAxisScale;
			else
				xCenterScale = selectedAxisScale;
		}

		std::ostringstream policy;
		policy << nlsHookSignature << '|' << binding.parameterCount << '|'
			<< binding.floatParameterNames << '|'
			<< std::lround(binding.stretchRatio * 100000.0f) << '|'
			<< std::lround(binding.warpAxis * 100000.0f) << '|'
			<< std::lround(xCenterScale * 100000.0) << '|'
			<< std::lround(yCenterScale * 100000.0);
		if (policy.str() == lastNlsHookBindingPolicy)
			return;
		lastNlsHookBindingPolicy = policy.str();
		DebugLog::Log(
			"Alpha NLS hook binding: shader=%s parameters=%d float_parameters=%s stretch_ratio=%.5f warp_axis=%.1f axis=%s strength=%.3f requested_balance=%.5f effective_balance=%.5f max_center_zoom=%.5f expected_center_scale_x=%.5f expected_center_scale_y=%.5f",
			activeNlsShaderPath.empty() ? nlsRule.filename.c_str() :
				activeNlsShaderPath.c_str(),
			binding.parameterCount,
			binding.floatParameterNames.empty() ? "(none)" :
				binding.floatParameterNames.c_str(),
			binding.stretchRatio, binding.warpAxis,
			decision.verticalWarp ? "vertical" : "horizontal",
			strength, requestedBalance, effectiveBalance, maximumCenterZoom,
			xCenterScale, yCenterScale);
	}

	bool EnsureNlsHook(const NlsMappingDecision& decision)
	{
		const std::map<std::string, std::string> parameters =
			FixedNlsParameters(nlsRule);
		const std::string hookKey = NlsHookKey(nlsRule, parameters);
		if (nlsHook && nlsHookSignature == hookKey)
		{
			NlsHookMappingState binding;
			const bool updated = SetNlsHookMapping(
				nlsHook, decision, &binding);
			if (updated)
				LogNlsHookBinding(decision, binding);
			return updated;
		}
		if (rejectedNlsHookSignature == hookKey)
			return false;

		std::string resolvedPath;
		std::string reason;
		std::string replacementKey;
		const struct pl_hook* replacement = nullptr;
		if (!CreateNlsHook(nlsRule, replacement, replacementKey,
			resolvedPath, reason))
		{
			rejectedNlsHookSignature = hookKey;
			DebugLog::Log(
				"Alpha shaders: rejected \"%s\": %s",
				nlsRule.filename.c_str(), reason.c_str());
			return false;
		}
		NlsHookMappingState binding;
		if (!SetNlsHookMapping(replacement, decision, &binding))
		{
			pl_mpv_user_shader_destroy(&replacement);
			rejectedNlsHookSignature = hookKey;
			DebugLog::Log(
				"Alpha shaders: rejected \"%s\": dynamic NLS parameters are unavailable",
				nlsRule.filename.c_str());
			return false;
		}
		pl_mpv_user_shader_destroy(&nlsHook);
		nlsHook = replacement;
		nlsHookSignature = replacementKey;
		rejectedNlsHookSignature.clear();
		activeNlsShaderPath = resolvedPath;
		LogNlsHookBinding(decision, binding);
		const auto balance = nlsRule.parameters.find("axis_balance");
		DebugLog::Log(
			"Alpha shaders: loaded mpv GLSL hook \"%s\" stretch=%.5f axis=%s axis_balance=%s",
			resolvedPath.c_str(), decision.stretchRatio,
			decision.verticalWarp ? "vertical" : "horizontal",
			balance == nlsRule.parameters.end() ? "single-axis" :
				balance->second.c_str());
		return true;
	}

	static bool ActivePictureBoundsContain(
		const ActivePictureBounds& outer,
		const ActivePictureBounds& inner)
	{
		return outer.rasterWidth > 0 && outer.rasterHeight > 0 &&
			outer.rasterWidth == inner.rasterWidth &&
			outer.rasterHeight == inner.rasterHeight &&
			outer.left <= inner.left && outer.top <= inner.top &&
			outer.right >= inner.right && outer.bottom >= inner.bottom;
	}

	void ClearSceneVerificationSnapshot()
	{
		activePictureSceneVerificationDeadlineTick = 0;
		sceneVerificationGeometryAvailable = false;
		sceneVerificationGeometry = {};
		sceneVerificationGeometrySourceGeneration = 0;
		sceneVerificationLatestSupportsCrop = false;
	}

	void ClearLatestActivePictureEvidence()
	{
		latestActivePictureEvidenceAvailable = false;
		latestActivePictureEvidenceClassification =
			ActivePictureClassification::UNAVAILABLE;
		latestActivePictureEvidenceBounds = {};
		latestActivePictureEvidenceFrame = 0;
		latestActivePictureEvidenceWasStartupHypothesis = false;
		presentationOwnedGeometryTransitionDeferred = false;
		latestActivePicturePresentationRetentionSafe = false;
		latestActivePicturePresentationRetentionEvaluated = false;
		latestActivePicturePresentationRetentionReason.clear();
		fullRasterPresentationAuthorityAvailable = false;
		fullRasterPresentationAuthoritySourceGeneration = 0;
	}

	void UpdateNlsForFrame(const AnalysisLumaSource& analysisSource,
		uint64_t frameNumber,
		const ActivePictureFrameIdentity& currentIdentity,
		double framesPerSecond,
		bool configuredScreenActive,
		AlphaSourceCrop::SceneHoldDecision& sceneHold,
		bool forceAnalysis = false,
		const ActivePictureFrameDecision* scheduledDecision = nullptr)
	{
		renderParams.hooks = nullptr;
		renderParams.num_hooks = 0;
		if (activePictureAnalysisSourceGeneration != analysisSource.generation)
		{
			nlsTransition.Reset();
			outwardPictureConfirmation = {};
			nlsGeometryAvailable = false;
			nlsTransitionWithdrawn = false;
			nlsGeometry = {};
			nlsGeometryClassification =
				ActivePictureClassification::UNAVAILABLE;
			latestActivePictureObservationSupportsCrop = false;
			nlsGeometrySourceGeneration = 0;
			activePictureAnalysisSourceGeneration = analysisSource.generation;
			activePictureAmbiguityHold.Reset();
			ClearSceneVerificationSnapshot();
			ClearLatestActivePictureEvidence();
			ClearScopeSubtitleEvidence();
			ClearScopePresentationEvidence();
			lastSourceCropPolicy.clear();
			lastFinalPresentationPolicy.clear();
			lastFinalLayoutPolicy.clear();
			DebugLog::Log(
				"Alpha active picture authority reset: source_generation=%llu",
				static_cast<unsigned long long>(analysisSource.generation));
		}

		const bool needsActivePictureAnalysis =
			nlsRequested || automaticSourceCrop || scopeSubtitleFit;
		if (!needsActivePictureAnalysis)
			return;

		const bool scheduledAnalysis =
			nlsTransition.ShouldAnalyze(frameNumber, framesPerSecond);
		const bool hasScheduledDecision = scheduledDecision &&
			scheduledDecision->transition.publish &&
			scheduledDecision->transition.stable;
		const bool trustedCropIsCurrentGeneration =
			nlsGeometryAvailable &&
			nlsGeometryClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED &&
			nlsGeometrySourceGeneration == analysisSource.generation;
		const bool sceneSnapshotIsCurrentGeneration =
			sceneVerificationGeometryAvailable &&
			sceneVerificationGeometrySourceGeneration ==
				analysisSource.generation;
		// Inspect every rendered frame while any crop presentation is active.
		// This catches a direct bars-to-live-raster cut on a non-scheduled frame;
		// sparse acquisition cadence is still used when no pixels are excluded.
		const bool forceRetentionSafetyAnalysis =
			AlphaSourceCrop::RequiresPerFramePresentationInspection(
				trustedCropIsCurrentGeneration,
				sceneSnapshotIsCurrentGeneration,
				latestActivePicturePresentationRetentionSafe);
		if (scheduledAnalysis || forceAnalysis || hasScheduledDecision ||
			forceRetentionSafetyAnalysis)
		{
			const bool hadCurrentTrustedCropGeometry =
				trustedCropIsCurrentGeneration;
			ActivePictureBounds presentationBeforeObservation;
			bool hadCompatiblePresentation = false;
			if (hadCurrentTrustedCropGeometry)
			{
				presentationBeforeObservation = nlsGeometry;
				hadCompatiblePresentation = true;
			}
			else if (sceneVerificationGeometryAvailable &&
				sceneVerificationGeometrySourceGeneration ==
					analysisSource.generation)
			{
				presentationBeforeObservation = sceneVerificationGeometry;
				hadCompatiblePresentation = true;
			}
			ActivePicturePresentationRetentionEvidence retentionEvidence;
			ActivePictureEvidence evidence;
			if (hadCompatiblePresentation)
			{
				retentionEvidence =
					EvaluateActivePicturePresentationRetention(
						analysisSource, presentationBeforeObservation);
				evidence = retentionEvidence.activePicture;
			}
			else
			{
				evidence = ExtractActivePictureEvidence(analysisSource);
			}
			latestActivePictureEvidenceWasStartupHypothesis = false;
			if (!hadCompatiblePresentation && evidence.available &&
				evidence.classification ==
					ActivePictureClassification::PROVISIONAL)
			{
				const ActivePictureEvidence hypothesis =
					EvaluateSymmetricVerticalBarHypothesis(
						analysisSource, evidence);
				if (hypothesis.classification ==
					ActivePictureClassification::BAR_CROP_TRUSTED)
				{
					evidence = hypothesis;
					latestActivePictureEvidenceWasStartupHypothesis = true;
				}
			}
			latestActivePictureObservationSupportsCrop = false;
			latestActivePictureEvidenceAvailable = evidence.available;
			latestActivePictureEvidenceClassification = evidence.available
				? evidence.classification
				: ActivePictureClassification::UNAVAILABLE;
			latestActivePictureEvidenceBounds = evidence.available
				? (evidence.classification ==
					ActivePictureClassification::PROVISIONAL
					? evidence.proposedBounds : evidence.trustedBounds)
				: ActivePictureBounds{};
			latestActivePictureEvidenceFrame = frameNumber;
			const bool currentBoundsAreFullRaster =
				latestActivePictureEvidenceBounds.left == 0 &&
				latestActivePictureEvidenceBounds.top == 0 &&
				latestActivePictureEvidenceBounds.right == analysisSource.width &&
				latestActivePictureEvidenceBounds.bottom == analysisSource.height &&
				latestActivePictureEvidenceBounds.rasterWidth ==
					analysisSource.width &&
				latestActivePictureEvidenceBounds.rasterHeight ==
					analysisSource.height;
			fullRasterPresentationAuthorityAvailable =
				AlphaSourceCrop::UpdateFullRasterPresentationAuthority(
					fullRasterPresentationAuthorityAvailable,
					latestActivePictureEvidenceClassification,
					currentBoundsAreFullRaster);
			fullRasterPresentationAuthoritySourceGeneration =
				fullRasterPresentationAuthorityAvailable
					? analysisSource.generation : 0;
			const bool ambiguousEvidence = !evidence.available ||
				evidence.classification ==
					ActivePictureClassification::PROVISIONAL ||
				evidence.classification ==
					ActivePictureClassification::UNAVAILABLE;
			latestActivePicturePresentationRetentionSafe =
				hadCompatiblePresentation && ambiguousEvidence &&
				retentionEvidence.currentlyPixelSafe;
			latestActivePicturePresentationRetentionEvaluated =
				hadCompatiblePresentation && retentionEvidence.analysisValid &&
				retentionEvidence.presentationValid;
			latestActivePicturePresentationRetentionReason =
				hadCompatiblePresentation ? retentionEvidence.reason :
					"no compatible retained presentation";
			const uint64_t now = GetTickCount64();
			auto sameBounds = [](const ActivePictureBounds& left,
				const ActivePictureBounds& right)
			{
				return left.left == right.left && left.top == right.top &&
					left.right == right.right && left.bottom == right.bottom &&
					left.rasterWidth == right.rasterWidth &&
					left.rasterHeight == right.rasterHeight;
			};
			if (automaticSourceCrop && configuredScreenActive &&
				hadCompatiblePresentation &&
				(evidence.available ||
					retentionEvidence.outwardVisibleBoundsAvailable))
			{
				ActivePictureBounds observed = presentationBeforeObservation;
				if (evidence.available)
					observed = evidence.classification ==
						ActivePictureClassification::PROVISIONAL
					? evidence.proposedBounds : evidence.trustedBounds;
				if (retentionEvidence.outwardVisibleBoundsAvailable)
				{
					observed.left = std::min(observed.left,
						retentionEvidence.outwardVisibleBounds.left);
					observed.top = std::min(observed.top,
						retentionEvidence.outwardVisibleBounds.top);
					observed.right = std::max(observed.right,
						retentionEvidence.outwardVisibleBounds.right);
					observed.bottom = std::max(observed.bottom,
						retentionEvidence.outwardVisibleBounds.bottom);
				}
				ActivePictureBounds outward = presentationBeforeObservation;
				outward.left = std::min(outward.left, observed.left) & ~1;
				outward.top = std::min(outward.top, observed.top) & ~1;
				outward.right = std::min(analysisSource.width,
					(std::max(outward.right, observed.right) + 1) & ~1);
				outward.bottom = std::min(analysisSource.height,
					(std::max(outward.bottom, observed.bottom) + 1) & ~1);
				outward.rasterWidth = analysisSource.width;
				outward.rasterHeight = analysisSource.height;
				outward.aspectRatio = static_cast<double>(
					outward.right - outward.left) /
					std::max(1, outward.bottom - outward.top);
				outward.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
				const bool expands =
					outward.left < presentationBeforeObservation.left ||
					outward.top < presentationBeforeObservation.top ||
					outward.right > presentationBeforeObservation.right ||
					outward.bottom > presentationBeforeObservation.bottom;
				if (expands)
				{
					// The accumulated envelope is useful for its same-edge release
					// hold. Keep the raw observation as well: top and bottom overlays
					// observed at different moments must not later become one vertical
					// aspect-fit decision.
					scopePresentationCurrentBounds = outward;
					scopePresentationCurrentSourceGeneration =
						analysisSource.generation;
					scopePresentationCurrentSourceSequence = frameNumber;
					const bool sameBase =
						scopePresentationEvidenceSourceGeneration ==
							analysisSource.generation &&
						sameBounds(scopePresentationEvidenceBase,
							presentationBeforeObservation);
					const bool envelopeChanged = !sameBase ||
						outward.left < scopePresentationEvidenceBounds.left ||
						outward.top < scopePresentationEvidenceBounds.top ||
						outward.right > scopePresentationEvidenceBounds.right ||
						outward.bottom > scopePresentationEvidenceBounds.bottom;
					if (!sameBase)
					{
						scopePresentationEvidenceBase =
							presentationBeforeObservation;
						scopePresentationEvidenceBounds = outward;
					}
					else
					{
						// Match mpv cropdetect's reset=0 behavior: while outward
						// content is present, retain the widest measured envelope.
						scopePresentationEvidenceBounds.left = std::min(
							scopePresentationEvidenceBounds.left, outward.left);
						scopePresentationEvidenceBounds.top = std::min(
							scopePresentationEvidenceBounds.top, outward.top);
						scopePresentationEvidenceBounds.right = std::max(
							scopePresentationEvidenceBounds.right, outward.right);
						scopePresentationEvidenceBounds.bottom = std::max(
							scopePresentationEvidenceBounds.bottom, outward.bottom);
						scopePresentationEvidenceBounds.aspectRatio =
							static_cast<double>(
								scopePresentationEvidenceBounds.right -
								scopePresentationEvidenceBounds.left) /
							std::max(1,
								scopePresentationEvidenceBounds.bottom -
								scopePresentationEvidenceBounds.top);
					}
					scopePresentationEvidenceSourceGeneration =
						analysisSource.generation;
					scopePresentationEvidenceSourceSequence = frameNumber;
					scopePresentationEvidenceLastTick = now;
					if (envelopeChanged)
					{
						const ActivePictureBounds& raw =
							retentionEvidence.outwardVisibleBoundsAvailable
							? retentionEvidence.outwardVisibleBounds : observed;
						DebugLog::Log(
							"Alpha presentation envelope: sequence=%llu generation=%llu base=%d,%d-%d,%d raw=%d,%d-%d,%d stored=%d,%d-%d,%d edges=%c%c%c%c reason=detected",
							static_cast<unsigned long long>(frameNumber),
							static_cast<unsigned long long>(analysisSource.generation),
							presentationBeforeObservation.left,
							presentationBeforeObservation.top,
							presentationBeforeObservation.right,
							presentationBeforeObservation.bottom,
							raw.left, raw.top, raw.right, raw.bottom,
							scopePresentationEvidenceBounds.left,
							scopePresentationEvidenceBounds.top,
							scopePresentationEvidenceBounds.right,
							scopePresentationEvidenceBounds.bottom,
							outward.left < presentationBeforeObservation.left ? 'L' : '-',
							outward.top < presentationBeforeObservation.top ? 'T' : '-',
							outward.right > presentationBeforeObservation.right ? 'R' : '-',
							outward.bottom > presentationBeforeObservation.bottom ? 'B' : '-');
					}
				}
			}
			sceneVerificationLatestSupportsCrop =
				sceneVerificationGeometryAvailable && evidence.available &&
				evidence.classification ==
					ActivePictureClassification::BAR_CROP_TRUSTED &&
				ActivePictureBoundsContain(
					sceneVerificationGeometry, evidence.trustedBounds);
			ActivePictureObservation observation;
			observation.frameNumber = frameNumber;
			observation.available = evidence.available;
			observation.framesPerSecond = framesPerSecond;
			const bool deferPresentationOwnedTransition = evidence.available &&
				nlsGeometryAvailable &&
				nlsGeometrySourceGeneration == analysisSource.generation &&
				AlphaSourceCrop::ShouldDeferVerticalGeometryTransition(
					nlsGeometry, evidence.trustedBounds, evidence.classification,
					scopeVerticalBarPresentation, scopeSubtitleDrift.IsActive(),
					scopeSubtitleEvidenceSourceGeneration,
					analysisSource.generation);
			const AlphaSourceCrop::OutwardPictureConfirmationDecision
				outwardConfirmation = hadCompatiblePresentation && evidence.available
				? AlphaSourceCrop::ConfirmOutwardPictureTransition(
					outwardPictureConfirmation, presentationBeforeObservation,
					latestActivePictureEvidenceBounds, retentionEvidence,
					analysisSource.generation)
				: AlphaSourceCrop::OutwardPictureConfirmationDecision{};
			outwardPictureConfirmation = outwardConfirmation.state;
			const bool deferOutwardLogicalTransition =
				outwardConfirmation.outwardTransition &&
				!outwardConfirmation.authoritative;
			if (deferPresentationOwnedTransition !=
				presentationOwnedGeometryTransitionDeferred)
			{
				DebugLog::Log(
					"Alpha active-picture transition: presentation-owned vertical expansion %s; stable=%d,%d-%d,%d candidate=%d,%d-%d,%d generation=%llu",
					deferPresentationOwnedTransition ? "deferred" : "released",
					nlsGeometry.left, nlsGeometry.top,
					nlsGeometry.right, nlsGeometry.bottom,
					evidence.trustedBounds.left, evidence.trustedBounds.top,
					evidence.trustedBounds.right, evidence.trustedBounds.bottom,
					static_cast<unsigned long long>(analysisSource.generation));
				presentationOwnedGeometryTransitionDeferred =
					deferPresentationOwnedTransition;
			}
			if (evidence.available)
			{
				observation.bounds = evidence.classification ==
					ActivePictureClassification::PROVISIONAL
					? evidence.proposedBounds
					: evidence.trustedBounds;
				observation.classification =
					(deferPresentationOwnedTransition ||
						deferOutwardLogicalTransition)
					? ActivePictureClassification::PROVISIONAL
					: evidence.classification;
			}
			ActivePictureScheduledDecisionValidation scheduledValidation =
				hasScheduledDecision
				? ((deferPresentationOwnedTransition ||
					deferOutwardLogicalTransition)
					? ActivePictureScheduledDecisionValidation::NON_AUTHORITATIVE
					: ValidateActivePictureScheduledDecision(
					*scheduledDecision, currentIdentity,
					latestActivePictureEvidenceBounds,
					latestActivePictureEvidenceClassification))
				: ActivePictureScheduledDecisionValidation::NON_AUTHORITATIVE;
			const bool applyScheduledDecision = hasScheduledDecision &&
				scheduledValidation ==
					ActivePictureScheduledDecisionValidation::ACCEPTED &&
				nlsTransition.AdoptPublishedDecision(
					scheduledDecision->transition, evidence.classification);
			if (hasScheduledDecision &&
				scheduledValidation ==
					ActivePictureScheduledDecisionValidation::ACCEPTED &&
				!applyScheduledDecision)
			{
				scheduledValidation = ActivePictureScheduledDecisionValidation::
					NON_AUTHORITATIVE;
			}
			const ActivePictureTransitionDecision transition =
				applyScheduledDecision ? scheduledDecision->transition :
				nlsTransition.Observe(observation);
			if (hasScheduledDecision && !applyScheduledDecision)
			{
				DebugLog::Log(
					"Alpha active-picture look-ahead rejected: generation=%llu observed=%llu effective=%llu frame=%llu classification=%d reason=%s runtime-apply=0",
					static_cast<unsigned long long>(
						scheduledDecision->effectiveIdentity.transportGeneration),
					static_cast<unsigned long long>(
						scheduledDecision->observationIdentity.acceptedSequence),
					static_cast<unsigned long long>(
						scheduledDecision->effectiveIdentity.acceptedSequence),
					static_cast<unsigned long long>(frameNumber),
					static_cast<int>(evidence.classification),
					ActivePictureScheduledDecisionValidationName(
						scheduledValidation));
			}
			if (transition.clearTransition)
			{
				nlsGeometryAvailable = false;
				nlsTransitionWithdrawn = true;
				nlsGeometry = {};
				nlsGeometryClassification =
					ActivePictureClassification::UNAVAILABLE;
				latestActivePictureObservationSupportsCrop = false;
				nlsGeometrySourceGeneration = 0;
			}
			else if (transition.publish && transition.stable)
			{
				nlsGeometry = transition.bounds;
				nlsGeometryAvailable = true;
				nlsTransitionWithdrawn = false;
				nlsGeometryClassification = evidence.classification;
				nlsGeometrySourceGeneration = analysisSource.generation;
				++nlsGeometryGeneration;
			}
			else if (transition.stable && !nlsTransitionWithdrawn)
			{
				// Retain the published geometry as temporal context for NLS. The
				// source-crop policy independently requires the latest observation
				// to reaffirm authority, so ambiguity expands to full raster.
				nlsGeometry = transition.stableBounds;
				nlsGeometryAvailable = true;
			}
			if (nlsGeometryAvailable && evidence.available &&
				evidence.classification ==
					ActivePictureClassification::BAR_CROP_TRUSTED &&
				evidence.trustedBounds.trustedBarAxes !=
					ActivePictureBounds::BarAxes::NONE &&
				nlsGeometry.rasterWidth == evidence.trustedBounds.rasterWidth &&
				nlsGeometry.rasterHeight == evidence.trustedBounds.rasterHeight &&
				// The retained crop may include extra bar pixels, but it must not
				// exclude any area the latest trusted observation calls picture.
				nlsGeometry.left <= evidence.trustedBounds.left &&
				nlsGeometry.top <= evidence.trustedBounds.top &&
				nlsGeometry.right >= evidence.trustedBounds.right &&
				nlsGeometry.bottom >= evidence.trustedBounds.bottom)
			{
				latestActivePictureObservationSupportsCrop = true;
				nlsGeometryClassification =
					ActivePictureClassification::BAR_CROP_TRUSTED;
				nlsGeometrySourceGeneration = analysisSource.generation;
			}
			// A dark/ambiguous sample may bridge a normal cut or fade, but it
			// cannot renew its own authority. Only a fresh trusted observation
			// can rearm this source-generation-local budget.
			activePictureAmbiguityHold.Observe(now, analysisSource.generation,
				hadCurrentTrustedCropGeometry,
				latestActivePictureObservationSupportsCrop,
				latestActivePictureEvidenceClassification,
				ACTIVE_PICTURE_AMBIGUITY_HOLD_MS);
			if (applyScheduledDecision)
			{
				DebugLog::Log(
					"Alpha active-picture look-ahead applied: generation=%llu observed=%llu effective=%llu frame=%llu rect=%d,%d-%d,%d classification=%d runtime-apply=1",
					static_cast<unsigned long long>(
						scheduledDecision->effectiveIdentity.transportGeneration),
					static_cast<unsigned long long>(
						scheduledDecision->observationIdentity.acceptedSequence),
					static_cast<unsigned long long>(
						scheduledDecision->effectiveIdentity.acceptedSequence),
					static_cast<unsigned long long>(frameNumber),
					transition.bounds.left, transition.bounds.top,
					transition.bounds.right, transition.bounds.bottom,
					static_cast<int>(evidence.classification));
			}
			if (transition.diagnostic)
			{
				DebugLog::Log(
					"Alpha NLS active picture: state=%d frame=%llu rect=%d,%d-%d,%d aspect=%.4f stable=%d clear=%d classification=%d retention_safe=%d reason=\"%s; %s; %s\"",
					static_cast<int>(transition.state),
					static_cast<unsigned long long>(frameNumber),
					transition.bounds.left, transition.bounds.top,
					transition.bounds.right, transition.bounds.bottom,
					transition.bounds.aspectRatio,
					transition.stable ? 1 : 0,
					transition.clearTransition ? 1 : 0,
					static_cast<int>(evidence.classification),
					latestActivePicturePresentationRetentionSafe ? 1 : 0,
					transition.reason.c_str(), evidence.reason.c_str(),
					latestActivePicturePresentationRetentionReason.c_str());
			}
		}
		const bool sceneNlsHoldActive = sceneHold.nlsActive &&
			(latestActivePictureEvidenceClassification ==
				ActivePictureClassification::PROVISIONAL ||
			 latestActivePictureEvidenceClassification ==
				ActivePictureClassification::UNAVAILABLE ||
			 sceneVerificationLatestSupportsCrop);
		if (sceneNlsHoldActive &&
			(!nlsGeometryAvailable ||
			 latestActivePictureEvidenceClassification ==
				ActivePictureClassification::PROVISIONAL ||
			 latestActivePictureEvidenceClassification ==
				ActivePictureClassification::UNAVAILABLE))
		{
			// The final presentation pass consumes this retained geometry and owns
			// both crop and NLS. Analysis must not publish an independent hook state.
			return;
		}
	}

	// The caller holds renderMutex. Queue-generation validation and rendering
	// share that lock so a frame removed before a reset cannot cross the reset
	// boundary while waiting to enter libplacebo.
	bool RenderLocked(
		const VideoFrame& videoFrame,
		VideoStateComPtr& statePtr,
		uint64_t frameGeneration,
		uint64_t sourceSequence,
		const ActivePictureFrameIdentity& activePictureIdentity,
		const ActivePictureFrameDecision* activePicturePreviewDecision,
		int64_t enqueueQpc,
		int64_t dequeueQpc,
		size_t queueDepthAfterDequeue,
		size_t desiredQueueDepth,
		double oldestQueuedAgeMs,
		bool cadenceRepeat,
		double captureRateHz,
		bool configuredScreenActive,
		uint64_t viewportRequestSerial,
		int64_t viewportRequestNs,
		bool sceneDetectionEnabled,
		VideoConversionOverride videoConversionOverride,
		uint64_t sceneDetectorGeneration,
		std::atomic<uint64_t>& sceneDetectedCount,
		std::atomic<int>& sceneDetectionStatus,
		AlphaCadenceCorrectionDecision& correctionDecision,
		bool& presentationTargetTimingKnown,
		double& presentationTargetLeadMs)
	{
		const HMONITOR currentMonitor = MonitorFromWindow(
			videoHwnd,
			MONITOR_DEFAULTTONEAREST);
		if (currentMonitor && currentMonitor != negotiatedMonitor)
			RetryAutoLimitedCandidate("monitor transition");
		if (!actualOutput.safeToRender)
		{
			const uint64_t now = GetTickCount64();
			if (now >= nextOutputRecoveryTick)
			{
				nextOutputRecoveryTick = now + 1000;
				RetryAutoLimitedCandidate("unsafe output recovery");
			}
			if (!actualOutput.safeToRender)
				return false;
		}
		if (!swapchain)
			return false;
		const VideoState& state = *statePtr;
		if (viewportRequestSerial !=
			activePictureScreenProfileRequestSerial)
		{
			activePictureScreenProfileRequestSerial =
				viewportRequestSerial;
			nlsTransition.Reset();
			outwardPictureConfirmation = {};
			nlsGeometryAvailable = false;
			nlsTransitionWithdrawn = true;
			nlsGeometry = {};
			nlsGeometryClassification =
				ActivePictureClassification::UNAVAILABLE;
			latestActivePictureObservationSupportsCrop = false;
			nlsGeometrySourceGeneration = 0;
			activePictureAnalysisSourceGeneration = 0;
			ClearSceneVerificationSnapshot();
			ClearLatestActivePictureEvidence();
			ClearScopeSubtitleEvidence();
			ClearScopePresentationEvidence();
			nlsDecision = {};
			renderParams.hooks = nullptr;
			renderParams.num_hooks = 0;
			lastSourceCropPolicy.clear();
			lastFinalPresentationPolicy.clear();
			lastFinalLayoutPolicy.clear();
			DebugLog::Log(
				"Alpha active picture authority reset: viewport_request=%llu",
				static_cast<unsigned long long>(
					viewportRequestSerial));
		}

		if (lastRenderedEotf != EOTF::UNKNOWN &&
			(lastRenderedEotf != state.eotf || lastRenderedColorspace != state.colorspace))
		{
			// Flush at the exact queued-frame boundary, not when metadata first
			// arrives. Older queued frames still belong to the prior color state.
			pl_renderer_flush_cache(renderer);
		}

		const int width = static_cast<int>(state.displayMode->FrameWidth());
		const int height = static_cast<int>(state.displayMode->FrameHeight());
		AlphaNativeRgbLayout nativeRgbLayout;
		const bool nativeRgbUpload =
			videoConversionOverride == VideoConversionOverride::VIDEOCONVERSION_NONE &&
			AlphaCanUseNativeRgbUpload(state.videoFrameEncoding) &&
			GetAlphaNativeRgbLayout(state.videoFrameEncoding, nativeRgbLayout);
		const bool nativeRgb8Upload = nativeRgbLayout.bitDepth == 8;
		const bool lossless422Upload =
			(state.videoFrameEncoding == VideoFrameEncoding::V210 ||
				state.videoFrameEncoding == VideoFrameEncoding::UYVY ||
				state.videoFrameEncoding == VideoFrameEncoding::HDYC) &&
			videoConversionOverride ==
				VideoConversionOverride::VIDEOCONVERSION_NONE;
		const size_t p010RowBytes =
			static_cast<size_t>(width) * sizeof(uint16_t);
		const int chromaHeight = lossless422Upload ? height : (height + 1) / 2;
		const BYTE* yPixels = nullptr;
		const BYTE* uvPixels = nullptr;
		VideoFrameFormatterOutputContract formattedContract;
		AnalysisLumaSource analysisSource;
		bool formatterStateChanged = false;
		bool logFormatterContract = false;
		if (!nativeRgbUpload)
		{
			if (!formatterState || formatterState->colorspace != state.colorspace)
			{
				formatter->OnVideoState(statePtr);
				formatterState = statePtr;
				formatterStateChanged = true;
			}
			if (!formatter->FormatVideoFrame(videoFrame, convertedFrame.data()))
				return false;
			formattedContract = formatter->GetOutputContract();
			if (!formattedContract.IsValid())
				throw std::runtime_error(
					"Alpha formatter did not declare its output range contract");
			logFormatterContract = !formatterContractLogged || formatterStateChanged;
			if (logFormatterContract)
			{
				const char* const range =
					formattedContract.sampleRange == VideoFrameSampleRange::FULL ?
						"full" : "limited";
				DebugLog::Log(
					"Alpha formatter output contract: source=%d ingress=%s range=%s libplacebo_levels=%s color_depth=%u bit_shift=%u size=%dx%d",
					static_cast<int>(state.videoFrameEncoding),
					lossless422Upload ? "P210" : "P010", range, range,
					static_cast<unsigned>(formattedContract.colorDepth),
					static_cast<unsigned>(formattedContract.bitShift), width, height);
				formatterContractLogged = true;
			}
			yPixels = convertedFrame.data();
			uvPixels = yPixels + p010RowBytes * static_cast<size_t>(height);
			if (logFormatterContract && outputDiagnostics)
			{
				LogFormatterInputDiagnostics(
					reinterpret_cast<const uint16_t*>(yPixels),
					reinterpret_cast<const uint16_t*>(uvPixels), width, height,
					chromaHeight, formattedContract);
			}
		}
		if (nativeRgbUpload)
		{
			analysisSource = {
				reinterpret_cast<const uint8_t*>(videoFrame.GetData()),
				static_cast<size_t>(state.BytesPerFrame()), width, height,
				static_cast<size_t>(state.BytesPerRow()), 0,
				AnalysisLumaFormat::NativeRgb, state.videoFrameEncoding,
				state.colorspace, frameGeneration
			};
		}
		else
		{
			analysisSource = {
				yPixels, convertedFrame.size(), width, height, p010RowBytes,
				p010RowBytes, lossless422Upload ? AnalysisLumaFormat::P210 :
				AnalysisLumaFormat::P010,
				state.videoFrameEncoding, state.colorspace, frameGeneration
			};
		}
		if (!analysisSource.IsValid())
		{
			renderParams.hooks = nullptr;
			renderParams.num_hooks = 0;
			nlsTransition.Reset();
			outwardPictureConfirmation = {};
			nlsGeometryAvailable = false;
			nlsTransitionWithdrawn = true;
			nlsGeometry = {};
			nlsGeometryClassification =
				ActivePictureClassification::UNAVAILABLE;
			latestActivePictureObservationSupportsCrop = false;
			nlsGeometrySourceGeneration = 0;
			activePictureAnalysisSourceGeneration = 0;
			ClearSceneVerificationSnapshot();
			ClearLatestActivePictureEvidence();
			lastSourceCropPolicy.clear();
			lastFinalPresentationPolicy.clear();
			lastFinalLayoutPolicy.clear();
			nlsDecision = {};
			pl_mpv_user_shader_destroy(&nlsHook);
			nlsHookSignature.clear();
			if (nlsRequested)
			{
				SetShaderStatus("NLS: unavailable (analysis input)");
				MadVRShaderLoader::SetRuntimeShaderSelection(
					requestedShaderSelector, requestedShaderSelector,
					NlsMappingMode::OFF);
			}
			if (sceneDetectionEnabled)
				sceneDetectionStatus.store(
					static_cast<int>(SceneDetectorStatus::Failed),
					std::memory_order_release);
			DebugLog::Log("Alpha analysis unavailable: format=%d path=%s dimensions=%dx%d generation=%llu",
				static_cast<int>(state.videoFrameEncoding),
				AnalysisLumaFormatName(analysisSource), width, height,
				static_cast<unsigned long long>(frameGeneration));
		}
		else
		{
			if (nativeRgbUpload &&
				(nativeRgbAnalysisLoggedGeneration != frameGeneration ||
					nativeRgbAnalysisLoggedEncoding != state.videoFrameEncoding ||
					nativeRgbAnalysisLoggedColorspace != state.colorspace ||
					nativeRgbAnalysisLoggedWidth != width ||
					nativeRgbAnalysisLoggedHeight != height))
			{
				nativeRgbAnalysisLoggedGeneration = frameGeneration;
				nativeRgbAnalysisLoggedEncoding = state.videoFrameEncoding;
				nativeRgbAnalysisLoggedColorspace = state.colorspace;
				nativeRgbAnalysisLoggedWidth = width;
				nativeRgbAnalysisLoggedHeight = height;
				DebugLog::Log(
					"Alpha native RGB analysis: ingress=%s color_space=%d eotf=%d range=full representation=%s %dx%d source_transform=identity encoded_luma=BT.%s bounded_active_samples=<30000 scene_samples=576 generation=%llu consumers[nls=%s scene=%s scope_subtitle=%s]",
					nativeRgbLayout.label,
					static_cast<int>(state.colorspace), static_cast<int>(state.eotf),
					AnalysisLumaFormatName(analysisSource), width, height,
					state.colorspace == ColorSpace::BT_2020 ? "2020" : "709",
					static_cast<unsigned long long>(frameGeneration),
					nlsRequested ? "available" : "disabled",
					sceneDetectionEnabled ? "available" : "disabled",
					configuredScreenActive ? "available" : "disabled");
			}
		}
		SceneDetectorResult sceneResult;
		if (!cadenceRepeat && analysisSource.IsValid())
		{
			sceneResult = sceneDetector.Analyze({
				reinterpret_cast<const uint16_t*>(yPixels),
				static_cast<size_t>(width), static_cast<size_t>(height), p010RowBytes,
				sourceSequence, videoFrame.GetTimingTimestamp(),
				sceneDetectorGeneration, state.displayMode->FrameDuration(),
				sceneDetectionEnabled || automaticSourceCrop, &analysisSource });
		}
		if (!cadenceRepeat && analysisSource.IsValid() &&
			sceneDetectionEnabled)
			sceneDetectionStatus.store(
				static_cast<int>(sceneResult.status), std::memory_order_release);
		const NlsMappingDecision nlsDecisionBeforeSceneAnalysis =
			nlsDecision;
		RECT outputClient{};
		double outputPanelAspect = 0.0;
		if (GetClientRect(videoHwnd, &outputClient) &&
			outputClient.right > outputClient.left &&
			outputClient.bottom > outputClient.top)
		{
			outputPanelAspect = static_cast<double>(
				outputClient.right - outputClient.left) /
				(outputClient.bottom - outputClient.top);
		}
		const double frameNlsTargetAspect = ResolveNlsTargetAspect(
			configuredScreenActive, configuredScreenAspect,
			outputPanelAspect);
		const bool retainedNlsModeAvailable =
			nlsDecisionBeforeSceneAnalysis.mode != NlsMappingMode::WAITING &&
			nlsDecisionBeforeSceneAnalysis.mode != NlsMappingMode::OFF;
		const bool retainedMappingCompatible = !nlsRequested ||
			(retainedNlsModeAvailable &&
			 std::abs(nlsDecisionBeforeSceneAnalysis.targetAspect -
				frameNlsTargetAspect) <= 0.0001);
		AlphaSourceCrop::SceneHoldInput sceneHoldInput;
		sceneHoldInput.snapshotAvailable =
			sceneVerificationGeometryAvailable;
		sceneHoldInput.nlsRequested = nlsRequested;
		sceneHoldInput.retainedMappingCompatible = retainedMappingCompatible;
		sceneHoldInput.snapshotSourceGeneration =
			sceneVerificationGeometrySourceGeneration;
		sceneHoldInput.frameSourceGeneration = frameGeneration;
		sceneHoldInput.deadlineTick =
			activePictureSceneVerificationDeadlineTick;
		sceneHoldInput.currentTick = GetTickCount64();
		AlphaSourceCrop::SceneHoldDecision sceneHold =
			AlphaSourceCrop::EvaluateSceneHold(sceneHoldInput);
		if (!cadenceRepeat && sceneResult.safeBoundary)
		{
			// A cut invalidates partial proof before the cut frame is analyzed.
			// The stable geometry remains the last affirmative logical reference.
			nlsTransition.ResetCandidateEvidence();
			outwardPictureConfirmation = {};
			scopeSubtitleTranslationConfirmation = {};
			scopeSubtitleFitConfirmation = {};
		}
		if (analysisSource.IsValid())
		{
			ActivePictureFrameIdentity currentActivePictureIdentity =
				activePictureIdentity;
			currentActivePictureIdentity.transportGeneration = frameGeneration;
			currentActivePictureIdentity.sourceFormatGeneration =
				AlphaSourceFormatKey(state);
			currentActivePictureIdentity.viewportGeneration =
				viewportRequestSerial;
			currentActivePictureIdentity.rendererGeneration = frameGeneration;
			UpdateNlsForFrame(analysisSource, sourceSequence,
				currentActivePictureIdentity,
				state.displayMode->RefreshRateHz(),
				configuredScreenActive, sceneHold,
				!cadenceRepeat && sceneResult.safeBoundary,
				activePicturePreviewDecision);
		}
		else
		{
			// An invalid analysis view is not a black frame. Publish an explicit
			// unsafe result so neither a timer nor stale evidence can preserve crop.
			latestActivePictureObservationSupportsCrop = false;
			latestActivePictureEvidenceAvailable = false;
			latestActivePictureEvidenceClassification =
				ActivePictureClassification::UNAVAILABLE;
			latestActivePictureEvidenceBounds = {};
			latestActivePictureEvidenceFrame = sourceSequence;
			latestActivePicturePresentationRetentionEvaluated = true;
			latestActivePicturePresentationRetentionSafe = false;
			latestActivePicturePresentationRetentionReason =
				"analysis source is invalid";
			fullRasterPresentationAuthorityAvailable = false;
			fullRasterPresentationAuthoritySourceGeneration = 0;
			activePictureAmbiguityHold.Reset();
		}
		if (!cadenceRepeat && sceneResult.safeBoundary)
		{
			// Capture only the already-published crop for bounded presentation,
			// then reset temporal proof. Confirmations for new geometry must never
			// accumulate across an edit. The current cut frame is force-analyzed
			// above. Trusted full-raster evidence must withdraw; a bounded
			// unavailable fade may preserve only an existing trusted scope snapshot.
			const bool latestEvidenceIsCurrent =
				latestActivePictureEvidenceFrame == sourceSequence;
			const bool latestEvidenceMayVerify =
				latestActivePictureEvidenceClassification ==
					ActivePictureClassification::BAR_CROP_TRUSTED ||
				 latestActivePictureEvidenceClassification ==
					ActivePictureClassification::PROVISIONAL ||
				latestActivePictureEvidenceClassification ==
					ActivePictureClassification::UNAVAILABLE;
			const bool canVerifyExistingCrop = automaticSourceCrop &&
				nlsGeometryAvailable &&
				nlsGeometryClassification ==
					ActivePictureClassification::BAR_CROP_TRUSTED &&
				nlsGeometrySourceGeneration == frameGeneration &&
				latestEvidenceIsCurrent && latestEvidenceMayVerify;
			AlphaSourceCrop::SceneInput sceneInput;
			sceneInput.geometryAvailable = nlsGeometryAvailable;
			sceneInput.geometryIsCurrentGeneration =
				nlsGeometrySourceGeneration == frameGeneration;
			sceneInput.latestEvidenceIsCurrent = latestEvidenceIsCurrent;
			sceneInput.latestObservationSupportsCrop =
				latestActivePictureObservationSupportsCrop;
			sceneInput.existingCropCanBeSnapshotted = canVerifyExistingCrop;
			const bool overlayBaseMatchesGeometry =
				nlsGeometryAvailable &&
				scopeSubtitlePictureLeft == nlsGeometry.left &&
				scopeSubtitlePictureTop == nlsGeometry.top &&
				scopeSubtitlePictureRight == nlsGeometry.right &&
				scopeSubtitlePictureBottom == nlsGeometry.bottom;
			const bool currentOverlayEnvelope =
				scopePresentationCurrentSourceGeneration == frameGeneration &&
				scopePresentationCurrentSourceSequence == sourceSequence;
			sceneInput.currentOverlayEvidenceSupportsGeometry =
				currentOverlayEnvelope &&
				scopeSubtitleEvidenceSourceGeneration == frameGeneration &&
				overlayBaseMatchesGeometry &&
				AlphaSourceCrop::CurrentTranslationEnvelopeSupportsGeometry(
					scopeVerticalBarPresentation, nlsGeometry,
					scopePresentationCurrentBounds);
			sceneInput.frameLocalPresentationRetentionSafe =
				latestActivePicturePresentationRetentionSafe;
			sceneInput.frameLocalPresentationRetentionEvaluated =
				latestActivePicturePresentationRetentionEvaluated;
			sceneInput.geometryClassification = nlsGeometryClassification;
			sceneInput.latestClassification =
				latestActivePictureEvidenceClassification;
			const AlphaSourceCrop::SceneDecision sceneDecision =
				AlphaSourceCrop::EvaluateSceneBoundary(sceneInput);
			const bool retainCurrentTrustedPresentation =
				sceneDecision.action ==
					AlphaSourceCrop::ScenePresentationAction::KEEP_CURRENT;
			const bool preserveLogicalReference =
				sceneDecision.action ==
					AlphaSourceCrop::ScenePresentationAction::PRESERVE_REFERENCE;
			bool retainBoundedSnapshot =
				sceneDecision.action ==
					AlphaSourceCrop::ScenePresentationAction::HOLD_SNAPSHOT;
			if (canVerifyExistingCrop)
			{
				sceneVerificationGeometryAvailable = true;
				sceneVerificationGeometry = nlsGeometry;
				sceneVerificationGeometrySourceGeneration =
					nlsGeometrySourceGeneration;
				sceneVerificationLatestSupportsCrop =
					latestActivePictureEvidenceClassification ==
						ActivePictureClassification::BAR_CROP_TRUSTED &&
					ActivePictureBoundsContain(
						sceneVerificationGeometry,
						latestActivePictureEvidenceBounds);
				activePictureSceneVerificationDeadlineTick =
					GetTickCount64() +
					ACTIVE_PICTURE_SCENE_VERIFICATION_MS;
				sceneHold.cropActive = !preserveLogicalReference;
				sceneHold.nlsActive = !preserveLogicalReference && nlsRequested &&
					retainedMappingCompatible;
			}
			else
			{
				ClearSceneVerificationSnapshot();
				sceneHold = {};
			}

			if (retainCurrentTrustedPresentation || preserveLogicalReference ||
				retainBoundedSnapshot)
			{
				// Candidate confirmations never cross an edit, but the cut frame has
				// already reaffirmed this exact presentation. Reset only temporal
				// acquisition proof; keep the crop, decision, and hook visible.
				nlsTransition.ResetCandidateEvidence();
				nlsTransitionWithdrawn = false;
				if (preserveLogicalReference)
				{
					latestActivePictureObservationSupportsCrop = false;
					nlsDecision = {};
					renderParams.hooks = nullptr;
					renderParams.num_hooks = 0;
				}
				else if (retainBoundedSnapshot)
				{
					// The deadline bounds only the old hook snapshot. The stable logical
					// geometry survives and current-frame pixel evidence decides crop.
					latestActivePictureObservationSupportsCrop = false;
					nlsDecision = nlsDecisionBeforeSceneAnalysis;
				}
			}
			else
			{
				nlsTransition.Reset();
				nlsGeometryAvailable = false;
				nlsTransitionWithdrawn = false;
				nlsGeometry = {};
				nlsGeometryClassification =
					ActivePictureClassification::UNAVAILABLE;
				latestActivePictureObservationSupportsCrop = false;
				nlsGeometrySourceGeneration = 0;
				nlsDecision = {};
				renderParams.hooks = nullptr;
				renderParams.num_hooks = 0;
				lastSourceCropPolicy.clear();
				lastFinalPresentationPolicy.clear();
				lastFinalLayoutPolicy.clear();
			}
			sceneDetectedCount.fetch_add(1, std::memory_order_relaxed);
			DebugLog::Log("libplacebo scene boundary: event=%llu sequence=%llu generation=%llu frames_back=%u luma=%u evidence=%d crop_verification_ms=%u nls_retained=%d reason=\"%s\"",
				static_cast<unsigned long long>(sceneResult.eventId),
				static_cast<unsigned long long>(sceneResult.sourceSequence),
				static_cast<unsigned long long>(sceneResult.generation),
				static_cast<unsigned>(sceneResult.eventFramesBack),
				static_cast<unsigned>(sceneResult.averageLuma),
				static_cast<int>(
					latestActivePictureEvidenceClassification),
				canVerifyExistingCrop ? static_cast<unsigned>(
					ACTIVE_PICTURE_SCENE_VERIFICATION_MS) : 0U,
				(retainCurrentTrustedPresentation || preserveLogicalReference ||
					retainBoundedSnapshot)
					? 1 : 0,
				sceneDecision.reason.c_str());
		}
		if (!cadenceRepeat)
		{
			const AlphaPresentationSnapshot presentation =
				presentationTelemetry.Snapshot();
			AlphaCadenceCorrectionInput correctionInput;
			correctionInput.enabled = sceneDetectionEnabled && analysisSource.IsValid();
			// Queue replacement and detector/source replacement are independent
			// reset boundaries. Fold both into the policy epoch so neither can
			// inherit a pending action or retained phase from the other.
			correctionInput.generation =
				frameGeneration ^
				(sceneDetectorGeneration + 0x9e3779b97f4a7c15ULL +
					(frameGeneration << 6) + (frameGeneration >> 2));
			correctionInput.detectorGeneration =
				sceneDetectorGeneration;
			correctionInput.presentationGeneration =
				presentation.generation;
			correctionInput.presentationEvidence = presentation.evidence;
			correctionInput.captureRateHz = captureRateHz;
			correctionInput.displayRateHz = presentation.measuredDisplayHz;
			correctionInput.queueDepth = queueDepthAfterDequeue;
			correctionInput.desiredQueueDepth = desiredQueueDepth;
			correctionInput.oldestQueuedAgeMs = oldestQueuedAgeMs;
			correctionInput.presentationDebt =
				presentation.sourceToPresentDebt;
			correctionInput.lastPresentId = presentation.lastPresentId;
			correctionInput.safeSceneBoundary =
				sceneDetectionEnabled && sceneResult.safeBoundary;
			correctionInput.sceneEventId = sceneDetectionEnabled
				? sceneResult.eventId : 0;
			correctionInput.sourceSequence = sourceSequence;
			correctionDecision =
				cadenceCorrectionPolicy.Evaluate(correctionInput);
			UpdateCadenceDiagnostics(
				correctionDecision, frameGeneration);
			if (correctionDecision.action == AlphaCadenceAction::Drop)
			{
				return true;
			}
		}
		const bool ambiguityOverlayHoldActive =
			activePictureAmbiguityHold.IsActive(
				GetTickCount64(), frameGeneration);
		const bool latestEvidenceAllowsBarOverlay =
			latestActivePictureEvidenceAvailable &&
			(latestActivePictureEvidenceClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED ||
			 (latestActivePictureEvidenceClassification ==
				ActivePictureClassification::PROVISIONAL &&
			  (sceneHold.cropActive || ambiguityOverlayHoldActive ||
			   latestActivePicturePresentationRetentionSafe)));
		auto hasCroppedEdge = [width, height](
			const ActivePictureBounds& bounds)
		{
			return bounds.left > 0 || bounds.top > 0 ||
				bounds.right < width || bounds.bottom < height;
		};
		const bool currentBarAuthority =
			nlsGeometryAvailable &&
			nlsGeometryClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED &&
			nlsGeometrySourceGeneration == frameGeneration &&
			hasCroppedEdge(nlsGeometry) &&
			latestEvidenceAllowsBarOverlay;
		const bool sceneBarAuthority =
			!currentBarAuthority &&
			sceneHold.cropActive &&
			sceneVerificationGeometryAvailable &&
			sceneVerificationGeometrySourceGeneration == frameGeneration &&
			hasCroppedEdge(sceneVerificationGeometry) &&
			latestEvidenceAllowsBarOverlay;
		const uint64_t subtitleNow = GetTickCount64();
		const bool trustedNlsBarGeometry =
			nlsGeometryAvailable &&
			nlsGeometryClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED &&
			nlsGeometrySourceGeneration == frameGeneration &&
			hasCroppedEdge(nlsGeometry);
		AlphaSourceCrop::HeldBarAnalysisInput heldAnalysisInput;
		heldAnalysisInput.currentBarAuthority =
			currentBarAuthority || sceneBarAuthority;
		heldAnalysisInput.trustedBarGeometryAvailable =
			trustedNlsBarGeometry;
		heldAnalysisInput.storedBaseMatchesTrustedGeometry =
			trustedNlsBarGeometry &&
			scopeSubtitlePictureLeft == nlsGeometry.left &&
			scopeSubtitlePictureTop == nlsGeometry.top &&
			scopeSubtitlePictureRight == nlsGeometry.right &&
			scopeSubtitlePictureBottom == nlsGeometry.bottom &&
			scopePresentationEvidenceBase.left == nlsGeometry.left &&
			scopePresentationEvidenceBase.top == nlsGeometry.top &&
			scopePresentationEvidenceBase.right == nlsGeometry.right &&
			scopePresentationEvidenceBase.bottom == nlsGeometry.bottom;
		heldAnalysisInput.currentEnvelopeAvailable =
			scopePresentationCurrentSourceGeneration == frameGeneration &&
			scopePresentationCurrentSourceSequence == sourceSequence;
		heldAnalysisInput.latestClassification =
			latestActivePictureEvidenceClassification;
		heldAnalysisInput.trustedGeometry = nlsGeometry;
		heldAnalysisInput.currentEnvelope = scopePresentationCurrentBounds;
		heldAnalysisInput.presentation = scopeVerticalBarPresentation;
		heldAnalysisInput.translationConfirmationPending =
			scopeSubtitleTranslationConfirmation.confirmations != 0;
		heldAnalysisInput.pendingTranslationPixels =
			scopeSubtitleTranslationConfirmation.candidateTranslationPixels;
		heldAnalysisInput.fitConfirmationPending =
			scopeSubtitleFitConfirmation.confirmations != 0 &&
			scopeSubtitleFitConfirmation.confirmations <
				AlphaSourceCrop::VERTICAL_FIT_CONFIRMATIONS_REQUIRED;
		heldAnalysisInput.evidenceSourceGeneration =
			scopeSubtitleEvidenceSourceGeneration;
		heldAnalysisInput.currentSourceGeneration = frameGeneration;
		heldAnalysisInput.currentTick = subtitleNow;
		heldAnalysisInput.holdMs = scopeSubtitleHoldMs;
		heldAnalysisInput.currentSourceSequence = sourceSequence;
		const bool heldBarAnalysisAuthority =
			AlphaSourceCrop::CanAnalyzeHeldVerticalBarGeometry(
				heldAnalysisInput);
		const ActivePictureBounds* subtitleBarAuthority =
			currentBarAuthority ? &nlsGeometry :
			(sceneBarAuthority ? &sceneVerificationGeometry :
				(heldBarAnalysisAuthority ? &nlsGeometry : nullptr));
		// The first subtitle/UI frame may be the one that makes the retained crop
		// pixel-unsafe. Do not wait for the normal three-frame subtitle scan in
		// that case: inspect the already-proven bars on this frame so final
		// presentation can translate immediately instead of flashing full raster
		// (or a generic fit) for one frame.
		const bool subtitleTranslationAlreadyActive =
			scopeSubtitleEvidenceSourceGeneration == frameGeneration &&
			scopeVerticalBarPresentation.action ==
				AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE;
		if (scopeSubtitleRetentionGeneration != frameGeneration)
		{
			scopeSubtitleRetentionGeneration = frameGeneration;
			scopeSubtitleRetentionWasUnsafe = false;
		}
		const bool retentionUnsafeNow =
			latestActivePicturePresentationRetentionEvaluated &&
			!latestActivePicturePresentationRetentionSafe;
		const bool retentionJustBecameUnsafe =
			retentionUnsafeNow && !scopeSubtitleRetentionWasUnsafe;
		if (latestActivePicturePresentationRetentionEvaluated)
			scopeSubtitleRetentionWasUnsafe = retentionUnsafeNow;
		const bool forceSubtitleBarAnalysis =
			AlphaSourceCrop::RequiresImmediateSubtitleBarAnalysis(
				subtitleBarAuthority != nullptr,
				retentionJustBecameUnsafe,
				latestActivePicturePresentationRetentionEvaluated,
				latestActivePicturePresentationRetentionSafe,
				subtitleTranslationAlreadyActive) ||
			(latestActivePictureEvidenceWasStartupHypothesis &&
			 subtitleBarAuthority != nullptr);
		const float subtitleShiftSourcePixels =
			UpdateScopeSubtitleShift(&analysisSource,
				width, height, configuredScreenActive, subtitleBarAuthority,
				sourceSequence, forceSubtitleBarAnalysis,
				heldBarAnalysisAuthority);

		struct pl_frame image{};
		if (nativeRgbUpload)
		{
			struct pl_plane_data plane{};
			plane.type = PL_FMT_UNORM;
			plane.width = width;
			plane.height = height;
			plane.pixel_stride = 4;
			plane.row_stride = state.BytesPerRow();
			plane.pixels = videoFrame.GetData();
			plane.swapped = nativeRgbLayout.swapped;
			pl_plane_data_from_mask(&plane, nativeRgbLayout.masks);
			if (!pl_upload_plane(d3d11->gpu, &image.planes[0],
				&textures[0], &plane))
				return false;
			image.num_planes = 1;
			image.planes[0].shift_x = 0.0f;
			image.planes[0].shift_y = 0.0f;
			image.planes[0].flipped = state.invertedVertical;
			ingressStatus = nativeRgbLayout.label;
			if (!analysisSource.IsValid())
				ingressStatus += " (analysis unavailable)";
		}
		else
		{
			struct pl_plane_data planes[2]{};
			planes[0].type = PL_FMT_UNORM;
			planes[0].width = width;
			planes[0].height = height;
			planes[0].component_size[0] = 16;
			planes[0].component_map[0] = PL_CHANNEL_Y;
			planes[0].pixel_stride = sizeof(uint16_t);
			planes[0].row_stride = p010RowBytes;
			planes[0].pixels = yPixels;

			planes[1].type = PL_FMT_UNORM;
			planes[1].width = (width + 1) / 2;
			planes[1].height = chromaHeight;
			planes[1].component_size[0] = 16;
			planes[1].component_size[1] = 16;
			planes[1].component_map[0] = PL_CHANNEL_CB;
			planes[1].component_map[1] = PL_CHANNEL_CR;
			planes[1].pixel_stride = sizeof(uint16_t) * 2;
			planes[1].row_stride = p010RowBytes;
			planes[1].pixels = uvPixels;

			image.num_planes = 2;
			for (int plane = 0; plane < 2; ++plane)
			{
				if (!pl_upload_plane(d3d11->gpu, &image.planes[plane],
					&textures[plane], &planes[plane]))
					return false;
				image.planes[plane].shift_x = 0.0f;
				image.planes[plane].shift_y = 0.0f;
				image.planes[plane].flipped = state.invertedVertical;
			}
			if (lossless422Upload)
			{
				switch (state.videoFrameEncoding)
				{
				case VideoFrameEncoding::V210:
					ingressStatus = "P210 (lossless v210 4:2:2)";
					break;
				case VideoFrameEncoding::HDYC:
					ingressStatus = "P210 (lossless HDYC 4:2:2)";
					break;
				default:
					ingressStatus = "P210 (lossless UYVY 4:2:2)";
					break;
				}
			}
			else
				ingressStatus = videoConversionOverride ==
					VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010 ?
					"P010 (forced)" :
					"P010 (source fallback)";
		}

		image.repr.sys = nativeRgbUpload ? PL_COLOR_SYSTEM_RGB :
			TranslateSystem(state.colorspace);
		image.repr.levels = nativeRgbUpload ?
			(nativeRgbLayout.limitedRange ? PL_COLOR_LEVELS_LIMITED :
				PL_COLOR_LEVELS_FULL) :
			(formattedContract.sampleRange == VideoFrameSampleRange::FULL ?
				PL_COLOR_LEVELS_FULL : PL_COLOR_LEVELS_LIMITED);
		image.repr.alpha = PL_ALPHA_NONE;
		image.repr.bits.sample_depth = nativeRgbUpload ? (nativeRgb8Upload ? 8 : 10) : 16;
		image.repr.bits.color_depth = nativeRgbUpload ? (nativeRgb8Upload ? 8 : 10) :
			formattedContract.colorDepth;
		image.repr.bits.bit_shift = nativeRgbUpload ? 0 :
			formattedContract.bitShift;
		image.color = TranslateColorSpace(state);
		if (state.eotf == EOTF::SDR &&
			sdrInputTransfer != PL_COLOR_TRC_UNKNOWN)
		{
			// This is an explicit interpretation override used for controlled
			// comparison with other renderers. It changes how captured SDR code
			// values are decoded, independently of the output transfer curve.
			image.color.transfer = sdrInputTransfer;
		}
		const enum pl_color_transfer declaredSourceTransfer =
			image.color.transfer;
		const enum pl_color_transfer acceptedOutputTransfer =
			ResolvedPixelTransfer(actualOutput.encoding,
				actualOutput.targetTransfer);
		lastSdrGammaDecision = LibplaceboOutput::ResolveSdrGamma(
			sdrAdjustGamma,
			state.eotf == EOTF::SDR,
			actualOutput.safeToRender,
			LibplaceboOutput::ParseGamma(activeSettings.outputGamma),
			ToSdrTransfer(declaredSourceTransfer),
			ToSdrTransfer(acceptedOutputTransfer));
		if (lastSdrGammaDecision.action ==
			LibplaceboOutput::SdrGammaAction::SUPPRESS)
		{
			const enum pl_color_transfer effective = FromSdrTransfer(
				lastSdrGammaDecision.effectiveSource);
			if (effective != PL_COLOR_TRC_UNKNOWN)
				image.color.transfer = effective;
		}
		const bool gammaRangeConversion = image.repr.levels !=
			EncodingLevels(actualOutput.encoding);
		const enum pl_color_primaries gammaTargetPrimaries = targetBt2020
			? PL_COLOR_PRIM_BT_2020 : PL_COLOR_PRIM_BT_709;
		const bool gammaPrimariesConversion = image.color.primaries !=
			PL_COLOR_PRIM_UNKNOWN && image.color.primaries != gammaTargetPrimaries;
		std::ostringstream gammaSignature;
		gammaSignature << static_cast<int>(state.eotf) << '|'
			<< static_cast<int>(lastSdrGammaDecision.requested) << '|'
			<< static_cast<int>(lastSdrGammaDecision.action) << '|'
			<< static_cast<int>(lastSdrGammaDecision.declaredSource) << '|'
			<< static_cast<int>(lastSdrGammaDecision.effectiveSource) << '|'
			<< static_cast<int>(lastSdrGammaDecision.actualTarget) << '|'
			<< actualOutput.requestedEncodingActive << '|'
			<< gammaRangeConversion << '|' << gammaPrimariesConversion << '|'
			<< (displayLutParsed && displayLut);
		if (lastSdrGammaDecisionSignature != gammaSignature.str())
		{
			lastSdrGammaDecisionSignature = gammaSignature.str();
			DebugLog::Log(
				"SDR_GAMMA requested=%s applicable=%s action=%s source_configured=%s source_declared=%s source_effective=%s target_requested=%s target_actual=%s output_fallback=%d other_processing=range:%d,primaries:%d,lut:%d,scaling:possible,dither:%d,deband:%d reason=\"%s\"",
				LibplaceboOutput::ToString(lastSdrGammaDecision.requested),
				lastSdrGammaDecision.action ==
					LibplaceboOutput::SdrGammaAction::NOT_APPLICABLE ? "no" : "yes",
				LibplaceboOutput::ToString(lastSdrGammaDecision.action),
				activeSettings.sdrInputTransfer.c_str(),
				LibplaceboOutput::ToString(lastSdrGammaDecision.declaredSource),
				LibplaceboOutput::ToString(lastSdrGammaDecision.effectiveSource),
				activeSettings.outputGamma.c_str(),
				LibplaceboOutput::ToString(lastSdrGammaDecision.actualTarget),
				actualOutput.requestedEncodingActive ? 0 : 1,
				gammaRangeConversion ? 1 : 0,
				gammaPrimariesConversion ? 1 : 0,
				displayLutParsed && displayLut ? 1 : 0,
				renderParams.dither_params ? 1 : 0,
				renderParams.deband_params ? 1 : 0,
				lastSdrGammaDecision.reason.c_str());
		}
		// SDR is already display-referred. Match its nominal luminance to the SDR
		// render target so libplacebo performs range/matrix/transfer conversion but
		// does not tone-map an inferred 203-nit SDR source into a different target.
		// HDR inputs retain their source/mastering luminance and use the configured
		// tone-mapping path.
		if (state.eotf == EOTF::SDR)
		{
			image.color.hdr.min_luma = static_cast<float>(sdrBlackNits);
			image.color.hdr.max_luma = static_cast<float>(sdrTargetNits);
			image.color.hdr.max_cll = 0.0f;
			image.color.hdr.max_fall = 0.0f;
		}
		image.crop.x0 = 0.0f;
		image.crop.y0 = 0.0f;
		image.crop.x1 = static_cast<float>(width);
		image.crop.y1 = static_cast<float>(height);
		if (!nativeRgbUpload)
			pl_frame_set_chroma_location(&image, PL_CHROMA_LEFT);

		struct pl_swapchain_frame swapchainFrame{};
		pl_tex vpOwnedFrameTarget = nullptr;
		CComPtr<ID3D11Texture2D> vpOwnedBackbuffer;
		if (vpOwnedSwapchain)
		{
			const HRESULT getBufferResult = vpOwnedSwapchain->GetBuffer(
				0, IID_PPV_ARGS(&vpOwnedBackbuffer));
			if (FAILED(getBufferResult) || !vpOwnedBackbuffer)
			{
				if (vpOwnedTargetFailureLoggedGeneration !=
					vpOwnedSwapchainGeneration)
				{
					vpOwnedTargetFailureLoggedGeneration =
						vpOwnedSwapchainGeneration;
					DebugLog::Log(
						"VP-owned DXGI target rejected: generation=%llu stage=GetBuffer result=0x%08lX present_skipped=1",
						static_cast<unsigned long long>(
							vpOwnedSwapchainGeneration),
						static_cast<unsigned long>(getBufferResult));
				}
				return false;
			}

			D3D11_TEXTURE2D_DESC backbufferDesc{};
			vpOwnedBackbuffer->GetDesc(&backbufferDesc);
			struct pl_d3d11_wrap_params wrapParams{};
			wrapParams.tex =
				static_cast<ID3D11Resource*>(vpOwnedBackbuffer.p);
			vpOwnedFrameTarget = pl_d3d11_wrap(
				d3d11->gpu, &wrapParams);
			const bool targetValid = vpOwnedFrameTarget &&
				vpOwnedFrameTarget->params.renderable &&
				vpOwnedFrameTarget->params.blit_dst;
			if (!targetValid)
			{
				if (vpOwnedTargetFailureLoggedGeneration !=
					vpOwnedSwapchainGeneration)
				{
					vpOwnedTargetFailureLoggedGeneration =
						vpOwnedSwapchainGeneration;
					DebugLog::Log(
						"VP-owned DXGI target rejected: generation=%llu stage=capability format=%u bind=0x%08X usage=%u renderable=%d blit_dst=%d present_skipped=1",
						static_cast<unsigned long long>(
							vpOwnedSwapchainGeneration),
						static_cast<unsigned int>(backbufferDesc.Format),
						backbufferDesc.BindFlags,
						static_cast<unsigned int>(backbufferDesc.Usage),
						vpOwnedFrameTarget &&
							vpOwnedFrameTarget->params.renderable ? 1 : 0,
						vpOwnedFrameTarget &&
							vpOwnedFrameTarget->params.blit_dst ? 1 : 0);
				}
				pl_tex_destroy(d3d11->gpu, &vpOwnedFrameTarget);
				vpOwnedBackbuffer.Release();
				return false;
			}

			if (vpOwnedTargetValidationLoggedGeneration !=
				vpOwnedSwapchainGeneration)
			{
				vpOwnedTargetValidationLoggedGeneration =
					vpOwnedSwapchainGeneration;
				DebugLog::Log(
					"VP-owned DXGI target verified: generation=%llu format=%u bind=0x%08X usage=%u size=%ux%u renderable=1 blit_dst=1 present_owner=VP",
					static_cast<unsigned long long>(
						vpOwnedSwapchainGeneration),
					static_cast<unsigned int>(backbufferDesc.Format),
					backbufferDesc.BindFlags,
					static_cast<unsigned int>(backbufferDesc.Usage),
					backbufferDesc.Width, backbufferDesc.Height);
			}

			int componentBits = 0;
			for (int component = 0;
				component < vpOwnedFrameTarget->params.format->num_components;
				++component)
			{
				componentBits = std::max(componentBits,
					static_cast<int>(vpOwnedFrameTarget->params.format->
						component_depth[component]));
			}
			swapchainFrame.fbo = vpOwnedFrameTarget;
			swapchainFrame.flipped = false;
			swapchainFrame.color_repr.sys = PL_COLOR_SYSTEM_RGB;
			swapchainFrame.color_repr.levels = PL_COLOR_LEVELS_FULL;
			swapchainFrame.color_repr.alpha = PL_ALPHA_UNKNOWN;
			swapchainFrame.color_repr.bits.sample_depth = componentBits;
			swapchainFrame.color_repr.bits.color_depth = componentBits;
			swapchainFrame.color_space = configuredOutputColor;
		}
		else if (!pl_swapchain_start_frame(swapchain, &swapchainFrame))
		{
			return false;
		}

		struct pl_frame baseTarget{};
		pl_frame_from_swapchain(&baseTarget, &swapchainFrame);
		if (vpOwnedSwapchain)
		{
			// Flip-discard backbuffers contain undefined prior contents. Rendering a
			// smaller destination rectangle must therefore begin with a full target
			// clear, otherwise old pixels can survive around a resized viewport.
			const float black[] = { 0.0f, 0.0f, 0.0f };
			pl_frame_clear(d3d11->gpu, &baseTarget, black);
			if (vpOwnedClearLoggedGeneration != vpOwnedSwapchainGeneration)
			{
				vpOwnedClearLoggedGeneration = vpOwnedSwapchainGeneration;
				DebugLog::Log(
					"VP-owned DXGI target clear: generation=%llu scope=whole_surface before_render=1 present_owner=VP",
					static_cast<unsigned long long>(vpOwnedSwapchainGeneration));
			}
		}
		const struct pl_color_repr returnedRepr = baseTarget.repr;
		const struct pl_color_space returnedColor = baseTarget.color;
		LibplaceboRenderParameters::ApplyDisplayBitDepth(
			activeSettings.displayBitDepth, baseTarget.repr);
		const bool returnedTargetMatchesActualOutput =
			ReturnedTargetMatchesActualOutput(returnedRepr, returnedColor);
		// The frame returned by libplacebo is the default host/compositor contract.
		// Override it only after the matching studio DXGI declaration was advertised
		// and accepted. Requested settings never flow directly into the target.
		baseTarget.repr.levels = EncodingLevels(actualOutput.encoding);
		const enum pl_color_transfer acceptedTransfer =
			ResolvedPixelTransfer(actualOutput.encoding,
				actualOutput.targetTransfer);
		if (acceptedTransfer != PL_COLOR_TRC_UNKNOWN)
			baseTarget.color.transfer = acceptedTransfer;
		if (targetBt2020)
		{
			baseTarget.color.primaries = PL_COLOR_PRIM_BT_2020;
		}
		else
		{
			baseTarget.color.primaries = PL_COLOR_PRIM_BT_709;
		}
		baseTarget.color.hdr.min_luma = static_cast<float>(sdrBlackNits);
		baseTarget.color.hdr.max_luma = static_cast<float>(sdrTargetNits);
		ConfigureDisplayLutForTarget(
			baseTarget, returnedTargetMatchesActualOutput);
		if (outputDiagnostics && !outputContractLogged)
		{
			DebugLog::Log(
				"libplacebo output diagnostic contract: source_eotf=%d source_system=%s source_levels=%d source_transfer=%s source_luma=%.4f..%.1f returned_system=%s returned_levels=%d returned_transfer=%s returned_bits=%d/%d returned_luma=%.4f..%.1f display_bit_depth=%s final_bits=%d/%d shift=%d final_levels=%d final_transfer=%s final_luma=%.4f..%.1f dxgi=%s presentation=%s",
				static_cast<int>(state.eotf),
				pl_color_system_name(image.repr.sys),
				static_cast<int>(image.repr.levels),
				pl_color_transfer_name(image.color.transfer),
				image.color.hdr.min_luma,
				image.color.hdr.max_luma,
				pl_color_system_name(returnedRepr.sys),
				static_cast<int>(returnedRepr.levels),
				pl_color_transfer_name(returnedColor.transfer),
				returnedRepr.bits.sample_depth,
				returnedRepr.bits.color_depth,
				returnedColor.hdr.min_luma,
				returnedColor.hdr.max_luma,
				activeSettings.displayBitDepth.c_str(),
				baseTarget.repr.bits.sample_depth,
				baseTarget.repr.bits.color_depth,
				baseTarget.repr.bits.bit_shift,
				static_cast<int>(baseTarget.repr.levels),
				pl_color_transfer_name(baseTarget.color.transfer),
				baseTarget.color.hdr.min_luma,
				baseTarget.color.hdr.max_luma,
				LibplaceboOutput::ToString(actualOutput.encoding),
				LibplaceboOutput::ToString(actualOutput.presentationModel));
			outputContractLogged = true;
		}

		auto configureViewport =
			[this, &image, width, height, frameGeneration, sourceSequence,
			 sceneHold, subtitleShiftSourcePixels](
				struct pl_frame& source,
				struct pl_frame& target,
				bool configuredScreenActive,
				float /* subtitleShift */,
				bool* trustedActivePicture = nullptr,
				bool publishFinalPresentation = false)
		{
			source = image;
			if (trustedActivePicture)
				*trustedActivePicture = false;
			const bool sceneVerificationHoldActive = sceneHold.cropActive;
			const bool useSceneVerificationGeometry =
				sceneVerificationHoldActive && !nlsGeometryAvailable;
			const bool effectiveGeometryAvailable =
				nlsGeometryAvailable || useSceneVerificationGeometry;
			const bool effectiveLatestSupportsCrop = nlsGeometryAvailable
				? latestActivePictureObservationSupportsCrop
				: sceneVerificationLatestSupportsCrop;
			const bool latestObservationIsProvisional =
				latestActivePictureEvidenceAvailable &&
				latestActivePictureEvidenceClassification ==
					ActivePictureClassification::PROVISIONAL;
			const bool latestObservationIsUnavailable =
				latestActivePictureEvidenceClassification ==
					ActivePictureClassification::UNAVAILABLE;
			const ActivePictureClassification effectiveClassification =
				useSceneVerificationGeometry
					? ActivePictureClassification::BAR_CROP_TRUSTED
					: nlsGeometryClassification;
			const ActivePictureBounds& effectiveGeometry =
				useSceneVerificationGeometry
					? sceneVerificationGeometry : nlsGeometry;
			const uint64_t effectiveGeometrySourceGeneration =
				useSceneVerificationGeometry
					? sceneVerificationGeometrySourceGeneration
					: nlsGeometrySourceGeneration;
			const uint64_t overlayNow = GetTickCount64();
			const bool ambiguityHoldActive =
				activePictureAmbiguityHold.IsActive(
					overlayNow, frameGeneration);
			const bool leftBarContentActive =
				scopeSubtitleLeftLastDetectionTick != 0 &&
				overlayNow - scopeSubtitleLeftLastDetectionTick <=
					scopeSubtitleHoldMs;
			const bool rightBarContentActive =
				scopeSubtitleRightLastDetectionTick != 0 &&
				overlayNow - scopeSubtitleRightLastDetectionTick <=
					scopeSubtitleHoldMs;
			const bool storedVerticalBaseMatchesEffectiveGeometry =
				effectiveGeometryAvailable &&
				scopeSubtitlePictureLeft == effectiveGeometry.left &&
				scopeSubtitlePictureTop == effectiveGeometry.top &&
				scopeSubtitlePictureRight == effectiveGeometry.right &&
				scopeSubtitlePictureBottom == effectiveGeometry.bottom;
			const bool detailedVerticalActive =
				scopeVerticalBarPresentation.action !=
					AlphaSourceCrop::VerticalBarPresentationAction::NONE &&
				scopeSubtitleEvidenceSourceGeneration == frameGeneration &&
				storedVerticalBaseMatchesEffectiveGeometry;
			auto sameBounds = [](const ActivePictureBounds& left,
				const ActivePictureBounds& right)
			{
				return left.left == right.left && left.top == right.top &&
					left.right == right.right && left.bottom == right.bottom &&
					left.rasterWidth == right.rasterWidth &&
					left.rasterHeight == right.rasterHeight;
			};
			AlphaSourceCrop::PresentationEnvelopeInput envelopeInput;
			envelopeInput.envelopeAvailable =
				scopePresentationEvidenceSourceSequence != 0;
			envelopeInput.effectiveGeometryAvailable =
				effectiveGeometryAvailable;
			envelopeInput.baseMatchesEffectiveGeometry =
				effectiveGeometryAvailable &&
				sameBounds(scopePresentationEvidenceBase, effectiveGeometry);
			envelopeInput.detectedSourceSequence =
				scopePresentationEvidenceSourceSequence;
			envelopeInput.currentSourceSequence = sourceSequence;
			envelopeInput.evidenceSourceGeneration =
				scopePresentationEvidenceSourceGeneration;
			envelopeInput.frameSourceGeneration = frameGeneration;
			envelopeInput.lastDetectionTick =
				scopePresentationEvidenceLastTick;
			envelopeInput.currentTick = overlayNow;
			envelopeInput.holdMs = scopeSubtitleHoldMs;
			const AlphaSourceCrop::PresentationEnvelopeDecision envelopeDecision =
				AlphaSourceCrop::EvaluatePresentationEnvelope(envelopeInput);
			const bool detectorEnvelopeActive = envelopeDecision.active;
			const bool detectorLeftExpansion = detectorEnvelopeActive &&
				effectiveGeometryAvailable && scopePresentationEvidenceBounds.left <
					effectiveGeometry.left;
			const bool detectorTopExpansion = detectorEnvelopeActive &&
				effectiveGeometryAvailable && scopePresentationEvidenceBounds.top <
					effectiveGeometry.top;
			const bool detectorRightExpansion = detectorEnvelopeActive &&
				effectiveGeometryAvailable && scopePresentationEvidenceBounds.right >
					effectiveGeometry.right;
			const bool detectorBottomExpansion = detectorEnvelopeActive &&
				effectiveGeometryAvailable && scopePresentationEvidenceBounds.bottom >
					effectiveGeometry.bottom;
			const bool currentDetectorEnvelope = detectorEnvelopeActive &&
				envelopeDecision.currentFrame &&
				scopePresentationCurrentSourceGeneration == frameGeneration &&
				scopePresentationCurrentSourceSequence == sourceSequence;
			const bool currentDetectorLeftExpansion = currentDetectorEnvelope &&
				effectiveGeometryAvailable &&
				scopePresentationCurrentBounds.left < effectiveGeometry.left;
			const bool currentDetectorTopExpansion = currentDetectorEnvelope &&
				effectiveGeometryAvailable &&
				scopePresentationCurrentBounds.top < effectiveGeometry.top;
			const bool currentDetectorRightExpansion = currentDetectorEnvelope &&
				effectiveGeometryAvailable &&
				scopePresentationCurrentBounds.right > effectiveGeometry.right;
			const bool currentDetectorBottomExpansion = currentDetectorEnvelope &&
				effectiveGeometryAvailable &&
				scopePresentationCurrentBounds.bottom > effectiveGeometry.bottom;
			const bool verticalInspectionPending =
				AlphaSourceCrop::ShouldRetainTrustedBaseForVerticalInspection(
					scopeSubtitleFit,
					currentDetectorEnvelope,
					latestObservationIsProvisional ||
						(!effectiveLatestSupportsCrop &&
						 latestActivePictureEvidenceClassification ==
							ActivePictureClassification::BAR_CROP_TRUSTED),
					currentDetectorLeftExpansion,
					currentDetectorTopExpansion,
					currentDetectorRightExpansion,
					currentDetectorBottomExpansion);
			const bool requestedSubtitleTranslation = detailedVerticalActive &&
				scopeVerticalBarPresentation.action ==
					AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE &&
				std::abs(subtitleShiftSourcePixels) > 0.5f;
			bool releaseDriftBaseRetention = false;
			float resolvedSubtitleTranslation = requestedSubtitleTranslation
				? subtitleShiftSourcePixels : 0.0f;
			if (publishFinalPresentation)
			{
				if (!storedVerticalBaseMatchesEffectiveGeometry)
					scopeSubtitleDrift.Reset();
				else
					resolvedSubtitleTranslation =
						scopeSubtitleDrift.Resolve(
							resolvedSubtitleTranslation, overlayNow,
							requestedSubtitleTranslation
								? scopeSubtitleEngageDriftMs
								: scopeSubtitleReleaseDriftMs);
				releaseDriftBaseRetention =
					!requestedSubtitleTranslation &&
					scopeSubtitleDrift.ConsumeFinalBaseFrame();
				const bool subtitleDriftActive = scopeSubtitleDrift.IsActive();
				if (subtitleDriftActive != scopeSubtitleDriftWasActive)
				{
					DebugLog::Log(
						"libplacebo scope subtitle fit: %s drift %s; duration=%llums shift=%.1f px",
						requestedSubtitleTranslation ? "engage" : "release",
						subtitleDriftActive ? "started" : "completed",
						static_cast<unsigned long long>(
							requestedSubtitleTranslation
								? scopeSubtitleEngageDriftMs
								: scopeSubtitleReleaseDriftMs),
						resolvedSubtitleTranslation);
					scopeSubtitleDriftWasActive = subtitleDriftActive;
				}
			}
			const bool subtitleDriftTranslation =
				std::abs(resolvedSubtitleTranslation) > 0.5f;
			const bool engageDriftBaseRetention =
				requestedSubtitleTranslation && scopeSubtitleDrift.IsActive() &&
				!subtitleDriftTranslation;
			const bool acceptedDenseVerticalFit = detailedVerticalActive &&
				scopeVerticalBarPresentation.action ==
					AlphaSourceCrop::VerticalBarPresentationAction::FIT;
			AlphaSourceCrop::VerticalBarPresentationResolutionInput
				verticalResolutionInput;
			verticalResolutionInput.detailedAction =
				requestedSubtitleTranslation
				? AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE
				: (subtitleDriftTranslation
					? AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE
					: (acceptedDenseVerticalFit
						? AlphaSourceCrop::VerticalBarPresentationAction::FIT
						: AlphaSourceCrop::VerticalBarPresentationAction::NONE));
			verticalResolutionInput.translationPixels =
				requestedSubtitleTranslation || subtitleDriftTranslation
				? resolvedSubtitleTranslation : 0.0f;
			verticalResolutionInput.zeroTranslationRetainsTrustedBase =
				requestedSubtitleTranslation && scopeSubtitleDrift.IsActive();
			verticalResolutionInput.genericUpperExpansion =
				currentDetectorTopExpansion;
			verticalResolutionInput.genericLowerExpansion =
				currentDetectorBottomExpansion;
			verticalResolutionInput.genericVerticalFitConfirmed =
				currentDetectorTopExpansion && currentDetectorBottomExpansion;
			verticalResolutionInput.genericVerticalFitAuthoritative =
				verticalResolutionInput.genericVerticalFitConfirmed &&
				effectiveClassification ==
					ActivePictureClassification::BAR_CROP_TRUSTED &&
				effectiveLatestSupportsCrop;
			verticalResolutionInput.denseVerticalArbitrationEnabled =
				scopeSubtitleFit;
			verticalResolutionInput.genericUpperBound =
				currentDetectorTopExpansion
				? scopePresentationCurrentBounds.top : effectiveGeometry.top;
			verticalResolutionInput.genericLowerBound =
				currentDetectorBottomExpansion
				? scopePresentationCurrentBounds.bottom : effectiveGeometry.bottom;
			verticalResolutionInput.authoritativeTop = effectiveGeometry.top;
			verticalResolutionInput.authoritativeBottom = effectiveGeometry.bottom;
			verticalResolutionInput.rasterHeight = height;
			const AlphaSourceCrop::VerticalBarPresentationResolution
				verticalResolution = AlphaSourceCrop::ResolveVerticalBarPresentation(
					verticalResolutionInput);
			const AlphaSourceCrop::VerticalBarRendererRouting verticalRouting =
				AlphaSourceCrop::ResolveVerticalBarRendererRouting(
					verticalResolution);
			const bool verticalTranslationActive =
				verticalRouting.translationActive;
			const bool verticalFitActive = verticalRouting.fitActive;
			const bool verticalFailOpen = verticalRouting.failOpen;
			const int verticalTranslationPixels =
				verticalRouting.translationPixels;
			const bool selectedDetectorTopExpansion = verticalFitActive &&
				detectorTopExpansion;
			const bool selectedDetectorBottomExpansion = verticalFitActive &&
				detectorBottomExpansion;
			const bool detectorFitActive = detectorLeftExpansion ||
				selectedDetectorTopExpansion || detectorRightExpansion ||
				selectedDetectorBottomExpansion;
			const bool detailedVerticalFitEvidence = verticalFitActive &&
				detailedVerticalActive &&
				(scopeVerticalBarPresentation.detectedTop > 0 ||
				 scopeVerticalBarPresentation.detectedBottom > 0);
			const bool denseFitEvidenceActive = leftBarContentActive ||
				rightBarContentActive || detailedVerticalFitEvidence;
			const bool barContentFitActive = detectorFitActive ||
				denseFitEvidenceActive;
			ActivePictureBounds outwardExpansion = effectiveGeometry;
			bool outwardExpansionAvailable = false;
			bool outwardExpansionInvalid = false;
			if (barContentFitActive && effectiveGeometryAvailable &&
				((denseFitEvidenceActive &&
				  scopeSubtitleEvidenceSourceGeneration == frameGeneration) ||
				 detectorFitActive))
			{
				AlphaSourceCrop::PresentationEnvelopeGeometryInput expansionInput;
				expansionInput.trustedPicture = effectiveGeometry;
				expansionInput.observedContent = effectiveGeometry;
				expansionInput.observedContentAvailable = true;
				if (detectorFitActive)
				{
					if (detectorLeftExpansion)
					{
						expansionInput.expandLeft = true;
						expansionInput.observedContent.left = std::min(
							expansionInput.observedContent.left,
							scopePresentationEvidenceBounds.left);
					}
					if (selectedDetectorTopExpansion)
					{
						expansionInput.expandTop = true;
						expansionInput.observedContent.top = std::min(
							expansionInput.observedContent.top,
							scopePresentationEvidenceBounds.top);
					}
					if (detectorRightExpansion)
					{
						expansionInput.expandRight = true;
						expansionInput.observedContent.right = std::max(
							expansionInput.observedContent.right,
							scopePresentationEvidenceBounds.right);
					}
					if (selectedDetectorBottomExpansion)
					{
						expansionInput.expandBottom = true;
						expansionInput.observedContent.bottom = std::max(
							expansionInput.observedContent.bottom,
							scopePresentationEvidenceBounds.bottom);
					}
				}
				const int verticalMargin = std::max(8, height / 90) +
					scopeSubtitlePaddingPixels;
				const int horizontalMargin = std::max(8, width / 160) +
					scopeSubtitlePaddingPixels;
				if (leftBarContentActive && scopeSubtitleDetectedLeft > 0)
				{
					expansionInput.expandLeft = true;
					expansionInput.observedContent.left = std::min(
						expansionInput.observedContent.left,
						scopeSubtitleDetectedLeft);
				}
				if (detailedVerticalFitEvidence &&
					scopeVerticalBarPresentation.detectedTop > 0)
				{
					expansionInput.expandTop = true;
					expansionInput.observedContent.top = std::min(
						expansionInput.observedContent.top,
						scopeVerticalBarPresentation.detectedTop);
				}
				if (detailedVerticalFitEvidence &&
					scopeVerticalBarPresentation.detectedBottom > 0)
				{
					expansionInput.expandBottom = true;
					expansionInput.observedContent.bottom = std::max(
						expansionInput.observedContent.bottom,
						scopeVerticalBarPresentation.detectedBottom);
				}
				if (rightBarContentActive && scopeSubtitleDetectedRight > 0)
				{
					expansionInput.expandRight = true;
					expansionInput.observedContent.right = std::max(
						expansionInput.observedContent.right,
						scopeSubtitleDetectedRight);
				}
				expansionInput.horizontalPadding = horizontalMargin;
				expansionInput.verticalPadding = verticalMargin;
				const AlphaSourceCrop::PresentationEnvelopeGeometryDecision
					expansion =
					AlphaSourceCrop::BuildPresentationEnvelope(expansionInput);
				outwardExpansionInvalid = !expansion.valid;
				if (expansion.valid)
				{
					outwardExpansion = expansion.bounds;
					outwardExpansionAvailable = expansion.expanded;
				}
			}
			AlphaSourceCrop::Input cropInput;
			cropInput.automaticCropEnabled = automaticSourceCrop;
			cropInput.fullRasterPresentationAuthoritative =
				fullRasterPresentationAuthorityAvailable &&
				fullRasterPresentationAuthoritySourceGeneration ==
					frameGeneration;
			cropInput.sharedGeometryAvailable = effectiveGeometryAvailable;
			cropInput.latestObservationSupportsCrop =
				effectiveLatestSupportsCrop;
			cropInput.sceneVerificationHoldActive =
				sceneVerificationHoldActive;
			cropInput.ambiguityHoldActive = ambiguityHoldActive;
			cropInput.latestObservationIsProvisional =
				latestObservationIsProvisional;
			cropInput.latestObservationIsUnavailable =
				latestObservationIsUnavailable;
			cropInput.latestObservationClassification =
				latestActivePictureEvidenceClassification;
			cropInput.frameLocalPresentationRetentionSafe =
				latestActivePicturePresentationRetentionSafe;
			cropInput.frameLocalPresentationRetentionEvaluated =
				latestActivePicturePresentationRetentionEvaluated;
			cropInput.presentationFailOpen =
				verticalFailOpen || outwardExpansionInvalid;
			const bool barCropRefinementPending =
				latestActivePictureEvidenceAvailable &&
				latestActivePictureEvidenceClassification ==
					ActivePictureClassification::BAR_CROP_TRUSTED &&
				effectiveClassification ==
					ActivePictureClassification::BAR_CROP_TRUSTED &&
				!effectiveLatestSupportsCrop;
			cropInput.barCropRefinementPending = barCropRefinementPending;
			const bool verticalTranslationConfirmationPending =
				scopeSubtitleTranslationConfirmation.confirmations != 0 ||
				(verticalInspectionPending &&
				 currentDetectorTopExpansion != currentDetectorBottomExpansion);
			const bool verticalFitConfirmationPending =
				scopeSubtitleFitConfirmation.confirmations != 0 ||
				(verticalInspectionPending && currentDetectorTopExpansion &&
				 currentDetectorBottomExpansion);
			cropInput.verticalTranslationConfirmationPending =
				verticalTranslationConfirmationPending;
			cropInput.verticalFitConfirmationPending =
				verticalFitConfirmationPending;
			cropInput.verticalTranslationActive = verticalTranslationActive;
			cropInput.verticalTranslationPixels = verticalTranslationPixels;
			cropInput.verticalTranslationBase = {
				scopeSubtitlePictureLeft, scopeSubtitlePictureTop,
				scopeSubtitlePictureRight, scopeSubtitlePictureBottom,
				width, height, scopeSubtitlePictureBottom > scopeSubtitlePictureTop
					? static_cast<double>(scopeSubtitlePictureRight -
						scopeSubtitlePictureLeft) /
						(scopeSubtitlePictureBottom - scopeSubtitlePictureTop)
					: 0.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			cropInput.verticalTranslationSourceGeneration =
				scopeSubtitleEvidenceSourceGeneration;
			cropInput.verticalTranslationBaseRetentionActive =
				releaseDriftBaseRetention;
			cropInput.verticalTranslationEngageBaseRetentionActive =
				engageDriftBaseRetention;
			cropInput.outwardPresentationActive = barContentFitActive;
			cropInput.outwardExpansionAvailable = outwardExpansionAvailable;
			cropInput.classification = effectiveClassification;
			cropInput.geometry = effectiveGeometry;
			cropInput.outwardExpansion = outwardExpansion;
			cropInput.geometrySourceGeneration =
				effectiveGeometrySourceGeneration;
			cropInput.outwardExpansionSourceGeneration =
				denseFitEvidenceActive
					? scopeSubtitleEvidenceSourceGeneration
					: scopePresentationEvidenceSourceGeneration;
			cropInput.frameSourceGeneration = frameGeneration;
			cropInput.rasterWidth = width;
			cropInput.rasterHeight = height;
			const AlphaSourceCrop::Decision cropDecision =
				AlphaSourceCrop::Evaluate(cropInput);
			std::ostringstream cropPolicy;
			cropPolicy << automaticSourceCrop << '|'
				<< cropDecision.applyCrop << '|'
				<< cropDecision.outwardExpanded << '|'
				<< cropDecision.verticallyTranslated << '|'
				<< cropDecision.verticalTranslationPixels << '|'
				<< effectiveLatestSupportsCrop << '|'
				<< sceneVerificationHoldActive << '|'
				<< ambiguityHoldActive << '|'
				<< latestObservationIsProvisional << '|'
				<< latestActivePicturePresentationRetentionSafe << '|'
				<< detectorEnvelopeActive << '|'
				<< barCropRefinementPending << '|'
				<< verticalInspectionPending << '|'
				<< verticalTranslationConfirmationPending << '|'
				<< verticalFitConfirmationPending << '|'
				<< engageDriftBaseRetention << '|'
				<< releaseDriftBaseRetention << '|'
				<< static_cast<int>(verticalResolution.action) << '|'
				<< leftBarContentActive << detailedVerticalFitEvidence
				<< rightBarContentActive << verticalTranslationActive << '|'
				<< cropDecision.sourceBounds.left << ','
				<< cropDecision.sourceBounds.top << '-'
				<< cropDecision.sourceBounds.right << ','
				<< cropDecision.sourceBounds.bottom << '|'
				<< static_cast<int>(effectiveClassification) << '|'
				<< effectiveGeometrySourceGeneration << '|'
				<< cropDecision.reason;
			if (cropPolicy.str() != lastSourceCropPolicy)
			{
				lastSourceCropPolicy = cropPolicy.str();
				const char* verticalActionLabel = verticalTranslationActive
					? "translate" : (verticalFitActive ? "fit" :
						(verticalFailOpen ? "fail-open" : "none"));
				DebugLog::Log(
					"Alpha source crop: sequence=%llu enabled=%d applied=%d expanded=%d translated=%d vertical_action=%s shift_request=%d shift_applied=%d latest_trusted=%d scene_hold=%d ambiguity_hold=%d retention_safe=%d latest_evidence=%d detector_envelope=%d bar_wait=%d inspection_wait=%d translation_wait=%d fit_wait=%d engage_base=%d release_settle=%d envelope_state=%s edges=%c%c%c%c evidence_rect=%d,%d-%d,%d rect=%d,%d-%d,%d classification=%d geometry_generation=%llu frame_generation=%llu reason=\"%s; %s; %s\"",
					static_cast<unsigned long long>(sourceSequence),
					automaticSourceCrop ? 1 : 0,
					cropDecision.applyCrop ? 1 : 0,
					cropDecision.outwardExpanded ? 1 : 0,
					cropDecision.verticallyTranslated ? 1 : 0,
					verticalActionLabel,
					verticalTranslationPixels,
					cropDecision.verticalTranslationPixels,
					effectiveLatestSupportsCrop ? 1 : 0,
					sceneVerificationHoldActive ? 1 : 0,
					ambiguityHoldActive ? 1 : 0,
					latestActivePicturePresentationRetentionSafe ? 1 : 0,
					static_cast<int>(
						latestActivePictureEvidenceClassification),
					detectorEnvelopeActive ? 1 : 0,
					barCropRefinementPending ? 1 : 0,
					verticalInspectionPending ? 1 : 0,
					verticalTranslationConfirmationPending ? 1 : 0,
					verticalFitConfirmationPending ? 1 : 0,
					engageDriftBaseRetention ? 1 : 0,
					releaseDriftBaseRetention ? 1 : 0,
					envelopeDecision.currentFrame ? "current" :
					(envelopeDecision.held ? "held" : "inactive"),
					leftBarContentActive ? 'L' : '-',
					((detailedVerticalActive &&
					  scopeVerticalBarPresentation.detectedTop > 0) ||
					 selectedDetectorTopExpansion) ? 'T' : '-',
					rightBarContentActive ? 'R' : '-',
					((detailedVerticalActive &&
					  scopeVerticalBarPresentation.detectedBottom > 0) ||
					 selectedDetectorBottomExpansion) ? 'B' : '-',
					outwardExpansion.left,
					outwardExpansion.top,
					outwardExpansion.right,
					outwardExpansion.bottom,
					cropDecision.sourceBounds.left,
					cropDecision.sourceBounds.top,
					cropDecision.sourceBounds.right,
					cropDecision.sourceBounds.bottom,
					static_cast<int>(effectiveClassification),
					static_cast<unsigned long long>(
						effectiveGeometrySourceGeneration),
					static_cast<unsigned long long>(frameGeneration),
					cropDecision.reason.c_str(),
					envelopeDecision.reason,
					latestActivePicturePresentationRetentionReason.c_str());
			}
			if (cropDecision.applyCrop)
			{
				source.crop.x0 = static_cast<float>(
					cropDecision.sourceBounds.left);
				source.crop.y0 = static_cast<float>(
					cropDecision.sourceBounds.top);
				source.crop.x1 = static_cast<float>(
					cropDecision.sourceBounds.right);
				source.crop.y1 = static_cast<float>(
					cropDecision.sourceBounds.bottom);
				if (trustedActivePicture)
					*trustedActivePicture = true;
			}
			auto fitTargetToAspect = [&target](double aspect,
				AlphaSourceCrop::VerticalPictureAlignment alignment)
			{
				const AlphaSourceCrop::PresentationRect available = {
					target.crop.x0, target.crop.y0,
					target.crop.x1, target.crop.y1 };
				const AlphaSourceCrop::CenteredFitDecision fit =
					AlphaSourceCrop::FitAspect(aspect, available, alignment);
				if (fit.valid)
				{
					target.crop.x0 = static_cast<float>(fit.picture.left);
					target.crop.y0 = static_cast<float>(fit.picture.top);
					target.crop.x1 = static_cast<float>(fit.picture.right);
					target.crop.y1 = static_cast<float>(fit.picture.bottom);
				}
				return fit;
			};
			if (!publishFinalPresentation)
			{
				// Shader/profile prewarming must not publish per-frame presentation
				// state. It only needs valid geometry for the selected viewport.
				if (configuredScreenActive)
					fitTargetToAspect(configuredScreenAspect,
						AlphaSourceCrop::VerticalPictureAlignment::CENTER);
				fitTargetToAspect(
					AlphaSourceCrop::ApplyAnamorphicLensCompensation(
						pl_rect2df_aspect(&source.crop), anamorphicScale),
					ResolveVerticalPictureAlignment(verticalAlignment));
				return;
			}

			// This is the sole per-frame NLS authority. Derive every mapping input
			// from the exact source rectangle selected above, then publish crop,
			// hook, runtime geometry, status, and destination layout as one decision.
			renderParams.hooks = nullptr;
			renderParams.num_hooks = 0;
			const double panelTargetAspect = pl_rect2df_aspect(&target.crop);
			const double finalTargetAspect = ResolveNlsTargetAspect(
				configuredScreenActive, configuredScreenAspect, panelTargetAspect);
			// Crop evidence decides whether bars may be removed. It must not veto
			// NLS geometry when the crop policy is already presenting the complete
			// raster. In that case the source aspect is known directly from the
			// resolution, independent of profile names or provisional dark edges.
			// NLS operates on the detected picture, not on source-baked black bars.
			// This is part of the established NLS presentation contract and is
			// independent of the viewport's ordinary automatic-crop setting. NLS-off
			// presentation remains governed exclusively by cropDecision above.
			const bool nlsActivePictureCropApplied = nlsRequested &&
				effectiveGeometryAvailable &&
				effectiveGeometrySourceGeneration == frameGeneration;
			const NlsSourceGeometry finalSourceGeometry =
				ResolveNlsPresentationSourceGeometry(nlsRequested,
					nlsActivePictureCropApplied,
					effectiveGeometry.left, effectiveGeometry.top,
					effectiveGeometry.right, effectiveGeometry.bottom,
					cropDecision.applyCrop,
					cropDecision.sourceBounds.left,
					cropDecision.sourceBounds.top,
					cropDecision.sourceBounds.right,
					cropDecision.sourceBounds.bottom,
					width, height);
			if (nlsActivePictureCropApplied && finalSourceGeometry.valid)
			{
				source.crop.x0 = static_cast<float>(finalSourceGeometry.left);
				source.crop.y0 = static_cast<float>(finalSourceGeometry.top);
				source.crop.x1 = static_cast<float>(finalSourceGeometry.right);
				source.crop.y1 = static_cast<float>(finalSourceGeometry.bottom);
			}
			NlsPresentationCropDecision nlsPresentationCrop;
			nlsPresentationCrop.source = finalSourceGeometry;
			if (nlsRequested)
			{
				nlsPresentationCrop = ResolveNlsPresentationCrop(
					finalSourceGeometry, finalTargetAspect,
					nlsRule.aspectTolerancePercent,
					nlsRule.activeAspectMinimum, nlsRule.aspectDirection,
					nlsRule.maximumStretchRatio,
					nlsRule.vpRendererMaximumCropPercent,
					nlsRule.vpRendererCropPreference);
				if (nlsPresentationCrop.applied)
				{
					source.crop.x0 = static_cast<float>(
						nlsPresentationCrop.source.left);
					source.crop.y0 = static_cast<float>(
						nlsPresentationCrop.source.top);
					source.crop.x1 = static_cast<float>(
						nlsPresentationCrop.source.right);
					source.crop.y1 = static_cast<float>(
						nlsPresentationCrop.source.bottom);
				}
			}
			const NlsSourceGeometry& presentationSourceGeometry =
				nlsPresentationCrop.source;
			const double finalSourceAspect =
				presentationSourceGeometry.aspect;
			const bool finalBoundsAuthoritative = finalSourceGeometry.valid &&
				presentationSourceGeometry.valid;
			const double screenLayoutAspect = configuredScreenActive || nlsRequested
				? finalTargetAspect : pl_rect2df_aspect(&target.crop);
			const AlphaSourceCrop::CenteredFitDecision screenFit =
				fitTargetToAspect(screenLayoutAspect,
					AlphaSourceCrop::VerticalPictureAlignment::CENTER);
			const AlphaSourceCrop::PresentationRect finalScreen = screenFit.picture;
			auto publishFinalLayout = [&](AlphaSourceCrop::UnusedSpaceAxis axis,
				const char* mapping)
			{
				std::ostringstream policy;
				policy << width << 'x' << height << '|'
					<< effectiveGeometry.left << ',' << effectiveGeometry.top << '-'
					<< effectiveGeometry.right << ',' << effectiveGeometry.bottom << '|'
					<< cropDecision.sourceBounds.left << ','
					<< cropDecision.sourceBounds.top << '-'
					<< cropDecision.sourceBounds.right << ','
					<< cropDecision.sourceBounds.bottom << '|'
					<< presentationSourceGeometry.left << ','
					<< presentationSourceGeometry.top << '-'
					<< presentationSourceGeometry.right << ','
					<< presentationSourceGeometry.bottom << '|'
					<< std::lround(finalTargetAspect * 100000.0) << '|'
					<< std::lround(target.crop.x0 * 10.0f) << ','
					<< std::lround(target.crop.y0 * 10.0f) << '-'
					<< std::lround(target.crop.x1 * 10.0f) << ','
					<< std::lround(target.crop.y1 * 10.0f) << '|'
					<< static_cast<int>(axis) << '|' << mapping << '|'
					<< verticalAlignment << '|'
					<< cropDecision.verticalTranslationPixels;
				if (policy.str() == lastFinalLayoutPolicy)
					return;
				lastFinalLayoutPolicy = policy.str();
				DebugLog::Log(
					"Alpha final layout: raster=%dx%d trusted=%d,%d-%d,%d envelope=%d,%d-%d,%d presentation=%d,%d-%d,%d screen_aspect=%.5f screen=%.1f,%.1f-%.1f,%.1f picture=%.1f,%.1f-%.1f,%.1f unused_axis=%s mapping=%s vertical_alignment=%s subtitle_shift_source_pixels=%d anamorphic=%.5f",
					width, height,
					effectiveGeometry.left, effectiveGeometry.top,
					effectiveGeometry.right, effectiveGeometry.bottom,
					cropDecision.sourceBounds.left,
					cropDecision.sourceBounds.top,
					cropDecision.sourceBounds.right,
					cropDecision.sourceBounds.bottom,
					presentationSourceGeometry.left,
					presentationSourceGeometry.top,
					presentationSourceGeometry.right,
					presentationSourceGeometry.bottom,
					finalTargetAspect,
					finalScreen.left, finalScreen.top,
					finalScreen.right, finalScreen.bottom,
					target.crop.x0, target.crop.y0,
					target.crop.x1, target.crop.y1,
					AlphaSourceCrop::UnusedSpaceAxisName(axis), mapping,
					verticalAlignment.c_str(),
					cropDecision.verticalTranslationPixels,
					anamorphicScale);
			};

			if (nlsRequested)
			{
				MadVRShaderLoader::SetRuntimeNlsTargetAspect(finalTargetAspect);
				NlsMappingDecision finalNlsDecision =
					EvaluateNlsMapping(finalBoundsAuthoritative,
						finalSourceAspect, finalTargetAspect,
						nlsRule.aspectTolerancePercent,
						nlsRule.activeAspectMinimum, nlsRule.aspectDirection,
						nlsRule.maximumStretchRatio);
				if (!finalBoundsAuthoritative)
					finalNlsDecision.reason =
						"final crop decision lacks current aspect authority";
				if (finalNlsDecision.mode == NlsMappingMode::ACTIVE &&
					!EnsureNlsHook(finalNlsDecision))
				{
					finalNlsDecision = {};
					finalNlsDecision.sourceAspect = finalSourceAspect;
					finalNlsDecision.targetAspect = finalTargetAspect;
					finalNlsDecision.reason =
						"GLSL hook is unavailable; preserving safe passthrough";
				}
				MadVRShaderLoader::SetRuntimeNlsDecision(finalNlsDecision);
				if (finalNlsDecision.mode == NlsMappingMode::ACTIVE)
				{
					renderParams.hooks = &nlsHook;
					renderParams.num_hooks = 1;
				}
				if (finalBoundsAuthoritative &&
					finalNlsDecision.mode != NlsMappingMode::WAITING)
				{
					const MadVRActivePictureGeometry geometry = {
						finalSourceGeometry.aspect,
						static_cast<double>(finalSourceGeometry.left) / width,
						static_cast<double>(finalSourceGeometry.top) / height,
						static_cast<double>(finalSourceGeometry.right) / width,
						static_cast<double>(finalSourceGeometry.bottom) / height,
						nlsGeometryGeneration, nlsRendererGeneration, true
					};
					MadVRShaderLoader::SetRuntimeActivePictureGeometry(geometry);
				}
				MadVRShaderLoader::SetRuntimeShaderSelection(
					requestedShaderSelector, requestedShaderSelector,
					finalNlsDecision.mode);
				const double detectorAspect = effectiveGeometryAvailable &&
					effectiveGeometry.bottom > effectiveGeometry.top
					? static_cast<double>(effectiveGeometry.right -
						effectiveGeometry.left) /
						(effectiveGeometry.bottom - effectiveGeometry.top)
					: 0.0;
				const NlsMappingDecision detectorNlsDecision =
					EvaluateNlsMapping(effectiveGeometryAvailable,
						detectorAspect, finalTargetAspect,
						nlsRule.aspectTolerancePercent,
						nlsRule.activeAspectMinimum,
						nlsRule.aspectDirection,
						nlsRule.maximumStretchRatio);
				const bool detectorGeometryExcluded =
					effectiveGeometryAvailable && !cropDecision.applyCrop &&
					!nlsActivePictureCropApplied &&
					detectorNlsDecision.mode == NlsMappingMode::ACTIVE &&
					finalNlsDecision.mode != NlsMappingMode::ACTIVE;
				const int nlsCropPixelsX = std::max(0,
					(finalSourceGeometry.right - finalSourceGeometry.left -
						(presentationSourceGeometry.right -
						 presentationSourceGeometry.left)) / 2);
				const int nlsCropPixelsY = std::max(0,
					(finalSourceGeometry.bottom - finalSourceGeometry.top -
						(presentationSourceGeometry.bottom -
						 presentationSourceGeometry.top)) / 2);
				std::ostringstream finalPresentationPolicy;
				finalPresentationPolicy << requestedShaderSelector << '|'
					<< static_cast<int>(finalNlsDecision.mode) << '|'
					<< automaticSourceCrop << '|'
					<< nlsActivePictureCropApplied << '|'
					<< effectiveGeometryAvailable << '|'
					<< effectiveGeometry.left << ','
					<< effectiveGeometry.top << '-'
					<< effectiveGeometry.right << ','
					<< effectiveGeometry.bottom << '|'
					<< effectiveGeometrySourceGeneration << '|'
					<< static_cast<int>(detectorNlsDecision.mode) << '|'
					<< detectorGeometryExcluded << '|'
					<< cropDecision.sourceBounds.left << ','
					<< cropDecision.sourceBounds.top << '-'
					<< cropDecision.sourceBounds.right << ','
					<< cropDecision.sourceBounds.bottom << '|'
					<< presentationSourceGeometry.left << ','
					<< presentationSourceGeometry.top << '-'
					<< presentationSourceGeometry.right << ','
					<< presentationSourceGeometry.bottom << '|'
					<< nlsPresentationCrop.reason << '|'
					<< finalNlsDecision.reason;
				if (finalPresentationPolicy.str() !=
					lastFinalPresentationPolicy)
				{
					lastFinalPresentationPolicy =
						finalPresentationPolicy.str();
					DebugLog::Log(
						"Alpha NLS backend=vprenderer requested=%s applicable=%s file=%s viewport_profiles=\"%s\" automatic_crop=%d nls_active_picture_crop=%d mapping=%s trusted_detector=%d detector_rect=%d,%d-%d,%d detector_aspect=%.4f detector_mapping=%s detector_generation=%llu source_policy=%s presentation_rect=%d,%d-%d,%d source=%.4f target=%.4f requested_ratio=%.5f max_ratio=%.5f direction=%s axis=%s stretch=%.5f crop_limit_percent=%.5f crop_applied=%d crop_percent=%.5f crop_pixels_x=%d crop_pixels_y=%d crop_preference=%s detector_excluded=%d blocker=%s renderer_generation=%llu crop_reason=\"%s\" reason=\"%s\"",
						requestedShaderSelector.c_str(), nlsRule.name.c_str(),
						nlsRule.filename.c_str(),
						activeDisplayRule.empty() ? "(base)" :
							activeDisplayRule.c_str(),
						automaticSourceCrop ? 1 : 0,
						nlsActivePictureCropApplied ? 1 : 0,
						NlsMappingModeName(finalNlsDecision.mode),
						effectiveGeometryAvailable ? 1 : 0,
						effectiveGeometry.left,
						effectiveGeometry.top,
						effectiveGeometry.right,
						effectiveGeometry.bottom,
						detectorAspect,
						NlsMappingModeName(detectorNlsDecision.mode),
						static_cast<unsigned long long>(
							effectiveGeometrySourceGeneration),
						nlsActivePictureCropApplied ? "nls-active-picture" :
							(cropDecision.applyCrop ? "viewport-trusted-crop" :
								"full-raster"),
						presentationSourceGeometry.left,
						presentationSourceGeometry.top,
						presentationSourceGeometry.right,
						presentationSourceGeometry.bottom,
						finalNlsDecision.sourceAspect,
						finalNlsDecision.targetAspect,
						finalNlsDecision.requestedRatio,
						finalNlsDecision.maximumRatio,
						NlsAspectDirectionName(nlsRule.aspectDirection),
						NlsMappingAxisName(finalNlsDecision),
						finalNlsDecision.stretchRatio,
						nlsRule.vpRendererMaximumCropPercent,
						nlsPresentationCrop.applied ? 1 : 0,
						nlsPresentationCrop.percentPerEdge,
						nlsCropPixelsX, nlsCropPixelsY,
						NlsPresentationCropPreferenceName(
							nlsRule.vpRendererCropPreference),
						detectorGeometryExcluded ? 1 : 0,
						detectorGeometryExcluded
							? "detector geometry unavailable to this shader rule"
							: "none",
						static_cast<unsigned long long>(nlsRendererGeneration),
						nlsPresentationCrop.reason.c_str(),
						finalNlsDecision.reason.c_str());
				}
				nlsDecision = finalNlsDecision;
				switch (finalNlsDecision.mode)
				{
				case NlsMappingMode::ACTIVE:
					SetShaderStatus("NLS: Active");
					break;
				case NlsMappingMode::LINEAR_PASSTHROUGH:
					SetShaderStatus("NLS: Passthrough");
					break;
				case NlsMappingMode::SAFE_FIT:
					SetShaderStatus("NLS: Safe fit");
					break;
				default:
					SetShaderStatus("NLS: Waiting");
					break;
				}

				if (finalNlsDecision.mode == NlsMappingMode::ACTIVE)
				{
					publishFinalLayout(
						AlphaSourceCrop::UnusedSpaceAxis::NONE, "nls");
					return;
				}
				// Passthrough means geometry passthrough too. WAITING, SAFE_FIT, and
				// LINEAR_PASSTHROUGH preserves the exact selected source aspect with
				// ordinary centered scaling inside the target viewport. Otherwise a
				// A source classified within target tolerance would otherwise still be
				// stretched vertically despite having no active NLS hook.
				const AlphaSourceCrop::CenteredFitDecision pictureFit =
					fitTargetToAspect(
						AlphaSourceCrop::ApplyAnamorphicLensCompensation(
							pl_rect2df_aspect(&source.crop), anamorphicScale),
						ResolveVerticalPictureAlignment(verticalAlignment));
				publishFinalLayout(pictureFit.unusedAxis, "linear-nls-fallback");
				return;
			}

			// NLS is off: use the same final source rectangle with ordinary scaling.
			const AlphaSourceCrop::CenteredFitDecision pictureFit =
				fitTargetToAspect(
					AlphaSourceCrop::ApplyAnamorphicLensCompensation(
						pl_rect2df_aspect(&source.crop), anamorphicScale),
					ResolveVerticalPictureAlignment(verticalAlignment));
			publishFinalLayout(pictureFit.unusedAxis, "linear");
			// Never apply subtitle translation to the fitted destination. Moving
			// target.crop cannot reveal source pixels; it only clips one edge and
			// leaves an unequal gap at the other. Unsupported overlay evidence is
			// therefore a centered geometry no-op.
		};

		if (!startupNlsPrewarmComplete)
		{
			startupNlsPrewarmComplete = true;
			std::set<std::string> warmedHookKeys;
			size_t renderAttempts = 0;
			size_t renderFailures = 0;
			for (const ConfiguredShaderRule& rule : startupNlsPrewarmRules)
			{
				const struct pl_hook* warmHook = nullptr;
				std::string hookKey;
				std::string resolvedPath;
				std::string reason;
				if (!CreateNlsHook(rule, warmHook, hookKey, resolvedPath, reason))
				{
					++renderFailures;
					DebugLog::Log(
						"Alpha shader startup prewarm: rule=%s result=rejected reason=\"%s\"",
						rule.name.c_str(), reason.c_str());
					continue;
				}
				if (!warmedHookKeys.insert(hookKey).second)
				{
					pl_mpv_user_shader_destroy(&warmHook);
					continue;
				}

				NlsMappingDecision warmDecision;
				warmDecision.stretchRatio = 1.2;
				if (!SetNlsHookMapping(warmHook, warmDecision))
				{
					++renderFailures;
					DebugLog::Log(
						"Alpha shader startup prewarm: rule=%s result=rejected reason=\"dynamic NLS parameters are unavailable\"",
						rule.name.c_str());
					pl_mpv_user_shader_destroy(&warmHook);
					continue;
				}

					struct pl_frame warmImage{};
					struct pl_frame warmTarget = baseTarget;
					configureViewport(
						warmImage, warmTarget, configuredScreenActive, 0.0f);
					struct pl_render_params warmParams = renderParams;
					warmParams.hooks = &warmHook;
					warmParams.num_hooks = 1;
					++renderAttempts;
					compileTelemetry.BeginRender();
					const SteadyClock::time_point warmStart = SteadyClock::now();
					const bool warmed = pl_render_image(
						renderer, &warmImage, &warmTarget, &warmParams);
					const double warmMs =
						std::chrono::duration<double, std::milli>(
							SteadyClock::now() - warmStart).count();
					const LibplaceboCompileSnapshot compileSnapshot =
						compileTelemetry.EndRender();
					if (!warmed)
						++renderFailures;
					const int warmOutputWidth = warmTarget.planes[0].texture
						? warmTarget.planes[0].texture->params.w : 0;
					const int warmOutputHeight = warmTarget.planes[0].texture
						? warmTarget.planes[0].texture->params.h : 0;
					DebugLog::Log(
						"Alpha shader startup prewarm: shader=%s rule=%s target=%s cache=%s renderer_generation=%llu input=%dx%d output=%dx%d glsl_ms=%.3f spirv_cross_ms=%.3f hlsl_ms=%.3f compile_ms=%.3f render_ms=%.3f result=%s",
						resolvedPath.c_str(), rule.name.c_str(),
						configuredScreenActive ? "configured" : "output-panel",
						compileSnapshot.Compiled() ? "cold" : "warm",
						static_cast<unsigned long long>(nlsRendererGeneration),
						width, height, warmOutputWidth, warmOutputHeight,
						compileSnapshot.glslMs,
						compileSnapshot.spirvCrossMs,
						compileSnapshot.hlslMs,
						compileSnapshot.glslMs + compileSnapshot.spirvCrossMs +
							compileSnapshot.hlslMs,
						warmMs, warmed ? "success" : "failed");
				pl_mpv_user_shader_destroy(&warmHook);
			}
			DebugLog::Log(
				"Alpha shader startup prewarm complete: rules=%zu unique_hooks=%zu renders=%zu failures=%zu cache=%d objects/%zu bytes",
				startupNlsPrewarmRules.size(), warmedHookKeys.size(),
				renderAttempts, renderFailures,
				cache ? pl_cache_objects(cache) : 0,
				cache ? pl_cache_size(cache) : 0);
		}

		struct pl_frame renderImage{};
		struct pl_frame target = baseTarget;
		bool trustedActivePicture = false;
		configureViewport(
			renderImage,
			target,
			configuredScreenActive,
			subtitleShiftSourcePixels,
			&trustedActivePicture,
			true);
		std::vector<uint8_t> overlayPixels;
		int overlayWidth = 0;
		int overlayHeight = 0;
		int overlayStride = 0;
		uint64_t overlaySerial = 0;
		{
			std::lock_guard<std::mutex> overlayGuard(statsOverlayMutex);
			overlaySerial = statsOverlaySerial;
			if (overlaySerial != appliedStatsOverlaySerial)
			{
				overlayPixels = statsOverlayPixels;
				overlayWidth = statsOverlayWidth;
				overlayHeight = statsOverlayHeight;
				overlayStride = statsOverlayStride;
			}
		}
		if (overlaySerial != appliedStatsOverlaySerial)
		{
			const bool hadOverlay = statsOverlayTexture != nullptr;
			const bool geometryChanged = statsOverlayTexture &&
				(statsOverlayTexture->params.w != overlayWidth ||
				 statsOverlayTexture->params.h != overlayHeight);
			if (!overlayPixels.empty())
			{
				struct pl_plane_data plane{};
				plane.type = PL_FMT_UNORM;
				plane.width = overlayWidth;
				plane.height = overlayHeight;
				plane.pixel_stride = 4;
				plane.row_stride = static_cast<size_t>(overlayStride);
				plane.pixels = overlayPixels.data();
				uint64_t masks[4] =
				{
					0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000
				};
				pl_plane_data_from_mask(&plane, masks);
				if (!pl_upload_plane(
					d3d11->gpu, nullptr, &statsOverlayTexture, &plane))
				{
					DebugLog::Log("Alpha native OSD texture upload failed");
				}
			}
			else
			{
				pl_tex_destroy(d3d11->gpu, &statsOverlayTexture);
			}
			appliedStatsOverlaySerial = overlaySerial;
			if (hadOverlay != (statsOverlayTexture != nullptr) || geometryChanged)
				pl_renderer_flush_cache(renderer);
		}
		std::vector<uint8_t> sweepPixels;
		int sweepWidth = 0;
		int sweepHeight = 0;
		int sweepStride = 0;
		uint64_t sweepSerial = 0;
		{
			std::lock_guard<std::mutex> overlayGuard(statsOverlayMutex);
			sweepSerial = sweepOverlaySerial;
			if (sweepSerial != appliedSweepOverlaySerial)
			{
				sweepPixels = sweepOverlayPixels;
				sweepWidth = sweepOverlayWidth;
				sweepHeight = sweepOverlayHeight;
				sweepStride = sweepOverlayStride;
			}
		}
		if (sweepSerial != appliedSweepOverlaySerial)
		{
			const bool hadOverlay = sweepOverlayTexture != nullptr;
			const bool geometryChanged = sweepOverlayTexture &&
				(sweepOverlayTexture->params.w != sweepWidth ||
				 sweepOverlayTexture->params.h != sweepHeight);
			if (!sweepPixels.empty())
			{
				struct pl_plane_data plane{};
				plane.type = PL_FMT_UNORM;
				plane.width = sweepWidth;
				plane.height = sweepHeight;
				plane.pixel_stride = 4;
				plane.row_stride = static_cast<size_t>(sweepStride);
				plane.pixels = sweepPixels.data();
				uint64_t masks[4] =
				{
					0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000
				};
				pl_plane_data_from_mask(&plane, masks);
				if (!pl_upload_plane(
					d3d11->gpu, nullptr, &sweepOverlayTexture, &plane))
				{
					DebugLog::Log("Alpha native output sweep banner texture upload failed");
				}
			}
			else
			{
				pl_tex_destroy(d3d11->gpu, &sweepOverlayTexture);
			}
			appliedSweepOverlaySerial = sweepSerial;
			if (hadOverlay != (sweepOverlayTexture != nullptr) || geometryChanged)
				pl_renderer_flush_cache(renderer);
		}
		struct pl_overlay overlays[2]{};
		struct pl_overlay_part overlayParts[2]{};
		int overlayCount = 0;
		if (statsOverlayTexture)
		{
			pl_overlay& overlay = overlays[overlayCount];
			pl_overlay_part& overlayPart = overlayParts[overlayCount];
			overlay.tex = statsOverlayTexture;
			overlay.mode = PL_OVERLAY_NORMAL;
			overlay.coords = PL_OVERLAY_COORDS_DST_FRAME;
			overlay.repr = pl_color_repr_rgb;
			overlay.repr.levels = PL_COLOR_LEVELS_FULL;
			overlay.repr.alpha = PL_ALPHA_INDEPENDENT;
			overlay.color = pl_color_space_srgb;
			overlayPart.src = { 0.0f, 0.0f,
				static_cast<float>(statsOverlayTexture->params.w),
				static_cast<float>(statsOverlayTexture->params.h) };
			const float dstWidth = static_cast<float>(baseTarget.planes[0].texture->params.w);
			const float dstHeight = static_cast<float>(baseTarget.planes[0].texture->params.h);
			const NativeStatsOverlayPlacement::Rect outputRect{
				0.0f, 0.0f, dstWidth, dstHeight };
			const NativeStatsOverlayPlacement::Rect pictureRect{
				target.crop.x0, target.crop.y0,
				target.crop.x1, target.crop.y1 };
			const NativeStatsOverlayPlacement::Result placement =
				NativeStatsOverlayPlacement::Place(
					pictureRect, outputRect,
					static_cast<float>(statsOverlayTexture->params.w),
					static_cast<float>(statsOverlayTexture->params.h));
			overlayPart.dst = {
				placement.panel.left, placement.panel.top,
				placement.panel.right, placement.panel.bottom };

			const auto rectChanged = [](const NativeStatsOverlayPlacement::Rect& first,
				const NativeStatsOverlayPlacement::Rect& second)
			{
				constexpr float epsilon = 0.25f;
				return std::fabs(first.left - second.left) > epsilon ||
					std::fabs(first.top - second.top) > epsilon ||
					std::fabs(first.right - second.right) > epsilon ||
					std::fabs(first.bottom - second.bottom) > epsilon;
			};
			const bool placementChanged = !hasStatsOverlayPlacement ||
				rectChanged(lastStatsOverlayPlacement.visiblePicture,
					placement.visiblePicture) ||
				rectChanged(lastStatsOverlayPlacement.panel, placement.panel) ||
				std::fabs(lastStatsOverlayPlacement.scale - placement.scale) > 0.001f ||
				lastStatsOverlayPlacement.insetClamped != placement.insetClamped ||
				lastStatsOverlayPlacement.usedOutputFallback != placement.usedOutputFallback;
			if (placementChanged)
			{
				DebugLog::Log(
					"Alpha native OSD placement: renderer_gen=%llu source_seq=%llu geometry_gen=%llu output=%.0fx%.0f viewport=%s aspect=%.4f picture=%.1f,%.1f-%.1f,%.1f trusted_active=%d bitmap=%dx%d scale=%.3f panel=%.1f,%.1f-%.1f,%.1f inset=%.0f clamped=%d fallback=%d",
					static_cast<unsigned long long>(frameGeneration),
					static_cast<unsigned long long>(sourceSequence),
					static_cast<unsigned long long>(nlsGeometryGeneration),
					dstWidth, dstHeight,
					configuredScreenActive ? "configured" : "output-panel",
					configuredScreenActive ? configuredScreenAspect :
						static_cast<double>(dstWidth) / dstHeight,
					placement.visiblePicture.left, placement.visiblePicture.top,
					placement.visiblePicture.right, placement.visiblePicture.bottom,
					trustedActivePicture ? 1 : 0,
					statsOverlayTexture->params.w, statsOverlayTexture->params.h,
					placement.scale,
					placement.panel.left, placement.panel.top,
					placement.panel.right, placement.panel.bottom,
					NativeStatsOverlayPlacement::kDefaultInsetPixels,
					placement.insetClamped ? 1 : 0,
					placement.usedOutputFallback ? 1 : 0);
				lastStatsOverlayPlacement = placement;
				hasStatsOverlayPlacement = true;
			}
			overlay.parts = &overlayPart;
			overlay.num_parts = 1;
			++overlayCount;
		}
		if (sweepOverlayTexture)
		{
			pl_overlay& overlay = overlays[overlayCount];
			pl_overlay_part& overlayPart = overlayParts[overlayCount];
			overlay.tex = sweepOverlayTexture;
			overlay.mode = PL_OVERLAY_NORMAL;
			overlay.coords = PL_OVERLAY_COORDS_DST_FRAME;
			overlay.repr = pl_color_repr_rgb;
			overlay.repr.levels = PL_COLOR_LEVELS_FULL;
			overlay.repr.alpha = PL_ALPHA_INDEPENDENT;
			overlay.color = pl_color_space_srgb;
			overlayPart.src = { 0.0f, 0.0f,
				static_cast<float>(sweepOverlayTexture->params.w),
				static_cast<float>(sweepOverlayTexture->params.h) };
			const float dstWidth = static_cast<float>(baseTarget.planes[0].texture->params.w);
			const float dstHeight = static_cast<float>(baseTarget.planes[0].texture->params.h);
			const NativeStatsOverlayPlacement::Rect outputRect{
				0.0f, 0.0f, dstWidth, dstHeight };
			const NativeStatsOverlayPlacement::Rect pictureRect{
				target.crop.x0, target.crop.y0, target.crop.x1, target.crop.y1 };
			const NativeStatsOverlayPlacement::Result placement =
				NativeStatsOverlayPlacement::PlaceTopRight(
					pictureRect, outputRect,
					static_cast<float>(sweepOverlayTexture->params.w),
					static_cast<float>(sweepOverlayTexture->params.h));
			overlayPart.dst = { placement.panel.left, placement.panel.top,
				placement.panel.right, placement.panel.bottom };
			if (!hasSweepOverlayPlacement ||
				std::fabs(lastSweepOverlayPlacement.panel.left - placement.panel.left) > 0.25f ||
				std::fabs(lastSweepOverlayPlacement.panel.top - placement.panel.top) > 0.25f ||
				std::fabs(lastSweepOverlayPlacement.panel.right - placement.panel.right) > 0.25f ||
				std::fabs(lastSweepOverlayPlacement.panel.bottom - placement.panel.bottom) > 0.25f)
			{
				DebugLog::Log(
					"Alpha native output sweep banner placement: renderer_gen=%llu source_seq=%llu picture=%.1f,%.1f-%.1f,%.1f banner=%.1f,%.1f-%.1f,%.1f scope_fit=%d",
					static_cast<unsigned long long>(frameGeneration),
					static_cast<unsigned long long>(sourceSequence),
					placement.visiblePicture.left, placement.visiblePicture.top,
					placement.visiblePicture.right, placement.visiblePicture.bottom,
					placement.panel.left, placement.panel.top,
					placement.panel.right, placement.panel.bottom,
					configuredScreenActive ? 1 : 0);
				lastSweepOverlayPlacement = placement;
				hasSweepOverlayPlacement = true;
			}
			overlay.parts = &overlayPart;
			overlay.num_parts = 1;
			++overlayCount;
		}
		if (overlayCount > 0)
		{
			target.overlays = overlays;
			target.num_overlays = overlayCount;
		}
		const int outputWidth = baseTarget.planes[0].texture ?
			baseTarget.planes[0].texture->params.w : 0;
		const int outputHeight = baseTarget.planes[0].texture ?
			baseTarget.planes[0].texture->params.h : 0;
		const bool nlsPipelineActive = renderParams.num_hooks > 0 &&
			!activeNlsShaderPath.empty();
		std::string nlsPipelineVariant;
		bool nlsPipelineVariantChanged = false;
		if (nlsPipelineActive)
		{
			std::ostringstream variant;
			variant << nlsHookSignature << '|' << width << 'x' << height << "->"
				<< outputWidth << 'x' << outputHeight << '|'
				<< std::lround(target.crop.x0 * 10.0f) << ','
				<< std::lround(target.crop.y0 * 10.0f) << '-'
				<< std::lround(target.crop.x1 * 10.0f) << ','
				<< std::lround(target.crop.y1 * 10.0f);
			nlsPipelineVariant = variant.str();
			nlsPipelineVariantChanged = !nlsPipelineWasActive ||
				nlsPipelineVariant != lastNlsPipelineVariant;
		}

		compileTelemetry.BeginRender();
		const SteadyClock::time_point renderStart = SteadyClock::now();
		const bool targetLutApplied =
			target.lut == displayLut && target.lut_type == PL_LUT_NATIVE;
		const bool rendered = pl_render_image(
			renderer,
			&renderImage,
			&target,
			&renderParams);
		const double renderMs = std::chrono::duration<double, std::milli>(
			SteadyClock::now() - renderStart).count();
		const LibplaceboCompileSnapshot compileSnapshot =
			compileTelemetry.EndRender();
		if (nlsPipelineActive &&
			(nlsPipelineVariantChanged || compileSnapshot.Compiled()))
		{
			const NlsHookMappingState binding =
				ReadNlsHookMapping(nlsHook);
			DebugLog::Log(
				"Alpha shader compile: shader=%s cache=%s render=%s hooks=%d parameters=%d stretch_ratio=%.5f warp_axis=%.1f renderer_generation=%llu queue_generation=%llu source=%llu input=%dx%d output=%dx%d source_crop=%.1f,%.1f-%.1f,%.1f target_crop=%.1f,%.1f-%.1f,%.1f glsl_ms=%.3f spirv_cross_ms=%.3f hlsl_ms=%.3f compile_ms=%.3f render_ms=%.3f queue=%zu/%zu oldest_ms=%.3f",
				activeNlsShaderPath.c_str(),
				compileSnapshot.Compiled() ? "cold" : "warm",
				rendered ? "success" : "failed",
				renderParams.num_hooks,
				binding.parameterCount,
				binding.stretchRatio,
				binding.warpAxis,
				static_cast<unsigned long long>(nlsRendererGeneration),
				static_cast<unsigned long long>(frameGeneration),
				static_cast<unsigned long long>(sourceSequence),
				width, height, outputWidth, outputHeight,
				renderImage.crop.x0, renderImage.crop.y0,
				renderImage.crop.x1, renderImage.crop.y1,
				target.crop.x0, target.crop.y0,
				target.crop.x1, target.crop.y1,
				compileSnapshot.glslMs,
				compileSnapshot.spirvCrossMs,
				compileSnapshot.hlslMs,
				compileSnapshot.glslMs + compileSnapshot.spirvCrossMs +
					compileSnapshot.hlslMs,
				renderMs,
				queueDepthAfterDequeue,
				desiredQueueDepth,
				oldestQueuedAgeMs);
			lastNlsPipelineVariant = nlsPipelineVariant;
		}
		nlsPipelineWasActive = nlsPipelineActive;
		if (!rendered && targetLutApplied)
			RejectDisplayLutAfterRenderFailure();
		const int64_t swapStartQpc = PerformanceCounterNow();
		bool submitted = false;
		if (vpOwnedSwapchain)
		{
			// This mirrors libplacebo's submit ordering while keeping the actual
			// DXGI presentation boundary in VP: flush GPU work, release every
			// backbuffer reference, then Present exactly once.
			pl_gpu_flush(d3d11->gpu);
			if (rendered && renderedOutputCaptureRequested.exchange(false,
				std::memory_order_acq_rel))
			{
				CaptureRenderedOutput(vpOwnedBackbuffer, "VP-owned-DXGI",
					frameGeneration, sourceSequence, image);
			}
			if (outputDiagnostics && rendered && !diagnosticReadbackComplete)
			{
				if (diagnosticReadbackFramesRemaining == 0)
					LogOutputReadback(vpOwnedBackbuffer, "VP-owned-DXGI");
				else
					--diagnosticReadbackFramesRemaining;
			}
			pl_tex_destroy(d3d11->gpu, &vpOwnedFrameTarget);
			vpOwnedBackbuffer.Release();
			if (rendered)
			{
				const HRESULT presentResult = vpOwnedSwapchain->Present(1, 0);
				submitted = SUCCEEDED(presentResult);
				if (!submitted && vpOwnedPresentFailureLoggedGeneration !=
					vpOwnedSwapchainGeneration)
				{
					vpOwnedPresentFailureLoggedGeneration =
						vpOwnedSwapchainGeneration;
					DebugLog::Log(
						"VP-owned DXGI Present failed: generation=%llu result=0x%08lX present_owner=VP",
						static_cast<unsigned long long>(
							vpOwnedSwapchainGeneration),
						static_cast<unsigned long>(presentResult));
				}
			}
		}
		else
		{
			if (rendered && renderedOutputCaptureRequested.exchange(false,
				std::memory_order_acq_rel))
			{
				CaptureRenderedOutput(nullptr, "libplacebo-owned",
					frameGeneration, sourceSequence, image);
			}
			if (outputDiagnostics && rendered && !diagnosticReadbackComplete)
			{
				if (diagnosticReadbackFramesRemaining == 0)
					LogOutputReadback(nullptr, "libplacebo-owned");
				else
					--diagnosticReadbackFramesRemaining;
			}
			submitted = pl_swapchain_submit_frame(swapchain);
			if (submitted)
				pl_swapchain_swap_buffers(swapchain);
		}
		const int64_t swapEndQpc = PerformanceCounterNow();
		LARGE_INTEGER qpcFrequency{};
		QueryPerformanceFrequency(&qpcFrequency);
		const double swapBlockMs =
			qpcFrequency.QuadPart > 0 && swapStartQpc > 0 &&
			swapEndQpc > swapStartQpc
				? static_cast<double>(swapEndQpc - swapStartQpc) * 1000.0 /
					static_cast<double>(qpcFrequency.QuadPart)
				: 0.0;
		if (rendered && submitted)
		{
			++successfulPresentCount;
			AlphaPresentationRecord record;
			record.generation = frameGeneration;
			record.sourceSequence = sourceSequence;
			record.captureTimestamp = videoFrame.GetTimingTimestamp();
			record.enqueueQpc = enqueueQpc;
			record.dequeueQpc = dequeueQpc;
			record.submitQpc = swapStartQpc;
			record.queueDepthAfterDequeue = queueDepthAfterDequeue;
			record.oldestQueuedAgeMs = oldestQueuedAgeMs;
			record.renderMs = renderMs;
			record.swapBlockMs = swapBlockMs;
			record.releaseReason = AlphaSourceReleaseReason::Submitted;

			AlphaDxgiPresentationSample sample;
			sample.generation = frameGeneration;
			sample.qpcFrequency = qpcFrequency.QuadPart;
			CComPtr<IDXGISwapChain> nativeSwapchain;
			nativeSwapchain.Attach(pl_d3d11_swapchain_unwrap(swapchain));
			if (nativeSwapchain)
			{
				UINT lastPresentCount = 0;
				if (SUCCEEDED(nativeSwapchain->GetLastPresentCount(
					&lastPresentCount)))
				{
					record.presentId = lastPresentCount;
				}

				DXGI_FRAME_STATISTICS statistics{};
				const HRESULT statisticsResult =
					nativeSwapchain->GetFrameStatistics(&statistics);
				if (statisticsResult == DXGI_ERROR_FRAME_STATISTICS_DISJOINT)
				{
					sample.timingStatus =
						AlphaPresentationTimingStatus::Disjoint;
					sample.frameStatisticsResult =
						static_cast<int32_t>(statisticsResult);
					sample.disjoint = true;
				}
				else if (SUCCEEDED(statisticsResult))
				{
					sample.timingStatus =
						AlphaPresentationTimingStatus::Available;
					sample.available = true;
					sample.presentCount = statistics.PresentCount;
					sample.presentRefreshCount =
						statistics.PresentRefreshCount;
					sample.syncRefreshCount = statistics.SyncRefreshCount;
					sample.syncQpc = statistics.SyncQPCTime.QuadPart;
				}
				else
				{
					sample.frameStatisticsResult =
						static_cast<int32_t>(statisticsResult);
					sample.timingStatus =
						statisticsResult == DXGI_ERROR_UNSUPPORTED ||
						actualOutput.presentationModel ==
							LibplaceboOutput::PresentationModel::BITBLT
						? AlphaPresentationTimingStatus::FrameStatisticsUnavailable
						: AlphaPresentationTimingStatus::FrameStatisticsFailed;
				}
			}
			presentationTelemetry.RecordSubmission(record);
			presentationTelemetry.Observe(sample);
			const AlphaPresentationSnapshot presentationSnapshot =
				presentationTelemetry.Snapshot();
			if (sample.available &&
				presentationSnapshot.evidence == AlphaPresentationEvidence::Stable &&
				presentationSnapshot.measuredDisplayHz >= 10.0 &&
				sample.qpcFrequency > 0 && sample.syncQpc > 0 &&
				swapEndQpc > 0)
			{
				const int64_t periodQpc = static_cast<int64_t>(std::llround(
					static_cast<double>(sample.qpcFrequency) /
					presentationSnapshot.measuredDisplayHz));
				if (periodQpc > 0)
				{
					int64_t targetQpc = sample.syncQpc + periodQpc;
					if (targetQpc < swapEndQpc)
					{
						const int64_t missedPeriods =
							(swapEndQpc - targetQpc + periodQpc - 1) / periodQpc;
						targetQpc += missedPeriods * periodQpc;
					}
					presentationTargetLeadMs = static_cast<double>(
						targetQpc - swapEndQpc) * 1000.0 /
						static_cast<double>(sample.qpcFrequency);
					presentationTargetTimingKnown = true;
				}
			}

			const uint64_t nowTick = GetTickCount64();
			if (nowTick >= nextPresentationTelemetryLogTick)
			{
				nextPresentationTelemetryLogTick = nowTick + 5000;
				const AlphaPresentationSnapshot snapshot =
					presentationTelemetry.Snapshot();
				DebugLog::Log(
					"Alpha presentation telemetry: generation=%llu evidence=%d timing=%s frame_stats_hr=0x%08lX retained=%zu source=%llu presented=%llu debt=%llu present_id=%u refresh=%u display_hz=%.5f cadence_samples=%u queue_after=%zu oldest_ms=%.2f render_ms=%.2f swap_ms=%.2f",
					static_cast<unsigned long long>(snapshot.generation),
					static_cast<int>(snapshot.evidence),
					AlphaPresentationTimingStatusText(snapshot.timingStatus),
					static_cast<unsigned long>(snapshot.frameStatisticsResult),
					snapshot.retainedRecords,
					static_cast<unsigned long long>(
						snapshot.lastSubmittedSequence),
					static_cast<unsigned long long>(
						snapshot.lastPresentedSequence),
					static_cast<unsigned long long>(
						snapshot.sourceToPresentDebt),
					snapshot.lastPresentId,
					snapshot.lastPresentRefresh,
					snapshot.measuredDisplayHz,
					snapshot.cadenceSamples,
					queueDepthAfterDequeue,
					oldestQueuedAgeMs,
					renderMs,
					swapBlockMs);
			}
		}
		if (rendered && submitted)
		{
			hasPresentedFrame = true;
			if (viewportRequestSerial != 0 &&
				viewportRequestSerial != lastSubmittedScreenProfileRequest)
			{
				const double commandToSubmitMs =
					viewportRequestNs > 0
					? static_cast<double>(SteadyClockNowNs() - viewportRequestNs) /
						1000000.0
					: 0.0;
				DebugLog::Log(
					"libplacebo viewport frame submitted: target=%s request=%llu command_to_submit=%.2f ms render=%.2f ms",
					configuredScreenActive ? "configured" : "output-panel",
					static_cast<unsigned long long>(viewportRequestSerial),
					commandToSubmitMs,
					renderMs);
				lastSubmittedScreenProfileRequest = viewportRequestSerial;
			}
			if (lastRenderedEotf != state.eotf ||
				lastRenderedColorspace != state.colorspace)
			{
				DebugLog::Log(
					"libplacebo tone mapping: input=%s/%s mastering=%.3f..%.1f nits MaxCLL=%.1f MaxFALL=%.1f -> SDR %s %.1f nits",
					CStringA(ToString(state.eotf)).GetString(),
					CStringA(ToString(state.colorspace)).GetString(),
					image.color.hdr.min_luma,
					image.color.hdr.max_luma,
					image.color.hdr.max_cll,
					image.color.hdr.max_fall,
					targetBt2020 ? "BT.2020" : "Rec.709",
					sdrTargetNits);
			}
			lastRenderedEotf = state.eotf;
			lastRenderedColorspace = state.colorspace;
		}
		return rendered && submitted;
	}

	void Resize(HWND videoHwnd)
	{
		std::lock_guard<std::mutex> guard(renderMutex);
		if (!swapchain)
			return;

		RECT client{};
		if (!GetClientRect(videoHwnd, &client))
			return;
		int width = std::max<LONG>(1, client.right - client.left);
		int height = std::max<LONG>(1, client.bottom - client.top);
		if (!pl_swapchain_resize(swapchain, &width, &height))
			DebugLog::Log("libplacebo: swapchain resize failed (%d x %d)", width, height);
		else
			ConfigureAndFallback("resize");
	}

	void RenegotiateOutput()
	{
		std::lock_guard<std::mutex> guard(renderMutex);
		RetryAutoLimitedCandidate("display change");
	}

	bool IsGpuFailed()
	{
		std::lock_guard<std::mutex> guard(renderMutex);
		return d3d11 && pl_gpu_is_failed(d3d11->gpu);
	}
};


namespace
{
uint64_t AlphaSourceFormatKey(const VideoState& state)
{
	// This is an immutable format fingerprint, not a temporal authority key.
	// FNV-1a keeps it compact while ensuring a queued decision cannot cross a
	// material raster/encoding/color contract change.
	uint64_t key = 1469598103934665603ULL;
	auto mix = [&key](uint64_t value)
	{
		key ^= value;
		key *= 1099511628211ULL;
	};
	mix(static_cast<uint64_t>(state.videoFrameEncoding));
	mix(static_cast<uint64_t>(state.eotf));
	mix(static_cast<uint64_t>(state.colorspace));
	mix(state.invertedVertical ? 1ULL : 0ULL);
	if (state.displayMode)
	{
		mix(static_cast<uint64_t>(state.displayMode->FrameWidth()));
		mix(static_cast<uint64_t>(state.displayMode->FrameHeight()));
		mix(static_cast<uint64_t>(state.displayMode->FrameDuration()));
	}
	return key;
}
}


LibplaceboVideoRenderer::LibplaceboVideoRenderer(
	IRendererCallback& callback,
	uint32_t rendererGeneration,
	HWND videoHwnd,
	ITimingClock* timingClock,
	bool useFrameQueue,
	size_t frameQueueMaxSize,
	VideoConversionOverride videoConversionOverride) :
	m_callback(callback),
	m_callbackGeneration(rendererGeneration),
	m_videoHwnd(videoHwnd),
	m_timingClock(timingClock),
	m_useFrameQueue(useFrameQueue),
	m_videoConversionOverride(videoConversionOverride),
	m_frameQueueDesiredDepth(AlphaQueuePolicy::NormalizeDesiredDepth(frameQueueMaxSize)),
	m_frameQueueMaxSize(AlphaQueuePolicy::HardCapacity(frameQueueMaxSize))
{
	{
		std::lock_guard<std::mutex> guard(g_runtimeDisplayRuleMutex);
		m_manualDisplayRule = g_runtimeManualDisplayRule;
	}
	m_shaderRendererGeneration =
		MadVRShaderLoader::BeginRendererGeneration();
	if (!m_manualDisplayRule.empty())
		DebugLog::Log("display: restored runtime manual rule '%s' for new renderer",
			m_manualDisplayRule.c_str());
	m_callback.OnRendererDetailString(
		TEXT("VP Renderer"), m_callbackGeneration);
}


LibplaceboVideoRenderer::~LibplaceboVideoRenderer()
{
	if (m_renderThread.joinable())
	{
		{
			std::lock_guard<std::mutex> guard(m_queueMutex);
			m_stopRequested = true;
		}
		m_queueChanged.notify_all();
		m_renderThread.join();
	}
	ClearQueue("renderer destruction");
	m_impl.reset();
}

bool LibplaceboVideoRenderer::PersistShaderCache()
{
	if (!m_impl) return false;
	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	m_impl->SaveShaderCache();
	return true;
}


bool LibplaceboVideoRenderer::ReloadConfiguredShaderPrewarm()
{
	if (!m_impl) return false;
	ConfigFile config;
	if (!config.Load(ConfigFile::RENDERER_FILENAME)) return false;
	std::vector<ConfiguredShaderRule> rules;
	std::string reason;
	const bool available =
		MadVRShaderLoader::ResolveConfiguredNlsPrewarmRules(
			config, rules, reason);
	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	m_impl->startupNlsPrewarmRules = std::move(rules);
	m_impl->startupNlsPrewarmComplete = !available;
	DebugLog::Log(
		"Alpha shader prewarm configuration reloaded: armed=%d rules=%zu reason=%s",
		available ? 1 : 0, m_impl->startupNlsPrewarmRules.size(),
		reason.empty() ? "none" : reason.c_str());
	return true;
}


bool LibplaceboVideoRenderer::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("null video state is invalid");

	std::lock_guard<std::mutex> guard(m_stateMutex);
	const bool materialSourceTransition = m_videoState &&
		(videoState->valid == false ||
		 !videoState->displayMode ||
		 !m_videoState->displayMode ||
		 *videoState->displayMode != *m_videoState->displayMode ||
		 videoState->videoFrameEncoding != m_videoState->videoFrameEncoding ||
		 videoState->eotf != m_videoState->eotf ||
		 videoState->colorspace != m_videoState->colorspace);
	if (materialSourceTransition && !m_manualDisplayRule.empty())
	{
		// F5/F6 select the display calibration intentionally. Keep that manual
		// selection across input changes until the user selects another profile.
		DebugLog::Log("display: retaining manual rule '%s' after material source transition",
			m_manualDisplayRule.c_str());
	}

	if (m_videoState &&
		(videoState->valid == false ||
		 !videoState->displayMode ||
		 !m_videoState->displayMode ||
		 *videoState->displayMode != *m_videoState->displayMode ||
		 videoState->videoFrameEncoding != m_videoState->videoFrameEncoding))
	{
		return false;
	}

	const EOTF previousEotf = m_videoState ? m_videoState->eotf : EOTF::UNKNOWN;
	const ColorSpace previousColorspace = m_videoState ? m_videoState->colorspace : ColorSpace::UNKNOWN;
	if (m_impl)
	{
		std::string nextRule;
		std::string nextFingerprint;
		RendererSettings nextSettings;
		bool hasUnifiedSettings = false;
		ConfigFile config;
		if (config.Load(ConfigFile::RENDERER_FILENAME) && RendererProfileConfig::IsUnified(config))
		{
			nextSettings = LoadRendererSettings(
				*videoState, nextRule, "", m_manualUnifiedProfiles);
			nextFingerprint = EffectiveSettingsFingerprint(nextSettings, false);
			hasUnifiedSettings = true;
		}
		else
		{
			nextRule = m_manualDisplayRule.empty() ?
				ResolveDisplayRuleName(*videoState) : m_manualDisplayRule;
		}
		const bool changed = nextFingerprint.empty() ?
			nextRule != m_impl->activeDisplayRule :
			nextFingerprint != m_impl->restartSettingsFingerprint;
		if (changed)
		{
			DebugLog::Log(
				"display: effective settings changed (%s -> %s); requesting renderer rebuild",
				m_impl->activeDisplayRule.empty() ? "base" : m_impl->activeDisplayRule.c_str(),
				nextRule.empty() ? "base" : nextRule.c_str());
			return false;
		}
		if (hasUnifiedSettings &&
			EffectiveSettingsFingerprint(nextSettings) !=
				m_impl->effectiveSettingsFingerprint)
		{
			m_impl->ApplyViewportSettings(nextSettings);
			ApplyViewportTarget(nextSettings.configuredScreenTarget,
				nextSettings.configuredScreenAspect, "automatic profile refresh");
			DebugLog::Log(
				"profiles: applied automatic viewport settings live without renderer rebuild");
		}
	}
	m_videoState = new VideoState(*videoState);
	if (materialSourceTransition)
	{
		// Source metadata transitions do not alter the pixels already queued, but
		// must not let their detector history create a boundary in the new epoch.
		m_sceneDetectorGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	const bool sourceColorTransition =
		previousEotf != EOTF::UNKNOWN &&
		(previousEotf != m_videoState->eotf || previousColorspace != m_videoState->colorspace);
	if (sourceColorTransition)
	{
		DebugLog::Log(
			"libplacebo source transition accepted in place: %s/%s -> %s/%s; output remains SDR Rec.709",
			CStringA(ToString(previousEotf)).GetString(),
			CStringA(ToString(previousColorspace)).GetString(),
			CStringA(ToString(m_videoState->eotf)).GetString(),
			CStringA(ToString(m_videoState->colorspace)).GetString());
	}

	return true;
}


void LibplaceboVideoRenderer::OnVideoFrame(VideoFrame& videoFrame)
{
	if (m_state.load(std::memory_order_acquire) != RendererState::RENDERSTATE_RENDERING)
		return;

	uint64_t enqueueGeneration = 0;
	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		if (m_stopRequested)
			return;
		enqueueGeneration = m_queueGeneration;
	}

	const uint64_t counter = m_frameCounter.fetch_add(1, std::memory_order_relaxed);
	UpdateFrameRateAndPPM(videoFrame.GetTimingTimestamp());
	if (m_timingClock && counter % 20 == 0)
	{
		m_entryLatencyMs.store(
			TimingClockDiffMs(
				videoFrame.GetTimingTimestamp(),
				m_timingClock->TimingClockNow(),
				m_timingClock->TimingClockTicksPerSecond()),
			std::memory_order_relaxed);
	}

	VideoStateComPtr frameState;
	{
		std::lock_guard<std::mutex> guard(m_stateMutex);
		frameState = m_videoState;
	}
	if (!frameState)
	{
		const uint64_t droppedTotal =
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed) + 1;
		const uint64_t reasonTotal =
			m_missingFrameStateDrops.fetch_add(
				1, std::memory_order_relaxed) + 1;
		// Log the first occurrence and powers of two. This remains useful for a
		// sustained failure without becoming a per-frame log.
		if ((reasonTotal & (reasonTotal - 1)) == 0)
		{
			DebugLog::Log(
				"Alpha dropped frame: reason=video_state_unavailable reason_total=%llu dropped_total=%llu",
				static_cast<unsigned long long>(reasonTotal),
				static_cast<unsigned long long>(droppedTotal));
		}
		return;
	}

	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		// A callback that began before a reset must not populate the new
		// generation after that reset cleared the queue.
		if (m_stopRequested || enqueueGeneration != m_queueGeneration)
			return;

		const size_t queueLimit = m_useFrameQueue ? m_frameQueueMaxSize : 1;
		while (m_frameQueue.size() >= queueLimit)
		{
			const auto dropCandidate = std::find_if(
				m_frameQueue.begin(), m_frameQueue.end(),
				[](const QueuedFrame& queuedFrame)
				{
					return !queuedFrame.cadenceRepeat;
				});
			if (dropCandidate == m_frameQueue.end())
			{
				DebugLog::Log(
					"Alpha queue pressure: generation=%llu depth=%zu capacity=%zu pending_repeat_action_id=%llu; dropping incoming frame to preserve authorized repeat",
					static_cast<unsigned long long>(
						m_queueGeneration),
					m_frameQueue.size(),
					queueLimit,
					static_cast<unsigned long long>(
						m_frameQueue.front().cadenceActionId));
				m_droppedFrames.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}
			if (m_overflowLoggedGeneration != m_queueGeneration)
			{
				DebugLog::Log(
					"Alpha queue hard overflow: generation=%llu depth=%zu capacity=%zu; dropping oldest frame",
					static_cast<unsigned long long>(m_queueGeneration),
					m_frameQueue.size(),
					queueLimit);
				m_overflowLoggedGeneration = m_queueGeneration;
			}
			m_activePictureTimeline.MarkDiscarded(
				dropCandidate->activePictureIdentity,
				dropCandidate->frame.GetCounter());
			dropCandidate->frame.SourceBufferRelease();
			m_frameQueue.erase(dropCandidate);
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
		}
		videoFrame.SourceBufferAddRef();
		try
		{
			const uint64_t sourceSequence =
				m_sourceSequence.fetch_add(1, std::memory_order_relaxed) + 1;
			ActivePictureFrameIdentity activePictureIdentity;
			activePictureIdentity.transportGeneration = enqueueGeneration;
			activePictureIdentity.acceptedSequence = sourceSequence;
			activePictureIdentity.sourceFrameNumber = videoFrame.GetCounter();
			activePictureIdentity.captureTimestamp = static_cast<uint64_t>(
				videoFrame.GetTimingTimestamp());
			activePictureIdentity.sourceFormatGeneration =
				AlphaSourceFormatKey(*frameState);
			activePictureIdentity.viewportGeneration =
				m_viewportRequestSerial.load(std::memory_order_acquire);
			activePictureIdentity.rendererGeneration = enqueueGeneration;
			if (videoFrame.IsSourceDiscontinuity())
				m_activePictureTimeline.BreakContinuity(videoFrame.GetCounter());
			if (!m_activePictureTimeline.TrackAcceptedFrame(
				activePictureIdentity))
			{
				videoFrame.SourceBufferRelease();
				m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
				DebugLog::Log(
					"Alpha active-picture identity rejected: generation=%llu source=%llu counter=%llu",
					static_cast<unsigned long long>(enqueueGeneration),
					static_cast<unsigned long long>(sourceSequence),
					static_cast<unsigned long long>(videoFrame.GetCounter()));
				return;
			}
			QueuedFrame queuedFrame;
			queuedFrame.frame = videoFrame;
			queuedFrame.state = frameState;
			queuedFrame.generation = enqueueGeneration;
			queuedFrame.sourceSequence = sourceSequence;
			queuedFrame.activePictureIdentity = activePictureIdentity;
			queuedFrame.enqueueQpc = PerformanceCounterNow();
			m_frameQueue.push_back(std::move(queuedFrame));
		}
		catch (const std::exception& e)
		{
			videoFrame.SourceBufferRelease();
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
			DebugLog::Log("libplacebo frame enqueue failed: %s", e.what());
			return;
		}
	}
	m_queueChanged.notify_one();
}


void LibplaceboVideoRenderer::UpdateFrameRateAndPPM(
	timingclocktime_t frameTimestamp)
{
	if (!m_timingClock || frameTimestamp <= 0)
		return;

	const timingclocktime_t previousTimestamp =
		m_lastPpmFrameTimestamp.exchange(frameTimestamp, std::memory_order_acq_rel);
	const timingclocktime_t expectedFrameTicks =
		m_expectedPpmFrameTicks.load(std::memory_order_acquire);
	// A fullscreen/window transition or capture re-prime can leave a real gap
	// between timestamps while frames are intentionally not delivered.  That is
	// not oscillator drift.  Restart the diagnostic window instead of folding
	// the gap into the cumulative cadence estimate.
	if (previousTimestamp > 0 && expectedFrameTicks > 0)
	{
		const timingclocktime_t observedTicks = frameTimestamp - previousTimestamp;
		if (observedTicks <= 0 || observedTicks > expectedFrameTicks * 3 / 2 ||
			observedTicks < expectedFrameTicks / 2)
		{
			DebugLog::Log(
				"libplacebo: capture timestamp discontinuity (%lld ticks; expected %lld); resetting cadence estimate",
				static_cast<long long>(observedTicks),
				static_cast<long long>(expectedFrameTicks));
			ResetFrameRateAndPPM();
			m_firstPpmFrameTimestamp.store(frameTimestamp, std::memory_order_release);
			m_lastPpmFrameTimestamp.store(frameTimestamp, std::memory_order_release);
			m_lastPpmPublishTimestamp.store(frameTimestamp, std::memory_order_release);
			m_ppmFrameCount.store(1, std::memory_order_release);
			return;
		}
	}

	timingclocktime_t firstTimestamp =
		m_firstPpmFrameTimestamp.load(std::memory_order_acquire);
	if (firstTimestamp == 0)
	{
		if (m_firstPpmFrameTimestamp.compare_exchange_strong(
			firstTimestamp, frameTimestamp, std::memory_order_acq_rel))
		{
			m_lastPpmPublishTimestamp.store(frameTimestamp,
				std::memory_order_release);
			m_lastPpmFrameTimestamp.store(frameTimestamp,
				std::memory_order_release);
			m_ppmFrameCount.store(1, std::memory_order_release);
		}
		return;
	}

	const uint64_t frameCount =
		m_ppmFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
	const timingclocktime_t ticksPerSecond =
		m_timingClock->TimingClockTicksPerSecond();
	if (ticksPerSecond <= 0)
		return;

	const timingclocktime_t publishTimestamp =
		m_lastPpmPublishTimestamp.load(std::memory_order_acquire);
	if (frameTimestamp <= publishTimestamp ||
		frameTimestamp - publishTimestamp < ticksPerSecond * 5)
	{
		return;
	}

	timingclocktime_t expectedPublishTimestamp = publishTimestamp;
	if (!m_lastPpmPublishTimestamp.compare_exchange_strong(
		expectedPublishTimestamp, frameTimestamp, std::memory_order_acq_rel))
	{
		return;
	}

	const timingclocktime_t elapsedTicks = frameTimestamp - firstTimestamp;
	if (elapsedTicks <= 0 || frameCount <= 1)
		return;

	double theoreticalRate = 0.0;
	{
		std::lock_guard<std::mutex> guard(m_stateMutex);
		if (m_videoState && m_videoState->displayMode)
			theoreticalRate = m_videoState->displayMode->RefreshRateHz();
	}
	if (theoreticalRate <= 0.0)
		return;

	const double elapsedSeconds = static_cast<double>(elapsedTicks) /
		static_cast<double>(ticksPerSecond);
	const double measuredRate = static_cast<double>(frameCount - 1) /
		elapsedSeconds;
	const int deviationPpm = static_cast<int>(std::round(
		(measuredRate - theoreticalRate) * 1000000.0 / theoreticalRate)) * -1;
	m_measuredFrameRate.store(measuredRate, std::memory_order_relaxed);
	m_ppmDeviation.store(deviationPpm, std::memory_order_relaxed);
	m_hasPpmData.store(true, std::memory_order_release);
	DebugLog::Log(
		"libplacebo: cumulative capture cadence %.6f Hz, %+d ppm (nominal %.6f Hz)",
		measuredRate, deviationPpm, theoreticalRate);
}


void LibplaceboVideoRenderer::ResetFrameRateAndPPM()
{
	m_firstPpmFrameTimestamp.store(0, std::memory_order_release);
	m_lastPpmFrameTimestamp.store(0, std::memory_order_release);
	m_lastPpmPublishTimestamp.store(0, std::memory_order_release);
	m_ppmFrameCount.store(0, std::memory_order_release);
	m_measuredFrameRate.store(0.0, std::memory_order_release);
	m_ppmDeviation.store(0, std::memory_order_release);
	m_hasPpmData.store(false, std::memory_order_release);
}


HRESULT LibplaceboVideoRenderer::OnWindowsEvent(LONG_PTR, LONG_PTR)
{
	return S_OK;
}


void LibplaceboVideoRenderer::Build()
{
	VideoStateComPtr state;
	std::string manualRule;
	std::map<std::string, std::string> manualUnifiedProfiles;
	{
		std::lock_guard<std::mutex> guard(m_stateMutex);
		if (!m_videoState || !m_videoState->valid || !m_videoState->displayMode)
			throw std::runtime_error("libplacebo requires a valid video state before Build");
		state = m_videoState;
		manualRule = m_manualDisplayRule;
		manualUnifiedProfiles = m_manualUnifiedProfiles;
	}

	std::unique_ptr<Impl> impl(new Impl());
	try
	{
		impl->Initialize(m_videoHwnd, state, manualRule, manualUnifiedProfiles,
			m_videoConversionOverride, m_nonCapturingPreparationMode);
	}
	catch (...)
	{
		throw;
	}
	const double nominalRate = state->displayMode->RefreshRateHz();
	const timingclocktime_t ticksPerSecond = m_timingClock ?
		m_timingClock->TimingClockTicksPerSecond() : 0;
	m_expectedPpmFrameTicks.store(
		(nominalRate > 0.0 && ticksPerSecond > 0) ?
		static_cast<timingclocktime_t>(std::llround(
			static_cast<double>(ticksPerSecond) / nominalRate)) : 0,
		std::memory_order_release);
	ResetFrameRateAndPPM();
	m_presentationTargetTimingKnown.store(false, std::memory_order_release);
	m_presentationTargetLeadMs.store(0.0, std::memory_order_relaxed);
	m_captureToPresentationTargetMs.store(0.0, std::memory_order_relaxed);
	m_configuredScreenActive.store(impl->configuredScreenTarget, std::memory_order_release);
	m_impl = std::move(impl);
	// Alpha has no implicit shader chain before a selector is applied. Resolve
	// the validated target grammar only to publish that authoritative baseline
	// (including the root/Off state of each single group); do not activate an
	// editor selection or mutate configuration.
	std::vector<ConfiguredShaderRule> baselineSelection;
	std::vector<std::string> baselineSections;
	std::string baselineReason;
	ConfigFile shaderConfig;
	if (shaderConfig.Load(ConfigFile::RENDERER_FILENAME) &&
		MadVRShaderLoader::ResolveConfiguredRuleSelection(shaderConfig,
			"@shader-key:", ShaderRendererBackend::LIBPLACEBO,
			baselineSelection, baselineSections, baselineReason))
	{
		std::lock_guard<std::mutex> guard(m_stateMutex);
		m_activeShaderSections = std::move(baselineSections);
		m_activeShaderSectionsAvailable = true;
	}
	SetState(RendererState::RENDERSTATE_READY);
}


bool LibplaceboVideoRenderer::SelectDisplayRule(
	const CString& ruleName,
	CString& activeRule,
	bool& rendererRestartRequired)
{
	activeRule.Empty();
	rendererRestartRequired = false;
	const std::string requested = ConfigFile::NormalizeName(CStringA(ruleName).GetString());
	if (requested.empty())
		return false;

	std::lock_guard<std::mutex> guard(m_stateMutex);
	if (requested == "auto")
	{
		if (m_manualDisplayRule.empty())
		{
			std::lock_guard<std::mutex> runtimeGuard(g_runtimeDisplayRuleMutex);
			g_runtimeManualDisplayRule.clear();
			activeRule = TEXT("Auto");
			return true;
		}
		m_manualDisplayRule.clear();
		{
			std::lock_guard<std::mutex> runtimeGuard(g_runtimeDisplayRuleMutex);
			g_runtimeManualDisplayRule.clear();
		}
		activeRule = TEXT("Auto");
		rendererRestartRequired = m_impl != nullptr;
		return true;
	}

	ConfigFile config;
	if (!config.Load(ConfigFile::RENDERER_FILENAME) ||
		(FindDisplayRule(config, requested).name.empty() &&
			FindProfile(config, requested).name.empty()))
	{
		return false;
	}

	if (m_manualDisplayRule == requested)
	{
		activeRule.Format(TEXT("Manual: %S"), requested.c_str());
		return true;
	}

	m_manualDisplayRule = requested;
	{
		std::lock_guard<std::mutex> runtimeGuard(g_runtimeDisplayRuleMutex);
		g_runtimeManualDisplayRule = requested;
	}
	activeRule.Format(TEXT("Manual: %S"), requested.c_str());
	rendererRestartRequired = m_impl != nullptr;
	return true;
}


bool LibplaceboVideoRenderer::SelectShaderRule(
	const CString& ruleName,
	CString& activeRule,
	bool& rendererRestartRequired)
{
	activeRule.Empty();
	rendererRestartRequired = false;
	if (!m_impl)
		return false;

	CT2A selectorUtf8(ruleName, CP_UTF8);
	const std::string selector =
		static_cast<const char*>(selectorUtf8);
	if (MadVRShaderLoader::CanonicalizeRuleSelector(selector).empty())
		return false;
	{
		std::lock_guard<std::mutex> guard(m_impl->renderMutex);
		if (MadVRShaderLoader::RuleSelectorsEqual(
			m_requestedShaderSelector, selector))
		{
			activeRule = CString(
				CStringA(m_impl->activeShaderStatus.c_str()));
			DebugLog::Log(
				"Alpha shaders: coalesced duplicate request for \"%s\"",
				selector.c_str());
			return true;
		}
	}

	std::vector<ConfiguredShaderRule> selection;
	std::vector<std::string> activeSections;
	std::string reason;
	ConfigFile shaderConfig;
	if (!shaderConfig.Load(ConfigFile::RENDERER_FILENAME) ||
		!MadVRShaderLoader::ResolveConfiguredRuleSelection(shaderConfig,
			selector, ShaderRendererBackend::LIBPLACEBO,
			selection, activeSections, reason))
	{
		DebugLog::Log(
			"Alpha shaders: rejected selector \"%s\": %s",
			selector.c_str(), reason.c_str());
		return false;
	}

	m_requestedShaderSelector = selector;
	{
		std::lock_guard<std::mutex> stateGuard(m_stateMutex);
		m_activeShaderSections = activeSections;
		m_activeShaderSectionsAvailable =
			selector.rfind("@shader-key:", 0) == 0;
	}
	{
		std::lock_guard<std::mutex> guard(m_impl->renderMutex);
		m_impl->SetConfiguredShaderSelection(
			selector, selection, m_shaderRendererGeneration);
		activeRule = CString(
			CStringA(m_impl->activeShaderStatus.c_str()));
		m_lastReportedShaderStatusSerial =
			m_impl->activeShaderStatusSerial;
	}
	return true;
}


bool LibplaceboVideoRenderer::RefreshShaderRule(
	CString& activeRule,
	bool& rendererRestartRequired)
{
	activeRule.Empty();
	rendererRestartRequired = false;
	if (!m_impl)
		return false;

	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	activeRule = CString(
		CStringA(m_impl->activeShaderStatus.c_str()));
	const bool changed =
		m_lastReportedShaderStatusSerial !=
		m_impl->activeShaderStatusSerial;
	m_lastReportedShaderStatusSerial =
		m_impl->activeShaderStatusSerial;
	return changed;
}


std::vector<CString> LibplaceboVideoRenderer::ActiveShaders() const
{
	std::vector<CString> shaders;
	if (!m_impl)
		return shaders;
	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	if (m_impl->renderParams.num_hooks > 0 &&
		!m_impl->activeNlsShaderPath.empty())
	{
		CString label;
		label.Format(TEXT("GLSL: %S"),
			FileNameFromPath(
				m_impl->activeNlsShaderPath).c_str());
		shaders.push_back(label);
	}
	return shaders;
}


bool LibplaceboVideoRenderer::GetActiveShaderSections(
	std::vector<CString>& sections) const
{
	sections.clear();
	std::lock_guard<std::mutex> guard(m_stateMutex);
	for (const std::string& section : m_activeShaderSections)
		sections.emplace_back(CStringA(section.c_str()));
	return m_activeShaderSectionsAvailable;
}


CString LibplaceboVideoRenderer::ActiveShaderRule() const
{
	if (!m_impl)
		return TEXT("None");
	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	return CString(CStringA(m_impl->activeShaderStatus.c_str()));
}


bool LibplaceboVideoRenderer::ApplyApplicationState(
	const UnifiedProfileRuntime::Snapshot& snapshot,
	CString& activeState,
	bool& rendererRestartRequired,
	bool& liveResetRequired)
{
	activeState.Empty();
	rendererRestartRequired = false;
	liveResetRequired = false;

	std::unique_lock<std::mutex> guard(m_stateMutex);
	const std::map<std::string, std::string>& next =
		snapshot.effectiveSelections;
	const auto groupChanged = [this, &next](const char* group)
	{
		const auto before = m_manualUnifiedProfiles.find(group);
		const auto after = next.find(group);
		return (before == m_manualUnifiedProfiles.end()) != (after == next.end()) ||
			(before != m_manualUnifiedProfiles.end() && after != next.end() &&
			 before->second != after->second);
	};
	const bool renderingProfileChanged =
		groupChanged("display") || groupChanged("input") ||
		groupChanged("scaling");
	const bool outputProfileChanged = groupChanged("output");
	VideoStateComPtr state = m_videoState;
	std::string candidateProfiles;
	const RendererSettings candidateSettings = state ?
		LoadRendererSettings(*state, candidateProfiles, "", next) :
		RendererSettings();
	rendererRestartRequired = m_impl != nullptr &&
		(!state || EffectiveSettingsFingerprint(candidateSettings, false) !=
			m_impl->restartSettingsFingerprint);
	const bool anyRendererSettingChanged = m_impl &&
		EffectiveSettingsFingerprint(candidateSettings) !=
			m_impl->effectiveSettingsFingerprint;
	if (state && m_impl && renderingProfileChanged && !outputProfileChanged)
	{
		// Rendering profiles own only shader/render description and calibration.
		// They are structurally forbidden from replacing the D3D device or
		// swapchain. Apply them to the existing renderer, then let the application
		// run its cache-preserving LiveQueue reset transaction.
		if (anyRendererSettingChanged &&
			!m_impl->ApplyProfileSettingsLive(candidateSettings))
		{
			rendererRestartRequired = false;
			DebugLog::Log(
				"rendering profile generation %llu rejected instead of rebuilding: output/device policy unexpectedly differed",
				static_cast<unsigned long long>(snapshot.generation));
			return false;
		}
		rendererRestartRequired = false;
		liveResetRequired = true;
	}
	else if (state && m_impl && anyRendererSettingChanged &&
		candidateSettings.profileUpdateMode == ProfileUpdateMode::NEVER)
	{
		// A cache miss cannot be discovered safely without asking libplacebo to
		// render (and therefore compile) the candidate program. NEVER is the safe
		// diagnostic: retain the known-good current program and all settings that
		// feed it rather than partially applying the requested profile.
		rendererRestartRequired = false;
		activeState = TEXT("Profile retained: current shader program");
		DebugLog::Log(
			"application profile generation %llu retained current renderer settings because profile_update_mode=never",
			static_cast<unsigned long long>(snapshot.generation));
		return false;
	}
	if (!liveResetRequired && rendererRestartRequired && state && m_impl &&
		candidateSettings.profileUpdateMode == ProfileUpdateMode::LIVE &&
		m_impl->ApplyProfileSettingsLive(candidateSettings))
	{
		rendererRestartRequired = false;
	}
	m_manualUnifiedProfiles = next;

	activeState.Format(TEXT("Viewport: %S (%S, %S)"),
		snapshot.viewport.profile.c_str(),
		candidateSettings.configuredScreenTarget ?
			snapshot.viewport.screenAspect.Canonical().c_str() :
			"output panel",
		candidateSettings.verticalAlignment.c_str());
	if (!rendererRestartRequired && m_impl)
	{
		m_impl->ApplyViewportSettings(candidateSettings);
		ApplyViewportTarget(candidateSettings.configuredScreenTarget,
			candidateSettings.configuredScreenAspect, "application snapshot");
		DebugLog::Log(
			"application viewport state applied live: %s target=%s explicit=%d vertical_alignment=%s",
			snapshot.viewport.profile.c_str(),
			candidateSettings.configuredScreenTarget ?
				snapshot.viewport.screenAspect.Canonical().c_str() :
				"output-panel",
			candidateSettings.configuredScreenTarget ? 1 : 0,
			candidateSettings.verticalAlignment.c_str());
	}
	DebugLog::Log("application profile generation %llu applied (%s)",
		static_cast<unsigned long long>(snapshot.generation),
		rendererRestartRequired ? "renderer rebuild required" :
			"live");
	return true;
}


void LibplaceboVideoRenderer::Start()
{
	if (!m_impl || m_state.load(std::memory_order_acquire) != RendererState::RENDERSTATE_READY)
		throw std::runtime_error("libplacebo renderer is not ready");

	BeginQueueGeneration("start", true);
	m_renderThread = std::thread(&LibplaceboVideoRenderer::RenderLoop, this);
	SetState(RendererState::RENDERSTATE_RENDERING);
}


void LibplaceboVideoRenderer::Stop()
{
	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		m_stopRequested = true;
	}
	m_queueChanged.notify_all();
	if (m_renderThread.joinable())
		m_renderThread.join();
	ClearQueue("renderer stop");
	SetState(RendererState::RENDERSTATE_STOPPED);
}


void LibplaceboVideoRenderer::Retire() noexcept
{
	try
	{
		// Do not call Stop(): this can run after the GUI has already processed
		// the stopped-state notification, and a second callback could be applied
		// to the replacement renderer. Join directly before releasing the
		// swapchain, so no render or present can still use it.
		if (m_renderThread.joinable())
		{
			{
				std::lock_guard<std::mutex> guard(m_queueMutex);
				m_stopRequested = true;
			}
			m_queueChanged.notify_all();
			m_renderThread.join();
		}
		ClearQueue("renderer retirement");
		bool blackPresented = false;
		bool externalStateRestored = true;
		if (m_impl)
		{
			std::lock_guard<std::mutex> renderGuard(m_impl->renderMutex);
			if (!m_impl->outputResourcesRetired)
				blackPresented = m_impl->PresentBlackFrame();
			externalStateRestored = m_impl->RetireOutput();
		}
		DebugLog::Log(
			"VP Renderer retirement: terminal black present=%d "
			"before swapchain release external_state=%s",
			blackPresented ? 1 : 0,
			externalStateRestored ? "restored" : "pending");
		m_retirementSucceeded.store(
			externalStateRestored, std::memory_order_release);
		if (externalStateRestored)
			m_impl.reset();
		m_hasPresentedLiveFrame.store(false, std::memory_order_release);
		DebugLog::Log(
			"VP Renderer retired: render thread stopped and presentation "
			"swapchain released external_state=%s",
			externalStateRestored ? "restored" : "pending-retry");
	}
	catch (const std::exception& error)
	{
		m_retirementSucceeded.store(false, std::memory_order_release);
		DebugLog::Log("VP Renderer retirement failed: %s", error.what());
	}
	catch (...)
	{
		m_retirementSucceeded.store(false, std::memory_order_release);
		DebugLog::Log("VP Renderer retirement failed with an unknown exception");
	}
}


void LibplaceboVideoRenderer::Reset()
{
	if (m_impl && m_impl->renderer)
	{
		std::lock_guard<std::mutex> guard(m_impl->renderMutex);
		BeginQueueGeneration("renderer reset");
		pl_renderer_flush_cache(m_impl->renderer);
	}
	else
	{
		BeginQueueGeneration("renderer reset");
	}
	m_frameCounter.store(0, std::memory_order_relaxed);
	m_sceneDetectorGeneration.fetch_add(1, std::memory_order_acq_rel);
	ResetFrameRateAndPPM();
	DebugLog::Log("libplacebo renderer reset: new queue generation and renderer cache flushed");
}


void LibplaceboVideoRenderer::ResetLiveQueue()
{
	if (m_impl)
	{
		std::lock_guard<std::mutex> guard(m_impl->renderMutex);
		BeginQueueGeneration("live queue reset");
	}
	else
	{
		BeginQueueGeneration("live queue reset");
	}
	m_sceneDetectorGeneration.fetch_add(1, std::memory_order_acq_rel);
}

void LibplaceboVideoRenderer::SetSceneAwareTimingCorrection(bool enabled)
{
	const bool previous = m_sceneDetectionEnabled.exchange(enabled, std::memory_order_acq_rel);
	if (previous != enabled)
	{
		m_sceneDetectorGeneration.fetch_add(1, std::memory_order_acq_rel);
		m_sceneDetectionStatus.store(
			static_cast<int>(enabled ? SceneDetectorStatus::Warming : SceneDetectorStatus::Disabled),
			std::memory_order_release);
		m_sceneTimingRatesCompatible.store(false, std::memory_order_release);
		m_sceneCorrectionPlanned.store(false, std::memory_order_release);
		m_scenePredictedAction.store(0, std::memory_order_release);
		m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_release);
		m_sceneSecondsUntilPlan.store(0.0, std::memory_order_release);
		m_sceneTimingStatus.store(
			static_cast<int>(enabled
				? AlphaCadenceTimingStatus::WaitingForDxgi
				: AlphaCadenceTimingStatus::Disabled),
			std::memory_order_release);
		m_sceneTimingRateSamples.store(0, std::memory_order_release);
		m_sceneTimingMismatchPpm.store(0.0, std::memory_order_release);
		m_sceneCorrectionDueState.store(0, std::memory_order_release);
		DebugLog::Log("libplacebo scene detection %s", enabled ? "enabled" : "disabled");
	}
}

uint64_t LibplaceboVideoRenderer::SceneAwareCorrectionDropCount() const
{
	return m_sceneCorrectionDropCount.load(std::memory_order_relaxed);
}

uint64_t LibplaceboVideoRenderer::SceneAwareCorrectionRepeatCount() const
{
	return m_sceneCorrectionRepeatCount.load(std::memory_order_relaxed);
}

uint64_t LibplaceboVideoRenderer::SceneAwareDetectedCount() const
{
	return m_sceneDetectedCount.load(std::memory_order_relaxed);
}

bool LibplaceboVideoRenderer::GetSceneDetectionStatus(CString& status) const
{
	switch (static_cast<SceneDetectorStatus>(
		m_sceneDetectionStatus.load(std::memory_order_acquire)))
	{
	case SceneDetectorStatus::Disabled:
		status = TEXT("Disabled");
		break;
	case SceneDetectorStatus::Warming:
		status = TEXT("Warming");
		break;
	case SceneDetectorStatus::Active:
		status = TEXT("Active");
		break;
	default:
		status = TEXT("Unavailable");
		break;
	}
	return true;
}

bool LibplaceboVideoRenderer::GetSceneTimingPrediction(
	double& secondsUntilCorrection, double& secondsUntilPlan,
	int& action, bool& planned) const
{
	action = m_scenePredictedAction.load(std::memory_order_acquire);
	secondsUntilCorrection =
		m_sceneSecondsUntilCorrection.load(std::memory_order_acquire);
	secondsUntilPlan =
		m_sceneSecondsUntilPlan.load(std::memory_order_acquire);
	planned = m_sceneCorrectionPlanned.load(std::memory_order_acquire);
	return action != 0;
}

bool LibplaceboVideoRenderer::GetSceneTimingLastCorrection(
	int& action, double& secondsFromDeadline, uint64_t& correctionTick) const
{
	action = m_sceneLastCorrectionAction.load(std::memory_order_acquire);
	secondsFromDeadline =
		m_sceneLastCorrectionSecondsFromDeadline.load(std::memory_order_acquire);
	correctionTick =
		m_sceneLastCorrectionTick.load(std::memory_order_acquire);
	return action != 0 && correctionTick != 0;
}

bool LibplaceboVideoRenderer::SceneTimingRatesCompatible() const
{
	return m_sceneTimingRatesCompatible.load(std::memory_order_acquire);
}

bool LibplaceboVideoRenderer::GetSceneTimingStatus(CString& status) const
{
	int dueAction = 0;
	CString dueReason;
	if (GetSceneTimingDueStatus(dueAction, dueReason))
	{
		status = TEXT("Due");
		return true;
	}

	const AlphaCadenceTimingStatus timingStatus =
		static_cast<AlphaCadenceTimingStatus>(
			m_sceneTimingStatus.load(std::memory_order_acquire));
	switch (timingStatus)
	{
	case AlphaCadenceTimingStatus::Disabled:
		status = TEXT("Disabled");
		break;
	case AlphaCadenceTimingStatus::WaitingForDxgi:
		status = TEXT("Waiting");
		break;
	case AlphaCadenceTimingStatus::Measuring:
		status = TEXT("Measuring");
		break;
	case AlphaCadenceTimingStatus::Matched:
		status = TEXT("Matched");
		break;
	case AlphaCadenceTimingStatus::Forecasting:
		status = TEXT("Forecasting");
		break;
	case AlphaCadenceTimingStatus::Verifying:
		status = TEXT("Checking");
		break;
	default:
		status = TEXT("Unavailable");
		break;
	}
	return true;
}

bool LibplaceboVideoRenderer::GetSceneTimingDueStatus(
	int& action, CString& reason) const
{
	const uint32_t dueState =
		m_sceneCorrectionDueState.load(std::memory_order_acquire);
	const uint32_t actionCode = dueState & 0x3U;
	action = actionCode == 2U ? 1 : (actionCode == 1U ? -1 : 0);
	const AlphaCadenceBlockReason blockReason =
		static_cast<AlphaCadenceBlockReason>(
			dueState >> 2);
	if (action == 0 || blockReason == AlphaCadenceBlockReason::None)
	{
		action = 0;
		reason.Empty();
		return false;
	}

	switch (blockReason)
	{
	case AlphaCadenceBlockReason::Cooldown:
		reason = TEXT("pause");
		break;
	case AlphaCadenceBlockReason::VerificationPending:
		reason = TEXT("checking");
		break;
	case AlphaCadenceBlockReason::DropQueueNotAboveDesired:
	case AlphaCadenceBlockReason::RepeatQueueNotBelowDesired:
		reason = TEXT("buffer");
		break;
	case AlphaCadenceBlockReason::DropPresentationDebtMissing:
	case AlphaCadenceBlockReason::RepeatPresentationDebtPresent:
		reason = TEXT("timing");
		break;
	case AlphaCadenceBlockReason::WaitingForFreshScene:
		reason = TEXT("scene");
		break;
	case AlphaCadenceBlockReason::FallbackNotMature:
		reason = TEXT("safe point");
		break;
	case AlphaCadenceBlockReason::DropFallbackQueueTooYoung:
		reason = TEXT("buffer wait");
		break;
	default:
		reason = TEXT("safe point");
		break;
	}
	return true;
}


void LibplaceboVideoRenderer::OnSize()
{
	if (m_impl)
		m_impl->Resize(m_videoHwnd);
}


void LibplaceboVideoRenderer::OnPaint()
{
	// DXGI owns presentation for both configured models. DISCARD and
	// FLIP_DISCARD do not guarantee reusable back-buffer contents here.
}


void LibplaceboVideoRenderer::OnDisplayChange()
{
	if (m_impl)
		m_impl->RenegotiateOutput();
}


void LibplaceboVideoRenderer::SetFrameQueueMaxSize(size_t size)
{
	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		m_frameQueueMaxSize = AlphaQueuePolicy::HardCapacity(size);
		m_frameQueueDesiredDepth = AlphaQueuePolicy::ClampDesiredDepthToCapacity(
			m_frameQueueDesiredDepth, m_frameQueueMaxSize);
		DebugLog::Log("Alpha queue hard capacity=%zu, steady target=%zu",
			m_frameQueueMaxSize, m_frameQueueDesiredDepth);
		size_t purgedFrames = 0;
		while (m_frameQueue.size() > m_frameQueueMaxSize)
		{
			m_activePictureTimeline.MarkDiscarded(
				m_frameQueue.front().activePictureIdentity,
				m_frameQueue.front().frame.GetCounter());
			m_frameQueue.front().frame.SourceBufferRelease();
			m_frameQueue.pop_front();
			++purgedFrames;
		}
		if (purgedFrames > 0)
		{
			const uint64_t droppedTotal =
				m_droppedFrames.fetch_add(
					purgedFrames, std::memory_order_relaxed) +
				purgedFrames;
			DebugLog::Log(
				"Alpha dropped frames: reason=queue_resize count=%zu dropped_total=%llu generation=%llu depth=%zu desired=%zu hard_capacity=%zu",
				purgedFrames,
				static_cast<unsigned long long>(droppedTotal),
				static_cast<unsigned long long>(m_queueGeneration),
				m_frameQueue.size(),
				m_frameQueueDesiredDepth,
				m_frameQueueMaxSize);
		}
		m_queueDepthWindowStartNs = SteadyClockNowNs();
		m_queueDepthWindowDequeues = 0;
		m_queueDepthWindowHasSamples = false;
	}
	// A target reduction can satisfy an already pending startup prefill.
	// A target increase after release deliberately does not re-arm it.
	m_queueChanged.notify_all();
}

void LibplaceboVideoRenderer::SetQueueFramePolicy(
	size_t, size_t steadyReserveFrames, bool hasSteadyReserveFrames)
{
	if (!hasSteadyReserveFrames)
		return;

	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		// Alpha has one VP-owned FIFO.  target_frames selects the
		// live/prefill target; queue_size remains the independently configured
		// hard capacity used for bursts and overflow protection.
		m_frameQueueDesiredDepth = AlphaQueuePolicy::ClampDesiredDepthToCapacity(
			steadyReserveFrames, m_frameQueueMaxSize);
		DebugLog::Log(
			"Alpha queue policy: target_frames=%zu target=%zu hard_capacity=%zu",
			steadyReserveFrames, m_frameQueueDesiredDepth, m_frameQueueMaxSize);
	}
	m_queueChanged.notify_all();
}


void LibplaceboVideoRenderer::SetActivePictureLookaheadFrames(size_t frames)
{
	const size_t boundedFrames = (std::min)(frames, size_t{ 8 });
	m_activePictureLookaheadFrames.store(
		boundedFrames, std::memory_order_release);
	DebugLog::Log(
		"Alpha active-picture look-ahead retained: configured=%zu queue-unchanged=1 runtime-active=0",
		boundedFrames);
}


void LibplaceboVideoRenderer::ApplyViewportTarget(
	bool configured, double aspect, const char* reason)
{
	if (!m_impl)
		return;

	m_viewportRequestNs.store(
		SteadyClockNowNs(), std::memory_order_relaxed);
	const uint64_t requestSerial =
		m_viewportRequestSerial.fetch_add(
			1, std::memory_order_relaxed) + 1;
	m_configuredScreenActive.store(configured, std::memory_order_release);
	DebugLog::Log(
		"libplacebo viewport target selected: source=%s target=%s aspect=%.7f request=%llu",
		reason ? reason : "unknown", configured ? "configured" : "output-panel",
		configured ? aspect : 0.0,
		static_cast<unsigned long long>(requestSerial));

	CString details;
	if (configured)
		details.Format(TEXT("VP Renderer - Screen: %.4f:1"), aspect);
	else
		details = TEXT("VP Renderer - Screen: output panel");
	m_callback.OnRendererDetailString(details, m_callbackGeneration);
}


size_t LibplaceboVideoRenderer::GetFrameQueueSize()
{
	std::lock_guard<std::mutex> guard(m_queueMutex);
	return m_frameQueue.size();
}


size_t LibplaceboVideoRenderer::GetConvertedQueueSize()
{
	return 0;
}


double LibplaceboVideoRenderer::EntryLatencyMs() const
{
	return m_entryLatencyMs.load(std::memory_order_relaxed);
}


double LibplaceboVideoRenderer::ExitLatencyMs() const
{
	return m_exitLatencyMs.load(std::memory_order_relaxed);
}


uint64_t LibplaceboVideoRenderer::DroppedFrameCount() const
{
	return m_droppedFrames.load(std::memory_order_relaxed);
}


bool LibplaceboVideoRenderer::GetOutputModeInfo(CString& details) const
{
	if (!m_impl)
	{
		details.Empty();
		return false;
	}

	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	auto requestPresentation = [](LibplaceboOutput::PresentationRequest value)
	{
		switch (value)
		{
		case LibplaceboOutput::PresentationRequest::COMPOSED: return "C";
		case LibplaceboOutput::PresentationRequest::DIRECT: return "D";
		default: return "A";
		}
	};
	auto requestRange = [](LibplaceboOutput::RangeRequest value)
	{
		switch (value)
		{
		case LibplaceboOutput::RangeRequest::FULL: return "F";
		case LibplaceboOutput::RangeRequest::LIMITED: return "L";
		default: return "A";
		}
	};
	auto actualPresentation = [](LibplaceboOutput::PresentationModel value)
	{
		switch (value)
		{
		case LibplaceboOutput::PresentationModel::BITBLT: return "Blt";
		case LibplaceboOutput::PresentationModel::FLIP: return "Flip";
		default: return "?";
		}
	};
	CStringA value;
	const char* outputTarget = m_impl->targetBt2020
		? (m_impl->reportBt2020ToDisplay
			? (m_impl->nvidiaBt2020Reporter.IsReadbackVerified()
				? "SDR BT.2020 / HDMI BT.2020 (verified)"
				: (m_impl->nvidiaBt2020Reporter.IsActive()
					? "SDR BT.2020 / HDMI BT.2020 (SET)"
					: "SDR BT.2020 / HDMI signal unavailable"))
			: (m_impl->bt2020SignalingFailed
				? "SDR BT.2020 / HDMI signal unavailable"
				: "SDR BT.2020 / display manual"))
		: "SDR Rec.709";
	value.Format(
		"Target %s | Req %s/%s/%s/%s -> %s/%s/%s/%s",
		outputTarget,
		requestPresentation(m_impl->outputPlan.request.presentation),
		requestRange(m_impl->outputPlan.request.range),
		LibplaceboOutput::ToString(m_impl->outputPlan.request.gamma),
		LibplaceboOutput::ToString(m_impl->outputPlan.request.primaries),
		actualPresentation(m_impl->actualOutput.presentationModel),
		m_impl->actualOutput.safeToRender
			? (std::string(LibplaceboOutput::ToRangeString(
				m_impl->actualOutput.encoding)) == "FULL" ? "F" : "L")
			: "?",
		m_impl->actualOutput.safeToRender
			? LibplaceboOutput::ToGammaString(m_impl->actualOutput.encoding)
			: "?",
		m_impl->actualOutput.safeToRender &&
			(m_impl->actualOutput.encoding ==
				LibplaceboOutput::DxgiEncoding::FULL_G22_P2020 ||
			 m_impl->actualOutput.encoding ==
				LibplaceboOutput::DxgiEncoding::STUDIO_G22_P2020 ||
			 m_impl->actualOutput.encoding ==
				LibplaceboOutput::DxgiEncoding::STUDIO_G24_P2020)
				? "2020" : "709");
	CStringA gammaAdjustment;
	CStringA outputStatus;
	if (!m_impl->actualOutput.safeToRender)
	{
		outputStatus.Format("Blocked: %s", m_impl->actualOutput.reason.c_str());
	}
	else if (!m_impl->actualOutput.requestedEncodingActive)
	{
		const auto requestedGamma = m_impl->outputPlan.request.gamma;
		if (requestedGamma != LibplaceboOutput::GammaRequest::AUTO &&
			requestedGamma != LibplaceboOutput::GammaRequest::SRGB &&
			m_impl->actualOutput.encoding ==
				LibplaceboOutput::DxgiEncoding::FULL_G22_P709)
		{
			gammaAdjustment.Format("%s req -> sRGB (Windows)",
				LibplaceboOutput::ToString(requestedGamma));
		}
		else
		{
			outputStatus = "Windows-compatible RGB active";
		}
	}
	if (m_impl->activeSettings.diagnosticVpOwnedDxgiPresenter &&
		m_impl->swapchainBlit)
	{
		if (!outputStatus.IsEmpty()) outputStatus += "; ";
		outputStatus += "Composed presentation active";
	}
	if (!gammaAdjustment.IsEmpty())
	{
		value += " | GAMMA: ";
		value += gammaAdjustment;
	}
	if (!outputStatus.IsEmpty())
	{
		value += " | STATUS: ";
		value += outputStatus;
	}
	if (m_impl->actualOutput.safeToRender &&
		m_impl->actualOutput.targetTransfer !=
			LibplaceboOutput::TargetTransfer::SWAPCHAIN)
	{
		value += " | DISPLAY: ";
		value += LibplaceboOutput::ToString(
			m_impl->actualOutput.targetTransfer);
	}
	if (!m_impl->lastSdrGammaDecision.reason.empty())
	{
		CStringA gamma;
		gamma.Format(" | SDR Gamma %s: %s (%s->%s; target %s)",
			LibplaceboOutput::ToString(
				m_impl->lastSdrGammaDecision.requested),
			LibplaceboOutput::ToString(
				m_impl->lastSdrGammaDecision.action),
			LibplaceboOutput::ToString(
				m_impl->lastSdrGammaDecision.declaredSource),
			LibplaceboOutput::ToString(
				m_impl->lastSdrGammaDecision.effectiveSource),
			LibplaceboOutput::ToString(
				m_impl->lastSdrGammaDecision.actualTarget));
		value += gamma;
	}
	details = CString(value);
	return true;
}


bool LibplaceboVideoRenderer::GetOutputContractStatus(
	RendererOutputContract::Status& status) const
{
	status = {};
	if (!m_impl)
		return false;

	using namespace RendererOutputContract;
	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	status.available = true;
	status.safeToRender = m_impl->actualOutput.safeToRender;
	status.requestedContractActive =
		m_impl->actualOutput.requestedEncodingActive;
	status.vpOwnsPresentation = m_impl->vpOwnedSwapchain != nullptr;
	status.dxgiAppliedVerified = status.vpOwnsPresentation &&
		m_impl->vpOwnedColorSpaceVerified;
	status.strictContract = m_impl->outputPlan.strictContract;
	status.successfulPresents = m_impl->successfulPresentCount;
	status.rendererContent = !m_impl->diagnosticReadbackComplete ?
		RendererOutputContract::RendererContentEvidence::UNKNOWN :
		m_impl->diagnosticReadbackNonBlack ?
			RendererOutputContract::RendererContentEvidence::NONBLACK :
			RendererOutputContract::RendererContentEvidence::ALL_BLACK;
	const AlphaPresentationSnapshot presentationSnapshot =
		m_impl->presentationTelemetry.Snapshot();
	status.displayDelivery = presentationSnapshot.lastPresentedSequence > 0 ?
		RendererOutputContract::DisplayDeliveryEvidence::PRESENTED :
		m_impl->successfulPresentCount > 0 ?
			RendererOutputContract::DisplayDeliveryEvidence::SUBMITTED :
			RendererOutputContract::DisplayDeliveryEvidence::UNKNOWN;
	status.swapchainBitDepth =
		m_impl->negotiatedSwapchainFormat == DXGI_FORMAT_R10G10B10A2_UNORM ? 10 :
		m_impl->negotiatedSwapchainFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 16 :
		m_impl->negotiatedSwapchainFormat == DXGI_FORMAT_B8G8R8A8_UNORM ||
		m_impl->negotiatedSwapchainFormat == DXGI_FORMAT_R8G8B8A8_UNORM ? 8 : 0;
	status.presentation = m_impl->actualOutput.presentationModel ==
		LibplaceboOutput::PresentationModel::FLIP ? Presentation::FLIP :
		m_impl->actualOutput.presentationModel ==
		LibplaceboOutput::PresentationModel::BITBLT ? Presentation::BITBLT :
		Presentation::UNKNOWN;
	status.range = std::string(LibplaceboOutput::ToRangeString(
		m_impl->actualOutput.encoding)) == "FULL" ? Range::FULL : Range::LIMITED;
	const pl_color_transfer transfer = m_impl->ResolvedPixelTransfer(
		m_impl->actualOutput.encoding, m_impl->actualOutput.targetTransfer);
	switch (transfer)
	{
	case PL_COLOR_TRC_SRGB: status.transfer = Transfer::SRGB; break;
	case PL_COLOR_TRC_BT_1886: status.transfer = Transfer::BT1886; break;
	case PL_COLOR_TRC_GAMMA18: status.transfer = Transfer::GAMMA18; break;
	case PL_COLOR_TRC_GAMMA20: status.transfer = Transfer::GAMMA20; break;
	case PL_COLOR_TRC_GAMMA22: status.transfer = Transfer::GAMMA22; break;
	case PL_COLOR_TRC_GAMMA24: status.transfer = Transfer::GAMMA24; break;
	case PL_COLOR_TRC_GAMMA26: status.transfer = Transfer::GAMMA26; break;
	case PL_COLOR_TRC_GAMMA28: status.transfer = Transfer::GAMMA28; break;
	default: status.transfer = Transfer::UNKNOWN; break;
	}
	status.primaries = m_impl->targetBt2020 ? Primaries::BT2020 : Primaries::REC709;
	status.dxgiDeclaration = LibplaceboOutput::ToString(
		m_impl->actualOutput.encoding);
	status.swapchainFormat =
		m_impl->negotiatedSwapchainFormat == DXGI_FORMAT_R10G10B10A2_UNORM ?
			"R10G10B10A2_UNORM" :
		m_impl->negotiatedSwapchainFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ?
			"R16G16B16A16_FLOAT" :
		m_impl->negotiatedSwapchainFormat == DXGI_FORMAT_B8G8R8A8_UNORM ?
			"B8G8R8A8_UNORM" :
		m_impl->negotiatedSwapchainFormat == DXGI_FORMAT_R8G8B8A8_UNORM ?
			"R8G8B8A8_UNORM" : "UNKNOWN";
	status.reason = m_impl->actualOutput.reason;
	return true;
}

bool LibplaceboVideoRenderer::RequestRenderedOutputCapture(CString& status)
{
	if (!m_impl || m_state.load(std::memory_order_acquire) !=
		RendererState::RENDERSTATE_RENDERING)
	{
		status = TEXT("Rendered-output capture requires an active VP Renderer");
		return false;
	}
	bool expected = false;
	if (!m_impl->renderedOutputCaptureRequested.compare_exchange_strong(
		expected, true, std::memory_order_acq_rel))
	{
		status = TEXT("A rendered-output capture is already queued");
		return false;
	}
	status = TEXT("Rendered-output capture queued (screenshots folder)");
	DebugLog::Log(
		"Rendered-output capture queued: stage=next-rendered-frame output_directory=%ls",
		ScreenshotDirectory().c_str());
	return true;
}


bool LibplaceboVideoRenderer::GetDisplayLutInfo(CString& details) const
{
	if (!m_impl)
	{
		details.Empty();
		return false;
	}

	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	details = CString(CStringA(m_impl->displayLutStatus.c_str()));
	return true;
}

bool LibplaceboVideoRenderer::GetVideoIngressInfo(CString& details) const
{
	if (!m_impl)
	{
		details.Empty();
		return false;
	}

	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	details = CString(CStringA(m_impl->ingressStatus.c_str()));
	return !details.IsEmpty();
}

bool LibplaceboVideoRenderer::GetPresentationTargetTiming(
	double& leadMs, double& captureToTargetMs) const
{
	leadMs = m_presentationTargetLeadMs.load(std::memory_order_relaxed);
	captureToTargetMs = m_captureToPresentationTargetMs.load(
		std::memory_order_relaxed);
	return m_presentationTargetTimingKnown.load(std::memory_order_acquire);
}

bool LibplaceboVideoRenderer::GetPresentationTimingStatus(CString& status) const
{
	status.Empty();
	if (!m_impl)
		return false;

	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	const AlphaPresentationSnapshot snapshot =
		m_impl->presentationTelemetry.Snapshot();
	CStringA value;
	if (snapshot.evidence == AlphaPresentationEvidence::Stable)
		value = "ready";
	else if (snapshot.evidence == AlphaPresentationEvidence::Warming)
		value = "warming";
	else if (snapshot.evidence == AlphaPresentationEvidence::Disjoint)
		value = "rewarming: disjoint";
	else
	{
		switch (snapshot.timingStatus)
		{
		case AlphaPresentationTimingStatus::FrameStatisticsUnavailable:
			value = m_impl->actualOutput.presentationModel ==
				LibplaceboOutput::PresentationModel::BITBLT
				? "unsupported: BitBlt" : "unsupported: frame stats";
			break;
		case AlphaPresentationTimingStatus::FrameStatisticsFailed:
			value.Format("failed: 0x%08lX",
				static_cast<unsigned long>(snapshot.frameStatisticsResult));
			break;
		case AlphaPresentationTimingStatus::Available:
			value = "warming: incomplete sample";
			break;
		default:
			value = "unsupported: no swapchain";
			break;
		}
	}
	status = CString(value);
	return true;
}

bool LibplaceboVideoRenderer::SetNativeStatsOverlay(
	const uint8_t* pixels, size_t byteCount, int width, int height, int stride)
{
	if (!m_impl)
		return false;
	if (pixels && (width <= 0 || height <= 0 || stride < width * 4 ||
		byteCount < static_cast<size_t>(stride) * height))
		return false;

	std::lock_guard<std::mutex> guard(m_impl->statsOverlayMutex);
	if (pixels)
		m_impl->statsOverlayPixels.assign(pixels, pixels + byteCount);
	else
		m_impl->statsOverlayPixels.clear();
	m_impl->statsOverlayWidth = pixels ? width : 0;
	m_impl->statsOverlayHeight = pixels ? height : 0;
	m_impl->statsOverlayStride = pixels ? stride : 0;
	++m_impl->statsOverlaySerial;
	return true;
}

bool LibplaceboVideoRenderer::SetNativeSweepOverlay(
	const uint8_t* pixels, size_t byteCount, int width, int height, int stride)
{
	if (!m_impl)
		return false;
	if (pixels && (width <= 0 || height <= 0 || stride < width * 4 ||
		byteCount < static_cast<size_t>(stride) * height))
		return false;

	std::lock_guard<std::mutex> guard(m_impl->statsOverlayMutex);
	if (pixels)
		m_impl->sweepOverlayPixels.assign(pixels, pixels + byteCount);
	else
		m_impl->sweepOverlayPixels.clear();
	m_impl->sweepOverlayWidth = pixels ? width : 0;
	m_impl->sweepOverlayHeight = pixels ? height : 0;
	m_impl->sweepOverlayStride = pixels ? stride : 0;
	++m_impl->sweepOverlaySerial;
	return true;
}


bool LibplaceboVideoRenderer::GetConversionPerformance(
	double& currentUs, double& avg10s, double& max10s) const
{
	if (!m_impl || !m_impl->formatter)
		return false;
	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	m_impl->formatter->GetConversionPerformance(currentUs, avg10s, max10s);
	return currentUs > 0.0 || avg10s > 0.0 || max10s > 0.0;
}


bool LibplaceboVideoRenderer::GetFrameRateAndPPM(
	double& measuredFps,
	int& ppmDeviation) const
{
	if (!m_hasPpmData.load(std::memory_order_acquire))
	{
		measuredFps = 0.0;
		ppmDeviation = 0;
		return false;
	}

	measuredFps = m_measuredFrameRate.load(std::memory_order_relaxed);
	ppmDeviation = m_ppmDeviation.load(std::memory_order_relaxed);
	return measuredFps > 0.0;
}


void LibplaceboVideoRenderer::AnalyzeActivePictureLookahead(
	std::vector<QueuedFrame>& previewFrames,
	uint8_t availableLookahead)
{
	struct PreviewEvidence
	{
		ActivePictureFrameIdentity identity;
		ActivePictureObservation observation;
		double framesPerSecond = 0.0;
	};
	std::vector<PreviewEvidence> observations;
	observations.reserve(previewFrames.size());
	for (const QueuedFrame& queued : previewFrames)
	{
		if (!queued.state || !queued.state->displayMode ||
			!queued.frame.GetData())
			continue;
		const VideoState& state = *queued.state;
		AnalysisLumaFormat format;
		AlphaNativeRgbLayout rgbLayout;
		if (GetAlphaNativeRgbLayout(state.videoFrameEncoding, rgbLayout))
			format = AnalysisLumaFormat::NativeRgb;
		else if (state.videoFrameEncoding == VideoFrameEncoding::UYVY ||
			state.videoFrameEncoding == VideoFrameEncoding::HDYC ||
			state.videoFrameEncoding == VideoFrameEncoding::V210)
			format = AnalysisLumaFormat::NativeYuv422;
		else
			continue;
		AnalysisLumaSource source = {
			reinterpret_cast<const uint8_t*>(queued.frame.GetData()),
			static_cast<size_t>(state.BytesPerFrame()),
			static_cast<int>(state.displayMode->FrameWidth()),
			static_cast<int>(state.displayMode->FrameHeight()),
			static_cast<size_t>(state.BytesPerRow()), 0, format,
			state.videoFrameEncoding, state.colorspace,
			queued.activePictureIdentity.sourceFormatGeneration
		};
		const ActivePictureEvidence evidence =
			ExtractActivePictureEvidence(source);
		PreviewEvidence preview;
		preview.identity = queued.activePictureIdentity;
		preview.observation.frameNumber = queued.sourceSequence;
		preview.observation.available = evidence.available;
		preview.observation.framesPerSecond =
			state.displayMode->RefreshRateHz();
		if (evidence.available)
		{
			preview.observation.bounds = evidence.classification ==
				ActivePictureClassification::BAR_CROP_TRUSTED
				? evidence.trustedBounds : evidence.proposedBounds;
			preview.observation.classification = evidence.classification;
		}
		preview.framesPerSecond = state.displayMode->RefreshRateHz();
		observations.push_back(preview);
	}

	const uint8_t configured = static_cast<uint8_t>((std::min)(
		m_activePictureLookaheadFrames.load(std::memory_order_acquire),
		size_t{ ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES }));
	{
		std::lock_guard<std::mutex> queueGuard(m_queueMutex);
		if (m_activePictureLookaheadLoggedGeneration != m_queueGeneration ||
			m_activePictureLookaheadLoggedAvailable != availableLookahead)
		{
			m_activePictureLookaheadLoggedGeneration = m_queueGeneration;
			m_activePictureLookaheadLoggedAvailable = availableLookahead;
			DebugLog::Log(
				"Alpha active-picture look-ahead preview: generation=%llu configured=%u available=%u effective=%u runtime-apply=pending",
				static_cast<unsigned long long>(m_queueGeneration),
				static_cast<unsigned>(configured),
				static_cast<unsigned>(availableLookahead),
				static_cast<unsigned>((std::min)(configured, availableLookahead)));
		}
		for (const PreviewEvidence& preview : observations)
		{
			auto queued = std::find_if(m_frameQueue.begin(), m_frameQueue.end(),
				[&preview](const QueuedFrame& candidate)
				{
					return candidate.activePictureIdentity.transportGeneration ==
						preview.identity.transportGeneration &&
						candidate.activePictureIdentity.acceptedSequence ==
						preview.identity.acceptedSequence;
				});
			if (queued == m_frameQueue.end() ||
				queued->activePicturePreviewAnalyzed)
				continue;
			queued->activePicturePreviewAnalyzed = true;
			if (!m_activePictureTimeline.ShouldAnalyze(
				preview.observation.frameNumber, preview.framesPerSecond))
				continue;
			ActivePictureFrameDecision decision;
			if (!m_activePictureTimeline.SubmitScheduledObservation(
				preview.identity, preview.observation, configured,
				availableLookahead, decision))
				continue;
			auto target = std::find_if(m_frameQueue.begin(), m_frameQueue.end(),
				[&decision](const QueuedFrame& candidate)
				{
					return candidate.activePictureIdentity.transportGeneration ==
						decision.effectiveIdentity.transportGeneration &&
						candidate.activePictureIdentity.acceptedSequence ==
						decision.effectiveIdentity.acceptedSequence;
				});
			if (target == m_frameQueue.end())
				continue;
			target->activePicturePreviewDecision = decision;
			target->activePicturePreviewDecisionAvailable = true;
			DebugLog::Log(
				"Alpha active-picture look-ahead decision: generation=%llu observed=%llu effective=%llu configured=%u available=%u effective_lead=%u late=%d rect=%d,%d-%d,%d runtime-apply=pending",
				static_cast<unsigned long long>(
					decision.observationIdentity.transportGeneration),
				static_cast<unsigned long long>(
					decision.observationIdentity.acceptedSequence),
				static_cast<unsigned long long>(
					decision.effectiveIdentity.acceptedSequence),
				static_cast<unsigned>(decision.configuredLookahead),
				static_cast<unsigned>(decision.availableLookahead),
				static_cast<unsigned>(decision.effectiveLookahead),
				decision.late ? 1 : 0,
				decision.transition.bounds.left,
				decision.transition.bounds.top,
				decision.transition.bounds.right,
				decision.transition.bounds.bottom);
		}
	}

}


void LibplaceboVideoRenderer::RenderLoop()
{
	unsigned int consecutiveFailures = 0;
	for (;;)
	{
		VideoFrame frame;
		VideoStateComPtr state;
		uint64_t frameGeneration = 0;
		uint64_t sourceSequence = 0;
		ActivePictureFrameIdentity activePictureIdentity;
		bool activePicturePreviewDecisionAvailable = false;
		ActivePictureFrameDecision activePicturePreviewDecision;
		int64_t enqueueQpc = 0;
		int64_t dequeueQpc = 0;
		size_t queueDepthAfterDequeue = 0;
		size_t desiredQueueDepth = 1;
		double oldestQueuedAgeMs = 0.0;
		std::vector<QueuedFrame> activePicturePreviewFrames;
		uint8_t activePictureAvailableLookahead = 0;
		bool cadenceRepeat = false;
		uint64_t cadenceActionId = 0;
		uint64_t cadencePolicyGeneration = 0;
		uint64_t cadenceDetectorGeneration = 0;
		uint64_t cadencePresentationDebt = 0;
		uint32_t cadencePresentId = 0;
		double cadenceDeadlineSeconds = 0.0;
		bool prefillReleased = false;
		size_t prefillDepth = 0;
		size_t prefillTarget = 0;
		bool depthSummaryReady = false;
		uint64_t depthSummaryGeneration = 0;
		size_t depthSummaryCurrent = 0;
		size_t depthSummaryDesired = 0;
		size_t depthSummaryLow = 0;
		size_t depthSummaryHigh = 0;
		size_t depthSummaryMin = 0;
		size_t depthSummaryMax = 0;
		size_t depthSummaryCapacity = 0;
		uint64_t depthSummaryDequeues = 0;
		{
			std::unique_lock<std::mutex> lock(m_queueMutex);
			m_queueChanged.wait(lock, [this]()
			{
				return m_stopRequested || CanDequeueLocked();
			});
			if (m_stopRequested)
				break;
			if (m_startupPrefillPending)
			{
				m_startupPrefillPending = false;
				prefillReleased = true;
				prefillDepth = m_frameQueue.size();
				prefillTarget = PrefillTargetLocked();
			}
			frame = m_frameQueue.front().frame;
			state = m_frameQueue.front().state;
			frameGeneration = m_frameQueue.front().generation;
			sourceSequence = m_frameQueue.front().sourceSequence;
			activePictureIdentity =
				m_frameQueue.front().activePictureIdentity;
			activePicturePreviewDecisionAvailable =
				m_frameQueue.front().activePicturePreviewDecisionAvailable;
			activePicturePreviewDecision =
				m_frameQueue.front().activePicturePreviewDecision;
			enqueueQpc = m_frameQueue.front().enqueueQpc;
			cadenceRepeat = m_frameQueue.front().cadenceRepeat;
			cadenceActionId =
				m_frameQueue.front().cadenceActionId;
			cadencePolicyGeneration =
				m_frameQueue.front().cadencePolicyGeneration;
			cadenceDetectorGeneration =
				m_frameQueue.front().cadenceDetectorGeneration;
			cadencePresentationDebt =
				m_frameQueue.front().cadencePresentationDebt;
			cadencePresentId =
				m_frameQueue.front().cadencePresentId;
			cadenceDeadlineSeconds =
				m_frameQueue.front().cadenceDeadlineSeconds;
			m_activePictureTimeline.MarkConsumed(
				m_frameQueue.front().activePictureIdentity);
			m_frameQueue.pop_front();

			const size_t requestedLookahead = (std::min)(
				m_activePictureLookaheadFrames.load(std::memory_order_acquire),
				size_t{ ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES });
			if (requestedLookahead > 0)
			{
				size_t sourceLead = 0;
				for (const QueuedFrame& queued : m_frameQueue)
				{
					if (queued.cadenceRepeat)
						continue;
					if (sourceLead >= requestedLookahead)
						break;
					++sourceLead;
					if (queued.activePicturePreviewAnalyzed)
						continue;
					activePicturePreviewFrames.push_back(queued);
					activePicturePreviewFrames.back().frame.SourceBufferAddRef();
				}
				activePictureAvailableLookahead = static_cast<uint8_t>(sourceLead);
			}

			const size_t remainingDepth = m_frameQueue.size();
			queueDepthAfterDequeue = remainingDepth;
			desiredQueueDepth = PrefillTargetLocked();
			dequeueQpc = PerformanceCounterNow();
			LARGE_INTEGER qpcFrequency{};
			if (!m_frameQueue.empty() &&
				QueryPerformanceFrequency(&qpcFrequency) &&
				qpcFrequency.QuadPart > 0 &&
				dequeueQpc > m_frameQueue.front().enqueueQpc)
			{
				oldestQueuedAgeMs =
					static_cast<double>(
						dequeueQpc - m_frameQueue.front().enqueueQpc) *
					1000.0 / static_cast<double>(qpcFrequency.QuadPart);
			}
			if (!m_queueDepthWindowHasSamples)
			{
				m_queueDepthWindowMin = remainingDepth;
				m_queueDepthWindowMax = remainingDepth;
				m_queueDepthWindowHasSamples = true;
			}
			else
			{
				m_queueDepthWindowMin =
					std::min(m_queueDepthWindowMin, remainingDepth);
				m_queueDepthWindowMax =
					std::max(m_queueDepthWindowMax, remainingDepth);
			}
			++m_queueDepthWindowDequeues;
			const int64_t nowNs = SteadyClockNowNs();
			if (m_queueDepthWindowStartNs == 0)
				m_queueDepthWindowStartNs = nowNs;
			if (nowNs - m_queueDepthWindowStartNs >= 5000000000LL)
			{
				depthSummaryReady = true;
				depthSummaryGeneration = m_queueGeneration;
				depthSummaryCurrent = remainingDepth;
				depthSummaryDesired = PrefillTargetLocked();
				depthSummaryLow =
					AlphaQueuePolicy::HealthyLowWater(depthSummaryDesired);
				depthSummaryHigh =
					AlphaQueuePolicy::HealthyHighWater(depthSummaryDesired);
				depthSummaryMin = m_queueDepthWindowMin;
				depthSummaryMax = m_queueDepthWindowMax;
				depthSummaryCapacity =
					m_useFrameQueue ? m_frameQueueMaxSize : 1;
				depthSummaryDequeues = m_queueDepthWindowDequeues;
				m_queueDepthWindowStartNs = nowNs;
				m_queueDepthWindowDequeues = 0;
				m_queueDepthWindowHasSamples = false;
			}
		}

		if (m_activePictureLookaheadFrames.load(std::memory_order_acquire) > 0)
		{
			try
			{
				AnalyzeActivePictureLookahead(
					activePicturePreviewFrames,
					activePictureAvailableLookahead);
			}
			catch (const std::exception& e)
			{
				DebugLog::Log(
					"Alpha active-picture look-ahead preview failed: %s",
					e.what());
			}
			catch (...)
			{
				DebugLog::Log(
					"Alpha active-picture look-ahead preview failed: unknown exception");
			}
			for (QueuedFrame& preview : activePicturePreviewFrames)
				preview.frame.SourceBufferRelease();
		}

		if (prefillReleased)
		{
			DebugLog::Log(
				"Alpha queue startup prefill released: generation=%llu depth=%zu target=%zu",
				static_cast<unsigned long long>(frameGeneration),
				prefillDepth,
				prefillTarget);
		}
		const ActivePictureFrameIdentity& effectiveIdentity =
			activePicturePreviewDecision.effectiveIdentity;
		const bool activePicturePreviewIdentityMatches =
			activePicturePreviewDecisionAvailable &&
			SameActivePictureFrameIdentity(
				effectiveIdentity, activePictureIdentity);
		if (activePicturePreviewDecisionAvailable &&
			!activePicturePreviewIdentityMatches)
		{
			DebugLog::Log(
				"Alpha active-picture look-ahead rejected: generation=%llu source=%llu observed=%llu effective=%llu reason=identity_mismatch runtime-apply=0",
				static_cast<unsigned long long>(frameGeneration),
				static_cast<unsigned long long>(sourceSequence),
				static_cast<unsigned long long>(
					activePicturePreviewDecision.observationIdentity.acceptedSequence),
				static_cast<unsigned long long>(effectiveIdentity.acceptedSequence));
		}
		if (depthSummaryReady)
		{
			DebugLog::Log(
				"Alpha queue depth summary: generation=%llu current=%zu desired=%zu healthy=%zu..%zu observed=%zu..%zu dequeues=%llu hard_capacity=%zu",
				static_cast<unsigned long long>(depthSummaryGeneration),
				depthSummaryCurrent,
				depthSummaryDesired,
				depthSummaryLow,
				depthSummaryHigh,
				depthSummaryMin,
				depthSummaryMax,
				static_cast<unsigned long long>(depthSummaryDequeues),
				depthSummaryCapacity);
		}

		bool rendered = false;
		bool staleGeneration = false;
		AlphaCadenceCorrectionDecision correctionDecision;
		bool presentationTargetTimingKnown = false;
		double presentationTargetLeadMs = 0.0;
		const double captureRateHz =
			m_measuredFrameRate.load(std::memory_order_relaxed);
		const SteadyClock::time_point renderCycleStart = SteadyClock::now();
		try
		{
			std::lock_guard<std::mutex> renderGuard(m_impl->renderMutex);
			{
				std::lock_guard<std::mutex> queueGuard(m_queueMutex);
				staleGeneration =
					m_stopRequested || frameGeneration != m_queueGeneration;
			}
			if (!staleGeneration)
			{
				rendered = state && m_impl->RenderLocked(
					frame,
					state,
					frameGeneration,
					sourceSequence,
					activePictureIdentity,
					activePicturePreviewIdentityMatches
						? &activePicturePreviewDecision : nullptr,
					enqueueQpc,
					dequeueQpc,
					queueDepthAfterDequeue,
					desiredQueueDepth,
					oldestQueuedAgeMs,
					cadenceRepeat,
					captureRateHz,
					m_configuredScreenActive.load(std::memory_order_acquire),
					m_viewportRequestSerial.load(std::memory_order_acquire),
					m_viewportRequestNs.load(std::memory_order_relaxed),
					m_sceneDetectionEnabled.load(std::memory_order_acquire),
					m_videoConversionOverride,
					m_sceneDetectorGeneration.load(std::memory_order_acquire),
					m_sceneDetectedCount,
					m_sceneDetectionStatus,
					correctionDecision,
					presentationTargetTimingKnown,
					presentationTargetLeadMs);

				const double renderCycleMs =
					std::chrono::duration<double, std::milli>(
						SteadyClock::now() - renderCycleStart).count();
				if (rendered &&
					correctionDecision.action != AlphaCadenceAction::Drop)
				{
					size_t queueBefore = 0;
					size_t queueAfter = 0;
					size_t recoveryTarget = 0;
					size_t droppedFrames = 0;
					size_t canceledRepeats = 0;
					double oldestBeforeMs = 0.0;
					double oldestAfterMs = 0.0;
					bool recoveredBacklog = false;
					{
						std::lock_guard<std::mutex> queueGuard(m_queueMutex);
						queueBefore = m_frameQueue.size();
						recoveryTarget = PrefillTargetLocked();
						LARGE_INTEGER qpcFrequency{};
						const int64_t nowQpc = PerformanceCounterNow();
						const bool qpcAvailable =
							nowQpc > 0 && QueryPerformanceFrequency(&qpcFrequency) &&
							qpcFrequency.QuadPart > 0;
						auto queuedAgeMs = [&](const QueuedFrame& queuedFrame)
						{
							return qpcAvailable && nowQpc > queuedFrame.enqueueQpc ?
								static_cast<double>(
									nowQpc - queuedFrame.enqueueQpc) * 1000.0 /
								static_cast<double>(qpcFrequency.QuadPart) : 0.0;
						};
						if (!m_frameQueue.empty())
							oldestBeforeMs = queuedAgeMs(m_frameQueue.front());
						recoveredBacklog = AlphaQueuePolicy::ShouldRecoverBacklog(
							queueBefore,
							recoveryTarget,
							oldestBeforeMs,
							renderCycleMs,
							captureRateHz);
						if (recoveredBacklog)
						{
							while (m_frameQueue.size() > recoveryTarget)
							{
								if (m_frameQueue.front().cadenceRepeat)
									++canceledRepeats;
								m_activePictureTimeline.MarkDiscarded(
									m_frameQueue.front().activePictureIdentity,
									m_frameQueue.front().frame.GetCounter());
								m_frameQueue.front().frame.SourceBufferRelease();
								m_frameQueue.pop_front();
								++droppedFrames;
							}
							queueAfter = m_frameQueue.size();
							if (!m_frameQueue.empty())
								oldestAfterMs = queuedAgeMs(m_frameQueue.front());
							m_queueDepthWindowStartNs = SteadyClockNowNs();
							m_queueDepthWindowDequeues = 0;
							m_queueDepthWindowHasSamples = false;
						}
					}

					if (recoveredBacklog)
					{
						const uint64_t droppedTotal =
							m_droppedFrames.fetch_add(
								droppedFrames, std::memory_order_relaxed) +
							droppedFrames;
						m_impl->ResetTimingAfterBacklogRecovery(frameGeneration);
						m_presentationTargetTimingKnown.store(
							false, std::memory_order_release);
						presentationTargetTimingKnown = false;
						correctionDecision = {};
						const bool renderStall = renderCycleMs >=
							AlphaQueuePolicy::RenderStallThresholdMs(captureRateHz);
						const std::string shaderName =
							m_impl->activeNlsShaderPath.empty() ? "none" :
							FileNameFromPath(m_impl->activeNlsShaderPath);
						DebugLog::Log(
							"Alpha backlog recovery: generation=%llu source=%llu trigger=%s render_cycle_ms=%.3f threshold_ms=%.3f queue_before=%zu target=%zu oldest_before_ms=%.3f dropped=%zu dropped_total=%llu canceled_repeats=%zu queue_after=%zu oldest_after_ms=%.3f shader=%s mapping=%s decision=flush_to_target",
							static_cast<unsigned long long>(frameGeneration),
							static_cast<unsigned long long>(sourceSequence),
							renderStall ? "render_stall" : "stale_age",
							renderCycleMs,
							AlphaQueuePolicy::RenderStallThresholdMs(captureRateHz),
							queueBefore,
							recoveryTarget,
							oldestBeforeMs,
							droppedFrames,
							static_cast<unsigned long long>(droppedTotal),
							canceledRepeats,
							queueAfter,
							oldestAfterMs,
							shaderName.c_str(),
							NlsMappingModeName(m_impl->nlsDecision.mode));
						m_queueChanged.notify_all();
					}
				}
			}
		}
		catch (const std::exception& e)
		{
			DebugLog::Log("libplacebo render failure: %s", e.what());
		}
		catch (...)
		{
			DebugLog::Log("libplacebo render failure: unknown exception");
		}

		if (cadenceRepeat)
		{
			uint64_t repeatCount =
				m_sceneCorrectionRepeatCount.load(
					std::memory_order_relaxed);
			if (rendered)
			{
				repeatCount =
					m_sceneCorrectionRepeatCount.fetch_add(
						1, std::memory_order_relaxed) + 1;
				m_sceneLastCorrectionAction.store(
					1, std::memory_order_release);
				m_sceneLastCorrectionSecondsFromDeadline.store(
					cadenceDeadlineSeconds,
					std::memory_order_release);
				m_sceneLastCorrectionTick.store(
					GetTickCount64(), std::memory_order_release);
			}
			{
				std::lock_guard<std::mutex> renderGuard(
					m_impl->renderMutex);
				m_impl->RecordCadenceRepeatConsumption(
					cadenceActionId,
					cadencePolicyGeneration,
					cadenceDetectorGeneration,
					frameGeneration,
					sourceSequence,
					cadencePresentId,
					cadencePresentationDebt,
					cadenceDeadlineSeconds,
					rendered,
					queueDepthAfterDequeue,
					repeatCount);
			}
		}

		if (!cadenceRepeat)
		{
			m_sceneTimingRatesCompatible.store(
				correctionDecision.ratesCompatible, std::memory_order_release);
			m_sceneCorrectionPlanned.store(
				correctionDecision.planned, std::memory_order_release);
			const int predictedAction = correctionDecision.predictionValid
				? static_cast<int>(correctionDecision.predictedAction)
				: 0;
			m_scenePredictedAction.store(
				predictedAction, std::memory_order_release);
			m_sceneSecondsUntilCorrection.store(
				correctionDecision.secondsUntilCorrection,
				std::memory_order_release);
			m_sceneSecondsUntilPlan.store(
				correctionDecision.secondsUntilPlan,
				std::memory_order_release);
			m_sceneTimingStatus.store(
				static_cast<int>(correctionDecision.timingStatus),
				std::memory_order_release);
			m_sceneTimingRateSamples.store(
				correctionDecision.rateFilterSamples,
				std::memory_order_release);
			m_sceneTimingMismatchPpm.store(
				correctionDecision.filteredMismatchPpm,
				std::memory_order_release);
			const uint32_t dueAction =
				correctionDecision.predictedAction ==
					AlphaCadenceAction::Repeat ? 2U :
				(correctionDecision.predictedAction ==
					AlphaCadenceAction::Drop ? 1U : 0U);
			const uint32_t dueState =
				correctionDecision.due &&
				correctionDecision.blockReason !=
					AlphaCadenceBlockReason::None
				? (static_cast<uint32_t>(
					correctionDecision.blockReason) << 2) | dueAction
				: 0U;
			m_sceneCorrectionDueState.store(
				dueState, std::memory_order_release);
		}

		if (!rendered)
		{
			if (correctionDecision.action != AlphaCadenceAction::None)
			{
				std::lock_guard<std::mutex> renderGuard(
					m_impl->renderMutex);
				m_impl->RecordCadenceNativeOutcome(
					correctionDecision,
					frameGeneration,
					"render_failed",
					false,
					"caller_release_pending",
					queueDepthAfterDequeue,
					0);
			}
			frame.SourceBufferRelease();
			if (staleGeneration)
				continue;

			const uint64_t droppedTotal =
				m_droppedFrames.fetch_add(
					1, std::memory_order_relaxed) + 1;
			const uint64_t reasonTotal =
				m_renderFailureDrops.fetch_add(
					1, std::memory_order_relaxed) + 1;
			const bool gpuFailed = m_impl->IsGpuFailed();
			const bool windowVisible = IsWindowVisible(m_videoHwnd);
			const bool windowIconic = IsIconic(m_videoHwnd);
			// First occurrence and powers of two make a persistent failure
			// obvious while keeping a per-frame failure from flooding the log.
			if ((reasonTotal & (reasonTotal - 1)) == 0)
			{
				DebugLog::Log(
					"Alpha dropped frame: reason=render_failed reason_total=%llu dropped_total=%llu generation=%llu visible=%d iconic=%d gpu_failed=%d",
					static_cast<unsigned long long>(reasonTotal),
					static_cast<unsigned long long>(droppedTotal),
					static_cast<unsigned long long>(frameGeneration),
					windowVisible ? 1 : 0,
					windowIconic ? 1 : 0,
					gpuFailed ? 1 : 0);
			}
			if (gpuFailed)
			{
				DebugLog::Log(
					"libplacebo renderer failure requires reconstruction: gpu_failed=%d consecutive_failures=%u",
					1,
					consecutiveFailures);
				SetState(RendererState::RENDERSTATE_FAILED);
				break;
			}

			// Swapchain acquisition may be unavailable by design while the render
			// window is hidden or minimized. Do not turn that presentation pause
			// into a device failure or carry it into a later visible failure streak.
			if (!windowVisible || windowIconic)
			{
				consecutiveFailures = 0;
				continue;
			}

			if (++consecutiveFailures == 1)
				DebugLog::Log("libplacebo failed to render a visible frame; retrying");
			if (consecutiveFailures >= 300)
			{
				DebugLog::Log(
					"libplacebo renderer failed 300 consecutive visible frames with a healthy GPU; marking renderer failed for swapchain reconstruction");
				SetState(RendererState::RENDERSTATE_FAILED);
				break;
			}
			continue;
		}
		consecutiveFailures = 0;
		m_hasPresentedLiveFrame.store(true, std::memory_order_release);
		m_presentedFrameCount.fetch_add(1, std::memory_order_release);

		if (correctionDecision.action == AlphaCadenceAction::Drop)
		{
			frame.SourceBufferRelease();
			const uint64_t dropCount =
				m_sceneCorrectionDropCount.fetch_add(
					1, std::memory_order_relaxed) + 1;
			m_droppedFrames.fetch_add(
				1, std::memory_order_relaxed);
			m_sceneLastCorrectionAction.store(-1, std::memory_order_release);
			m_sceneLastCorrectionSecondsFromDeadline.store(
				correctionDecision.secondsUntilCorrection,
				std::memory_order_release);
			m_sceneLastCorrectionTick.store(
				GetTickCount64(), std::memory_order_release);
			{
				std::lock_guard<std::mutex> renderGuard(
					m_impl->renderMutex);
				m_impl->RecordCadenceNativeOutcome(
					correctionDecision,
					frameGeneration,
					"drop_released",
					true,
					"released",
					queueDepthAfterDequeue,
					dropCount);
			}
			continue;
		}

		if (correctionDecision.action == AlphaCadenceAction::Repeat)
		{
			bool repeatQueued = false;
			size_t repeatQueueDepth = 0;
			const char* repeatOutcome = "unknown";
			std::string repeatError;
			{
				std::lock_guard<std::mutex> queueGuard(m_queueMutex);
				if (m_stopRequested)
				{
					repeatOutcome = "stop_requested";
				}
				else if (frameGeneration != m_queueGeneration)
				{
					repeatOutcome = "generation_changed";
				}
				else
				{
					try
					{
						QueuedFrame repeatFrame;
						repeatFrame.frame = frame;
						repeatFrame.state = state;
						repeatFrame.generation = frameGeneration;
						repeatFrame.sourceSequence = sourceSequence;
						repeatFrame.activePictureIdentity = activePictureIdentity;
						repeatFrame.enqueueQpc = enqueueQpc;
						repeatFrame.cadenceRepeat = true;
						repeatFrame.cadenceActionId = correctionDecision.actionId;
						repeatFrame.cadencePolicyGeneration =
							correctionDecision.diagnostic.policyGeneration;
						repeatFrame.cadenceDetectorGeneration =
							correctionDecision.diagnostic.detectorGeneration;
						repeatFrame.cadencePresentationDebt =
							correctionDecision.diagnostic.presentationDebt;
						repeatFrame.cadencePresentId =
							correctionDecision.diagnostic.lastPresentId;
						repeatFrame.cadenceDeadlineSeconds =
							correctionDecision.secondsUntilCorrection;
						m_frameQueue.push_front(std::move(repeatFrame));
						repeatQueued = true;
						repeatQueueDepth = m_frameQueue.size();
						repeatOutcome = "queued";
					}
					catch (const std::exception& e)
					{
						repeatOutcome = "enqueue_exception";
						repeatError = e.what();
					}
					catch (...)
					{
						repeatOutcome =
							"enqueue_unknown_exception";
					}
				}
			}
			if (repeatQueued)
			{
				const uint64_t repeatCount =
					m_sceneCorrectionRepeatCount.load(
						std::memory_order_relaxed);
				{
					std::lock_guard<std::mutex> renderGuard(
						m_impl->renderMutex);
					m_impl->RecordCadenceNativeOutcome(
						correctionDecision,
						frameGeneration,
						repeatOutcome,
						true,
						"retained_in_queue",
						repeatQueueDepth,
						repeatCount);
				}
				m_queueChanged.notify_one();
				continue;
			}
			{
				std::lock_guard<std::mutex> renderGuard(m_impl->renderMutex);
				m_impl->RecordCadenceNativeOutcome(
					correctionDecision,
					frameGeneration,
					repeatOutcome,
					false,
					"caller_release_pending",
					queueDepthAfterDequeue,
					m_sceneCorrectionRepeatCount.load(
						std::memory_order_relaxed),
					repeatError.empty()
						? "none" : repeatError.c_str());
			}
		}

		if (m_timingClock)
		{
			const double captureToSubmitMs = TimingClockDiffMs(
				frame.GetTimingTimestamp(),
				m_timingClock->TimingClockNow(),
				m_timingClock->TimingClockTicksPerSecond());
			m_exitLatencyMs.store(captureToSubmitMs,
				std::memory_order_relaxed);
			if (presentationTargetTimingKnown)
			{
				// Use the raw hardware-capture timestamp here, rather than the
				// offset-adjusted timestamp used for VP's internal timing. This
				// makes Total comparable with the audio extraction boundary while
				// leaving Alpha's immediate FIFO presentation unchanged.
				const double captureToTargetMs = TimingClockDiffMs(
					frame.GetCaptureTimingTimestamp(),
					m_timingClock->TimingClockNow(),
					m_timingClock->TimingClockTicksPerSecond()) + presentationTargetLeadMs;
				m_presentationTargetLeadMs.store(presentationTargetLeadMs,
					std::memory_order_relaxed);
				m_captureToPresentationTargetMs.store(
					captureToTargetMs,
					std::memory_order_relaxed);
				m_presentationTargetTimingKnown.store(true,
					std::memory_order_release);
			}
		}

		frame.SourceBufferRelease();
	}
}


void LibplaceboVideoRenderer::ClearQueue(const char* reason)
{
	std::lock_guard<std::mutex> guard(m_queueMutex);
	ClearQueueLocked(reason);
}


void LibplaceboVideoRenderer::BeginQueueGeneration(
	const char* reason,
	bool clearStopRequest)
{
	m_presentationTargetTimingKnown.store(false, std::memory_order_release);
	uint64_t generation = 0;
	size_t target = 0;
	size_t capacity = 0;
	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		ClearQueueLocked(reason);
		if (++m_queueGeneration == 0)
			++m_queueGeneration;
		m_activePictureTimeline.Reset(m_queueGeneration);
		m_overflowLoggedGeneration = 0;
		m_startupPrefillPending = true;
		m_queueDepthWindowStartNs = SteadyClockNowNs();
		m_queueDepthWindowDequeues = 0;
		m_queueDepthWindowHasSamples = false;
		if (clearStopRequest)
			m_stopRequested = false;
		generation = m_queueGeneration;
		target = PrefillTargetLocked();
		capacity = m_useFrameQueue ? m_frameQueueMaxSize : 1;
	}
	DebugLog::Log(
		"Alpha queue generation armed: generation=%llu reason=%s prefill_target=%zu hard_capacity=%zu",
		static_cast<unsigned long long>(generation),
		reason,
		target,
		capacity);
	m_queueChanged.notify_all();
}


void LibplaceboVideoRenderer::ClearQueueLocked(const char* reason)
{
	if (!m_frameQueue.empty())
	{
		m_activePictureTimeline.BreakContinuity(
			m_frameQueue.front().frame.GetCounter());
	}
	for (QueuedFrame& queuedFrame : m_frameQueue)
	{
		m_activePictureTimeline.MarkConsumed(
			queuedFrame.activePictureIdentity);
		if (queuedFrame.cadenceRepeat)
		{
			DebugLog::Log(
				"Alpha cadence native: action_id=%llu action=repeat outcome=repeat_queue_canceled success=0 policy_generation=%llu detector_generation=%llu queue_generation=%llu source=%llu present_id=%u deadline_s=%+.6f debt=%llu counter=%llu rollback=deferred_to_policy_generation ownership=released cancel_reason=%s",
				static_cast<unsigned long long>(
					queuedFrame.cadenceActionId),
				static_cast<unsigned long long>(
					queuedFrame.cadencePolicyGeneration),
				static_cast<unsigned long long>(
					queuedFrame.cadenceDetectorGeneration),
				static_cast<unsigned long long>(
					queuedFrame.generation),
				static_cast<unsigned long long>(
					queuedFrame.sourceSequence),
				queuedFrame.cadencePresentId,
				queuedFrame.cadenceDeadlineSeconds,
				static_cast<unsigned long long>(
					queuedFrame.cadencePresentationDebt),
				static_cast<unsigned long long>(
					m_sceneCorrectionRepeatCount.load(
						std::memory_order_relaxed)),
				reason ? reason : "unknown");
		}
		queuedFrame.frame.SourceBufferRelease();
	}
	m_frameQueue.clear();
}


size_t LibplaceboVideoRenderer::PrefillTargetLocked() const
{
	return m_useFrameQueue ?
		AlphaQueuePolicy::ClampDesiredDepthToCapacity(
			m_frameQueueDesiredDepth, m_frameQueueMaxSize) : 1;
}


bool LibplaceboVideoRenderer::CanDequeueLocked() const
{
	return AlphaQueuePolicy::CanDequeue(
		m_frameQueue.size(),
		PrefillTargetLocked(),
		m_startupPrefillPending);
}


void LibplaceboVideoRenderer::SetState(RendererState state)
{
	m_state.store(state, std::memory_order_release);
	m_callback.OnRendererState(state, m_callbackGeneration);
}
