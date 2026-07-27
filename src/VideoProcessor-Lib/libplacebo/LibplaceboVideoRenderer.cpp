#include <pch.h>

#include "LibplaceboVideoRenderer.h"

#include <ConfigFile.h>
#include <RendererProfileConfig.h>
#include <DebugLog.h>
#include <DisplayRuleExpression.h>
#include <libplacebo/AlphaCadenceCorrectionPolicy.h>
#include <libplacebo/AlphaQueuePolicy.h>
#include <libplacebo/LibplaceboDisplayLut.h>
#include <libplacebo/AlphaPresentationTelemetry.h>
#include <SceneDetector.h>
#include <libplacebo/LibplaceboOutputPolicy.h>
#include <video_frame_formatter/CARGBtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CDeckLinkRGBToP010VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>

#pragma warning(push)
#pragma warning(disable: 4244) // conversion warning in an upstream inline helper
#include <libplacebo/cache.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/utils/upload.h>
#pragma warning(pop)

#include <dxgi1_4.h>
#include <nvapi.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{
	using SteadyClock = std::chrono::steady_clock;

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

	constexpr const char* RENDERER_STATE_FILENAME =
		"VideoProcessorRenderer.state";
	constexpr const char* SHADER_CACHE_RELATIVE_PATH =
		"libplacebo\\VideoProcessorShaderCache.bin";
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
		~NvidiaBt2020Reporter() { Restore(); }

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
			Restore();

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
			const bool verifiedBt2020 =
				status == NVAPI_OK &&
				verified.infoframe.video.colorimetry ==
					NV_INFOFRAME_FIELD_VALUE_AVI_COLORIMETRY_USE_EXTENDED_COLORIMETRY &&
				verified.infoframe.video.extendedColorimetry ==
					NV_INFOFRAME_FIELD_VALUE_AVI_EXTENDEDCOLORIMETRY_RESERVED06;
			if (!verifiedBt2020)
			{
				DebugLog::Log("NVIDIA BT.2020 report: AVI InfoFrame verification failed for %s status=%s colorimetry=%u extended=%u; restoring prior signal while BT.2020 rendering continues", name.c_str(), NvApiStatusText(status).c_str(), static_cast<unsigned int>(verified.infoframe.video.colorimetry), static_cast<unsigned int>(verified.infoframe.video.extendedColorimetry));
				m_active = true;
				m_displayName = name;
				Restore();
				return false;
			}

			m_active = true;
			m_displayName = name;
			DebugLog::Log("NVIDIA BT.2020 report: AVI InfoFrame enabled on %s display_id=0x%08X previous_colorimetry=%u previous_extended=%u verified_colorimetry=%u verified_extended=%u", name.c_str(), m_displayId, static_cast<unsigned int>(m_originalInfoFrame.infoframe.video.colorimetry), static_cast<unsigned int>(m_originalInfoFrame.infoframe.video.extendedColorimetry), static_cast<unsigned int>(verified.infoframe.video.colorimetry), static_cast<unsigned int>(verified.infoframe.video.extendedColorimetry));
			return true;
		}

		void Restore()
		{
			if (m_active)
			{
				NV_INFOFRAME_DATA restore = m_originalInfoFrame;
				restore.cmd = NV_INFOFRAME_CMD_SET;
				restore.type = INFOFRAME_TYPE_AVI;
				const NvAPI_Status status =
					NvAPI_Disp_InfoFrameControl(m_displayId, &restore);
				DebugLog::Log("NVIDIA BT.2020 report: AVI InfoFrame restore on %s display_id=0x%08X colorimetry=%u extended=%u result=%s", m_displayName.c_str(), m_displayId, static_cast<unsigned int>(restore.infoframe.video.colorimetry), static_cast<unsigned int>(restore.infoframe.video.extendedColorimetry), NvApiStatusText(status).c_str());
			}
			m_active = false;
			m_displayName.clear();
			Shutdown();
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
		NvU32 m_displayId = 0;
		std::string m_displayName;
		NV_INFOFRAME_DATA m_originalInfoFrame{};
		static constexpr NvU8 INFOFRAME_TYPE_AVI = 2;
	};

	std::string RendererStatePath()
	{
		char modulePath[MAX_PATH] = {};
		const DWORD length =
			GetModuleFileNameA(nullptr, modulePath, ARRAYSIZE(modulePath));
		if (length == 0 || length >= ARRAYSIZE(modulePath))
			return RENDERER_STATE_FILENAME;

		std::string path(modulePath, length);
		const size_t separator = path.find_last_of("\\/");
		if (separator == std::string::npos)
			return RENDERER_STATE_FILENAME;
		path.resize(separator + 1);
		path += RENDERER_STATE_FILENAME;
		return path;
	}

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

	bool TryLoadPersistedScreenProfile(bool& scopeScreen)
	{
		const std::string path = RendererStatePath();
		std::ifstream stateFile(path);
		if (!stateFile.is_open())
			return false;

		std::string line;
		while (std::getline(stateFile, line))
		{
			line = ConfigFile::Trim(line);
			if (line.empty() || line.front() == '#' || line.front() == ';')
				continue;

			const size_t equals = line.find('=');
			if (equals == std::string::npos ||
				ConfigFile::NormalizeName(line.substr(0, equals)) !=
					"screen_profile")
			{
				continue;
			}

			const std::string value =
				ConfigFile::NormalizeName(line.substr(equals + 1));
			if (value == "normal" || value == "scope")
			{
				scopeScreen = value == "scope";
				DebugLog::Log(
					"libplacebo screen profile restored from %s: %s",
					path.c_str(),
					scopeScreen ? "scope" : "normal");
				return true;
			}

			DebugLog::Log(
				"libplacebo: invalid persisted screen profile '%s' in %s; using configured default",
				value.c_str(),
				path.c_str());
			return false;
		}

		DebugLog::Log(
			"libplacebo: no screen_profile entry in %s; using configured default",
			path.c_str());
		return false;
	}

	void PersistScreenProfile(bool scopeScreen)
	{
		const std::string path = RendererStatePath();
		const std::string temporaryPath = path + ".tmp";
		{
			std::ofstream stateFile(
				temporaryPath,
				std::ios::out | std::ios::trunc);
			if (!stateFile.is_open())
			{
				DebugLog::Log(
					"libplacebo: unable to save screen profile state to %s",
					temporaryPath.c_str());
				return;
			}

			stateFile
				<< "# Managed by VideoProcessor. Delete this file to use "
					"default_screen_profile.\n"
				<< "screen_profile="
				<< (scopeScreen ? "scope" : "normal")
				<< "\n";
			stateFile.close();
			if (!stateFile)
			{
				DeleteFileA(temporaryPath.c_str());
				DebugLog::Log(
					"libplacebo: failed while saving screen profile state to %s",
					temporaryPath.c_str());
				return;
			}
		}

		if (!MoveFileExA(
			temporaryPath.c_str(),
			path.c_str(),
			MOVEFILE_REPLACE_EXISTING))
		{
			const DWORD error = GetLastError();
			DeleteFileA(temporaryPath.c_str());
			DebugLog::Log(
				"libplacebo: unable to replace screen profile state %s error=%lu",
				path.c_str(),
				error);
			return;
		}

		DebugLog::Log(
			"libplacebo screen profile saved to %s: %s",
			path.c_str(),
			scopeScreen ? "scope" : "normal");
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

	void LibplaceboLog(void*, enum pl_log_level level, const char* message)
	{
		if (!message)
			return;

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

	std::unique_ptr<IVideoFrameFormatter> CreateP010Formatter(VideoFrameEncoding encoding)
	{
		switch (encoding)
		{
		case VideoFrameEncoding::V210:
			return std::unique_ptr<IVideoFrameFormatter>(new CV210toP010VideoFrameFormatter());
		case VideoFrameEncoding::UYVY:
			return std::unique_ptr<IVideoFrameFormatter>(new CUYVYtoP010VideoFrameFormatter());
		case VideoFrameEncoding::ARGB_8BIT:
		case VideoFrameEncoding::BGRA_8BIT:
			return std::unique_ptr<IVideoFrameFormatter>(new CARGBtoP010VideoFrameFormatter());
		case VideoFrameEncoding::R10b:
		case VideoFrameEncoding::R10l:
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
		bool switchRefreshRate = true;
		std::string quality = "high";
		std::string toneMapping = "auto";
		std::string gamutMapping = "auto";
		AutoToggle peakDetection = AutoToggle::AUTO;
		bool hasContrastRecovery = false;
		float contrastRecovery = 0.0f;
		std::string upscaler = "auto";
		std::string downscaler = "auto";
		AutoToggle deband = AutoToggle::AUTO;
		AutoToggle dithering = AutoToggle::AUTO;
		std::string outputPresentation = "auto";
		std::string outputRange = "auto";
		std::string outputGamma = "auto";
		std::string sdrTargetPrimaries = "rec709";
		bool reportBt2020ToDisplay = false;
		std::string sdrInputTransfer = "auto";
		bool outputDiagnostics = false;
		bool diagnosticDisableShaderCache = false;
		double scopeScreenAspect = 2.35;
		bool defaultScopeScreen = false;
		bool scopeSubtitleFit = false;
		uint64_t scopeSubtitleHoldMs = 2000;
		int scopeSubtitlePaddingPixels = 20;
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
				{
					if (variable == "eotf" || variable == "transfer") value = CStringA(ToString(state.eotf)).GetString();
					else if (variable == "colorspace" || variable == "primaries") value = CStringA(ToString(state.colorspace)).GetString();
					else if (variable == "format") value = CStringA(ToString(state.videoFrameEncoding)).GetString();
					else if (variable == "range" || variable == "scan") return false;
					else if (variable == "hdr_metadata") value = state.hdrData && state.hdrData->IsValid() ? "true" : "false";
					else if (variable == "interlaced") value = state.displayMode && state.displayMode->IsInterlaced() ? "true" : "false";
					else if (!state.displayMode) return false;
					else if (variable == "source_rate") value = std::to_string(static_cast<int>(std::floor(state.displayMode->RefreshRateHz())));
					else if (variable == "width") value = std::to_string(state.displayMode->FrameWidth());
					else if (variable == "height") value = std::to_string(state.displayMode->FrameHeight());
					else if (variable == "resolution") value = std::to_string(state.displayMode->FrameWidth()) + "x" + std::to_string(state.displayMode->FrameHeight());
					else return false;
					return true;
				}, selections, modelError))
			{
				DebugLog::Log("unified renderer profile selection failed: %s", modelError.c_str());
				return;
			}
			for (const RendererProfileConfig::AutomaticSelection& selection : selections)
			{
				const auto profile = model.profiles.find(selection.group + "." + selection.profile);
				if (profile == model.profiles.end()) continue;
				const DisplayRule rule = { selection.group + "/" + selection.profile,
					"profiles." + selection.group + "." + selection.profile, profile->second.priority, 0 };
				ApplyDisplayRuleOverrides(config, rule, settings);
				if (!activeProfiles.empty()) activeProfiles += ", ";
				activeProfiles += rule.name;
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

	bool ParseAspectRatio(const std::string& value, double& parsed)
	{
		const std::string trimmed = ConfigFile::Trim(value);
		const size_t separator = trimmed.find(':');
		if (separator == std::string::npos)
			return ParseDouble(trimmed, parsed);

		double width = 0.0;
		double height = 0.0;
		if (!ParseDouble(trimmed.substr(0, separator), width) ||
			!ParseDouble(trimmed.substr(separator + 1), height) ||
			width <= 0.0 || height <= 0.0)
		{
			return false;
		}

		parsed = width / height;
		return std::isfinite(parsed);
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

	constexpr const char* DISPLAY_CONFIG_SECTION = "display";
	constexpr const char* LEGACY_DISPLAY_CONFIG_SECTION = "libplacebo";

	bool TryGetDisplayString(
		const ConfigFile& config,
		const char* key,
		std::string& value)
	{
		if (config.TryGetString(DISPLAY_CONFIG_SECTION, key, value))
			return true;
		return config.TryGetString(LEGACY_DISPLAY_CONFIG_SECTION, key, value);
	}

	bool TryGetDisplayBool(
		const ConfigFile& config,
		const char* key,
		bool& value)
	{
		std::string rawValue;
		if (config.TryGetString(DISPLAY_CONFIG_SECTION, key, rawValue))
			return config.TryGetBool(DISPLAY_CONFIG_SECTION, key, value);
		return config.TryGetBool(LEGACY_DISPLAY_CONFIG_SECTION, key, value);
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
		readChoice("upscaler", settings.upscaler, { "auto", "ewa_lanczossharp", "ewa_lanczos", "bicubic", "bilinear" });
		readChoice("downscaler", settings.downscaler, { "auto", "ewa_lanczos", "bicubic", "bilinear" });
		readToggle("deband", settings.deband);
		readToggle("dithering", settings.dithering);
		readChoice("output_presentation", settings.outputPresentation,
			{ "auto", "composed", "direct" });
		readChoice("output_range", settings.outputRange, { "auto", "full", "limited" });
		readChoice("output_gamma", settings.outputGamma, { "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
		readChoice("sdr_target_primaries", settings.sdrTargetPrimaries, { "rec709", "bt2020" });
		if (!config.TryGetBool(rule.section, "report_bt2020_to_display",
			settings.reportBt2020ToDisplay) &&
			config.TryGetString(rule.section, "report_bt2020_to_display", raw))
		{
			DebugLog::Log("display rule '%s': invalid report_bt2020_to_display value '%s'; retaining base setting", rule.name.c_str(), raw.c_str());
		}
		readChoice("sdr_input_transfer", settings.sdrInputTransfer, { "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
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
	}

	RendererSettings LoadRendererSettings(const VideoState& state, std::string& activeRule,
		const std::string& manualRule = "")
	{
		RendererSettings settings;
		activeRule.clear();
		ConfigFile config;
		if (!config.Load(ConfigFile::RENDERER_FILENAME))
		{
			bool persistedScopeScreen = false;
			if (TryLoadPersistedScreenProfile(persistedScopeScreen))
				settings.defaultScopeScreen = persistedScopeScreen;
			return settings;
		}
		DebugLog::Log(
			"libplacebo configuration loaded from %s",
			config.GetLoadedPath().c_str());
		for (const std::string& warning : config.GetWarnings())
			DebugLog::Log(
				"libplacebo configuration warning: %s",
				warning.c_str());

		std::string rawValue;
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
		settings.upscaler = ReadChoice(
			config, "upscaler", "auto",
			{ "auto", "ewa_lanczossharp", "ewa_lanczos", "bicubic", "bilinear" });
		settings.downscaler = ReadChoice(
			config, "downscaler", "auto",
			{ "auto", "ewa_lanczos", "bicubic", "bilinear" });
		settings.deband = ReadAutoToggle(config, "deband");
		settings.dithering = ReadAutoToggle(config, "dithering");
		settings.outputPresentation = ReadChoice(
			config, "output_presentation", "auto",
			{ "auto", "composed", "direct" });
		settings.outputRange = ReadChoice(
			config, "output_range", "auto", { "auto", "full", "limited" });
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

		if (TryGetDisplayString(config, "scope_screen_aspect", rawValue))
		{
			double parsed = 0.0;
			if (ParseAspectRatio(rawValue, parsed) && parsed >= 1.5 && parsed <= 4.0)
				settings.scopeScreenAspect = parsed;
			else
				DebugLog::Log(
					"libplacebo: scope_screen_aspect must be a ratio or decimal between 1.5 and 4.0; using 2.35:1");
		}

		settings.defaultScopeScreen = ReadChoice(
			config, "default_screen_profile", "normal", { "normal", "scope" }) == "scope";
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
		if (TryGetDisplayString(config, "scope_subtitle_fit", rawValue) &&
			!TryGetDisplayBool(config, "scope_subtitle_fit", settings.scopeSubtitleFit))
		{
			DebugLog::Log(
				"libplacebo: invalid scope_subtitle_fit value '%s'; using false",
				rawValue.c_str());
			settings.scopeSubtitleFit = false;
		}
		if (TryGetDisplayString(config, "scope_subtitle_hold_seconds", rawValue))
		{
			double seconds = 0.0;
			if (ParseDouble(rawValue, seconds) && seconds >= 0.0 && seconds <= 30.0)
				settings.scopeSubtitleHoldMs = static_cast<uint64_t>(std::llround(seconds * 1000.0));
			else
				DebugLog::Log("libplacebo: scope_subtitle_hold_seconds must be between 0 and 30; using 2.0");
		}
		if (TryGetDisplayString(config, "scope_subtitle_padding_pixels", rawValue))
		{
			double pixels = 0.0;
			if (ParseDouble(rawValue, pixels) && pixels >= 0.0 && pixels <= 500.0)
				settings.scopeSubtitlePaddingPixels = static_cast<int>(std::llround(pixels));
			else
				DebugLog::Log("libplacebo: scope_subtitle_padding_pixels must be between 0 and 500; using 20");
		}
		bool persistedScopeScreen = false;
		if (TryLoadPersistedScreenProfile(persistedScopeScreen))
			settings.defaultScopeScreen = persistedScopeScreen;

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
		ApplyAutomaticProfiles(config, state, settings, activeProfiles);
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

		// General owns cross-profile renderer behavior.  The legacy [display]
		// locations remain valid, while these values become the canonical home for
		// new profile configurations.
		if (config.TryGetString("general", "switch_refresh_rate", rawValue) &&
			!config.TryGetBool("general", "switch_refresh_rate", settings.switchRefreshRate))
		{
			DebugLog::Log("libplacebo: invalid [general] switch_refresh_rate '%s'; retaining display setting", rawValue.c_str());
		}
		if (config.TryGetString("general", "output_diagnostics", rawValue) &&
			!config.TryGetBool("general", "output_diagnostics", settings.outputDiagnostics))
		{
			DebugLog::Log("libplacebo: invalid [general] output_diagnostics '%s'; retaining display setting", rawValue.c_str());
		}
		if (config.TryGetString("general", "diagnostic_disable_shader_cache", rawValue) &&
			!config.TryGetBool("general", "diagnostic_disable_shader_cache", settings.diagnosticDisableShaderCache))
		{
			DebugLog::Log("libplacebo: invalid [general] diagnostic_disable_shader_cache '%s'; retaining display setting", rawValue.c_str());
		}
		else if (!activeProfiles.empty())
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
		return std::abs(RefreshRateHz(first) - RefreshRateHz(second)) < 0.002;
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
		~ScopedDisplayRefreshRate()
		{
			Restore();
		}

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

		void Restore()
		{
			if (!m_changed)
				return;

			std::vector<DISPLAYCONFIG_PATH_INFO> paths;
			std::vector<DISPLAYCONFIG_MODE_INFO> modes;
			UINT32 pathCount = 0;
			UINT32 modeCount = 0;
			size_t pathIndex = 0;
			if (!QueryDisplayPath(
				m_displayDeviceName, paths, modes, pathCount, modeCount, pathIndex))
			{
				DebugLog::Log("libplacebo refresh-rate restore failed: active display path not found");
				return;
			}

			const LONG restoreResult = ApplyDisplayRefreshRate(
				paths,
				modes,
				pathCount,
				modeCount,
				pathIndex,
				m_originalRefreshRate);
			if (restoreResult == ERROR_SUCCESS)
			{
				DebugLog::Log(
					"libplacebo refresh-rate restore applied: %.6f Hz",
					RefreshRateHz(m_originalRefreshRate));
				RunRefreshRateCommand(
					m_refreshCommandSettings,
					RefreshRateHz(m_originalRefreshRate));
			}
			else
			{
				DebugLog::Log(
					"libplacebo refresh-rate restore failed: %.6f Hz error=%ld",
					RefreshRateHz(m_originalRefreshRate),
					restoreResult);
			}
			m_changed = false;
		}

		std::wstring m_displayDeviceName;
		DISPLAYCONFIG_RATIONAL m_originalRefreshRate{};
		bool m_changed = false;
		RendererSettings m_refreshCommandSettings;
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
	pl_renderer renderer = nullptr;
	pl_tex textures[2] = { nullptr, nullptr };
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
	struct pl_render_params renderParams{};
	struct pl_color_map_params colorMapParams{};
	struct pl_peak_detect_params peakDetectParams{};
	struct pl_deband_params debandParams{};
	struct pl_dither_params ditherParams{};
	double sdrTargetNits = PL_COLOR_SDR_WHITE;
	double sdrBlackNits = PL_COLOR_SDR_WHITE / PL_COLOR_SDR_CONTRAST;
	struct pl_color_space configuredOutputColor{};
	LibplaceboOutput::Plan outputPlan;
	LibplaceboOutput::Actual actualOutput;
	bool targetBt2020 = false;
	bool reportBt2020ToDisplay = false;
	std::wstring negotiatedDisplayDeviceName;
	NvidiaBt2020Reporter nvidiaBt2020Reporter;
	bool swapchainBlit = true;
	bool suppressLimitedNegotiation = false;
	uint64_t nextOutputRecoveryTick = 0;
	enum pl_color_transfer sdrInputTransfer = PL_COLOR_TRC_UNKNOWN;
	double scopeScreenAspect = 2.35;
	bool defaultScopeScreen = false;
	bool scopeSubtitleFit = false;
	uint64_t scopeSubtitleHoldMs = 2000;
	int scopeSubtitlePaddingPixels = 20;
	uint64_t scopeSubtitleAnalysisFrame = 0;
	int scopeSubtitlePendingPictureTop = 0;
	int scopeSubtitlePendingPictureBottom = 0;
	int scopeSubtitlePictureTop = 0;
	int scopeSubtitlePictureBottom = 0;
	int scopeSubtitleBarHits = 0;
	uint64_t scopeActivePictureLastConfirmedTick = 0;
	int scopeSubtitleDetectedBottom = 0;
	int scopeSubtitleDetectedTop = 0;
	uint64_t scopeSubtitleLastDetectionTick = 0;
	uint64_t scopeSubtitleTopLastDetectionTick = 0;
	int scopeSubtitleOppositeCandidateSide = 0;
	int scopeSubtitleOppositeCandidateHits = 0;
	float scopeSubtitleShiftSourcePixels = 0.0f;
	bool scopeSubtitleWasActive = false;
	bool scopeSubtitleWasTopActive = false;
	std::string activeDisplayRule;
	HWND videoHwnd = nullptr;
	HMONITOR negotiatedMonitor = nullptr;
	bool cursorPositioned = false;
	bool hasPresentedFrame = false;
	uint64_t nextPresentationTelemetryLogTick = 0;
	bool screenProfilesPrewarmed = false;
	uint64_t lastSubmittedScreenProfileRequest = 0;
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

	~Impl()
	{
		nvidiaBt2020Reporter.Restore();
		pl_renderer_destroy(&renderer);
		pl_lut_free(&displayLut);
		if (d3d11)
		{
			for (pl_tex& texture : textures)
				pl_tex_destroy(d3d11->gpu, &texture);
		}
		pl_swapchain_destroy(&swapchain);
		SaveShaderCache();
		pl_d3d11_destroy(&d3d11);
		pl_cache_destroy(&cache);
		pl_log_destroy(&log);
	}

	void LoadShaderCache()
	{
		shaderCachePath = ShaderCachePath();
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

	void LogOutputReadback()
	{
		using namespace LibplaceboOutput;
		diagnosticReadbackComplete = true;
		CComPtr<IDXGISwapChain> nativeSwapchain;
		nativeSwapchain.Attach(pl_d3d11_swapchain_unwrap(swapchain));
		if (!nativeSwapchain)
		{
			DebugLog::Log(
				"libplacebo output diagnostic readback failed: cannot unwrap swapchain");
			return;
		}

		CComPtr<ID3D11Texture2D> backBuffer;
		HRESULT result = nativeSwapchain->GetBuffer(
			0,
			__uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(&backBuffer));
		if (FAILED(result) || !backBuffer)
		{
			DebugLog::Log(
				"libplacebo output diagnostic readback failed: GetBuffer=0x%08lX",
				static_cast<unsigned long>(result));
			return;
		}

		D3D11_TEXTURE2D_DESC desc{};
		backBuffer->GetDesc(&desc);
		if (desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM)
		{
			DebugLog::Log(
				"libplacebo output diagnostic readback skipped: format=%u expected=%u",
				static_cast<unsigned int>(desc.Format),
				static_cast<unsigned int>(DXGI_FORMAT_R10G10B10A2_UNORM));
			return;
		}

		D3D11_TEXTURE2D_DESC stagingDesc = desc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;
		CComPtr<ID3D11Texture2D> staging;
		result = d3d11->device->CreateTexture2D(&stagingDesc, nullptr, &staging);
		if (FAILED(result) || !staging)
		{
			DebugLog::Log(
				"libplacebo output diagnostic readback failed: CreateTexture2D=0x%08lX",
				static_cast<unsigned long>(result));
			return;
		}

		CComPtr<ID3D11DeviceContext> context;
		d3d11->device->GetImmediateContext(&context);
		context->CopyResource(staging, backBuffer);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		result = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(result))
		{
			DebugLog::Log(
				"libplacebo output diagnostic readback failed: Map=0x%08lX",
				static_cast<unsigned long>(result));
			return;
		}

		const PackedR10Stats stats = AnalyzePackedR10(
			static_cast<const uint32_t*>(mapped.pData),
			mapped.RowPitch / sizeof(uint32_t),
			desc.Width,
			desc.Height,
			8);
		context->Unmap(staging, 0);
		const double divisor =
			stats.sampledPixels > 0 ? static_cast<double>(stats.sampledPixels) : 1.0;
		DebugLog::Log(
			"libplacebo output diagnostic readback: format=R10G10B10A2 size=%ux%u sampled=%llu step=8 encoding=%s transfer=%s min_rgb=%u/%u/%u max_rgb=%u/%u/%u mean_rgb=%.2f/%.2f/%.2f channels_below_64=%llu channels_above_940=%llu cache=%s objects=%d signature=%016llX",
			desc.Width,
			desc.Height,
			static_cast<unsigned long long>(stats.sampledPixels),
			ToString(actualOutput.encoding),
			actualOutput.targetTransfer == TargetTransfer::GAMMA24
				? "GAMMA24" : "SWAPCHAIN",
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
			shaderCacheEnabled ? "enabled" : "disabled",
			cache ? pl_cache_objects(cache) : 0,
			static_cast<unsigned long long>(cache ? pl_cache_signature(cache) : 0));
	}

	void ConfigureRenderParams(const RendererSettings& settings)
	{
		const char* presetExport = "pl_render_high_quality_params";
		if (settings.quality == "fast")
			presetExport = "pl_render_fast_params";
		else if (settings.quality == "balanced")
			presetExport = "pl_render_default_params";
		renderParams = LibplaceboExportedData<pl_render_params>(presetExport);
		// libplacebo 7.360's D3D11 compute implementation can generate
		// colliding texture registers when error diffusion and a target 3D LUT
		// are combined. Keep normal rendering and the target LUT intact, but
		// drop this optional high-quality preset feature for a valid display LUT.
		if (displayLutParsed && displayLut && renderParams.error_diffusion)
		{
			renderParams.error_diffusion = nullptr;
			DebugLog::Log(
				"display: disabled error-diffusion dithering while a 3D LUT is active; using the compatible target-LUT render path");
		}
		// Display calibration is attached only to the final target frame.
		// Keeping the render-parameter/image LUT empty prevents a second,
		// pre-target application in a different color domain.
		renderParams.lut = nullptr;
		renderParams.lut_type = PL_LUT_UNKNOWN;

		if (renderParams.color_map_params)
			colorMapParams = *renderParams.color_map_params;
		else
			colorMapParams =
				LibplaceboExportedData<pl_color_map_params>("pl_color_map_default_params");

		if (settings.toneMapping != "auto")
		{
			const char* toneMapExport = "pl_tone_map_spline";
			if (settings.toneMapping == "bt2390")
				toneMapExport = "pl_tone_map_bt2390";
			else if (settings.toneMapping == "st2094-40")
				toneMapExport = "pl_tone_map_st2094_40";
			else if (settings.toneMapping == "reinhard")
				toneMapExport = "pl_tone_map_reinhard";
			colorMapParams.tone_mapping_function =
				&LibplaceboExportedData<pl_tone_map_function>(toneMapExport);
		}

		if (settings.gamutMapping != "auto")
		{
			const char* gamutMapExport = "pl_gamut_map_perceptual";
			if (settings.gamutMapping == "softclip")
				gamutMapExport = "pl_gamut_map_softclip";
			else if (settings.gamutMapping == "relative")
				gamutMapExport = "pl_gamut_map_relative";
			else if (settings.gamutMapping == "desaturate")
				gamutMapExport = "pl_gamut_map_desaturate";
			colorMapParams.gamut_mapping =
				&LibplaceboExportedData<pl_gamut_map_function>(gamutMapExport);
		}

		if (settings.hasContrastRecovery)
			colorMapParams.contrast_recovery = settings.contrastRecovery;
		renderParams.color_map_params = &colorMapParams;

		if (settings.peakDetection == AutoToggle::OFF)
		{
			renderParams.peak_detect_params = nullptr;
		}
		else if (settings.peakDetection == AutoToggle::ON)
		{
			peakDetectParams = LibplaceboExportedData<pl_peak_detect_params>(
				"pl_peak_detect_high_quality_params");
			renderParams.peak_detect_params = &peakDetectParams;
		}
		else if (renderParams.peak_detect_params)
		{
			peakDetectParams = *renderParams.peak_detect_params;
			renderParams.peak_detect_params = &peakDetectParams;
		}

		if (settings.deband == AutoToggle::OFF)
		{
			renderParams.deband_params = nullptr;
		}
		else if (settings.deband == AutoToggle::ON)
		{
			debandParams = LibplaceboExportedData<pl_deband_params>(
				"pl_deband_default_params");
			renderParams.deband_params = &debandParams;
		}
		else if (renderParams.deband_params)
		{
			debandParams = *renderParams.deband_params;
			renderParams.deband_params = &debandParams;
		}

		if (settings.dithering == AutoToggle::OFF)
		{
			renderParams.dither_params = nullptr;
		}
		else if (settings.dithering == AutoToggle::ON)
		{
			ditherParams = LibplaceboExportedData<pl_dither_params>(
				"pl_dither_default_params");
			renderParams.dither_params = &ditherParams;
		}
		else if (renderParams.dither_params)
		{
			ditherParams = *renderParams.dither_params;
			renderParams.dither_params = &ditherParams;
		}

		if (settings.upscaler != "auto")
		{
			const char* filterExport = "pl_filter_ewa_lanczossharp";
			if (settings.upscaler == "ewa_lanczos")
				filterExport = "pl_filter_ewa_lanczos";
			else if (settings.upscaler == "bicubic")
				filterExport = "pl_filter_bicubic";
			else if (settings.upscaler == "bilinear")
				filterExport = "pl_filter_bilinear";
			renderParams.upscaler =
				&LibplaceboExportedData<pl_filter_config>(filterExport);
		}

		if (settings.downscaler != "auto")
		{
			const char* filterExport = "pl_filter_ewa_lanczos";
			if (settings.downscaler == "bicubic")
				filterExport = "pl_filter_bicubic";
			else if (settings.downscaler == "bilinear")
				filterExport = "pl_filter_bilinear";
			renderParams.downscaler =
				&LibplaceboExportedData<pl_filter_config>(filterExport);
		}

		DebugLog::Log(
			"libplacebo settings: quality=%s tone_mapping=%s gamut_mapping=%s peak_detection=%s contrast_recovery=%.2f upscaler=%s downscaler=%s deband=%s dithering=%s output_presentation=%s output_range=%s output_gamma=%s sdr_input_transfer=%s target=%.1f nits black=%.3f nits output_diagnostics=%d diagnostic_disable_shader_cache=%d refresh_switch=%d refresh_command_delay=%llus refresh_commands=%u scope_aspect=%.4f default_screen_profile=%s scope_subtitle_fit=%d subtitle_hold=%llums subtitle_padding=%dpx",
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
			renderParams.deband_params ? "on" : "off",
			renderParams.dither_params ? "on" : "off",
			settings.outputPresentation.c_str(),
			settings.outputRange.c_str(),
			settings.outputGamma.c_str(),
			settings.sdrInputTransfer.c_str(),
			sdrTargetNits,
			sdrBlackNits,
			settings.outputDiagnostics ? 1 : 0,
			settings.diagnosticDisableShaderCache ? 1 : 0,
			settings.switchRefreshRate ? 1 : 0,
			static_cast<unsigned long long>(settings.refreshRateCommandDelayMs / 1000),
			static_cast<unsigned int>(settings.refreshRateCommandRules.size()),
			scopeScreenAspect,
			defaultScopeScreen ? "scope" : "normal",
			scopeSubtitleFit ? 1 : 0,
			static_cast<unsigned long long>(scopeSubtitleHoldMs),
			scopeSubtitlePaddingPixels);
	}

	float UpdateScopeSubtitleShift(
		const uint16_t* luma,
		int width,
		int height,
		bool scopeScreenActive)
	{
		const uint64_t now = GetTickCount64();
		if (!scopeScreenActive || !luma ||
			width < 320 || height < 180)
		{
			scopeSubtitleShiftSourcePixels = 0.0f;
			scopeSubtitleWasActive = false;
			scopeSubtitleWasTopActive = false;
			scopeSubtitlePendingPictureTop = 0;
			scopeSubtitlePendingPictureBottom = 0;
			scopeSubtitlePictureTop = 0;
			scopeSubtitlePictureBottom = 0;
			scopeSubtitleBarHits = 0;
			scopeActivePictureLastConfirmedTick = 0;
			scopeSubtitleDetectedBottom = 0;
			scopeSubtitleDetectedTop = 0;
			scopeSubtitleLastDetectionTick = 0;
			scopeSubtitleTopLastDetectionTick = 0;
			scopeSubtitleOppositeCandidateSide = 0;
			scopeSubtitleOppositeCandidateHits = 0;
			return 0.0f;
		}

		// This inexpensive geometry pass deliberately avoids OCR and models. It
		// finds stable letterbox edges, then treats meaningful non-black content
		// inside either encoded bar as content which must remain visible.
		// Sampling every third rendered frame keeps the 4K CPU cost negligible
		// while still reacting in roughly 50-125 ms.
		if (++scopeSubtitleAnalysisFrame % 3 == 0)
		{
			std::vector<uint16_t> blackSamples;
			const int sampleStep = std::max(2, width / 256);
			const int edgeWidth = std::max(16, width / 5);
			const int edgeRows = std::max(3, std::min(10, height / 60));
			blackSamples.reserve(static_cast<size_t>(edgeRows) *
				(edgeWidth / sampleStep + 1) * 2);
			for (int y = height - edgeRows; y < height; ++y)
			{
				const uint16_t* row =
					luma + static_cast<size_t>(y) * width;
				for (int x = 0; x < edgeWidth; x += sampleStep)
					blackSamples.push_back(static_cast<uint16_t>(row[x] >> 6));
				for (int x = width - edgeWidth; x < width; x += sampleStep)
					blackSamples.push_back(static_cast<uint16_t>(row[x] >> 6));
			}

			const size_t median = blackSamples.size() / 2;
			std::nth_element(
				blackSamples.begin(),
				blackSamples.begin() + median,
				blackSamples.end());
			const int blackCode = blackSamples[median];
			const int darkLimit = std::min(1023, blackCode + 36);

			auto edgeRowIsBlack =
				[=](int y)
				{
					const uint16_t* row =
						luma + static_cast<size_t>(y) * width;
					int dark = 0;
					int count = 0;
					uint64_t sum = 0;
					for (int x = 0; x < edgeWidth; x += sampleStep)
					{
						const int code = row[x] >> 6;
						dark += code <= darkLimit;
						sum += code;
						++count;
					}
					for (int x = width - edgeWidth; x < width; x += sampleStep)
					{
						const int code = row[x] >> 6;
						dark += code <= darkLimit;
						sum += code;
						++count;
					}
					return count > 0 && dark * 100 >= count * 84 &&
						sum <= static_cast<uint64_t>(count) *
							static_cast<uint64_t>(blackCode + 28);
				};

			const int rowStep = std::max(1, height / 1080);
			int topRows = 0;
			while (topRows + rowStep < height / 3 &&
				edgeRowIsBlack(topRows))
			{
				topRows += rowStep;
			}
			int bottomRows = 0;
			while (bottomRows + rowStep < height / 3 &&
				edgeRowIsBlack(height - 1 - bottomRows))
			{
				bottomRows += rowStep;
			}

			const int minimumBar = std::max(8, height / 35);
			const int candidateTop =
				topRows >= minimumBar ? topRows : 0;
			const int candidateBottom =
				bottomRows >= minimumBar ? height - bottomRows : 0;
			const int tolerance = std::max(3, height / 240);
			const int barSymmetryTolerance =
				std::max(tolerance * 2, height / 60);
			const int candidateActiveHeight =
				std::max(1, candidateBottom - candidateTop);
			const double candidateAspect =
				static_cast<double>(width) / candidateActiveHeight;
			const bool plausibleLetterbox =
				candidateTop > 0 &&
				candidateBottom > candidateTop &&
				std::abs(candidateTop - (height - candidateBottom)) <=
					barSymmetryTolerance &&
				// Reject implausibly deep crops caused by a uniformly dark scene.
				// This still covers common scope formats through 2.76:1.
				candidateAspect >= 1.90 &&
				candidateAspect <= 3.00;
			const bool haveStablePicture =
				scopeSubtitlePictureTop > 0 &&
				scopeSubtitlePictureBottom > scopeSubtitlePictureTop;
			const bool topConfirmsStable =
				haveStablePicture && candidateTop > 0 &&
				candidateTop >= scopeSubtitlePictureTop - tolerance;
			const bool bottomConfirmsStable =
				haveStablePicture && candidateBottom > 0 &&
				candidateBottom <= scopeSubtitlePictureBottom + tolerance;
			const bool stablePictureConfirmed =
				topConfirmsStable || bottomConfirmsStable;
			const bool candidateMatchesStable =
				haveStablePicture &&
				std::abs(candidateTop - scopeSubtitlePictureTop) <= tolerance &&
				std::abs(candidateBottom -
					scopeSubtitlePictureBottom) <= tolerance;
			const bool candidateCanReplaceStable =
				plausibleLetterbox &&
				(!haveStablePicture || !candidateMatchesStable);

			// A temporary receiver/streaming OSD may cover one black bar. Keep a
			// locked aspect when the unobscured opposite edge still confirms it.
			// A dark picture may also make a detected bar appear deeper; that
			// supports the existing crop but does not immediately enlarge it.
			if (stablePictureConfirmed)
				scopeActivePictureLastConfirmedTick = now;

			if (candidateCanReplaceStable &&
				scopeSubtitlePendingPictureTop > 0 &&
				scopeSubtitlePendingPictureBottom > 0 &&
				std::abs(candidateTop -
					scopeSubtitlePendingPictureTop) <= tolerance &&
				std::abs(candidateBottom -
					scopeSubtitlePendingPictureBottom) <= tolerance)
			{
				scopeSubtitleBarHits = std::min(8, scopeSubtitleBarHits + 1);
			}
			else if (candidateCanReplaceStable)
			{
				scopeSubtitleBarHits = 1;
				scopeSubtitlePendingPictureTop = candidateTop;
				scopeSubtitlePendingPictureBottom = candidateBottom;
			}
			else
			{
				scopeSubtitleBarHits = 0;
				scopeSubtitlePendingPictureTop = 0;
				scopeSubtitlePendingPictureBottom = 0;
			}

			// Require several matching analyses before adopting a new aspect.
			// This filters short overlays and dark-scene excursions while staying
			// responsive to a real source aspect change.
			if (scopeSubtitleBarHits >= 8)
			{
				scopeSubtitlePictureTop = scopeSubtitlePendingPictureTop;
				scopeSubtitlePictureBottom = scopeSubtitlePendingPictureBottom;
				scopeActivePictureLastConfirmedTick = now;
				scopeSubtitleBarHits = 0;
				// A refined letterbox estimate must not cancel an active caption hold.
				// Captions themselves can obscure a bar edge, and clearing the hold here
				// made the picture snap back and forth during otherwise stable text.
				const double activeAspect =
					static_cast<double>(width) /
					std::max(1,
						scopeSubtitlePictureBottom -
						scopeSubtitlePictureTop);
				DebugLog::Log(
					"libplacebo scope active picture: stable aspect %.4f bounds 0,%d-%d,%d raster %dx%d",
					activeAspect,
					scopeSubtitlePictureTop,
					width,
					scopeSubtitlePictureBottom,
					width,
					height);
			}

			// A genuine switch back to full-raster content removes both bar
			// confirmations. Hold briefly across fades and overlays, then release
			// in one step instead of oscillating on dark frames.
			if (scopeSubtitlePictureTop > 0 &&
				!stablePictureConfirmed &&
				scopeActivePictureLastConfirmedTick != 0 &&
				now - scopeActivePictureLastConfirmedTick >= 1500)
			{
				DebugLog::Log(
					"libplacebo scope active picture: letterbox released; using full raster");
				scopeSubtitlePictureTop = 0;
				scopeSubtitlePictureBottom = 0;
				scopeActivePictureLastConfirmedTick = 0;
				scopeSubtitleLastDetectionTick = 0;
				scopeSubtitleTopLastDetectionTick = 0;
				scopeSubtitleOppositeCandidateSide = 0;
				scopeSubtitleOppositeCandidateHits = 0;
				scopeSubtitleShiftSourcePixels = 0.0f;
			}

			// Subtitle fitting deliberately uses a simple, renderer-local rule:
			// meaningful non-black content inside a confirmed encoded black bar
			// must remain visible. It does not try to recognize glyphs, language,
			// alignment, or subtitle style.
			if (scopeSubtitleFit &&
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
						int& detectedTop, int& detectedBottom)
					{
						detectedTop = height;
						detectedBottom = 0;
						int contentRows = 0;
						for (int y = searchTop; y < searchBottom; y += rowStep)
						{
							const uint16_t* row =
								luma + static_cast<size_t>(y) * width;
							int contentSamples = 0;
							int left = width;
							int right = 0;
							for (int x = x0; x < x1; x += xStep)
							{
								if ((row[x] >> 6) <= contentLimit)
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
				const bool upperContent =
					scopeSubtitlePictureTop > barInset * 2 &&
					findBarContent(
						barInset,
						scopeSubtitlePictureTop - barInset,
						upperContentTop,
						upperContentBottom);
				const bool lowerContent =
					scopeSubtitlePictureBottom + barInset * 2 < height &&
					findBarContent(
						scopeSubtitlePictureBottom + barInset,
						height - barInset,
						lowerContentTop,
						lowerContentBottom);

				const float visibleHeight = std::min(
					static_cast<float>(height),
					static_cast<float>(width / scopeScreenAspect));
				const float visibleTop =
					(static_cast<float>(height) - visibleHeight) * 0.5f;
				const float visibleBottom =
					(static_cast<float>(height) + visibleHeight) * 0.5f;
				const float margin = std::max(8.0f, height / 90.0f) +
					static_cast<float>(scopeSubtitlePaddingPixels);
				const float upperRequiredShift = upperContent
					? std::max(0.0f, visibleTop + margin - upperContentTop)
					: 0.0f;
				const float lowerRequiredShift = lowerContent
					? std::max(0.0f,
						lowerContentBottom + margin - visibleBottom)
					: 0.0f;

				// If both bars contain something, prefer the side requiring the larger
				// displacement. Keep a current placement for its complete hold interval;
				// an apparent caption on the opposite edge must be confirmed by a second
				// analysis before it may reverse direction. This rejects transient OSDs
				// and noisy letterbox-edge measurements without delaying a real subtitle
				// by more than two analysis intervals.
				int detectedSide = 0; // -1 top, +1 bottom
				if (upperRequiredShift > lowerRequiredShift &&
					upperRequiredShift > 0.5f)
					detectedSide = -1;
				else if (lowerRequiredShift > 0.5f)
					detectedSide = 1;

				const bool lowerHoldActive =
					scopeSubtitleLastDetectionTick != 0 &&
					now - scopeSubtitleLastDetectionTick <= scopeSubtitleHoldMs;
				const bool topHoldActive =
					scopeSubtitleTopLastDetectionTick != 0 &&
					now - scopeSubtitleTopLastDetectionTick <= scopeSubtitleHoldMs;
				const int heldSide = topHoldActive ? -1 :
					(lowerHoldActive ? 1 : 0);

				if (detectedSide != 0)
				{
					const bool oppositeActive = heldSide != 0 &&
						detectedSide != heldSide;
					if (oppositeActive)
					{
						if (scopeSubtitleOppositeCandidateSide == detectedSide)
							++scopeSubtitleOppositeCandidateHits;
						else
						{
							scopeSubtitleOppositeCandidateSide = detectedSide;
							scopeSubtitleOppositeCandidateHits = 1;
						}
						if (scopeSubtitleOppositeCandidateHits < 2)
							detectedSide = 0;
					}
					else
					{
						scopeSubtitleOppositeCandidateSide = 0;
						scopeSubtitleOppositeCandidateHits = 0;
					}
				}
				else
				{
					scopeSubtitleOppositeCandidateSide = 0;
					scopeSubtitleOppositeCandidateHits = 0;
				}

				if (detectedSide == -1)
				{
					scopeSubtitleDetectedTop = upperContentTop;
					scopeSubtitleTopLastDetectionTick = now;
					scopeSubtitleLastDetectionTick = 0;
					scopeSubtitleOppositeCandidateSide = 0;
					scopeSubtitleOppositeCandidateHits = 0;
				}
				else if (detectedSide == 1)
				{
					scopeSubtitleDetectedBottom = lowerContentBottom;
					scopeSubtitleLastDetectionTick = now;
					scopeSubtitleTopLastDetectionTick = 0;
					scopeSubtitleOppositeCandidateSide = 0;
					scopeSubtitleOppositeCandidateHits = 0;
				}
			}
		}

		if (!scopeSubtitleFit)
		{
			scopeSubtitleShiftSourcePixels = 0.0f;
			scopeSubtitleWasActive = false;
			scopeSubtitleWasTopActive = false;
			return 0.0f;
		}

		const bool lowerSubtitleActive =
			scopeSubtitleLastDetectionTick != 0 &&
			now - scopeSubtitleLastDetectionTick <= scopeSubtitleHoldMs;
		// Hold either edge for the configured duration. A confirmed detection on
		// the opposite edge clears the previous timer and changes direction.
		const bool topSubtitleActive =
			scopeSubtitleTopLastDetectionTick != 0 &&
			now - scopeSubtitleTopLastDetectionTick <= scopeSubtitleHoldMs;
		const bool subtitleActive = topSubtitleActive || lowerSubtitleActive;
		float requestedShift = 0.0f;
		if (topSubtitleActive && scopeSubtitlePictureTop > 0)
		{
			const float visibleHeight = std::min(
				static_cast<float>(height),
				static_cast<float>(width / scopeScreenAspect));
			const float visibleTop =
				(static_cast<float>(height) - visibleHeight) * 0.5f;
			const float margin = std::max(8.0f, height / 90.0f) +
				static_cast<float>(scopeSubtitlePaddingPixels);
			// Positive shifts move the picture up. Top-bar content needs the
			// inverse: shift down until its first detected row is visible.
			requestedShift = -std::max(
				0.0f, visibleTop + margin - scopeSubtitleDetectedTop);
		}
		else if (lowerSubtitleActive && scopeSubtitlePictureTop > 0)
		{
			const float visibleHeight = std::min(
				static_cast<float>(height),
				static_cast<float>(width / scopeScreenAspect));
			const float visibleBottom =
				(static_cast<float>(height) + visibleHeight) * 0.5f;
			const float margin = std::max(8.0f, height / 90.0f) +
				static_cast<float>(scopeSubtitlePaddingPixels);
			requestedShift = std::max(
				0.0f,
				scopeSubtitleDetectedBottom + margin - visibleBottom);
		}

		// Snap to a new placement and keep it fixed for the hold interval. It may
		// move farther only when a newly detected extent differs materially;
		// small detection changes are ignored so the picture does not stutter.
		// A confirmed caption at the opposite edge changes direction immediately.
		const float placementSnapThreshold =
			static_cast<float>(std::max(4, height / 180));
		if (topSubtitleActive)
		{
			if (scopeSubtitleShiftSourcePixels >= 0.0f ||
				requestedShift <
					scopeSubtitleShiftSourcePixels - placementSnapThreshold)
			{
				scopeSubtitleShiftSourcePixels = requestedShift;
			}
		}
		else if (lowerSubtitleActive)
		{
			if (scopeSubtitleShiftSourcePixels <= 0.0f ||
				requestedShift >
					scopeSubtitleShiftSourcePixels + placementSnapThreshold)
			{
				scopeSubtitleShiftSourcePixels = requestedShift;
			}
		}
		else
		{
			// When the configured hold expires, snap back to the normal crop.
			// Slow easing made fixed subtitles appear to stutter or drift.
			scopeSubtitleShiftSourcePixels = 0.0f;
		}
		// Subtitle placement is signed: positive moves the picture up for a
		// lower caption, negative moves it down for an upper caption.  Clamp
		// only the magnitude near zero or valid upper-caption shifts would be
		// discarded entirely.
		if (std::abs(scopeSubtitleShiftSourcePixels) < 0.5f)
			scopeSubtitleShiftSourcePixels = 0.0f;

		if (subtitleActive != scopeSubtitleWasActive)
		{
			DebugLog::Log(
				"libplacebo scope subtitle fit: %s picture=%d..%d subtitle_bottom=%d requested_shift=%.1f px",
				subtitleActive ? "engaged" : "released",
				scopeSubtitlePictureTop,
				scopeSubtitlePictureBottom,
				scopeSubtitleDetectedBottom,
				requestedShift);
			scopeSubtitleWasActive = subtitleActive;
		}
		if (topSubtitleActive != scopeSubtitleWasTopActive)
		{
			DebugLog::Log("libplacebo scope subtitle fit: upper-edge placement %s; shift=%.1f px",
				topSubtitleActive ? "active" : "released",
				scopeSubtitleShiftSourcePixels);
			scopeSubtitleWasTopActive = topSubtitleActive;
		}
		return scopeSubtitleShiftSourcePixels;
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
		// Studio G22 is intentionally not selected because libplacebo 7.360.1
		// has no exact target transfer for that DXGI declaration.
		return PL_COLOR_TRC_UNKNOWN;
	}

	void SetSwapchainColorHint(LibplaceboOutput::DxgiEncoding encoding)
	{
		configuredOutputColor =
			LibplaceboExportedData<pl_color_space>("pl_color_space_bt709");
		configuredOutputColor.primaries = EncodingUsesBt2020(encoding)
			? PL_COLOR_PRIM_BT_2020 : PL_COLOR_PRIM_BT_709;
		const enum pl_color_transfer transfer = EncodingTransfer(encoding);
		configuredOutputColor.transfer =
			transfer == PL_COLOR_TRC_UNKNOWN ? PL_COLOR_TRC_SRGB : transfer;
		configuredOutputColor.hdr.min_luma = static_cast<float>(sdrBlackNits);
		configuredOutputColor.hdr.max_luma = static_cast<float>(sdrTargetNits);
		if (swapchain)
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
				EncodingTransfer(actualOutput.encoding),
				EncodingLevels(actualOutput.encoding));
	}

	void ConfigureSwapchainOutput(const char* trigger)
	{
		using namespace LibplaceboOutput;

		const bool previousStateMayBeStudio =
			actualOutput.encoding != DxgiEncoding::FULL_G22_P709 ||
			!actualOutput.safeToRender;
		negotiatedMonitor = MonitorFromWindow(
			videoHwnd,
			MONITOR_DEFAULTTONEAREST);
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
		IDXGISwapChain* nativeSwapchain = pl_d3d11_swapchain_unwrap(swapchain);
		if (!nativeSwapchain)
		{
			evidence.fullRestoreRequired = previousStateMayBeStudio;
			actualOutput = Finalize(outputPlan, evidence);
			SetSwapchainColorHint(actualOutput.encoding);
			DebugLog::Log(
				"libplacebo output negotiation (%s): requested=%s/%s/%s/%s actual=UNKNOWN/FULL/sRGB/Rec.709 reason=cannot unwrap DXGI swapchain",
				trigger,
				ToString(outputPlan.request.presentation),
				ToString(outputPlan.request.range),
				ToString(outputPlan.request.gamma),
				ToString(outputPlan.request.primaries));
			return;
		}

		DXGI_SWAP_CHAIN_DESC swapchainDesc{};
		const HRESULT descResult = nativeSwapchain->GetDesc(&swapchainDesc);
		if (SUCCEEDED(descResult))
		{
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
		SetSwapchainColorHint(actualOutput.encoding);
		DebugLog::Log(
			"libplacebo output negotiation (%s): requested=%s/%s/%s/%s actual_contract=%s/%s/%s/%s dxgi=%s accepted=%d safe=%d wire_state=unverified reason=%s",
			trigger,
			ToString(outputPlan.request.presentation),
			ToString(outputPlan.request.range),
			ToString(outputPlan.request.gamma),
			ToString(outputPlan.request.primaries),
			ToString(actualOutput.presentationModel),
			ToRangeString(actualOutput.encoding),
			ToGammaString(actualOutput.encoding),
			EncodingUsesBt2020(actualOutput.encoding) ? "BT.2020" : "Rec.709",
			ToString(actualOutput.encoding),
			actualOutput.requestedEncodingActive ? 1 : 0,
			actualOutput.safeToRender ? 1 : 0,
			actualOutput.reason.c_str());

		if (EncodingUsesBt2020(actualOutput.encoding) &&
			reportBt2020ToDisplay)
			nvidiaBt2020Reporter.Enable(negotiatedDisplayDeviceName.c_str());
		else
			nvidiaBt2020Reporter.Restore();
	}

	bool RecreateSwapchain(
		bool blit,
		bool suppressLimited,
		const char* trigger)
	{
		pl_swapchain_destroy(&swapchain);
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
		swapchainParams.window = videoHwnd;
		swapchainParams.color_bits = 10;
		swapchainParams.blit = blit;
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

	void Initialize(HWND videoHwnd, VideoStateComPtr& state, const std::string& manualRule)
	{
		this->videoHwnd = videoHwnd;
		const RendererSettings settings = LoadRendererSettings(*state, activeDisplayRule, manualRule);
		outputDiagnostics = settings.outputDiagnostics;
		shaderCacheEnabled = !settings.diagnosticDisableShaderCache;

		struct pl_log_params logParams{};
		logParams.log_cb = LibplaceboLog;
		logParams.log_level = PL_LOG_INFO;
		log = pl_log_create(PL_API_VER, &logParams);
		if (!log)
			throw std::runtime_error("Failed to create libplacebo log context");

		struct pl_d3d11_params deviceParams =
			LibplaceboExportedData<pl_d3d11_params>("pl_d3d11_default_params");
		deviceParams.allow_software = false;
		deviceParams.min_feature_level = D3D_FEATURE_LEVEL_10_0;
		deviceParams.max_frame_latency = 2;
		d3d11 = pl_d3d11_create(log, &deviceParams);
		if (!d3d11)
			throw std::runtime_error("Failed to create libplacebo D3D11 device");

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

		struct pl_d3d11_swapchain_params swapchainParams{};
		swapchainParams.window = videoHwnd;
		swapchainParams.color_bits = 10;
		LibplaceboOutput::Request outputRequest;
		outputRequest.presentation = LibplaceboOutput::ParsePresentation(
			settings.outputPresentation);
		outputRequest.range = LibplaceboOutput::ParseRange(settings.outputRange);
		outputRequest.gamma = LibplaceboOutput::ParseGamma(settings.outputGamma);
		targetBt2020 = settings.sdrTargetPrimaries == "bt2020";
		outputRequest.primaries = targetBt2020
			? LibplaceboOutput::PrimariesRequest::BT2020
			: LibplaceboOutput::PrimariesRequest::REC709;
		reportBt2020ToDisplay = settings.reportBt2020ToDisplay;
		if (targetBt2020 && reportBt2020ToDisplay)
			DebugLog::Log("libplacebo: BT.2020 target with NVIDIA output reporting requested; proceed with caution");
		else if (targetBt2020)
			DebugLog::Log("libplacebo: BT.2020 target requested without NVIDIA reporting; display must be selected manually");
		else if (reportBt2020ToDisplay)
			DebugLog::Log("libplacebo: report_bt2020_to_display ignored because target primaries are Rec.709");
		outputPlan = LibplaceboOutput::MakePlan(outputRequest);
		// COMPOSED retains the known-stable bitblt/DWM path. AUTO uses it unless
		// Limited requests a flip candidate; failed AUTO negotiation recreates
		// this stable composed path. DIRECT always asks for the flip candidate.
		// Windows decides whether a flip chain is composed, DirectFlip, MPO, or
		// independent flip.
		swapchainParams.blit = outputPlan.useBlit;
		swapchainBlit = outputPlan.useBlit;
		swapchain = pl_d3d11_create_swapchain(d3d11, &swapchainParams);
		if (!swapchain)
			throw std::runtime_error("Failed to create libplacebo D3D11 swapchain");

		sdrTargetNits = settings.sdrTargetNits;
		sdrBlackNits = settings.sdrBlackNits;
		sdrInputTransfer = TranslateOutputGamma(settings.sdrInputTransfer);
		scopeScreenAspect = settings.scopeScreenAspect;
		defaultScopeScreen = settings.defaultScopeScreen;
		scopeSubtitleFit = settings.scopeSubtitleFit;
		scopeSubtitleHoldMs = settings.scopeSubtitleHoldMs;
		scopeSubtitlePaddingPixels = settings.scopeSubtitlePaddingPixels;
		SetSwapchainColorHint(
			outputPlan.valid ? outputPlan.desiredEncoding :
				LibplaceboOutput::DxgiEncoding::FULL_G22_P709);

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
		displayRefreshRate.Switch(videoHwnd, *state, settings);
		// Negotiate only after libplacebo has applied its hint and completed the
		// initial ResizeBuffers operation; either may otherwise replace DXGI state.
		ConfigureAndFallback("initialize");

		renderer = pl_renderer_create(log, d3d11->gpu);
		if (!renderer)
			throw std::runtime_error("Failed to create libplacebo renderer");

		formatter = CreateP010Formatter(state->videoFrameEncoding);
		formatter->OnVideoState(state);
		formatterState = state;
		convertedFrame.resize(static_cast<size_t>(formatter->GetOutFrameSize()));
		LoadDisplayLut(settings);
		ConfigureRenderParams(settings);

		DebugLog::Log(
			"libplacebo initialized: D3D11, P010 upload, SDR target request=%s %.1f nits",
			targetBt2020 ? "BT.2020" : "Rec.709",
			sdrTargetNits);
	}

	// The caller holds renderMutex. Queue-generation validation and rendering
	// share that lock so a frame removed before a reset cannot cross the reset
	// boundary while waiting to enter libplacebo.
	bool RenderLocked(
		const VideoFrame& videoFrame,
		VideoStateComPtr& statePtr,
		uint64_t frameGeneration,
		uint64_t sourceSequence,
		int64_t enqueueQpc,
		int64_t dequeueQpc,
		size_t queueDepthAfterDequeue,
		size_t desiredQueueDepth,
		double oldestQueuedAgeMs,
		bool cadenceRepeat,
		double captureRateHz,
		bool scopeScreenActive,
		uint64_t screenProfileRequestSerial,
		int64_t screenProfileRequestNs,
		bool sceneDetectionEnabled,
		uint64_t sceneDetectorGeneration,
		std::atomic<uint64_t>& sceneDetectedCount,
		std::atomic<int>& sceneDetectionStatus,
		AlphaCadenceCorrectionDecision& correctionDecision)
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

		if (!formatterState || formatterState->colorspace != state.colorspace)
		{
			formatter->OnVideoState(statePtr);
			formatterState = statePtr;
		}

		if (lastRenderedEotf != EOTF::UNKNOWN &&
			(lastRenderedEotf != state.eotf || lastRenderedColorspace != state.colorspace))
		{
			// Flush at the exact queued-frame boundary, not when metadata first
			// arrives. Older queued frames still belong to the prior color state.
			pl_renderer_flush_cache(renderer);
		}

		if (!formatter->FormatVideoFrame(videoFrame, convertedFrame.data()))
			return false;

		const int width = static_cast<int>(state.displayMode->FrameWidth());
		const int height = static_cast<int>(state.displayMode->FrameHeight());
		const size_t rowBytes = static_cast<size_t>(width) * sizeof(uint16_t);
		const BYTE* yPixels = convertedFrame.data();
		const BYTE* uvPixels = yPixels + rowBytes * static_cast<size_t>(height);
		SceneDetectorResult sceneResult;
		if (!cadenceRepeat)
		{
			sceneResult = sceneDetector.Analyze({
				reinterpret_cast<const uint16_t*>(yPixels),
				static_cast<size_t>(width), static_cast<size_t>(height), rowBytes,
				sourceSequence, videoFrame.GetTimingTimestamp(),
				sceneDetectorGeneration, state.displayMode->FrameDuration(),
				sceneDetectionEnabled });
		}
		if (!cadenceRepeat)
			sceneDetectionStatus.store(
				static_cast<int>(sceneResult.status), std::memory_order_release);
		if (!cadenceRepeat && sceneResult.safeBoundary)
		{
			sceneDetectedCount.fetch_add(1, std::memory_order_relaxed);
			DebugLog::Log("libplacebo scene boundary: event=%llu sequence=%llu generation=%llu frames_back=%u luma=%u",
				static_cast<unsigned long long>(sceneResult.eventId),
				static_cast<unsigned long long>(sceneResult.sourceSequence),
				static_cast<unsigned long long>(sceneResult.generation),
				static_cast<unsigned>(sceneResult.eventFramesBack),
				static_cast<unsigned>(sceneResult.averageLuma));
		}
		if (!cadenceRepeat)
		{
			const AlphaPresentationSnapshot presentation =
				presentationTelemetry.Snapshot();
			AlphaCadenceCorrectionInput correctionInput;
			correctionInput.enabled = sceneDetectionEnabled;
			// Queue replacement and detector/source replacement are independent
			// reset boundaries. Fold both into the policy epoch so neither can
			// inherit a pending action or retained phase from the other.
			correctionInput.generation =
				frameGeneration ^
				(sceneDetectorGeneration + 0x9e3779b97f4a7c15ULL +
					(frameGeneration << 6) + (frameGeneration >> 2));
			correctionInput.presentationEvidence = presentation.evidence;
			correctionInput.captureRateHz = captureRateHz;
			correctionInput.displayRateHz = presentation.measuredDisplayHz;
			correctionInput.queueDepth = queueDepthAfterDequeue;
			correctionInput.desiredQueueDepth = desiredQueueDepth;
			correctionInput.oldestQueuedAgeMs = oldestQueuedAgeMs;
			correctionInput.presentationDebt =
				presentation.sourceToPresentDebt;
			correctionInput.lastPresentId = presentation.lastPresentId;
			correctionInput.safeSceneBoundary = sceneResult.safeBoundary;
			correctionInput.sceneEventId = sceneResult.eventId;
			correctionInput.sourceSequence = sourceSequence;
			correctionDecision =
				cadenceCorrectionPolicy.Evaluate(correctionInput);
			if (correctionDecision.action == AlphaCadenceAction::Drop)
			{
				DebugLog::Log(
					"Alpha cadence correction: action=drop generation=%llu source=%llu scene=%llu fallback=%d debt=%llu queue=%zu/%zu phase=%.6f",
					static_cast<unsigned long long>(frameGeneration),
					static_cast<unsigned long long>(sourceSequence),
					static_cast<unsigned long long>(
						correctionDecision.sceneEventId),
					correctionDecision.deadlineFallback ? 1 : 0,
					static_cast<unsigned long long>(
						presentation.sourceToPresentDebt),
					queueDepthAfterDequeue,
					desiredQueueDepth,
					correctionDecision.phaseFrames);
				return true;
			}
		}
		const float subtitleShiftSourcePixels = UpdateScopeSubtitleShift(
			reinterpret_cast<const uint16_t*>(yPixels),
			width,
			height,
			scopeScreenActive);

		struct pl_plane_data planes[2]{};
		planes[0].type = PL_FMT_UNORM;
		planes[0].width = width;
		planes[0].height = height;
		planes[0].component_size[0] = 16;
		planes[0].component_map[0] = PL_CHANNEL_Y;
		planes[0].pixel_stride = sizeof(uint16_t);
		planes[0].row_stride = rowBytes;
		planes[0].pixels = yPixels;

		planes[1].type = PL_FMT_UNORM;
		planes[1].width = (width + 1) / 2;
		planes[1].height = (height + 1) / 2;
		planes[1].component_size[0] = 16;
		planes[1].component_size[1] = 16;
		planes[1].component_map[0] = PL_CHANNEL_CB;
		planes[1].component_map[1] = PL_CHANNEL_CR;
		planes[1].pixel_stride = sizeof(uint16_t) * 2;
		planes[1].row_stride = rowBytes;
		planes[1].pixels = uvPixels;

		struct pl_frame image{};
		image.num_planes = 2;
		for (int plane = 0; plane < 2; ++plane)
		{
			if (!pl_upload_plane(d3d11->gpu, &image.planes[plane], &textures[plane], &planes[plane]))
				return false;
			image.planes[plane].shift_x = 0.0f;
			image.planes[plane].shift_y = 0.0f;
			image.planes[plane].flipped = state.invertedVertical;
		}

		image.repr.sys = TranslateSystem(state.colorspace);
		image.repr.levels =
			state.videoFrameEncoding == VideoFrameEncoding::ARGB_8BIT ||
			state.videoFrameEncoding == VideoFrameEncoding::BGRA_8BIT ||
			state.videoFrameEncoding == VideoFrameEncoding::R10b ||
			state.videoFrameEncoding == VideoFrameEncoding::R10l ||
			state.videoFrameEncoding == VideoFrameEncoding::R12L
			? PL_COLOR_LEVELS_FULL
			: PL_COLOR_LEVELS_LIMITED;
		image.repr.alpha = PL_ALPHA_NONE;
		image.repr.bits.sample_depth = 16;
		image.repr.bits.color_depth = 10;
		image.repr.bits.bit_shift = 6;
		image.color = TranslateColorSpace(state);
		if (state.eotf == EOTF::SDR &&
			sdrInputTransfer != PL_COLOR_TRC_UNKNOWN)
		{
			// This is an explicit interpretation override used for controlled
			// comparison with other renderers. It changes how captured SDR code
			// values are decoded, independently of the output transfer curve.
			image.color.transfer = sdrInputTransfer;
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
		pl_frame_set_chroma_location(&image, PL_CHROMA_LEFT);

		struct pl_swapchain_frame swapchainFrame{};
		if (!pl_swapchain_start_frame(swapchain, &swapchainFrame))
			return false;

		struct pl_frame baseTarget{};
		pl_frame_from_swapchain(&baseTarget, &swapchainFrame);
		const struct pl_color_repr returnedRepr = baseTarget.repr;
		const struct pl_color_space returnedColor = baseTarget.color;
		const bool returnedTargetMatchesActualOutput =
			ReturnedTargetMatchesActualOutput(returnedRepr, returnedColor);
		// The frame returned by libplacebo is the default host/compositor contract.
		// Override it only after the matching studio DXGI declaration was advertised
		// and accepted. Requested settings never flow directly into the target.
		baseTarget.repr.levels = EncodingLevels(actualOutput.encoding);
		const enum pl_color_transfer acceptedTransfer =
			EncodingTransfer(actualOutput.encoding);
		if (acceptedTransfer != PL_COLOR_TRC_UNKNOWN)
			baseTarget.color.transfer = acceptedTransfer;
		if (EncodingUsesBt2020(actualOutput.encoding))
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
				"libplacebo output diagnostic contract: source_eotf=%d source_system=%s source_levels=%d source_transfer=%s source_luma=%.4f..%.1f returned_system=%s returned_levels=%d returned_transfer=%s returned_bits=%d/%d returned_luma=%.4f..%.1f final_levels=%d final_transfer=%s final_luma=%.4f..%.1f dxgi=%s presentation=%s",
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
				static_cast<int>(baseTarget.repr.levels),
				pl_color_transfer_name(baseTarget.color.transfer),
				baseTarget.color.hdr.min_luma,
				baseTarget.color.hdr.max_luma,
				LibplaceboOutput::ToString(actualOutput.encoding),
				LibplaceboOutput::ToString(actualOutput.presentationModel));
			outputContractLogged = true;
		}

		auto configureScreenProfile =
			[this, &image, height](
				struct pl_frame& source,
				struct pl_frame& target,
				bool scopeActive,
				float subtitleShift)
		{
			source = image;
			bool croppedActivePicture = false;
			if (scopeActive &&
				scopeSubtitlePictureTop > 0 &&
				scopeSubtitlePictureBottom > scopeSubtitlePictureTop)
			{
				const float cropHeight = static_cast<float>(
					scopeSubtitlePictureBottom - scopeSubtitlePictureTop);
				float cropTop =
					static_cast<float>(scopeSubtitlePictureTop) + subtitleShift;
				cropTop = std::max(
					0.0f,
					std::min(cropTop, static_cast<float>(height) - cropHeight));
				source.crop.y0 = cropTop;
				source.crop.y1 = cropTop + cropHeight;
				croppedActivePicture = true;
			}

			if (scopeActive)
			{
				pl_rect2df_aspect_set(
					&target.crop,
					static_cast<float>(scopeScreenAspect),
					0.0f);
			}
			pl_rect2df_aspect_copy(&target.crop, &source.crop, 0.0f);
			if (scopeActive && !croppedActivePicture &&
				subtitleShift != 0.0f)
			{
				const float targetHeight =
					std::abs(target.crop.y1 - target.crop.y0);
				const float outputShift =
					subtitleShift * targetHeight / std::max(1, height);
				target.crop.y0 -= outputShift;
				target.crop.y1 -= outputShift;
			}
		};

		double prewarmMs = 0.0;
		if (hasPresentedFrame && !screenProfilesPrewarmed)
		{
			struct pl_frame warmImage{};
			struct pl_frame warmTarget = baseTarget;
			configureScreenProfile(
				warmImage,
				warmTarget,
				!scopeScreenActive,
				0.0f);
			const SteadyClock::time_point prewarmStart = SteadyClock::now();
			const bool warmed =
				pl_render_image(renderer, &warmImage, &warmTarget, &renderParams);
			prewarmMs = std::chrono::duration<double, std::milli>(
				SteadyClock::now() - prewarmStart).count();
			if (warmed)
			{
				screenProfilesPrewarmed = true;
				DebugLog::Log(
					"libplacebo screen profiles prewarmed: alternate=%s render=%.2f ms cache=%d objects/%zu bytes",
					scopeScreenActive ? "normal" : "scope",
					prewarmMs,
					cache ? pl_cache_objects(cache) : 0,
					cache ? pl_cache_size(cache) : 0);
			}
			else
			{
				if (warmTarget.lut == displayLut &&
					warmTarget.lut_type == PL_LUT_NATIVE)
				{
					RejectDisplayLutAfterRenderFailure();
					baseTarget.lut = nullptr;
					baseTarget.lut_type = PL_LUT_UNKNOWN;
				}
				DebugLog::Log(
					"libplacebo screen profile prewarm failed after %.2f ms; normal rendering continues",
					prewarmMs);
			}
		}

		struct pl_frame renderImage{};
		struct pl_frame target = baseTarget;
		configureScreenProfile(
			renderImage,
			target,
			scopeScreenActive,
			subtitleShiftSourcePixels);
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
		if (!rendered && targetLutApplied)
			RejectDisplayLutAfterRenderFailure();
		if (outputDiagnostics && rendered && !diagnosticReadbackComplete)
		{
			if (diagnosticReadbackFramesRemaining == 0)
				LogOutputReadback();
			else
				--diagnosticReadbackFramesRemaining;
		}
		const bool submitted = pl_swapchain_submit_frame(swapchain);
		const int64_t swapStartQpc = PerformanceCounterNow();
		if (submitted)
			pl_swapchain_swap_buffers(swapchain);
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
					sample.disjoint = true;
				}
				else if (SUCCEEDED(statisticsResult))
				{
					sample.available = true;
					sample.presentCount = statistics.PresentCount;
					sample.presentRefreshCount =
						statistics.PresentRefreshCount;
					sample.syncRefreshCount = statistics.SyncRefreshCount;
					sample.syncQpc = statistics.SyncQPCTime.QuadPart;
				}
			}
			presentationTelemetry.RecordSubmission(record);
			presentationTelemetry.Observe(sample);

			const uint64_t nowTick = GetTickCount64();
			if (nowTick >= nextPresentationTelemetryLogTick)
			{
				nextPresentationTelemetryLogTick = nowTick + 5000;
				const AlphaPresentationSnapshot snapshot =
					presentationTelemetry.Snapshot();
				DebugLog::Log(
					"Alpha presentation telemetry: generation=%llu evidence=%d retained=%zu source=%llu presented=%llu debt=%llu present_id=%u refresh=%u display_hz=%.5f cadence_samples=%u queue_after=%zu oldest_ms=%.2f render_ms=%.2f swap_ms=%.2f",
					static_cast<unsigned long long>(snapshot.generation),
					static_cast<int>(snapshot.evidence),
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
			if (screenProfileRequestSerial != 0 &&
				screenProfileRequestSerial != lastSubmittedScreenProfileRequest)
			{
				const double commandToSubmitMs =
					screenProfileRequestNs > 0
					? static_cast<double>(SteadyClockNowNs() - screenProfileRequestNs) /
						1000000.0
					: 0.0;
				DebugLog::Log(
					"libplacebo screen profile frame submitted: profile=%s request=%llu command_to_submit=%.2f ms render=%.2f ms prewarm=%.2f ms",
					scopeScreenActive ? "scope" : "normal",
					static_cast<unsigned long long>(screenProfileRequestSerial),
					commandToSubmitMs,
					renderMs,
					prewarmMs);
				lastSubmittedScreenProfileRequest = screenProfileRequestSerial;
			}
			if (!cursorPositioned)
			{
				// Move the pointer out of the picture once, but only if it is currently
				// over this render window. The user remains free to move and use it.
				POINT cursor{};
				RECT window{};
				if (GetCursorPos(&cursor) && GetWindowRect(videoHwnd, &window) &&
					PtInRect(&window, cursor))
				{
					SetCursorPos(
						std::max<LONG>(window.left, window.right - 2),
						std::max<LONG>(window.top, window.bottom - 2));
				}
				cursorPositioned = true;
			}
			if (lastRenderedEotf != state.eotf ||
				lastRenderedColorspace != state.colorspace)
			{
				DebugLog::Log(
					"libplacebo tone mapping: input=%s/%s mastering=%.3f..%.1f nits MaxCLL=%.1f MaxFALL=%.1f -> SDR Rec.709 %.1f nits",
					CStringA(ToString(state.eotf)).GetString(),
					CStringA(ToString(state.colorspace)).GetString(),
					image.color.hdr.min_luma,
					image.color.hdr.max_luma,
					image.color.hdr.max_cll,
					image.color.hdr.max_fall,
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
		cursorPositioned = false;
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


LibplaceboVideoRenderer::LibplaceboVideoRenderer(
	IRendererCallback& callback,
	HWND videoHwnd,
	ITimingClock* timingClock,
	bool useFrameQueue,
	size_t frameQueueMaxSize) :
	m_callback(callback),
	m_videoHwnd(videoHwnd),
	m_timingClock(timingClock),
	m_useFrameQueue(useFrameQueue),
	m_frameQueueDesiredDepth(AlphaQueuePolicy::NormalizeDesiredDepth(frameQueueMaxSize))
{
	m_frameQueueMaxSize =
		AlphaQueuePolicy::HardCapacity(m_frameQueueDesiredDepth);
	{
		std::lock_guard<std::mutex> guard(g_runtimeDisplayRuleMutex);
		m_manualDisplayRule = g_runtimeManualDisplayRule;
	}
	if (!m_manualDisplayRule.empty())
		DebugLog::Log("display: restored runtime manual rule '%s' for new renderer",
			m_manualDisplayRule.c_str());
	m_callback.OnRendererDetailString(TEXT("VideoProcessor Renderer (Alpha)"));
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
	ClearQueue();
	m_impl.reset();
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
		const std::string nextRule = m_manualDisplayRule.empty() ?
			ResolveDisplayRuleName(*videoState) : m_manualDisplayRule;
		if (nextRule != m_impl->activeDisplayRule)
		{
			DebugLog::Log(
				"display: input state selects rule '%s' instead of '%s'; requesting renderer rebuild",
				nextRule.empty() ? "base" : nextRule.c_str(),
				m_impl->activeDisplayRule.empty() ? "base" : m_impl->activeDisplayRule.c_str());
			return false;
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
		m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
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
			if (m_overflowLoggedGeneration != m_queueGeneration)
			{
				DebugLog::Log(
					"Alpha queue hard overflow: generation=%llu depth=%zu capacity=%zu; dropping oldest frame",
					static_cast<unsigned long long>(m_queueGeneration),
					m_frameQueue.size(),
					queueLimit);
				m_overflowLoggedGeneration = m_queueGeneration;
			}
			m_frameQueue.front().frame.SourceBufferRelease();
			m_frameQueue.pop_front();
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
		}
		videoFrame.SourceBufferAddRef();
		try
		{
			const uint64_t sourceSequence =
				m_sourceSequence.fetch_add(1, std::memory_order_relaxed) + 1;
			m_frameQueue.push_back({
				videoFrame,
				frameState,
				enqueueGeneration,
				sourceSequence,
				PerformanceCounterNow() });
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
	{
		std::lock_guard<std::mutex> guard(m_stateMutex);
		if (!m_videoState || !m_videoState->valid || !m_videoState->displayMode)
			throw std::runtime_error("libplacebo requires a valid video state before Build");
		state = m_videoState;
		manualRule = m_manualDisplayRule;
	}

	std::unique_ptr<Impl> impl(new Impl());
	impl->Initialize(m_videoHwnd, state, manualRule);
	const double nominalRate = state->displayMode->RefreshRateHz();
	const timingclocktime_t ticksPerSecond = m_timingClock ?
		m_timingClock->TimingClockTicksPerSecond() : 0;
	m_expectedPpmFrameTicks.store(
		(nominalRate > 0.0 && ticksPerSecond > 0) ?
		static_cast<timingclocktime_t>(std::llround(
			static_cast<double>(ticksPerSecond) / nominalRate)) : 0,
		std::memory_order_release);
	ResetFrameRateAndPPM();
	m_scopeScreenActive.store(impl->defaultScopeScreen, std::memory_order_release);
	m_impl = std::move(impl);
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
	ClearQueue();
	SetState(RendererState::RENDERSTATE_STOPPED);
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
	const AlphaCadenceTimingStatus timingStatus =
		static_cast<AlphaCadenceTimingStatus>(
			m_sceneTimingStatus.load(std::memory_order_acquire));
	switch (timingStatus)
	{
	case AlphaCadenceTimingStatus::Disabled:
		status = TEXT("Disabled");
		break;
	case AlphaCadenceTimingStatus::WaitingForDxgi:
		status = TEXT("Waiting for DXGI evidence");
		break;
	case AlphaCadenceTimingStatus::Measuring:
		status.Format(TEXT("Measuring DXGI rate (%u/600)"),
			m_sceneTimingRateSamples.load(std::memory_order_acquire));
		break;
	case AlphaCadenceTimingStatus::Matched:
		status.Format(TEXT("Rates matched (%.2f ppm)"),
			m_sceneTimingMismatchPpm.load(std::memory_order_acquire));
		break;
	case AlphaCadenceTimingStatus::Forecasting:
		status.Format(TEXT("Forecasting (%.2f ppm)"),
			m_sceneTimingMismatchPpm.load(std::memory_order_acquire));
		break;
	case AlphaCadenceTimingStatus::Verifying:
		status = TEXT("Verifying correction");
		break;
	default:
		status = TEXT("Rate mismatch unavailable");
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
		m_frameQueueDesiredDepth =
			AlphaQueuePolicy::NormalizeDesiredDepth(size);
		m_frameQueueMaxSize =
			AlphaQueuePolicy::HardCapacity(m_frameQueueDesiredDepth);
		DebugLog::Log("Alpha queue desired depth=%zu, internal hard capacity=%zu",
			m_frameQueueDesiredDepth, m_frameQueueMaxSize);
		while (m_frameQueue.size() > m_frameQueueMaxSize)
		{
			m_frameQueue.front().frame.SourceBufferRelease();
			m_frameQueue.pop_front();
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
		}
		m_queueDepthWindowStartNs = SteadyClockNowNs();
		m_queueDepthWindowDequeues = 0;
		m_queueDepthWindowHasSamples = false;
	}
	// A target reduction can satisfy an already pending startup prefill.
	// A target increase after release deliberately does not re-arm it.
	m_queueChanged.notify_all();
}


bool LibplaceboVideoRenderer::SetScreenProfile(
	bool scopeScreen,
	CString& activeProfile)
{
	if (!m_impl)
		return false;

	const int64_t requestNs = SteadyClockNowNs();
	m_screenProfileRequestNs.store(requestNs, std::memory_order_relaxed);
	const uint64_t requestSerial =
		m_screenProfileRequestSerial.fetch_add(1, std::memory_order_relaxed) + 1;
	m_scopeScreenActive.store(scopeScreen, std::memory_order_release);

	if (scopeScreen)
	{
		activeProfile.Format(TEXT("Scope (%.3f:1)"), m_impl->scopeScreenAspect);
		DebugLog::Log(
			"libplacebo screen profile selected: scope (%.4f:1) request=%llu",
			m_impl->scopeScreenAspect,
			static_cast<unsigned long long>(requestSerial));
	}
	else
	{
		activeProfile = TEXT("Normal");
		DebugLog::Log(
			"libplacebo screen profile selected: normal request=%llu",
			static_cast<unsigned long long>(requestSerial));
	}

	CString details;
	details.Format(TEXT("VideoProcessor Renderer (Alpha) - Screen: %s"), activeProfile.GetString());
	m_callback.OnRendererDetailString(details);
	PersistScreenProfile(scopeScreen);
	return true;
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
	value.Format(
		"Req %s/%s/%s/%s -> %s/%s/%s/%s",
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
	details = CString(value);
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


void LibplaceboVideoRenderer::RenderLoop()
{
	unsigned int consecutiveFailures = 0;
	for (;;)
	{
		VideoFrame frame;
		VideoStateComPtr state;
		uint64_t frameGeneration = 0;
		uint64_t sourceSequence = 0;
		int64_t enqueueQpc = 0;
		int64_t dequeueQpc = 0;
		size_t queueDepthAfterDequeue = 0;
		size_t desiredQueueDepth = 1;
		double oldestQueuedAgeMs = 0.0;
		bool cadenceRepeat = false;
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
			enqueueQpc = m_frameQueue.front().enqueueQpc;
			cadenceRepeat = m_frameQueue.front().cadenceRepeat;
			m_frameQueue.pop_front();

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

		if (prefillReleased)
		{
			DebugLog::Log(
				"Alpha queue startup prefill released: generation=%llu depth=%zu target=%zu",
				static_cast<unsigned long long>(frameGeneration),
				prefillDepth,
				prefillTarget);
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
					enqueueQpc,
					dequeueQpc,
					queueDepthAfterDequeue,
					desiredQueueDepth,
					oldestQueuedAgeMs,
					cadenceRepeat,
					m_measuredFrameRate.load(std::memory_order_relaxed),
					m_scopeScreenActive.load(std::memory_order_acquire),
					m_screenProfileRequestSerial.load(std::memory_order_acquire),
					m_screenProfileRequestNs.load(std::memory_order_relaxed),
					m_sceneDetectionEnabled.load(std::memory_order_acquire),
					m_sceneDetectorGeneration.load(std::memory_order_acquire),
					m_sceneDetectedCount,
					m_sceneDetectionStatus,
					correctionDecision);
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
			if (correctionDecision.verificationCompleted)
			{
				DebugLog::Log(
					"Alpha cadence correction verification: generation=%llu result=%s",
					static_cast<unsigned long long>(frameGeneration),
					correctionDecision.lastVerificationSucceeded
						? "verified" : "ambiguous");
			}
		}

		if (!rendered)
		{
			frame.SourceBufferRelease();
			if (staleGeneration)
				continue;

			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
			const bool gpuFailed = m_impl->IsGpuFailed();
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
			if (!IsWindowVisible(m_videoHwnd) || IsIconic(m_videoHwnd))
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

		if (correctionDecision.action == AlphaCadenceAction::Drop)
		{
			m_sceneCorrectionDropCount.fetch_add(1, std::memory_order_relaxed);
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
			m_sceneLastCorrectionAction.store(-1, std::memory_order_release);
			m_sceneLastCorrectionSecondsFromDeadline.store(
				0.0, std::memory_order_release);
			m_sceneLastCorrectionTick.store(
				GetTickCount64(), std::memory_order_release);
			frame.SourceBufferRelease();
			continue;
		}

		if (correctionDecision.action == AlphaCadenceAction::Repeat)
		{
			bool repeatQueued = false;
			{
				std::lock_guard<std::mutex> queueGuard(m_queueMutex);
				if (!m_stopRequested && frameGeneration == m_queueGeneration)
				{
					try
					{
						m_frameQueue.push_front({
							frame,
							state,
							frameGeneration,
							sourceSequence,
							enqueueQpc,
							true });
						repeatQueued = true;
					}
					catch (const std::exception& e)
					{
						DebugLog::Log(
							"Alpha cadence repeat enqueue failed: %s", e.what());
					}
				}
			}
			if (repeatQueued)
			{
				m_sceneCorrectionRepeatCount.fetch_add(
					1, std::memory_order_relaxed);
				m_sceneLastCorrectionAction.store(
					1, std::memory_order_release);
				m_sceneLastCorrectionSecondsFromDeadline.store(
					0.0, std::memory_order_release);
				m_sceneLastCorrectionTick.store(
					GetTickCount64(), std::memory_order_release);
				DebugLog::Log(
					"Alpha cadence correction: action=repeat generation=%llu source=%llu scene=%llu fallback=%d phase=%.6f",
					static_cast<unsigned long long>(frameGeneration),
					static_cast<unsigned long long>(sourceSequence),
					static_cast<unsigned long long>(
						correctionDecision.sceneEventId),
					correctionDecision.deadlineFallback ? 1 : 0,
					correctionDecision.phaseFrames);
				m_queueChanged.notify_one();
				continue;
			}
			{
				std::lock_guard<std::mutex> renderGuard(m_impl->renderMutex);
				m_impl->cadenceCorrectionPolicy.CancelPendingAction();
			}
		}

		if (m_timingClock)
		{
			m_exitLatencyMs.store(
				TimingClockDiffMs(
					frame.GetTimingTimestamp(),
					m_timingClock->TimingClockNow(),
					m_timingClock->TimingClockTicksPerSecond()),
				std::memory_order_relaxed);
		}

		frame.SourceBufferRelease();
	}
}


void LibplaceboVideoRenderer::ClearQueue()
{
	std::lock_guard<std::mutex> guard(m_queueMutex);
	ClearQueueLocked();
}


void LibplaceboVideoRenderer::BeginQueueGeneration(
	const char* reason,
	bool clearStopRequest)
{
	uint64_t generation = 0;
	size_t target = 0;
	size_t capacity = 0;
	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		ClearQueueLocked();
		if (++m_queueGeneration == 0)
			++m_queueGeneration;
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


void LibplaceboVideoRenderer::ClearQueueLocked()
{
	for (QueuedFrame& queuedFrame : m_frameQueue)
		queuedFrame.frame.SourceBufferRelease();
	m_frameQueue.clear();
}


size_t LibplaceboVideoRenderer::PrefillTargetLocked() const
{
	return m_useFrameQueue ? m_frameQueueDesiredDepth : 1;
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
	m_callback.OnRendererState(state);
}
