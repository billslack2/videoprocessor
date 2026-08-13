#include "pch.h"

#include "PatternGeneratorApp.h"

#include <CalibrationPatterns.h>
#include <ConfigFile.h>
#include <HDRData.h>
#include <VideoFrame.h>
#include <VideoState.h>
#include <vprenderer/LibplaceboPluginVideoRenderer.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>


namespace
{
constexpr wchar_t MENU_CLASS[] = L"VideoProcessorPatternGeneratorMenu";
constexpr wchar_t SURFACE_CLASS[] = L"VideoProcessorPatternGeneratorSurface";

constexpr int IDC_PATTERNS = 1001;
constexpr int IDC_INSTRUCTIONS = 1002;
constexpr int IDC_SHOW = 1003;
constexpr int IDC_EXIT = 1004;

struct PatternDefinition
{
	CalibrationPattern pattern;
	const wchar_t* name;
	const wchar_t* instructions;
	bool hdrSource = false;
};

const PatternDefinition PATTERNS[] = {
	{ CalibrationPattern::BLACK_LEVEL, L"Brightness / black level (PLUGE)",
	  L"Lower Brightness until reference black disappears. Then raise it until the first one or two above-black bars are just visible. Bars run left to right from reference black into progressively brighter near-black." },
	{ CalibrationPattern::WHITE_CLIPPING, L"Contrast / white clipping",
	  L"Raise Contrast until the brightest bars merge, then reduce it until the desired near-white bars are distinct. The VP Renderer configuration owns output transfer and range; this tool adds no range override." },
	{ CalibrationPattern::GRAYSCALE_STEPS, L"Grayscale steps",
	  L"Inspect the 0 through 100 percent steps for a neutral tint and smooth brightness progression. A meter is required to set gamma accurately; this pattern is best for spotting obvious tracking, banding, or clipping problems." },
	{ CalibrationPattern::GAMMA_COMPARISON, L"Visual gamma comparison (2.2 / 2.4)",
	  L"View from normal distance. The left solid patch is encoded for gamma 2.2 and the right for gamma 2.4; compare each with the black/white checkerboard average. Scaling, local dimming, and eyesight make this an approximation." },
	{ CalibrationPattern::COLOR_CLIPPING, L"RGB / CMY clipping",
	  L"Each row is red, green, blue, cyan, magenta, or yellow, increasing from 72 to 100 percent left to right. Look for upper steps merging or changing hue unexpectedly." },
	{ CalibrationPattern::SHARPNESS, L"Sharpness and one-pixel detail",
	  L"Reduce Sharpness until halos and ringing around the center cross disappear while the one-pixel and two-pixel checkerboards remain clean. Any configured VP scaling or shader remains active by design." },
	{ CalibrationPattern::GEOMETRY, L"Cinema grids - SDR Rec.709 source",
	  L"Shows the common cinema formats as centered, individually colored SDR Rec.709 grids. Confirm each border is visible, cells are uniform, and the circle is circular. Any key or click advances; input on the final 2.76:1 grid returns here." },
	{ CalibrationPattern::GEOMETRY, L"Cinema grids - HDR10 source (VP tone mapping)",
	  L"Shows the same cinema formats as HDR10 PQ / BT.2020 source grids, limited to approximately 203-nit pattern peaks. Use this to exercise VP's configured HDR-to-SDR tone mapping. Any key or click advances; input on the final 2.76:1 grid returns here. Alpha and the selected VP configuration own output processing.", true },
	{ CalibrationPattern::BLACK_FIELD, L"Full black field", L"Use to inspect black floor, uniformity, glow, and light leakage. The renderer configuration remains the sole output authority." },
	{ CalibrationPattern::GRAY_FIELD, L"50 percent gray field", L"Use to inspect grayscale neutrality, dirty-screen effect, panel uniformity, and color tinting. A meter is required for quantitative adjustment." },
	{ CalibrationPattern::WHITE_FIELD, L"Full white field", L"Use briefly to inspect white uniformity and full-field luminance behavior. Bright displays may engage ABL and can be uncomfortable." },
	{ CalibrationPattern::TEN_PERCENT_WHITE_WINDOW, L"10 percent white window", L"A centered 10 percent-area reference-white window on reference black, supplied as canonical SDR Rec.709 Full RGB to Alpha. The configured renderer owns every output conversion." }
};

struct CinemaAspectDefinition
{
	double ratio;
	const wchar_t* name;
	std::uint8_t red;
	std::uint8_t green;
	std::uint8_t blue;
};

const CinemaAspectDefinition CINEMA_ASPECTS[] = {
	{ 1.33, L"1.33:1 - Silent / 4:3", 255, 96, 96 },
	{ 1.37, L"1.37:1 - Academy", 255, 160, 64 },
	{ 1.43, L"1.43:1 - IMAX 15/70", 255, 224, 64 },
	{ 1.66, L"1.66:1 - European widescreen", 160, 255, 64 },
	{ 1.78, L"1.78:1 - 16:9", 64, 255, 128 },
	{ 1.85, L"1.85:1 - Flat", 64, 255, 224 },
	{ 1.90, L"1.90:1 - Digital IMAX", 64, 192, 255 },
	{ 2.00, L"2.00:1 - Univisium", 64, 112, 255 },
	{ 2.20, L"2.20:1 - 70mm", 128, 96, 255 },
	{ 2.35, L"2.35:1 - Legacy Scope", 192, 96, 255 },
	{ 2.39, L"2.39:1 - Modern Scope", 255, 96, 224 },
	{ 2.40, L"2.40:1 - Scope shorthand", 255, 96, 160 },
	{ 2.55, L"2.55:1 - Early CinemaScope", 255, 128, 128 },
	{ 2.59, L"2.59:1 - Cinerama", 192, 224, 255 },
	{ 2.76, L"2.76:1 - Ultra Panavision 70", 255, 255, 255 }
};

struct ActiveMonitor
{
	HMONITOR handle = nullptr;
	std::wstring sourceName;
	std::wstring friendlyName;
	RECT rectangle{};
};

BOOL CALLBACK EnumerateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
{
	auto& monitors = *reinterpret_cast<std::vector<ActiveMonitor>*>(parameter);
	MONITORINFOEXW info{};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info))
		return TRUE;
	ActiveMonitor entry;
	entry.handle = monitor;
	entry.sourceName = info.szDevice;
	entry.rectangle = info.rcMonitor;
	monitors.push_back(entry);
	return TRUE;
}

void PopulateFriendlyNames(std::vector<ActiveMonitor>& monitors)
{
	UINT32 pathCount = 0;
	UINT32 modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount,
		&modeCount) != ERROR_SUCCESS)
		return;
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
		&modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
		return;
	for (UINT32 index = 0; index < pathCount; ++index)
	{
		DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
		source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		source.header.size = sizeof(source);
		source.header.adapterId = paths[index].sourceInfo.adapterId;
		source.header.id = paths[index].sourceInfo.id;
		DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
		target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		target.header.size = sizeof(target);
		target.header.adapterId = paths[index].targetInfo.adapterId;
		target.header.id = paths[index].targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
			DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
			continue;
		for (ActiveMonitor& monitor : monitors)
			if (_wcsicmp(monitor.sourceName.c_str(),
				source.viewGdiDeviceName) == 0)
				monitor.friendlyName = target.monitorFriendlyDeviceName;
	}
}

std::wstring Widen(const std::string& value)
{
	if (value.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1,
		nullptr, 0);
	if (length <= 1)
		return std::wstring(value.begin(), value.end());
	std::wstring result(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &result[0], length);
	result.resize(static_cast<size_t>(length - 1));
	return result;
}

class QpcClock final : public ITimingClock
{
public:
	QpcClock()
	{
		LARGE_INTEGER frequency{};
		if (!QueryPerformanceFrequency(&frequency))
			throw std::runtime_error("QueryPerformanceFrequency failed");
		m_frequency = frequency.QuadPart;
	}

	timingclocktime_t TimingClockNow() override
	{
		LARGE_INTEGER value{};
		QueryPerformanceCounter(&value);
		return value.QuadPart;
	}
	timingclocktime_t TimingClockTicksPerSecond() const override
	{
		return m_frequency;
	}
	const TCHAR* TimingClockDescription() override
	{
		return TEXT("Pattern generator QPC");
	}

private:
	timingclocktime_t m_frequency = 0;
};

class FrameOwner final : public IUnknown
{
public:
	HRESULT QueryInterface(REFIID iid, void** value) override
	{
		if (!value)
			return E_POINTER;
		if (iid == IID_IUnknown)
		{
			*value = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}
		*value = nullptr;
		return E_NOINTERFACE;
	}
	ULONG AddRef() override { return ++m_refs; }
	ULONG Release() override { return --m_refs; }

private:
	std::atomic<ULONG> m_refs{0};
};

class AlphaPatternSession;

struct SurfaceState
{
	AlphaPatternSession* session = nullptr;
};

class AlphaPatternSession final : public IRendererCallback
{
public:
	AlphaPatternSession(HWND window, CalibrationPatternFrame frame,
		bool hdrSource) :
		m_window(window), m_frame(std::move(frame)), m_hdrSource(hdrSource)
	{
	}

	~AlphaPatternSession()
	{
		Stop();
	}

	void Start()
	{
		if (!LibplaceboPluginVideoRenderer::IsAvailable())
			throw std::runtime_error("VP Renderer (Alpha) plugin is unavailable beside this executable");

		m_state = new VideoState();
		m_state->valid = true;
		m_state->displayMode = std::make_shared<DisplayMode>(
			m_frame.width, m_frame.height, false, 60000, 1001);
		m_state->videoFrameEncoding = VideoFrameEncoding::BGRA_8BIT;
		m_state->eotf = m_hdrSource ? EOTF::PQ : EOTF::SDR;
		m_state->colorspace = m_hdrSource ? ColorSpace::BT_2020 :
			ColorSpace::REC_709;
		m_state->hdrData = std::make_shared<HDRData>();
		if (m_hdrSource)
		{
			m_state->hdrData->displayPrimaryRedX = 0.708;
			m_state->hdrData->displayPrimaryRedY = 0.292;
			m_state->hdrData->displayPrimaryGreenX = 0.170;
			m_state->hdrData->displayPrimaryGreenY = 0.797;
			m_state->hdrData->displayPrimaryBlueX = 0.131;
			m_state->hdrData->displayPrimaryBlueY = 0.046;
			m_state->hdrData->whitePointX = 0.3127;
			m_state->hdrData->whitePointY = 0.3290;
			m_state->hdrData->masteringDisplayMaxLuminance = 1000.0;
			m_state->hdrData->masteringDisplayMinLuminance = 0.005;
			m_state->hdrData->maxCll = 203.0;
			m_state->hdrData->maxFall = 203.0;
		}

		m_renderer.reset(new LibplaceboPluginVideoRenderer(*this, m_window,
			&m_clock, false, 1, VideoConversionOverride::VIDEOCONVERSION_NONE));
		if (!m_renderer->OnVideoState(m_state))
			throw std::runtime_error("VP Renderer rejected the canonical SDR pattern source state");
		m_renderer->Build();
		m_renderer->Start();
		if (m_rendererState.load() != RendererState::RENDERSTATE_RENDERING)
			throw std::runtime_error("VP Renderer did not enter the rendering state");

		VideoFrame videoFrame(m_frame.bgra.data(), 1,
			m_clock.TimingClockNow(), &m_frameOwner);
		m_renderer->OnVideoFrame(videoFrame);
		CString contract;
		if (m_renderer->GetOutputModeInfo(contract))
			DebugLog::Log("Pattern generator Alpha output contract: %s",
				CStringA(contract).GetString());
	}

	std::wstring GetOutputEvidenceSummary()
	{
		using namespace RendererOutputContract;
		Status status;
		// The pattern is queued asynchronously. Give Alpha a bounded interval to
		// expose submission/presentation evidence without delaying clean pattern
		// display indefinitely on a path where frame statistics are unavailable.
		for (unsigned int attempt = 0; attempt < 40; ++attempt)
		{
			if (m_renderer && m_renderer->GetOutputContractStatus(status) &&
				status.successfulPresents > 0)
				break;
			Sleep(25);
		}
		if (!status.available)
			return L"Result: MEASURE\nOutput contract evidence is unavailable.\n\n"
				L"Software cannot claim that this pattern reached the display.";

		const wchar_t* content = status.rendererContent ==
			RendererContentEvidence::NONBLACK ? L"nonblack readback" :
			status.rendererContent == RendererContentEvidence::ALL_BLACK ?
				L"all-black readback" : L"unverified";
		const wchar_t* delivery = status.displayDelivery ==
			DisplayDeliveryEvidence::PRESENTED ? L"DXGI presentation evidence" :
			status.displayDelivery == DisplayDeliveryEvidence::SUBMITTED ?
				L"submitted only; display delivery unverified" : L"unverified";
		const bool presented = status.displayDelivery ==
			DisplayDeliveryEvidence::PRESENTED;
		std::wostringstream text;
		text << (presented ? L"Result: PRESENTED / VISUALLY CONFIRM" :
			L"Result: MEASURE / DISPLAY DELIVERY UNVERIFIED")
			<< L"\nRenderer content: " << content
			<< L"\nFrame submission: "
			<< (status.successfulPresents > 0 ? L"accepted" : L"unverified")
			<< L"\nDisplay delivery: " << delivery
			<< L"\nSwapchain: " << Widen(status.swapchainFormat)
			<< L" (" << status.swapchainBitDepth << L"-bit)"
			<< L"\nDXGI declaration: " << Widen(status.dxgiDeclaration)
			<< L"\n\nThis does not prove physical HDMI values or calibration accuracy.";
		DebugLog::Log(
			"Pattern generator output evidence: rendered=%s submissions=%llu display_delivery=%s format=%s bits=%u dxgi=%s",
			status.rendererContent == RendererContentEvidence::NONBLACK ? "nonblack" :
				status.rendererContent == RendererContentEvidence::ALL_BLACK ? "black" : "unverified",
			static_cast<unsigned long long>(status.successfulPresents),
			status.displayDelivery == DisplayDeliveryEvidence::PRESENTED ? "presented" :
				status.displayDelivery == DisplayDeliveryEvidence::SUBMITTED ? "submitted-only" : "unverified",
			status.swapchainFormat.c_str(), status.swapchainBitDepth,
			status.dxgiDeclaration.c_str());
		return text.str();
	}

	void Stop()
	{
		if (!m_renderer)
			return;
		if (m_rendererState.load() == RendererState::RENDERSTATE_RENDERING)
			m_renderer->Stop();
		m_renderer.reset();
		m_state.Release();
	}

	void OnPaint()
	{
		if (m_renderer)
			m_renderer->OnPaint();
	}
	void OnSize()
	{
		if (m_renderer)
			m_renderer->OnSize();
	}
	void OnDisplayChange()
	{
		if (m_renderer)
			m_renderer->OnDisplayChange();
	}

	void OnRendererState(RendererState state) override
	{
		m_rendererState.store(state);
	}
	void OnRendererDetailString(const CString& details) override
	{
		DebugLog::Log("Pattern generator Alpha: %s",
			CStringA(details).GetString());
	}

private:
	HWND m_window = nullptr;
	CalibrationPatternFrame m_frame;
	FrameOwner m_frameOwner;
	QpcClock m_clock;
	VideoStateComPtr m_state;
	std::unique_ptr<LibplaceboPluginVideoRenderer> m_renderer;
	std::atomic<RendererState> m_rendererState{RendererState::RENDERSTATE_UNKNOWN};
	bool m_hdrSource = false;
};

LRESULT CALLBACK SurfaceWindowProc(HWND window, UINT message, WPARAM wParam,
	LPARAM lParam)
{
	SurfaceState* state = reinterpret_cast<SurfaceState*>(
		GetWindowLongPtr(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		const auto create = reinterpret_cast<CREATESTRUCT*>(lParam);
		state = reinterpret_cast<SurfaceState*>(create->lpCreateParams);
		SetWindowLongPtr(window, GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(state));
	}
	switch (message)
	{
	case WM_SETCURSOR:
		SetCursor(nullptr);
		return TRUE;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
		if (state && state->session)
			state->session->OnPaint();
		else
		{
			PAINTSTRUCT paint{};
			BeginPaint(window, &paint);
			EndPaint(window, &paint);
		}
		return 0;
	case WM_SIZE:
		if (state && state->session)
			state->session->OnSize();
		return 0;
	case WM_DISPLAYCHANGE:
		if (state && state->session)
			state->session->OnDisplayChange();
		return 0;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_XBUTTONDOWN:
	case WM_CLOSE:
		DestroyWindow(window);
		return 0;
	case WM_DESTROY:
		return 0;
	default:
		return DefWindowProc(window, message, wParam, lParam);
	}
}

class PatternMenu
{
public:
	explicit PatternMenu(HINSTANCE instance) : m_instance(instance)
	{
		ConfigFile rendererConfig;
		if (rendererConfig.Load(ConfigFile::RENDERER_FILENAME))
			m_rendererConfigPath = Widen(rendererConfig.GetLoadedPath());
		else if (!rendererConfig.GetWarnings().empty())
		{
			throw std::runtime_error(rendererConfig.GetWarnings().front());
		}
		ConfigFile mainConfig;
		std::string target;
		if (mainConfig.Load() && mainConfig.TryGetString("general",
			"fullscreen_monitor_name", target))
			m_configuredMonitorName = Widen(ConfigFile::Trim(target));
	}

	int Run()
	{
		RegisterWindows();
		m_window = CreateWindowEx(WS_EX_APPWINDOW, MENU_CLASS,
			L"VideoProcessor Pattern Generator (Alpha / SDR and HDR sources)",
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
			CW_USEDEFAULT, CW_USEDEFAULT, 840, 690, nullptr, nullptr,
			m_instance, this);
		if (!m_window)
			throw std::runtime_error("Failed to create pattern-generator menu");
		ShowWindow(m_window, SW_SHOW);
		UpdateWindow(m_window);
		MSG message{};
		while (GetMessage(&message, nullptr, 0, 0) > 0)
		{
			if (!IsDialogMessage(m_window, &message))
			{
				TranslateMessage(&message);
				DispatchMessage(&message);
			}
		}
		return static_cast<int>(message.wParam);
	}

private:
	void RegisterWindows()
	{
		WNDCLASSEX menuClass{};
		menuClass.cbSize = sizeof(menuClass);
		menuClass.lpfnWndProc = WindowProc;
		menuClass.hInstance = m_instance;
		menuClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
		menuClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		menuClass.lpszClassName = MENU_CLASS;
		if (!RegisterClassEx(&menuClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			throw std::runtime_error("Failed to register pattern-generator menu window");
		WNDCLASSEX surfaceClass{};
		surfaceClass.cbSize = sizeof(surfaceClass);
		surfaceClass.lpfnWndProc = SurfaceWindowProc;
		surfaceClass.hInstance = m_instance;
		surfaceClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
		surfaceClass.lpszClassName = SURFACE_CLASS;
		if (!RegisterClassEx(&surfaceClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			throw std::runtime_error("Failed to register Alpha pattern surface window");
	}

	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam,
		LPARAM lParam)
	{
		PatternMenu* self = reinterpret_cast<PatternMenu*>(
			GetWindowLongPtr(window, GWLP_USERDATA));
		if (message == WM_NCCREATE)
		{
			const auto create = reinterpret_cast<CREATESTRUCT*>(lParam);
			self = reinterpret_cast<PatternMenu*>(create->lpCreateParams);
			self->m_window = window;
			SetWindowLongPtr(window, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(self));
		}
		return self ? self->HandleMessage(message, wParam, lParam)
			: DefWindowProc(window, message, wParam, lParam);
	}

	HWND AddControl(const wchar_t* type, const wchar_t* text, DWORD style,
		int x, int y, int width, int height, int id)
	{
		HWND control = CreateWindowEx(0, type, text, WS_CHILD | WS_VISIBLE | style,
			x, y, width, height, m_window,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_instance, nullptr);
		SendMessage(control, WM_SETFONT,
			reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
		return control;
	}

	void CreateControls()
	{
		AddControl(L"STATIC", L"VideoProcessor Pattern Generator - Alpha renderer",
			SS_LEFT, 20, 18, 790, 28, 0);
		AddControl(L"STATIC",
			L"Dedicated mode: no capture device and no normal VP runtime. Standard patterns and the SDR cinema sequence enter Alpha as Rec.709 Full RGB; the HDR cinema sequence is PQ/BT.2020 source. The loaded VP configuration is the sole authority for output, tone mapping, LUT, shaders, scaling, viewport, gamma, range, and presentation.",
			SS_LEFT, 20, 50, 790, 58, 0);

		std::wstring configLine = m_rendererConfigPath.empty()
			? L"Renderer config: VP Renderer defaults (no configuration file selected)"
			: L"Renderer config selected by VP: " + m_rendererConfigPath;
		AddControl(L"STATIC", configLine.c_str(), SS_LEFT,
			20, 116, 790, 35, 0);
		std::wstring targetLine = m_configuredMonitorName.empty()
			? L"Fullscreen target: normal VP fallback (monitor nearest this menu)"
			: L"Fullscreen target from VideoProcessor.cfg: " + m_configuredMonitorName;
		AddControl(L"STATIC", targetLine.c_str(), SS_LEFT,
			20, 153, 790, 28, 0);

		AddControl(L"STATIC", L"Pattern:", SS_LEFT, 20, 192, 120, 22, 0);
		m_patternList = AddControl(L"LISTBOX", L"",
			LBS_NOTIFY | WS_BORDER | WS_TABSTOP | WS_VSCROLL,
			20, 216, 360, 330, IDC_PATTERNS);
		for (const auto& pattern : PATTERNS)
			SendMessage(m_patternList, LB_ADDSTRING, 0,
				reinterpret_cast<LPARAM>(pattern.name));
		SendMessage(m_patternList, LB_SETCURSEL, 0, 0);

		AddControl(L"STATIC", L"Before displaying:", SS_LEFT,
			400, 192, 410, 22, 0);
		m_instructionText = AddControl(L"STATIC", L"", SS_LEFT | WS_BORDER,
			400, 216, 410, 250, IDC_INSTRUCTIONS);
		AddControl(L"STATIC",
			L"Any key or mouse click returns here. Pattern Generator exposes no renderer override controls. Edit or select the VP configuration you intend to test, then relaunch this dedicated mode.",
			SS_LEFT, 400, 480, 410, 65, 0);
		AddControl(L"BUTTON", L"Show selected pattern",
			BS_DEFPUSHBUTTON | WS_TABSTOP, 400, 568, 260, 38, IDC_SHOW);
		AddControl(L"BUTTON", L"Exit", BS_PUSHBUTTON | WS_TABSTOP,
			680, 568, 130, 38, IDC_EXIT);
		UpdateInstructions();
		SetFocus(m_patternList);
	}

	ActiveMonitor ResolveTargetMonitor()
	{
		std::vector<ActiveMonitor> monitors;
		EnumDisplayMonitors(nullptr, nullptr, EnumerateMonitor,
			reinterpret_cast<LPARAM>(&monitors));
		PopulateFriendlyNames(monitors);
		if (!m_configuredMonitorName.empty())
		{
			std::vector<const ActiveMonitor*> matches;
			for (ActiveMonitor& monitor : monitors)
				if (_wcsicmp(monitor.friendlyName.c_str(),
					m_configuredMonitorName.c_str()) == 0)
					matches.push_back(&monitor);
			if (matches.size() == 1)
				return *matches.front();
			DebugLog::Log(
				"Pattern generator fullscreen monitor selection: requested='%S' fallback=existing reason=%s",
				m_configuredMonitorName.c_str(), matches.empty() ?
				"configured monitor unavailable" :
				"configured monitor name is ambiguous");
		}
		const HMONITOR fallback = MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST);
		for (const ActiveMonitor& monitor : monitors)
			if (monitor.handle == fallback)
				return monitor;
		throw std::runtime_error("VP fallback monitor is unavailable");
	}

	void UpdateInstructions()
	{
		const LRESULT selected = SendMessage(m_patternList, LB_GETCURSEL, 0, 0);
		if (selected >= 0 && selected < static_cast<LRESULT>(ARRAYSIZE(PATTERNS)))
			SetWindowText(m_instructionText, PATTERNS[selected].instructions);
	}

	void ShowSelectedPattern()
	{
		const LRESULT patternIndex = SendMessage(m_patternList, LB_GETCURSEL, 0, 0);
		if (patternIndex < 0 || patternIndex >= static_cast<LRESULT>(ARRAYSIZE(PATTERNS)))
			return;
		std::wstring confirmation = PATTERNS[patternIndex].instructions;
		confirmation += L"\n\nAlpha will use only the loaded VP configuration. Any key or mouse click returns to the menu. Show now?";
		if (PATTERNS[patternIndex].pattern == CalibrationPattern::GEOMETRY)
			confirmation = PATTERNS[patternIndex].instructions +
				std::wstring(L"\n\nAlpha will use only the loaded VP configuration. Show the 15-grid sequence now?");
		if (MessageBox(m_window, confirmation.c_str(), PATTERNS[patternIndex].name,
			MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
			return;
		HWND surface = nullptr;
		try
		{
			const ActiveMonitor monitor = ResolveTargetMonitor();
			const unsigned int width = static_cast<unsigned int>(
				monitor.rectangle.right - monitor.rectangle.left);
			const unsigned int height = static_cast<unsigned int>(
				monitor.rectangle.bottom - monitor.rectangle.top);
			ShowWindow(m_window, SW_HIDE);
			const bool cinemaSequence = PATTERNS[patternIndex].pattern ==
				CalibrationPattern::GEOMETRY;
			const bool hdrSource = PATTERNS[patternIndex].hdrSource;
			const size_t frameCount = cinemaSequence ? ARRAYSIZE(CINEMA_ASPECTS) : 1;
			for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
			{
				const double hdrScale = hdrSource ? 148.0 / 255.0 : 1.0;
				CalibrationPatternFrame frame = cinemaSequence ?
					CalibrationPatterns::GenerateCinemaGeometry(
						CINEMA_ASPECTS[frameIndex].ratio,
						static_cast<std::uint8_t>(CINEMA_ASPECTS[frameIndex].red * hdrScale + 0.5),
						static_cast<std::uint8_t>(CINEMA_ASPECTS[frameIndex].green * hdrScale + 0.5),
						static_cast<std::uint8_t>(CINEMA_ASPECTS[frameIndex].blue * hdrScale + 0.5),
						width, height) :
					CalibrationPatterns::Generate(PATTERNS[patternIndex].pattern,
						CalibrationSignalRange::FULL, width, height);
				SurfaceState state;
				const wchar_t* surfaceTitle = cinemaSequence ?
					CINEMA_ASPECTS[frameIndex].name :
					L"VideoProcessor Alpha calibration pattern";
				surface = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
					SURFACE_CLASS, surfaceTitle, WS_POPUP,
					monitor.rectangle.left, monitor.rectangle.top,
					static_cast<int>(width), static_cast<int>(height), nullptr,
					nullptr, m_instance, &state);
				if (!surface)
					throw std::runtime_error("Failed to create fullscreen Alpha surface");
				ShowWindow(surface, SW_SHOW);
				SetForegroundWindow(surface);
				SetFocus(surface);
				AlphaPatternSession session(surface, std::move(frame), hdrSource);
				state.session = &session;
				session.Start();
				if (frameIndex == 0)
				{
					std::wstring evidence = session.GetOutputEvidenceSummary();
					evidence += L"\n\nDoes the display match the intended pattern described before launch?\n"
						L"Yes continues to the clean pattern. No records a visible-delivery failure.";
					const int grade = MessageBox(surface, evidence.c_str(),
						L"Alpha pattern output evidence", MB_YESNO | MB_ICONWARNING |
						MB_DEFBUTTON2 | MB_TOPMOST);
					DebugLog::Log("Pattern generator visual delivery grade: result=%s",
						grade == IDYES ? "visible" : "not-visible");
					if (grade != IDYES)
						throw std::runtime_error(
							"Tester reported that the generated pattern was not visibly delivered");
					SetForegroundWindow(surface);
					SetFocus(surface);
				}
				MSG message{};
				while (IsWindow(surface) && GetMessage(&message, nullptr, 0, 0) > 0)
				{
					TranslateMessage(&message);
					DispatchMessage(&message);
				}
				state.session = nullptr;
				session.Stop();
				if (IsWindow(surface))
					DestroyWindow(surface);
				surface = nullptr;
			}
		}
		catch (const std::exception& error)
		{
			if (surface && IsWindow(surface))
				DestroyWindow(surface);
			const std::wstring text = Widen(error.what());
			MessageBox(m_window, text.c_str(), L"Pattern output failed",
				MB_OK | MB_ICONERROR);
		}
		ShowWindow(m_window, SW_SHOW);
		SetForegroundWindow(m_window);
		SetFocus(m_patternList);
	}

	LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_CREATE:
			CreateControls();
			return 0;
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
			case IDC_PATTERNS:
				if (HIWORD(wParam) == LBN_SELCHANGE)
					UpdateInstructions();
				else if (HIWORD(wParam) == LBN_DBLCLK)
					ShowSelectedPattern();
				return 0;
			case IDC_SHOW:
				if (HIWORD(wParam) == BN_CLICKED)
					ShowSelectedPattern();
				return 0;
			case IDC_EXIT:
				DestroyWindow(m_window);
				return 0;
			}
			break;
		case WM_CLOSE:
			DestroyWindow(m_window);
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProc(m_window, message, wParam, lParam);
	}

	HINSTANCE m_instance = nullptr;
	HWND m_window = nullptr;
	HWND m_patternList = nullptr;
	HWND m_instructionText = nullptr;
	std::wstring m_rendererConfigPath;
	std::wstring m_configuredMonitorName;
};
}


int RunPatternGenerator(HINSTANCE instance)
{
	try
	{
		PatternMenu menu(instance);
		return menu.Run();
	}
	catch (const std::exception& error)
	{
		const std::wstring text = Widen(error.what());
		MessageBox(nullptr, text.c_str(), L"VideoProcessor Pattern Generator",
			MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		return 1;
	}
}
