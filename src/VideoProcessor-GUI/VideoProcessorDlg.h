/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once



#include <set>
#include <map>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <blackmagic_decklink/BlackMagicDeckLinkCaptureDeviceDiscoverer.h>
#include <PixelValueRange.h>
#include <OutputReadinessController.h>
#include <ShortcutRepeatGuard.h>
#include <CCie1931Control.h>
#include <IRenderer.h>
#include <RendererResetCoordinator.h>
#include <RendererResetPolicy.h>
#include <RendererRetirementService.h>
#include <RendererTransitionModel.h>
#include <RendererQueueLaunchContractModel.h>
#include <QueueProfileRestartPolicy.h>
#include <EventActionLauncher.h>
#include <UnifiedProfileRuntime.h>
#include <VideoFrame.h>
#include <FullscreenVideoWindow.h>
#include <RendererTransitionWindow.h>
#include <ShaderLoadingWindow.h>
#include <VideoConversionOverride.h>
#include <WindowedVideoWindow.h>
#include <microsoft_directshow/DirectShowRendererStartStopTimeMethod.h>
#include <microsoft_directshow/DirectShowDefines.h>
#include <microsoft_directshow/video_renderers/DirectShowVideoRenderer.h>
#include <StatsOverlayWindow.h>
#include <ApplicationInterface.h>
#include <ConfigurationApplyPolicy.h>
#include <ConfigEditorCore.h>
#include "ModernOperatorView.h"

#include "resource.h"


// Custom messages
#define WM_MESSAGE_CAPTURE_DEVICE_FOUND					(WM_APP + 1)
#define WM_MESSAGE_CAPTURE_DEVICE_LOST					(WM_APP + 2)
#define WM_MESSAGE_CAPTURE_DEVICE_STATE_CHANGE	        (WM_APP + 3)
#define WM_MESSAGE_CAPTURE_DEVICE_VIDEO_STATE_CHANGE	(WM_APP + 4)
#define WM_MESSAGE_CAPTURE_DEVICE_CARD_STATE_CHANGE		(WM_APP + 5)
#define WM_MESSAGE_CAPTURE_DEVICE_ERROR					(WM_APP + 6)
#define WM_MESSAGE_DIRECTSHOW_NOTIFICATION              (WM_APP + 7)
#define WM_MESSAGE_RENDERER_STATE_CHANGE                (WM_APP + 8)
#define WM_MESSAGE_RENDERER_DETAIL_STRING               (WM_APP + 9)
#define WM_MESSAGE_RENDERER_LIVE_FRAME                  (WM_APP + 10)
#define WM_MESSAGE_EVALUATE_RENDERER_START              (WM_APP + 11)
#define WM_MESSAGE_RENDERER_RESET_REQUEST               (WM_APP + 12)
#define WM_MESSAGE_RENDERER_RETIRED                     (WM_APP + 13)
#define WM_MESSAGE_EXTERNAL_SHORTCUT                    (WM_APP + 14)
#define WM_MESSAGE_RENDERER_INTENT_READY                (WM_APP + 15)
#define WM_MESSAGE_RENDERER_GRAPH_EVENT                 (WM_APP + 16)
#define WM_MESSAGE_RENDERER_RESTART_REQUIRED            (WM_APP + 17)
#define WM_MESSAGE_DIRECTSHOW_OWNER_COMPLETION           (WM_APP + 18)
#define WM_MESSAGE_FULLSCREEN_HOST_RESIZED               (WM_APP + 19)
#define WM_MESSAGE_RENDERER_ACTION_EVENT                 (WM_APP + 20)
#define WM_MESSAGE_RENDERER_QUEUE_CONTRACT_CHANGED       (WM_APP + 21)

static_assert(WM_MESSAGE_DIRECTSHOW_NOTIFICATION !=
	WM_MESSAGE_DIRECTSHOW_OWNER_COMPLETION,
	"DirectShow graph and owner-completion wakes must remain distinct");

// Timer IDs
#define TIMER_ID_1SECOND 1
#define RESIZE_DEBOUNCE_TIMER_ID 2
#define FULLSCREEN_FOCUS_TIMER_ID 4
#define EOTF_CHANGE_RESTART_TIMER_ID 5  // Was 4, now unique
#define LLDV_CHANGE_RESTART_TIMER_ID 6
#define UI_LAYOUT_RESTORE_TIMER_ID 7
#define SHADER_RULE_REFRESH_TIMER_ID 8
#define RENDERER_RESET_MAILBOX_TIMER_ID 9
#define TRANSIENT_INVALID_VIDEO_STATE_TIMER_ID 10
#define SHADER_SHORTCUT_DEBOUNCE_TIMER_ID 11
// Repeat keydowns are filtered by ShortcutDebounceState; retain a brief
// settle window for key-up/modifier ordering without making live NLS toggles
// feel delayed before VP even reaches madVR.
#define SHADER_SHORTCUT_DEBOUNCE_MS 75
#define LLDV_PROFILE_APPLY_TIMER_ID 12
#define CONFIGURATION_LIVE_APPLY_TIMER_ID 13
#define QUEUE_PROFILE_RESET_TIMER_ID 14
#define PROFILE_CHANGE_OVERLAY_TIMER_ID 15
#define CONFIGURATION_EDITOR_HOTKEY_ID 0x5650
#define SHADER_RULE_REFRESH_INTERVAL_MS 25
#define CONFIGURATION_LIVE_APPLY_INTERVAL_MS 250
#define QUEUE_PROFILE_RESET_DEBOUNCE_MS 100
#define BACKGROUND_SHORTCUT_DUPLICATE_WINDOW_MS 250




enum class HdrColorspaceOptions
{
	HDR_COLORSPACE_FOLLOW_INPUT,
	HDR_COLORSPACE_FOLLOW_INPUT_LLDV,
	HDR_COLORSPACE_FOLLOW_CONTAINER,
	HDR_COLORSPACE_BT2020,
	HDR_COLORSPACE_P3,
	HDR_COLORSPACE_REC709
};


enum class HdrLuminanceOptions
{
	HDR_LUMINANCE_FOLLOW_INPUT,
	HDR_LUMINANCE_FOLLOW_INPUT_LLDV,
	HDR_LUMINANCE_USER,
};

/**
 * Main UI is a simple dialog defined in VideoProcessor.rc
 */
class CVideoProcessorDlg:
	public CDialog,
	public ICaptureDeviceDiscovererCallback,
	public ICaptureDeviceCallback,
	public IRendererCallback
{
public:
	CVideoProcessorDlg();
	virtual ~CVideoProcessorDlg();
	void InterfaceMode(ApplicationInterface::Mode mode) { m_interfaceMode = mode; }
	ApplicationInterface::Mode InterfaceMode() const { return m_interfaceMode; }
	BOOL TranslateConfiguredAccelerator(MSG* message);

	// Dialog Data
	enum { IDD = IDD_VIDEOPROCESSOR_DIALOG };

	int CalculateAutoFrameOffset();

	// Option handlers
	void StartFullScreen(bool enabled = true);
	bool StartsFullScreen() const { return m_rendererFullScreenStart; }
	void WindowedFullScreenMode(bool enabled = true);
	void FullscreenMonitorName(const CString& name);
	CString FullscreenMonitorName() const { return m_fullscreenMonitorName; }
	void HideUI(bool enabled = true);
	void AlwaysWarnPci(bool enabled = true);
	void StartMinimized(bool enabled = true);
	void SceneDetect(bool enabled = true);
	void SceneCorrectionUpstreamSample(bool enabled);
	void SubtitleRepositioning(SubtitleRepositionMode mode);
	void EnableNewLldvHeuristic(bool enabled = true);
	void SetLldvMaxCll(double value);
	void SetLldvMaxFall(double value);
	void SetLldvMasteringMinLuminance(double value);
	void SetLldvMasteringMaxLuminance(double value);
	void DefaultRendererName(const CString&);
	void SetQueueSize(const CString&);
	void SetQueueResetDelaySeconds(const CString&);
	void SetQueueResetHighWaterPercent(const CString&);
	void SetCaptureDevice(const CString&);
	void SetCaptureInput(const CString&);

	void StartFrameOffsetAuto();
	void StartFrameOffset(const CString&);
	void DefaultVideoConversionOverride(VideoConversionOverride);
	void DefaultContainerColorSpace(ColorSpace);
	void DefaultHDRColorSpace(HdrColorspaceOptions);
	void DefaultHDRLuminance(HdrLuminanceOptions);
	void DefaultRendererStartStopTimeMethod(DirectShowStartStopTimeMethod);
	void DefaultRendererNominalRange(DXVA_NominalRange);
	void DefaultRendererTransferFunction(DXVA_VideoTransferFunction);
	void DefaultRendererTransferMatrix(DXVA_VideoTransferMatrix);
	void DefaultRendererPrimaries(DXVA_VideoPrimaries);
	// Diagnostic-only runner. It only accepts the explicitly generated temporary
	// sweep configuration, leaving the user's normal config untouched.
	void ConfigureActiveOutputSweep(bool enabled, DWORD holdMs, bool showInfo,
		bool captureRestart, const CString& suite, const CString& requestedTests);


	// UI-related handlers
	afx_msg void OnCaptureDeviceSelected();
	afx_msg void OnCaptureInputSelected();
	afx_msg void OnBnClickedCaptureRestart();
	afx_msg void OnBnClickedTimingClockFrameOffsetAutoCheck();
	afx_msg void OnEnChangeTimingClockFrameOffset();
	afx_msg void OnColorSpaceContainerSelected();
	afx_msg void OnHdrColorSpaceSelected();
	afx_msg void OnHdrLuminanceSelected();
	afx_msg void OnRendererSelected();
	afx_msg void OnBnClickedRendererRestart();
	afx_msg void OnRendererVideoConversionSelected();
	afx_msg void OnBnClickedRendererVideoFrameUseQueueCheck();
	afx_msg void OnRendererSceneCorrectionModeSelected();
	afx_msg void OnBnClickedRendererReset();
	afx_msg void OnBnClickedRendererResetAutoCheck();
	afx_msg void OnRendererDirectShowStartStopTimeMethodSelected();
	afx_msg void OnRendererDirectShowNominalRangeSelected();
	afx_msg void OnRendererDirectShowTransferFunctionSelected();
	afx_msg void OnRendererDirectShowTransferMatrixSelected();
	afx_msg void OnRendererDirectShowPrimariesSelected();
	afx_msg void OnBnClickedRendererFullScreenCheck();
	afx_msg void OnCbnSelchangeFullscreenmodeCombo();
	afx_msg void OnDisplayChange(UINT bitsPerPixel, int width, int height);


	// Custom message handlers
	afx_msg LRESULT OnMessageCaptureDeviceFound(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageCaptureDeviceLost(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageCaptureDeviceStateChange(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageCaptureDeviceCardStateChange(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageCaptureDeviceVideoStateChange(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageEvaluateRendererStart(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageCaptureDeviceError(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageDirectShowNotification(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageDirectShowOwnerCompletion(
		WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererStateChange(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererDetailString(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererGraphEvent(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererActionEvent(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererRestartRequired(
		WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererQueueContractChanged(
		WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageExternalShortcut(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageFullscreenHostResized(
		WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererLiveFrame(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererResetRequest(
		WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererRetired(
		WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMessageRendererIntentReady(
		WPARAM wParam, LPARAM lParam);
	bool TryFinalizeRendererRetirement(
		uint64_t token, const char* completionSource);
	afx_msg LRESULT OnMessageModernOperatorAction(
		WPARAM wParam, LPARAM lParam);

	// Command handlers
	void OnCommandFullScreenToggle();
	void OnCommandFullScreenExit();
	void OnCommandRendererReset();
	void OnCommandRendererRestart();
	void OnCommandReapplyRules();
	void OnCommandDisplayRuleAuto();
	afx_msg void OnCommandShaderRule(UINT commandId);
	afx_msg void OnCommandDisplayRule(UINT commandId);
	afx_msg void OnCommandRendererSelect(UINT commandId);
	void SelectRendererFromShortcut(unsigned int oneBasedIndex);
	void OnCommandPQSet();
	void OnCommandAutoSet();
	void OnCommandToggleStatsOverlay();
	void OnCommandCaptureRenderedOutput();
	void OnCommandConfigEditor();

	// ICaptureDeviceDiscovererCallback
	void OnCaptureDeviceFound(ACaptureDeviceComPtr& captureDevice) override;
	void OnCaptureDeviceLost(ACaptureDeviceComPtr& captureDevice) override;

	// ICaptureDeviceCallback
	void OnCaptureDeviceState(CaptureDeviceState state) override;
	void OnCaptureDeviceCardStateChange(CaptureDeviceCardStateComPtr cardState) override;
	void OnCaptureDeviceVideoStateChange(
		ACaptureDevice* source,
		CaptureRunToken captureRunToken,
		VideoStateComPtr videoState) override;
	void OnCaptureDeviceVideoFrame(
		ACaptureDevice* source,
		CaptureRunToken captureRunToken,
		VideoFrame& videoFrame) override;
	void OnCaptureDeviceError(const CString& error) override;

	// IRendererCallback
	void OnRendererState(
		RendererState rendererState, uint32_t rendererGeneration) override;
	void OnRendererDetailString(
		const CString& details, uint32_t rendererGeneration) override;
	void OnRendererGraphEvent(
		long eventCode, uint32_t rendererGeneration) override;
	void OnRendererActionEvent(const char* event,
		double actualRefreshRate, double requestedRefreshRate,
		double previousRefreshRate, uint32_t rendererGeneration) override;
	void OnRendererRestartRequired(uint32_t rendererGeneration) override;
	void OnRendererQueueContractChanged(
		uint32_t rendererGeneration) override;

protected:

	int m_resyncPendingResetSeconds = -1;  // Countdown timer for scheduled reset after resync/refresh rate change (-1 = no reset pending)
	double m_lastKnownRefreshRate = 0.0;  // Track last refresh rate for change detection (0 = not initialized)
	std::map<int, double> m_displayRefreshRateOverridesHz;

	// EOTF change detection for SDR/HDR switching
	// Feature flag: Set to true to enable automatic renderer restart on EOTF change
	bool m_enableEotfChangeRestart = true;  // TODO: Make this configurable via UI or config file
	EOTF m_lastKnownEotf = EOTF::UNKNOWN;  // Track last EOTF for change detection
	int m_eotfChangeRestartCooldownSeconds = -1;  // Cooldown timer to prevent restart loops (-1 = no cooldown active
	
	// SIMPLIFIED EOTF TRACKING: Store the EOTF when renderer starts, detect changes while rendering
	EOTF m_rendererStartedWithEotf = EOTF::UNKNOWN;
	int m_eotfCheckCooldownSeconds = 0;  // Cooldown to wait before checking EOTF changes (starts at 5 seconds after renderer start)

	// Optional LLDV heuristic.  DeckLink does not expose the HDMI VSIF, so
	// BT.2020 + SDR + no static HDR metadata is only a best-effort signal.
	bool m_useNewLldvHeuristic = false;
	// Negative means "use the profile/default fallback". Explicit command-line
	// overrides intentionally win over a selected LLDV profile on both the
	// legacy and new detection paths.
	double m_lldvMaxCllOverride = -1.0;
	double m_lldvMaxFallOverride = -1.0;
	double m_lldvMasteringMinLuminanceOverride = -1.0;
	double m_lldvMasteringMaxLuminanceOverride = -1.0;
	// Resolved [lldv] / [lldv.name] values. These remain distinct from the
	// explicit command-line overrides above so manual invocation precedence is
	// preserved when a profile changes at runtime.
	double m_profileLldvMaxCllOverride = -1.0;
	double m_profileLldvMaxFallOverride = -1.0;
	double m_profileLldvMasteringMinLuminanceOverride = -1.0;
	double m_profileLldvMasteringMaxLuminanceOverride = -1.0;
	bool m_lldvProfileApplyPending = false;
	bool m_newLldvCandidateActive = false;
	bool m_newLldvCandidateConfirmed = false;
	DWORD m_newLldvCandidateSince = 0;
	int m_lldvChangeRestartDelaySeconds = -1;
	bool m_lldvRestartPending = false;

	//
	// UI elements
	//

	// Capture device group
	CString m_initialCaptureDevice = TEXT("");
	CString m_initialCaptureInput = TEXT("");

	CComboBox m_captureDeviceCombo;
	CComboBox m_captureInputCombo;
	CStatic m_captureDeviceStateText;
	CButton m_captureDeviceRestartButton;
	CListBox m_captureDeviceOtherList;

	// Input group
	CStatic m_inputLockedText;
	CStatic m_inputDisplayModeText;
	CStatic m_inputEncodingText;
	CStatic m_inputBitDepthText;
	CStatic m_inputVideoFrameCountText;
	CStatic m_inputVideoFrameMissedText;
	CStatic m_inputLatencyMsText;

	// Captured video group
	CStatic m_videoValidText;
	CStatic m_videoDisplayModeText;
	CStatic m_videoPixelFormatText;
	CStatic m_videoEotfText;
	CStatic m_videoColorSpaceText;

	// Timing clock group
	CStatic m_timingClockDescriptionText;
	CButton m_timingClockFrameOffsetAutoCheck;
	CEdit m_timingClockFrameOffsetEdit;


	// Colorspace group
	CComboBox m_colorspaceContainerCombo;

	// HDR colorSpace group
	CEdit m_hdrColorspaceREdit;
	CEdit m_hdrColorspaceGEdit;
	CEdit m_hdrColorspaceBEdit;
	CEdit m_hdrColorspaceWPEdit;
	CComboBox m_hdrColorspaceCombo;

	// HDR Lumiance group
	CEdit m_hdrLuminanceMaxCll;
	CEdit m_hdrLuminanceMaxFall;
	CEdit m_hdrLuminanceMasterMin;
	CEdit m_hdrLuminanceMasterMax;
	CComboBox m_hdrLuminanceCombo;

	// CIE1931 graph
	CCie1931Control m_colorspaceCie1931xy;

	// Renderer group
	CComboBox m_rendererCombo;
	CStatic m_rendererDetailStringStatic;
	CButton m_rendererRestartButton;
	CStatic m_rendererStateText;
	WindowedVideoWindow	m_windowedVideoWindow;

	
	// Renderer Queue group
	CButton m_rendererVideoFrameUseQeueueCheck;
	CComboBox m_rendererSceneCorrectionModeCombo;
	CStatic m_rendererVideoFrameQueueSizeText;
	CEdit m_rendererVideoFrameQueueSizeMaxEdit;
	CStatic m_rendererDroppedFrameCountText;
	CButton m_rendererResetButton;
	CButton m_rendererResetAutoCheck;

	// Renderer Video conversion group
	CComboBox m_rendererVideoConversionCombo;

	// Renderer DirectShow override group
	CComboBox m_rendererDirectShowStartStopTimeMethodCombo;
	CComboBox m_rendererNominalRangeCombo;
	CComboBox m_rendererTransferFunctionCombo;
	CComboBox m_rendererTransferMatrixCombo;
	CComboBox m_rendererPrimariesCombo;

	// Renderer latency (ms) group
	CStatic m_rendererLatencyToVPText;
	CStatic m_rendererLatencyDsLeadText;
	CStatic m_rendererLatencyToDSText;

	// Renderer output group
	CButton m_rendererFullscreenCheck;
	CComboBox m_fullScreenModeCombo;

	CSize m_minDialogSize;
	ApplicationInterface::Mode m_interfaceMode =
		ApplicationInterface::Mode::Modern;
	ModernOperatorView m_modernOperatorView;
	struct FixedControlLayout
	{
		HWND hwnd = nullptr;
		CRect rect;
	};
	std::vector<FixedControlLayout> m_fixedControlLayout;
	struct ChildVisibility
	{
		HWND hwnd = nullptr;
		bool visible = false;
	};
	std::vector<ChildVisibility> m_normalUiChildVisibility;
	WINDOWPLACEMENT m_normalUiWindowPlacement = { sizeof(WINDOWPLACEMENT) };
	CSize m_normalUiMinDialogSize;
	bool m_noUiLayoutApplied = false;
	CSize m_initialClientSize;
	CRect m_initialVideoWindowRect;
	HICON m_hIcon;
	HACCEL m_accelerator = nullptr;
	std::vector<ACCEL> m_configuredAccelerators;
	bool m_shortcutsForegroundOnly = false;
	bool m_configurationEditorModal = false;
	bool m_configurationEditorActivationPending = false;
	bool m_configurationEditorFallbackLaunched = false;
	bool m_configurationEditorRevealAcknowledged = false;
	bool m_configurationEditorForegroundFallbackAttempted = false;
	unsigned int m_configurationEditorActivationAttempts = 0;
	ULONGLONG m_configurationEditorRevealStartedTick = 0;
	ULONGLONG m_configurationEditorLastRevealAttemptTick = 0;
	ULONGLONG m_configurationEditorActivationAcknowledgedTick = 0;
	HWND m_configurationEditorHwnd = nullptr;
	DWORD m_configurationEditorProcessId = 0;
	uint32_t m_configurationEditorPresentationSequence = 0;
	uint32_t m_configurationEditorPresentationRequired = 0;
	uint32_t m_configurationEditorPresentationAcknowledged = 0;
	HWND m_configurationEditorPresentationEditor = nullptr;
	HWND m_configurationEditorPresentationTarget = nullptr;
	ULONGLONG m_configurationEditorPresentationQueuedTick = 0;
	bool m_configurationEditorPresentationTimeoutLogged = false;
	WORD m_lastBackgroundShortcutCommand = 0;
	ULONGLONG m_lastBackgroundShortcutTick = 0;
	HANDLE m_configurationChangedEvent = nullptr;
	std::map<std::string, std::map<std::string, std::string>>
		m_configurationSnapshot;
	struct StagedRuntimeSettings
	{
		bool shortcutsForegroundOnly = false;
		bool hasCaptureDevice = false;
		CString captureDevice;
		bool hasCaptureInput = false;
		CString captureInput;
		bool hasRenderer = false;
		CString renderer;
		bool hasFullscreenMonitorName = false;
		CString fullscreenMonitorName;
		bool hasFrameOffset = false;
		bool frameOffsetAuto = false;
		int frameOffsetMs = 0;
		bool hasDirectShowVideoConversion = false;
		VideoConversionOverride directShowVideoConversion =
			VideoConversionOverride::VIDEOCONVERSION_NONE;
		bool hasVpRendererVideoConversion = false;
		VideoConversionOverride vpRendererVideoConversion =
			VideoConversionOverride::VIDEOCONVERSION_NONE;
		bool hasDirectShowContainerColorSpace = false;
		ColorSpace directShowContainerColorSpace = ColorSpace::UNKNOWN;
		bool hasVpRendererContainerColorSpace = false;
		ColorSpace vpRendererContainerColorSpace = ColorSpace::UNKNOWN;
		bool hasDirectShowHdrColorSpace = false;
		HdrColorspaceOptions directShowHdrColorSpace =
			HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT;
		bool hasVpRendererHdrColorSpace = false;
		HdrColorspaceOptions vpRendererHdrColorSpace =
			HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT;
		bool hasDirectShowHdrLuminance = false;
		HdrLuminanceOptions directShowHdrLuminance =
			HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT;
		bool hasVpRendererHdrLuminance = false;
		HdrLuminanceOptions vpRendererHdrLuminance =
			HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT;
		bool hasDirectShowTimeMethod = false;
		DirectShowStartStopTimeMethod directShowTimeMethod =
			DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART;
		bool hasNominalRange = false;
		DXVA_NominalRange nominalRange =
			DXVA_NominalRange::DXVA_NominalRange_Unknown;
		bool hasTransferFunction = false;
		DXVA_VideoTransferFunction transferFunction =
			DXVA_VideoTransferFunction::DXVA_VideoTransFunc_Unknown;
		bool hasTransferMatrix = false;
		DXVA_VideoTransferMatrix transferMatrix =
			DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_Unknown;
		bool hasPrimaries = false;
		DXVA_VideoPrimaries primaries =
			DXVA_VideoPrimaries::DXVA_VideoPrimaries_Unknown;
		bool hasSceneDetect = false;
		bool sceneDetect = false;
		unsigned int profileChangeDisplaySeconds =
			ProfileChangeOverlay::DefaultDisplaySeconds;
	};
	StagedRuntimeSettings m_stagedRuntimeSettings;
	std::unique_ptr<ConfigFile> m_stagedConfiguration;
	ConfigurationApplyPolicy::Action m_stagedConfigurationAction =
		ConfigurationApplyPolicy::Action::SaveOnly;
	std::string m_stagedConfigurationIdentity;
	bool m_stagedShortcutsChanged = false;
	bool m_stagedEditorApply = false;
	bool m_stagedRendererChanged = false;
	HACCEL m_stagedAccelerator = nullptr;
	std::vector<ACCEL> m_stagedConfiguredAccelerators;
	std::map<WORD, CString> m_stagedShaderShortcutRules;
	std::set<WORD> m_stagedShaderShortcutKeys;
	std::map<WORD, CString> m_stagedDisplayRuleShortcutRules;
	std::map<WORD, unsigned int> m_stagedRendererShortcutIndices;
	std::map<WORD, CString> m_stagedUnifiedProfileShortcutKeys;
	std::map<WORD, CString> m_shaderShortcutRules;
	std::set<WORD> m_shaderShortcutKeys;
	ShortcutDebounceState m_shaderShortcutDebounce;
	CString m_requestedShaderSelector;
	std::map<WORD, CString> m_displayRuleShortcutRules;
	std::map<WORD, unsigned int> m_rendererShortcutIndices;

	FullscreenVideoWindow* m_fullScreenVideoWindow = nullptr;

	//
	// Program data
	//

	CComPtr<BlackMagicDeckLinkCaptureDeviceDiscoverer> m_blackMagicDeviceDiscoverer;

	std::set<ACaptureDeviceComPtr> m_captureDevices;
	CComPtr<ACaptureDevice>	m_captureDevice;
	CaptureInputId m_currentCaptureInputId = INVALID_CAPTURE_INPUT_ID;
	CaptureDeviceState m_captureDeviceState = CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN;
	VideoStateComPtr m_captureDeviceVideoState = nullptr;  // This is what we get from the capture card

	VideoStateComPtr m_builtVideoState = nullptr;  // This is what we make of it


	// Startup options
	bool m_rendererFullScreenStart = false;
	bool m_windowedFullScreenMode = false;
	CString m_fullscreenMonitorName;
	ULONGLONG m_lastFullscreenMonitorSelectionLogTick = 0;
	bool m_hideUI = false;
	bool m_alwaysWarnPci = false;
	bool m_noUiToggleShortcutLatched = false;
	bool m_startMinimized = false;

	// Queue health monitoring variables
	size_t m_consecutiveFullSeconds = 0;
	size_t m_consecutiveStuckSeconds = 0;
	uint64_t m_lastDroppedFrames = 0;
	size_t m_lastQueueSize = 0;
	DWORD m_lastQueueHealthDiagnostic = 0;
	IVideoRenderer* m_dropDiagnosticRenderer = nullptr;
	bool m_dropDiagnosticInitialized = false;
	uint64_t m_lastLoggedCaptureMissed = 0;
	uint64_t m_lastLoggedRendererDropped = 0;
	ULONGLONG m_lastLivenessRecoveryTick = 0;
	bool m_queuePressureRecoveryRequested = false;
	bool m_queueCapacityRecoveryRequested = false;
	// One in-place DirectShow recovery is allowed to prove healthy delivery.
	// If the fresh epoch deadlocks before that proof, replace the opaque madVR
	// filter instance instead of cycling Stop/Run resets.
	bool m_directShowGraphRecoveryAwaitingHealth = false;
	bool m_directShowGraphRecoveryWasRetarget = false;
	bool m_directShowRecoveryRebuildRequested = false;
	uint32_t m_directShowGraphRecoveryGeneration = 0;
	uint64_t m_directShowGraphRecoveryEpoch = 0;
	ULONGLONG m_directShowGraphRecoveryStartedTick = 0;
	// A failed in-place recovery may replace madVR once for the current capture
	// state. The replacement graph itself is already a clean downstream prime,
	// so output readiness must adopt it instead of stacking another reset.
	bool m_nextRendererIsRecoveryRecreation = false;
	uint32_t m_directShowRecoveryRecreatedGeneration = 0;
	bool m_directShowRecoveryRecreationAttempted = false;
	uint64_t m_directShowRecoveryRecreationCaptureSequence = 0;
	ULONGLONG m_lastResetDeferralLogTick = 0;
	std::atomic<ULONGLONG> m_lastUiMessageTick = 0;
	std::atomic<ULONGLONG> m_lastUiPaintTick = 0;
	std::atomic<uint64_t> m_activeGraphRequestId = 0;
	std::atomic<uint32_t> m_activeGraphRequestGeneration = 0;
	std::atomic<ULONGLONG> m_activeGraphRequestStartedTick = 0;
	uint64_t m_lastGraphTimeoutLoggedOperationId = 0;
	HANDLE m_livenessWatchdogStopEvent = nullptr;
	std::thread m_livenessWatchdogThread;
	// Reset() can emit another RENDERSTATE_RENDERING callback. This marker is
	// reset only when a new renderer graph is constructed.
	ULONGLONG m_queueResetIgnoreEventsUntil = 0;
	bool m_displayTransitionAwaitingRenderer = false;
	// The last configured Windows target rate seen while Alpha is active. A
	// WM_DISPLAYCHANGE only earns a delayed Alpha re-prime when this crosses a
	// material refresh family boundary; 59.94/60 and 23.976/24 remain families.
	double m_lastAlphaTargetRefreshRateHz = 0.0;
	bool m_alphaRefreshTransitionPending = false;
	// Fresh Alpha start causes retained until Rendering so reset policy can
	// preserve refresh cleanup while logging host/backend skips explicitly.
	bool m_alphaHostTransitionPending = false;
	bool m_alphaBackendHandoffPending = false;
	double m_alphaRefreshTransitionPreviousRateHz = 0.0;
	double m_alphaRefreshTransitionCurrentRateHz = 0.0;
	// Initial DirectShow starts and backend handoffs need the proven madVR
	// stop/reset/run re-prime. Profile-only renderer replacements deliberately
	// consume this as false so NLS/shader changes do not add a second blackout.
	bool m_postRendererStartRequiresGraph = true;
	UINT_PTR m_rendererStartTime = 0;  // Tick count when renderer started rendering
	int m_queueResetDelaySeconds = 5;
	int m_queueResetHighWaterPercent = 75;

	// Frame offset by refresh data
	std::vector<int> m_frameOffsetsByRefresh;
	

	CString m_defaultRendererName;
	bool m_frameOffsetAutoStart = false;
	CString m_defaultFrameOffset = TEXT("90");
	int m_directShowFrameOffsetMs = 90;
	bool m_alphaFrameOffsetDisabled = false;
	CString m_defaultQueueSize = TEXT("32");
	size_t m_profileBaseQueueCapacity = 32;
	size_t m_profileBaseLeadFrames = 1;
	size_t m_profileBaseTargetFrames = 0;
	size_t m_profileBaseActivePictureLookaheadFrames = 0;
	size_t m_profileBaseStartupPrerollFrames = 0;
	int m_profileBaseQueueResetDelaySeconds = 5;
	int m_profileBaseQueueResetHighWaterPercent = 75;
	bool m_profileQueueDefaultsCaptured = false;
	size_t m_directShowQueueCapacity = 32;
	bool m_queueRendererSelectionInitialized = false;
	bool m_sceneAwareTimingCorrection = false;
	// DirectShow defaults to the more robust upstream-sample correction.
	// Basic remains available as a configuration-only compatibility override;
	// Alpha has a single native correction path and ignores this value.
	bool m_sceneCorrectionUpstreamSample = true;
	SubtitleRepositionMode m_subtitleRepositionMode =
		SubtitleRepositionMode::DISABLED;
	VideoConversionOverride m_directShowVideoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_NONE;
	VideoConversionOverride m_vpRendererVideoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_NONE;
	ColorSpace m_directShowContainerColorSpace = ColorSpace::UNKNOWN;
	ColorSpace m_vpRendererContainerColorSpace = ColorSpace::UNKNOWN;
	HdrColorspaceOptions m_directShowHDRColorSpaceOption = HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT;
	HdrColorspaceOptions m_vpRendererHDRColorSpaceOption = HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT;
	HdrLuminanceOptions m_directShowHDRLuminanceOption = HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT;
	HdrLuminanceOptions m_vpRendererHDRLuminanceOption = HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT;
	DirectShowStartStopTimeMethod m_defaultDSSSTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART;
	DXVA_NominalRange m_defaultNominalRange = DXVA_NominalRange::DXVA_NominalRange_Unknown;  // Auto
	DXVA_VideoTransferFunction m_defaultTransferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_Unknown;  // Auto
	DXVA_VideoTransferMatrix m_defaultTransferMatrix = DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_Unknown;  // Auto
	DXVA_VideoPrimaries m_defaultPrimaries = DXVA_VideoPrimaries::DXVA_VideoPrimaries_Unknown;  // Auto


	std::shared_ptr<IVideoRenderer> m_videoRenderer;
	std::shared_ptr<IVideoRenderer> m_failedRendererRetirement;
	ULONGLONG m_failedRendererRetirementNextRetryTick = 0;
	RendererRetirementService m_rendererRetirementService;
	bool m_rendererRetirementPending = false;
	bool m_rendererRetirementRetryActive = false;
	bool m_rendererConstructionActive = false;
	uint64_t m_rendererRetirementToken = 0;
	uint64_t m_rendererRetirementWaitLoggedToken = 0;
	CString m_retiringRendererName;
	uint32_t m_retiringRendererGeneration = 0;
	RendererState m_rendererState = RendererState::RENDERSTATE_UNKNOWN;
	RendererTransitionWindow m_rendererTransitionWindow;
	ShaderLoadingWindow m_shaderLoadingWindow;
	ULONGLONG m_shaderLoadingPopupShownTick = 0;
	HWND m_rendererTargetHwnd = nullptr;
	bool m_preserveFullscreenHostForProfileRestart = false;
	bool m_fullscreenRetargetPending = false;
	HWND m_fullscreenRetargetTargetHwnd = nullptr;
	HWND m_fullscreenRetargetPreviousTargetHwnd = nullptr;
	uint64_t m_fullscreenRetargetPreviousTargetRevision = 0;
	uint32_t m_fullscreenRetargetRendererGeneration = 0;
	bool m_fullscreenRetargetExiting = false;
	ULONGLONG m_fullscreenRetargetStartTick = 0;
	bool m_fullscreenModeChangePending = false;
	CString m_activeRendererName;
	CString m_acceptedRendererName;
	CString m_sessionRendererOverride;
	bool m_activeRendererIsDirectShow = false;
	std::atomic<uint32_t> m_rendererGeneration{0};
	uint32_t m_transitionGeneration = 0;
	// Owns the readiness-driven DirectShow LiveQueue reset/prefill state. It
	// never infers madVR queue state; completion and depth come from the
	// epoch-owned VP transport snapshot.
	OutputReadinessController m_outputReadinessObserver;
	bool m_outputReadinessObservationValid = false;
	OutputReadinessState m_lastObservedOutputReadinessState =
		OutputReadinessState::OutputNotReady;
	OutputReadinessReason m_lastObservedOutputReadinessReason =
		OutputReadinessReason::AwaitingGraph;
	bool m_lastObservedReadinessResetRequest = false;
	// True only while the intentional DirectShow/madVR graph re-prime selected
	// from validated readiness evidence is in flight. Its own renderer events
	// must not discard the evidence that selected it.
	bool m_outputReadinessGraphReprimeActive = false;
	OutputReadinessCompletionLatch m_outputReadinessResetCompletion;
	// A non-readiness DirectShow graph reset (for example capacity recovery or
	// a graph retarget) has already performed the madVR re-prime transaction.
	// It can satisfy readiness once the *new* DXGI measurement generation is
	// validated, avoiding a redundant second graph reset and black flash.
	OutputReadinessCompletionLatch m_outputReadinessExistingGraphResetCompletion;
	// Non-zero only between a successful GraphRetarget and its explicitly
	// scheduled delayed LiveQueue settle phase. This lineage lets that covered
	// successor advance readiness from the retarget epoch E1 to E2 without
	// granting the same credit to arbitrary queue-only resets.
	uint64_t m_outputReadinessRetargetSettleLineageGeneration = 0;
	uint64_t m_outputReadinessExistingGraphReservePublishedEpoch = 0;
	uint64_t m_currentGraphPrimeEvidenceEpoch = 0;
	uint64_t m_currentGraphPrimeEvidenceTick = 0;
	uint64_t m_currentGraphPrimeEvidenceTransitionGeneration = 0;
	uint64_t m_currentGraphPrimeObservedTransitionGeneration = 0;
	uint64_t m_currentGraphPrimeTransitionStartTick = 0;
	uint64_t m_currentGraphPrimeObservedQueueEpoch = 0;
	uint64_t m_currentGraphPrimeQueueTransitionGeneration = 0;
	uint64_t m_rendererTargetRevision = 0;
	uint64_t m_transitionBlackStartTick = 0;
	uint32_t m_rendererFirstFrameRevealPendingGeneration = 0;
	HWND m_rendererFirstFrameRevealTargetHwnd = nullptr;
	std::atomic_bool m_transitionRevealPosted{false};
	RendererTransitionModel m_rendererTransitionModel;
	bool m_rendererResetTransitionActive = false;
	RendererQueueLaunchContractModel m_queueLaunchContractModel;
	uint64_t m_queueLaunchContractRevision = 0;
	std::string m_queueLaunchContractProfileName;
	uint32_t m_queueLaunchContractCommitLoggedGeneration = 0;
	bool m_queueLaunchContractManualRetryPending = false;
	bool m_queueLaunchContractTerminalFailure = false;
	uint64_t m_rendererStartCapturedFrameCount = 0;
	bool m_rendererFrameBaselineValid = false;

	std::shared_ptr<RendererIngressState> m_rendererIngressState =
		std::make_shared<RendererIngressState>();
	std::mutex m_captureVideoStateNotificationMutex;
	ACaptureDevice* m_captureVideoStateSource = nullptr;
	uint64_t m_captureVideoStateSourceEpoch = 0;
	uint64_t m_captureVideoStateNextEpoch = 0;
	uint64_t m_appliedCaptureVideoStateNotificationSequence = 0;
	uint64_t m_rendererCaptureVideoStateNotificationSequence = 0;
	// Some capture drivers briefly publish UNKNOWN video state while frames are
	// still flowing.  Hold the last valid state for a short, bounded interval so
	// that one notification cannot tear down the DirectShow/madVR graph.
	VideoStateComPtr m_deferredInvalidCaptureVideoState;
	ULONGLONG m_deferredInvalidCaptureVideoStateDeadlineTick = 0;
	uint64_t m_deferredInvalidCaptureVideoStateFrameCount = 0;
	bool m_rendererStartEvaluationPosted = false;
	std::unique_ptr<RendererResetCoordinator> m_rendererResetCoordinator;
	RendererBindingToken m_rendererResetBindingToken = 0;
	UnifiedProfileRuntime::Runtime m_profileRuntime;
	HANDLE m_unifiedActionCancelEvent = nullptr;
	EventActionLauncher::PendingActionCoalescer m_unifiedActionCoalescer;
	std::vector<std::thread> m_unifiedActionWorkers;
	std::map<WORD, CString> m_unifiedProfileShortcutKeys;
	WORD m_lastUnifiedProfileCommand = 0;
	DWORD m_lastUnifiedProfileCommandTime = 0;
	QueueProfileRestartPolicy::PendingRequest m_queueProfileResetRequest;
	bool m_freshRendererProfileConstruction = false;
	uint32_t m_freshRendererProfileRendererGeneration = 0;
	uint64_t m_freshRendererProfileSnapshotGeneration = 0;
	std::string m_freshRendererProfileName;

	uint32_t m_timerSeconds = 0;

	// We often have to wait for devices to come back etc. Hence many functions can't complete
	// immediately. We solve this by setting a desired capture device and input and calling UpdateState()
	// at various points which will work towards our desired state
	CComPtr<ACaptureDevice>	m_desiredCaptureDevice = nullptr;
	CaptureInputId m_desiredCaptureInputId = INVALID_CAPTURE_INPUT_ID;
	//PixelValueRange m_desiredRendererPixelValueRange = PixelValueRange::PIXELVALUERANGE_UNKNOWN;  // = let render decide
	bool m_wantToRestartCapture = false;
	bool m_wantToRestartRenderer = false;
	bool m_wantToTerminate = false;

	// Stats overlay
	StatsOverlayWindow* m_statsOverlay = nullptr;
	StatsData* m_lastStatsData = nullptr;
	// Renderer telemetry getters are deliberately nonblocking. Retain their
	// last valid values only within the same renderer/host generation when a
	// periodic OSD read loses the render-lock race.
	const IVideoRenderer* m_lastStatsTelemetryRenderer = nullptr;
	uint32_t m_lastStatsTelemetryGeneration = 0;
	bool m_statsOverlayRequestedVisible = false;
	bool m_profileChangeOverlayInitialized = false;
	std::map<std::string, std::string> m_profileChangeOverlaySelections;
	std::vector<ProfileChangeOverlay::Item> m_profileChangeOverlayItems;
	unsigned int m_profileChangeDisplaySeconds =
		ProfileChangeOverlay::DefaultDisplaySeconds;
	ULONGLONG m_profileChangeOverlayHoldUntil = 0;
	ULONGLONG m_profileChangeOverlayFadeUntil = 0;

	struct ActiveOutputSweepCase
	{
		const wchar_t* label = L"";
		const char* presentation = "auto";
		const char* range = "auto";
		const char* gamma = "auto";
		bool force8Bit = false;
		bool allowLimitedG22 = false;
		bool vpOwnedPresenter = false;
		bool disableCompute = false;
		bool disableShaderCache = false;
		const wchar_t* description = L"";
		// HDR-suite values are null for a transport-contract case.  Keeping them
		// nullable lets the HDR suite preserve its dedicated output template and
		// change only the color-mapping control being diagnosed.
		const char* sdrTargetNits = nullptr;
		const char* toneMapping = nullptr;
		const char* gamutMapping = nullptr;
		const char* peakDetection = nullptr;
		const char* contrastRecovery = nullptr;
		const char* targetPrimaries = nullptr;
		const char* reportBt2020ToDisplay = nullptr;
	};
	bool m_activeOutputSweepRequested = false;
	bool m_activeOutputSweepRunning = false;
	bool m_activeOutputSweepAwaitingLiveFrame = false;
	bool m_activeOutputSweepPaused = false;
	bool m_activeOutputSweepCaseFailed = false;
	SweepBannerState m_activeOutputSweepBannerState = SweepBannerState::Testing;
	SweepResultState m_activeOutputSweepCaseResult = SweepResultState::Failed;
	CString m_activeOutputSweepCaseDetail;
	bool m_activeOutputSweepRestorePending = false;
	bool m_activeOutputSweepSummaryVisible = false;
	ULONGLONG m_activeOutputSweepSummaryStartedTick = 0;
	bool m_activeOutputSweepShowInfo = true;
	bool m_activeOutputSweepCaptureRestart = true;
	DWORD m_activeOutputSweepHoldMs = 5000;
	CString m_activeOutputSweepSuite = L"sdr";
	CString m_activeOutputSweepRequestedTests;
	size_t m_activeOutputSweepCaseIndex = 0;
	ULONGLONG m_activeOutputSweepDeadlineTick = 0;
	CString m_activeOutputSweepStatus;
	std::vector<ActiveOutputSweepCase> m_activeOutputSweepCases;
	std::vector<SweepSummaryItem> m_activeOutputSweepResults;
	std::unique_ptr<ConfigEditorCore::ConfigDocument> m_activeOutputSweepDocument;
	std::unique_ptr<ConfigEditorCore::ConfigDocument> m_activeOutputSweepOriginalDocument;

	void UpdateState();

	// Helpers
	void RefreshCaptureDeviceList();

	afx_msg void OnSelectCaptureDevice(UINT nID); // Handler for ON_COMMAND_RANGE

	void SelectCaptureDevice(int n); // Function to process capture selection
	void SelectCaptureDevice(CString& captureDeviceName);
	void SetVideoConversionOff();
	void SetVideoConversionP010();
	bool IsP010VideoConversionSelected() const;
	bool IsAlphaRendererSelected() const;
	bool IsUnifiedActionRendererSelected(
		const RendererProfileConfig::Model::EventAction& action) const;
	void UpdateRendererQueueControl();
	void UpdateSceneCorrectionModeUi();
	void UpdateRendererBackendUi();
	void CaptureFixedDialogLayout();
	void InitializeModernInterface(bool preserveWindowBounds = false);
	void ApplyModernLayout();
	void RefreshModernStatus();
	void RestoreNormalUiLayout();
	void RefreshPresentationLayoutAfterSessionToggle(const char* phase);
	void CloseOwnedTopLevelWindowsForShutdown();
	void RestoreFixedDialogLayout();
	void RestoreFrameOffsetEditLayout();
	void RefreshInputConnectionCombo();
	void CaptureStart();
	void CaptureStop();
	void CaptureRemove();
	void CaptureGUIClear();
	void RenderStart();
	void RenderStop();
	void RenderRemove();
	void DestroyVideoRenderer();
	void RenderGUIClear();
	void PauseRendererIngress();
	void WaitForRendererIngressDrain();
	void ResumeRendererIngress();
	void BindRendererResetSink();
	void RevokeRendererResetSink();
	void PumpRendererResetMailbox();
	bool ApplyRequestedShaderSelection();
	bool ShowRendererTransitionBlack(const char* reason);
	void TryRevealRendererTransition(uint32_t generation);
	bool TryStartFullscreenRetarget();
	void ClearFullscreenRetarget(bool restorePreviousTarget);
	void FullScreenVideoWindowConstruct();
	void FullScreenVideoWindowDestroy();
	HMONITOR SelectFullscreenMonitor();
	HWND GetRenderWindow();
	size_t GetRendererVideoFrameQueueSizeMax();
	bool GetRendererVideoFrameUseQueue();
	double GetWindowTextAsDouble(CEdit&);
	void UpdateTimingClockFrameOffsetAvailability();
	int GetTimingClockFrameOffsetMs();
	void SetTimingClockFrameOffsetMs(int timingClockFrameOffsetMs);
	std::vector<int> GetFrameOffsetByRefresh();
	void SetFrameOffsetByRefresh(std::vector<int> offsets);


	void UpdateTimingClockFrameOffset();
	void RebuildRendererCombo();
	void ClearRendererCombo();
	void UpdateStatsOverlay();
	void LogDroppedCounterChanges(const StatsData& stats);
	void ApplyStatsOverlayForActiveRenderer();
	void LoadDisplayRefreshRateOverrides();
	void ApplySavedConfiguration();
	void UpdateActiveOutputSweep(ULONGLONG now);
	bool StartActiveOutputSweep();
	bool ApplyActiveOutputSweepCase(size_t index);
	bool ApplyActiveOutputSweepConfiguration();
	void RestoreActiveOutputSweepConfiguration(const wchar_t* reason);
	void CompleteActiveOutputSweep(const wchar_t* result);
	void RecordActiveOutputSweepResult(SweepResultState state,
		const wchar_t* detail);
	bool EvaluateActiveOutputSweepCase(SweepResultState& state,
		CString& detail) const;
	bool TryClassifyActiveOutputSweepCase(ULONGLONG now,
		const char* trigger);
	void ToggleActiveOutputSweepPause();
	void ClearActiveOutputSweepSummary(const char* reason);
	bool StageSavedConfiguration(const char* reason, bool stageAccelerators);
	bool PublishStagedConfiguration(bool replaceAccelerators);
	bool PublishStagedShortcutsOnly();
	bool ReplaceStagedAccelerators();
	bool StageRuntimeSettings(const ConfigFile& config, std::string& error);
	void PublishStagedFullscreenMonitorSelection();
	void PublishStagedRuntimeSettings();
	void RestoreAcceptedRendererSelectionAfterReloadFailure();
	bool EstablishSessionRendererOverrideFromSelection(const char* reason);
	void ClearStagedConfiguration();
	void ReloadConfiguredAccelerators();
	void StartGlobalShortcutObserver();
	void StopGlobalShortcutObserver();
	void RequestPresentationFocus(const char* reason, unsigned int generation);
	void ToggleConfigurationEditor();
	void StartConfigurationEditorInTray();
	// Legacy activation-intent cleanup retained for source compatibility only;
	// no recurring timer schedules it under the native-owner architecture.
	void UpdateConfigurationEditorModal();
	void TrackConfigurationEditor(HWND editor);
	HWND VisibleAssociatedConfigurationEditor() const;
	bool RequestConfigurationEditorReveal(HWND editor);
	bool PublishConfigurationEditorPresentationTarget(HWND editor);
	bool RequestConfigurationEditorOneShotReassert(HWND editor,
		HWND presentationTarget);
	HWND ConfigurationEditorOwner();
	bool TryGetDisplayRefreshRateOverride(double nominalRateHz,
		double& overrideRateHz, int& matchedNominalRate) const;
	void MonitorQueueHealth(size_t rawQueueSize, size_t convertedQueueSize,
		size_t queueMaxSize, uint64_t droppedFrames);
	RendererResetCoordinator::SubmissionReceipt RequestRendererReset(
		RendererResetReason reason, bool requiresGraph, UINT delayMs,
		RendererResetOrigin origin = RendererResetOrigin::Unspecified,
		uint64_t originGeneration = 0);
	void CompleteRendererResetOperation();
	bool RendererResetOperationInProgress() const;
	RendererQueueLaunchDesired PublishRendererQueueLaunchDesired(
		RendererQueueLaunchBackend backend, size_t capacity,
		uint64_t profileGeneration, const char* source);
	void EvaluateDirectShowQueueLaunchContract(const char* trigger);
	void LogLivenessSnapshot(const RendererLivenessSnapshot& snapshot,
		size_t rawQueueSize, size_t convertedQueueSize, size_t queueMaxSize,
		const char* trigger);
	void LivenessWatchdogWorker();
	void ApplyNoUiLayout();
	bool UpdateNewLldvCandidate();
	bool IsNewLldvModeSelected();


	bool BuildPushVideoState();
	void BuildPushRestartVideoState();
	void ScheduleNewLldvRendererRestart();
	DisplayRuleExpression::ValueLookup GetUnifiedProfileSourceLookup();
	void RefreshUnifiedProfilesForRuleContext(const char* reason);
	void PublishActiveProfileStatus();
	void ApplyUnifiedProfileSnapshot(
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& snapshot,
		bool allowRestart, bool queueProfileResetPending = false);
	void PublishProfileChangeOverlay(
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& snapshot);
	void UpdateProfileChangeOverlay(ULONGLONG now);
	void ClearProfileChangeOverlay();
	void QueueUnifiedQueueProfileReset(
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& snapshot,
		const std::string& source);
	bool TryConsumeQueueProfileResetSatisfiedByFreshConstruction();
	void DispatchQueuedQueueProfileReset();
	void ScheduleUnifiedProfileActions(
		const std::vector<UnifiedProfileRuntime::ActionInvocation>& actions);
	void PublishUnifiedProfileEvent(const std::string& event,
		const std::string& reason,
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& previous,
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& current);

	// Track effective EOTF the renderer is currently configured for (post-UI overrides)
	EOTF m_lastEffectiveEotf = EOTF::UNKNOWN;
	bool m_hasLastEffectiveEotf = false;

	// Optional: prevent spam if capture toggles rapidly
	bool m_restartQueuedBecauseEotf = false;


#define FatalError(error) (_FatalError(__LINE__, __FUNCTION__, error))
	void _FatalError(int line, const std::string& functionName, const CString& error);

	// CDialog
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	std::atomic<bool> m_needsRenderRestart = false;
	std::atomic<bool> m_isRestartingRender = false;
	BOOL PreTranslateMessage(MSG* pMsg) override;
	void ApplyShaderRuleCommand(UINT commandId);
	void OnOK() override;
	afx_msg void OnPaint();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnClose();
	afx_msg void OnSysCommand(UINT command, LPARAM lParam);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnConfigurationEditorHotkey(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnConfigurationEditorAssociation(WPARAM wParam,
		LPARAM lParam);
	afx_msg LRESULT OnConfigurationEditorPresentationTargetAcknowledgement(
		WPARAM wParam, LPARAM lParam);
	afx_msg void OnCommandToggleNoUi();
	afx_msg HCURSOR	OnQueryDragIcon();
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* minMaxInfo);

	DECLARE_MESSAGE_MAP()


};
