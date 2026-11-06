// LibScope.cpp : application entry point
#include "pch.h"
#include "LibScope.h"
#include "LibScopeDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CLibScopeApp, CWinAppEx)
END_MESSAGE_MAP()

CLibScopeApp theApp;

CLibScopeApp::CLibScopeApp()
{
    SetAppID(_T("LibScope.LibraryExplorer.1.0"));
}

BOOL CLibScopeApp::InitInstance()
{
#ifdef _DEBUG
    // Report CRT leaks on exit (Output window when debugging).
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    CWinAppEx::InitInstance();

    // Needed by the folder picker and the Windows Properties dialog.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    SetRegistryKey(_T("LibScope"));

    CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_LunaBlue);
    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerOffice2007));

    m_elevated = QueryElevation();

    // LibScope.exe [file or folder]: opens there.
    std::wstring startPath;
    if (__argc > 1 && __wargv[1])
    {
        startPath = __wargv[1];
        // The command-line rules turn "C:\" into C:" (backslash-quote is an escaped quote),
        // and any text after it, such as a trailing space, joins the same argument.
        while (!startPath.empty() && startPath.back() == L' ')
            startPath.pop_back();
        if (!startPath.empty() && startPath.back() == L'"')
            startPath.back() = L'\\';
    }

    CLibScopeDlg dlg(startPath);
    m_pMainWnd = &dlg;
    dlg.DoModal();

    CoUninitialize();

    // Dialog closed: return FALSE so the application exits instead of entering a message pump.
    return FALSE;
}

bool CLibScopeApp::QueryElevation()
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;
    TOKEN_ELEVATION elevation = {};
    DWORD cb = 0;
    const bool elevated = GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cb) && elevation.TokenIsElevated;
    CloseHandle(hToken);
    return elevated;
}
