#pragma once

#include <array>

#define WM_MODERN_OPERATOR_ACTION (WM_APP + 60)

enum class ModernOperatorAction : WPARAM
{
	CaptureRestart = 1,
	RendererRestart = 2,
	QueueReset = 3,
	OpenConfiguration = 4,
	ExitApplication = 5
};

struct ModernOperatorStatus
{
	CString captureDevice = TEXT("No capture device");
	CString captureState = TEXT("Unavailable");
	CString inputLock = TEXT("---");
	CString inputMode = TEXT("---");
	CString inputRate = TEXT("---");
	CString inputFormat = TEXT("---");
	CString inputBitDepth = TEXT("---");
	CString inputFrames = TEXT("---");
	CString inputMissed = TEXT("---");
	CString capturedValid = TEXT("---");
	CString capturedMode = TEXT("---");
	CString capturedRate = TEXT("---");
	CString capturedPixelFormat = TEXT("---");
	CString capturedPrimaries = TEXT("---");
	CString capturedTransfer = TEXT("---");
	CString captureLatency = TEXT("---");
	std::array<CString, 4> hardware = {
		TEXT("---"), TEXT("---"), TEXT("---"), TEXT("---") };
	CString maxCll = TEXT("---");
	CString maxFall = TEXT("---");
	CString masteringMin = TEXT("---");
	CString masteringMax = TEXT("---");
	CString rendererName = TEXT("No active renderer");
	CString rendererState = TEXT("Unavailable");
	CString queueRaw = TEXT("---");
	CString queueConverted = TEXT("---");
	CString queueTotal = TEXT("---");
	CString queueCapacity = TEXT("---");
	bool singleQueue = false;
	CString dropped = TEXT("---");
	CString vpLatency = TEXT("---");
	CString ptsLead = TEXT("---");
	CString outputLatency = TEXT("---");
};

class ModernOperatorView : public CWnd
{
public:
	bool Create(CWnd* parent);
	void SetStatus(const ModernOperatorStatus& status);
	void LayoutControls();

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	afx_msg void OnSize(UINT type, int width, int height);
	afx_msg void OnCaptureRestart();
	afx_msg void OnRendererRestart();
	afx_msg void OnQueueReset();
	afx_msg void OnConfiguration();
	afx_msg void OnExit();
	afx_msg void OnDrawItem(int controlId, LPDRAWITEMSTRUCT drawItem);

	DECLARE_MESSAGE_MAP()

private:
	int Px(int logicalPixels) const;
	void DrawCard(CDC& dc, const CRect& rect, const CString& eyebrow,
		const CString& title, const CString& state = CString());
	void DrawRows(CDC& dc, int x, int y, int width,
		const std::initializer_list<std::pair<CString, CString>>& rows);
	void DrawWideValue(CDC& dc, int x, int y, int width,
		const CString& value);
	void DrawQueueMetric(CDC& dc, int x, const CString& label,
		const CString& value);
	void CreateButton(CButton& button, UINT id, const CString& text);

	ModernOperatorStatus m_status;
	CButton m_configuration;
	CButton m_exit;
	CButton m_captureRestart;
	CButton m_rendererRestart;
	CButton m_queueReset;
	CFont m_regularFont;
	CFont m_boldFont;
	CFont m_titleFont;
};
