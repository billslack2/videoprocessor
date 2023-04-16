// CHelpDialog.cpp : implementation file
//

#include "pch.h"
#include "HelpDialog.h"
#include "afxdialogex.h"
#include <resource.h>


// CHelpDialog dialog

IMPLEMENT_DYNAMIC(CHelpDialog, CDialog)

CHelpDialog::CHelpDialog(CWnd* pParent /*=nullptr*/)
	: CDialog(IDD_HELP_DIALOG, pParent)
{

}

CHelpDialog::~CHelpDialog()
{
}

void CHelpDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CHelpDialog, CDialog)
END_MESSAGE_MAP()


// CHelpDialog message handlers

BOOL CHelpDialog::OnInitDialog()
{
    CDialog::OnInitDialog();

    // Set the text of the static control
    // SetDlgItemText(IDC_STATIC_TEXT, _T("This is some static text"));

    return TRUE;
}