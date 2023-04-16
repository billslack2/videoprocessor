#pragma once


// CHelpDialog dialog

class CHelpDialog : public CDialog
{
	DECLARE_DYNAMIC(CHelpDialog)

public:
	CHelpDialog(CWnd* pParent = nullptr);   // standard constructor
	virtual ~CHelpDialog();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_HELP_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
	virtual BOOL OnInitDialog();

};
