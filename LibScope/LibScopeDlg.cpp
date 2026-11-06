// LibScopeDlg.cpp
#include "pch.h"
#include "LibScope.h"
#include "LibScopeDlg.h"
#include "Format.h"
#include "Report.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    enum { IDC_VOLUMES = 1100, IDC_SCAN, IDC_STOP, IDC_INCLUDE_EXE, IDC_FILTER, IDC_TREE, IDC_LIBRARIES, IDC_DETAILS };
    enum { kScanTimer = 1, kScanTimerMs = 250, kSelectionTimer = 2, kSelectionDelayMs = 120, kFilterTimer = 3, kFilterDelayMs = 250 };

    // Library list columns.
    enum { colName, colType, colArch, colExports, colDeps, colVersion, colCompany, colSize, colModified, colFolder };

    // Context menu commands: returned by TrackPopupMenu, so they never reach the message map.
    enum
    {
        cmdSummary = 1, cmdExports, cmdDependencies, cmdImports, cmdTree, cmdModules, cmdProblems, cmdVersion, cmdSignature,
        cmdOpenLocation, cmdCopyPath, cmdCopyName, cmdGoToFolder, cmdProperties,
        cmdSaveReport, cmdSaveExports,
        cmdList, cmdScan, cmdRefreshFolder
    };

    class CAboutDlg : public CDialogEx
    {
    public:
        CAboutDlg() : CDialogEx(IDD_ABOUTBOX) {}
    };

    CString DefaultFileName(const std::wstring& libraryName, LPCTSTR suffix)
    {
        return fmt::Wide(libraryName) + suffix;
    }

    bool AskSavePath(CWnd* parent, const CString& defaultName, LPCTSTR extension, LPCTSTR filter, CString& path)
    {
        CFileDialog dialog(FALSE, extension, defaultName, OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST, filter, parent);
        if (dialog.DoModal() != IDOK)
            return false;
        path = dialog.GetPathName();
        return true;
    }
}

BEGIN_MESSAGE_MAP(CLibScopeDlg, CDialogEx)
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_TIMER()
    ON_WM_SETCURSOR()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_CAPTURECHANGED()
    ON_BN_CLICKED(IDC_SCAN, &CLibScopeDlg::OnScanButton)
    ON_BN_CLICKED(IDC_STOP, &CLibScopeDlg::OnStopButton)
    ON_BN_CLICKED(IDC_INCLUDE_EXE, &CLibScopeDlg::OnIncludeExe)
    ON_EN_CHANGE(IDC_FILTER, &CLibScopeDlg::OnFilterChanged)
    ON_NOTIFY(TVN_SELCHANGED, IDC_TREE, &CLibScopeDlg::OnTreeSelChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIBRARIES, &CLibScopeDlg::OnLibraryChanged)
    ON_NOTIFY(NM_DBLCLK, IDC_LIBRARIES, &CLibScopeDlg::OnLibraryDblClk)
    ON_NOTIFY(NM_CLICK, IDC_VOLUMES, &CLibScopeDlg::OnVolumeClick)
    ON_NOTIFY(NM_DBLCLK, IDC_VOLUMES, &CLibScopeDlg::OnVolumeDblClk)
    ON_MESSAGE(WM_DETAILS_READY, &CLibScopeDlg::OnDetailsReady)
    ON_MESSAGE(WM_SIGNATURE_READY, &CLibScopeDlg::OnSignatureReady)
    ON_MESSAGE(WM_DEPENDENCIES_READY, &CLibScopeDlg::OnDependenciesReady)
    ON_COMMAND(ID_FILE_OPENFOLDER, &CLibScopeDlg::OnFileOpenFolder)
    ON_COMMAND(ID_FILE_OPENLIBRARY, &CLibScopeDlg::OnFileOpenLibrary)
    ON_COMMAND(ID_FILE_SAVELIST, &CLibScopeDlg::OnFileSaveList)
    ON_COMMAND(ID_FILE_EXIT, &CLibScopeDlg::OnFileExit)
    ON_COMMAND(ID_VIEW_REFRESH, &CLibScopeDlg::OnViewRefresh)
    ON_COMMAND(ID_HELP_ABOUT, &CLibScopeDlg::OnHelpAbout)
END_MESSAGE_MAP()

CLibScopeDlg::CLibScopeDlg(const std::wstring& startPath)
    : CDialogEx(IDD_LIBSCOPE_DIALOG), m_startPath(startPath)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

BOOL CLibScopeDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    m_hAccel = LoadAccelerators(AfxGetResourceHandle(), MAKEINTRESOURCE(IDR_ACCELERATORS));

    CString title;
    title.Format(_T("LibScope - Library Explorer%s"), theApp.IsElevated() ? _T(" (Administrator)") : _T(""));
    SetWindowText(title);

    // UI font: the system message font (Segoe UI on Windows 10/11) at the current DPI.
    NONCLIENTMETRICS ncm = { sizeof(ncm) };
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_font.CreateFontIndirect(&ncm.lfMessageFont);
    SetFont(&m_font);

    const CRect none(0, 0, 10, 10);
    const DWORD label = WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS;

    m_volumeSummary.Create(_T("Volumes"), label, none, this);
    m_volumeList.CreateList(this, IDC_VOLUMES, false);
    m_scanButton.Create(_T("&Scan Subfolders"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, none, this, IDC_SCAN);
    m_stopButton.Create(_T("S&top"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_PUSHBUTTON, none, this, IDC_STOP);
    m_includeExe.Create(_T("Include .&exe"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, none, this, IDC_INCLUDE_EXE);
    m_progress.Create(WS_CHILD | PBS_MARQUEE, none, this, IDC_STATIC);
    m_scanStatus.Create(_T(""), label, none, this);
    m_filterLabel.Create(_T("Filter:"), WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX, none, this);
    m_filter.CreateEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, none, this, IDC_FILTER);
    m_filter.SetCueBanner(_T("name, company, type..."));
    m_tree.CreateTree(this, IDC_TREE);
    m_libraryList.CreateList(this, IDC_LIBRARIES, false);
    m_details.CreatePane(this, IDC_DETAILS, &m_font);
    m_status.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS | SS_SUNKEN, none, this);

    for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
        child->SetFont(&m_font, FALSE);

    using C = CVirtualList::Column;
    const int R = LVCFMT_RIGHT;

    // Same view as WinMon's Disk Volumes tab.
    m_volumeList.SetColumns({ { _T("Drive"), 60 }, { _T("Label"), 170 }, { _T("Type"), 80 }, { _T("File System"), 85 },
                              { _T("Capacity"), 85, true, R }, { _T("Used"), 85, true, R }, { _T("Free"), 85, true, R },
                              { _T("% Used"), 70, true, R }, { _T("Usage"), 250 } });
    m_volumeList.SetSource(
        [this](size_t row, int col) -> CString {
            const VolumeInfo& v = m_volumes[row];
            const ULONGLONG used = v.totalBytes - v.freeBytes;
            const double pct = v.totalBytes ? 100.0 * double(used) / double(v.totalBytes) : 0.0;
            switch (col)
            {
            case 0: return fmt::Wide(v.root);
            case 1: return v.label;
            case 2: return v.driveType;
            case 3: return v.fileSystem;
            case 4: return fmt::Bytes(double(v.totalBytes));
            case 5: return fmt::Bytes(double(used));
            case 6: return fmt::Bytes(double(v.freeBytes));
            case 7: return fmt::Percent(pct);
            default:
            {
                const int filled = int(pct / 5.0 + 0.5);
                return CString(_T('\x2588'), filled) + CString(_T('\x2591'), 20 - filled);
            }
            }
        },
        [this](size_t row, int col) -> double {
            const VolumeInfo& v = m_volumes[row];
            const ULONGLONG used = v.totalBytes - v.freeBytes;
            switch (col)
            {
            case 4: return double(v.totalBytes);
            case 5: return double(used);
            case 6: return double(v.freeBytes);
            default: return v.totalBytes ? double(used) / double(v.totalBytes) : 0.0;
            }
        });
    m_volumeList.SetContextMenuHandler([this](CPoint point) { ShowVolumeMenu(point); });
    m_tree.SetContextMenuHandler([this](CPoint point) { ShowTreeMenu(point); });

    m_libraryList.SetColumns({ { _T("Name"), 230 }, { _T("Type"), 110 }, { _T("Arch"), 55 }, { _T("Exports"), 65, true, R },
                               { _T("Depends On"), 80, true, R }, { _T("Version"), 110 }, { _T("Company"), 170 },
                               { _T("Size"), 75, true, R }, { _T("Modified"), 115, true }, { _T("Folder"), 420 } });
    m_libraryList.SetSource(
        [this](size_t row, int col) -> CString {
            const LibraryEntry& e = m_libraries[row];
            switch (col)
            {
            case colName:     return fmt::Wide(e.name);
            case colType:     return fmt::Wide(e.type);
            case colArch:     return fmt::Wide(e.architecture);
            case colExports:  return e.exports >= 0 ? fmt::Count(ULONGLONG(e.exports)) : CString();
            case colDeps:     return e.dependencies >= 0 ? fmt::Count(ULONGLONG(e.dependencies)) : CString();
            case colVersion:  return fmt::Wide(e.version);
            case colCompany:  return !e.company.empty() ? fmt::Wide(e.company) : fmt::Wide(e.error);
            case colSize:     return fmt::Bytes(double(e.size));
            case colModified: return fmt::Wide(FileTimeText(e.modified));
            default:          return fmt::Wide(e.folder);
            }
        },
        [this](size_t row, int col) -> double {
            const LibraryEntry& e = m_libraries[row];
            switch (col)
            {
            case colExports:  return double(e.exports);
            case colDeps:     return double(e.dependencies);
            case colSize:     return double(e.size);
            case colModified: return double(e.modified);
            default:          return 0.0;
            }
        },
        [this](size_t row) { return !m_libraries[row].error.empty() ? RGB(150, 150, 150) : COLORREF(CLR_DEFAULT); });
    m_libraryList.SetContextMenuHandler([this](CPoint point) { ShowLibraryMenu(point); });
    m_libraryList.Sort(colName, TRUE);

    m_details.SetOpenHandler([this](const std::wstring& path) { OpenPath(path); });
    m_details.ShowPlaceholder(_T("Select a library to see its exported functions, dependencies, version and signature."));

    // Initial size: 1320 x 880 at 96 DPI, capped to the work area, centred.
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &mi);
    const CRect work(mi.rcWork);
    SetWindowPos(nullptr, 0, 0, min(fmt::Dpi(1320), work.Width() * 95 / 100), min(fmt::Dpi(880), work.Height() * 95 / 100),
                 SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    LoadVolumes();
    LayoutControls();
    UpdateScanStatus();

    m_loader.Start(GetSafeHwnd());

    if (!m_startPath.empty())
        OpenPath(m_startPath);
    else if (HTREEITEM root = m_tree.GetRootItem())
        m_tree.Expand(root, TVE_EXPAND);

    return TRUE;
}

BOOL CLibScopeDlg::PreTranslateMessage(MSG* pMsg)
{
    if (m_hAccel && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST &&
        ::TranslateAccelerator(GetSafeHwnd(), m_hAccel, pMsg))
        return TRUE;
    return CDialogEx::PreTranslateMessage(pMsg);
}

void CLibScopeDlg::OnClose()
{
    StopScan();
    m_loader.Stop();
    EndDialog(IDCANCEL);
}

void CLibScopeDlg::OnDestroy()
{
    KillTimer(kScanTimer);
    KillTimer(kSelectionTimer);
    KillTimer(kFilterTimer);
    m_scanner.reset();   // cancels and joins the workers
    m_loader.Stop();
    CDialogEx::OnDestroy();
}

// ---------------------------------------------------------------------------
// Layout and splitters

void CLibScopeDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED)
        LayoutControls();
}

void CLibScopeDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    lpMMI->ptMinTrackSize.x = fmt::Dpi(860);
    lpMMI->ptMinTrackSize.y = fmt::Dpi(620);
    CDialogEx::OnGetMinMaxInfo(lpMMI);
}

void CLibScopeDlg::LayoutControls()
{
    if (!m_status.GetSafeHwnd())
        return;

    CRect rc;
    GetClientRect(&rc);
    const int cx = rc.Width(), cy = rc.Height();
    const int m = fmt::Dpi(8), gap = fmt::Dpi(6), labelH = fmt::Dpi(20), rowH = fmt::Dpi(26), statusH = fmt::Dpi(22);
    const int splitter = fmt::Dpi(6);

    int y = m;
    m_volumeSummary.MoveWindow(m, y, cx - 2 * m, labelH);
    y += labelH;

    // Tall enough for its rows, but never at the expense of the panes below.
    const int rows = max(int(m_volumes.size()), 1);
    const int volumesH = min(fmt::Dpi(26) + rows * fmt::Dpi(20) + fmt::Dpi(6), max(fmt::Dpi(70), cy / 6));
    m_volumeList.MoveWindow(m, y, cx - 2 * m, volumesH);
    y += volumesH + gap;

    // Toolbar.
    int x = m;
    const int buttonH = rowH - fmt::Dpi(2);
    m_scanButton.MoveWindow(x, y, fmt::Dpi(120), buttonH);
    x += fmt::Dpi(120) + gap;
    m_stopButton.MoveWindow(x, y, fmt::Dpi(70), buttonH);
    x += fmt::Dpi(70) + gap;
    m_includeExe.MoveWindow(x, y + fmt::Dpi(2), fmt::Dpi(92), buttonH - fmt::Dpi(4));
    x += fmt::Dpi(92) + gap;

    const int filterW = fmt::Dpi(200), filterLabelW = fmt::Dpi(36);
    const int filterX = cx - m - filterW;
    m_filter.MoveWindow(filterX, y, filterW, buttonH);
    m_filterLabel.MoveWindow(filterX - gap - filterLabelW, y + fmt::Dpi(4), filterLabelW, buttonH - fmt::Dpi(6));

    const int statusRight = filterX - 2 * gap - filterLabelW;
    const bool scanning = m_progress.IsWindowVisible() != FALSE;
    if (scanning)
    {
        m_progress.MoveWindow(x, y + fmt::Dpi(5), fmt::Dpi(100), buttonH - fmt::Dpi(10));
        x += fmt::Dpi(100) + gap;
    }
    m_scanStatus.MoveWindow(x, y + fmt::Dpi(4), max(statusRight - x, 0), buttonH - fmt::Dpi(6));
    y += rowH + fmt::Dpi(2);

    // The content area is split by two draggable gaps.
    const int bottom = cy - statusH - fmt::Dpi(2);
    m_contentTop = y;
    m_contentBottom = bottom;
    const int contentH = max(bottom - y, 0);

    const int minDetails = fmt::Dpi(160), minUpper = fmt::Dpi(110);
    int detailsH = int(contentH * m_detailFraction);
    detailsH = max(detailsH, minDetails);
    detailsH = min(detailsH, max(contentH - minUpper - splitter, minDetails));
    const int upperH = max(contentH - detailsH - splitter, 0);

    const int width = cx - 2 * m;
    int treeW = int(width * m_treeFraction);
    treeW = max(treeW, fmt::Dpi(140));
    treeW = min(treeW, max(width - fmt::Dpi(260), fmt::Dpi(140)));

    m_tree.MoveWindow(m, y, treeW, upperH);
    m_treeListGap.SetRect(m + treeW, y, m + treeW + splitter, y + upperH);
    m_libraryList.MoveWindow(m + treeW + splitter, y, max(width - treeW - splitter, 0), upperH);

    const int detailsY = y + upperH + splitter;
    m_listDetailsGap.SetRect(m, y + upperH, cx - m, detailsY);
    m_details.MoveWindow(m, detailsY, width, detailsH);

    m_status.MoveWindow(0, cy - statusH, cx, statusH);
}

CLibScopeDlg::Splitter CLibScopeDlg::HitSplitter(CPoint point) const
{
    if (m_treeListGap.PtInRect(point))
        return Splitter::TreeList;
    if (m_listDetailsGap.PtInRect(point))
        return Splitter::ListDetails;
    return Splitter::None;
}

BOOL CLibScopeDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    if (nHitTest == HTCLIENT && pWnd == this)
    {
        CPoint point;
        GetCursorPos(&point);
        ScreenToClient(&point);
        const Splitter hit = m_dragging != Splitter::None ? m_dragging : HitSplitter(point);
        if (hit != Splitter::None)
        {
            ::SetCursor(::LoadCursor(nullptr, hit == Splitter::TreeList ? IDC_SIZEWE : IDC_SIZENS));
            return TRUE;
        }
    }
    return CDialogEx::OnSetCursor(pWnd, nHitTest, message);
}

void CLibScopeDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
    m_dragging = HitSplitter(point);
    if (m_dragging != Splitter::None)
        SetCapture();
    else
        CDialogEx::OnLButtonDown(nFlags, point);
}

void CLibScopeDlg::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_dragging != Splitter::None && GetCapture() == this)
    {
        CRect rc;
        GetClientRect(&rc);
        const int m = fmt::Dpi(8);
        if (m_dragging == Splitter::TreeList)
        {
            const double width = max(rc.Width() - 2 * m, 1);
            m_treeFraction = min(max((point.x - m) / width, 0.08), 0.8);
        }
        else
        {
            const double height = max(m_contentBottom - m_contentTop, 1);
            m_detailFraction = min(max((m_contentBottom - point.y) / height, 0.15), 0.85);
        }
        LayoutControls();
        return;
    }
    CDialogEx::OnMouseMove(nFlags, point);
}

void CLibScopeDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_dragging != Splitter::None)
    {
        m_dragging = Splitter::None;
        ReleaseCapture();
        return;
    }
    CDialogEx::OnLButtonUp(nFlags, point);
}

void CLibScopeDlg::OnCaptureChanged(CWnd* pWnd)
{
    m_dragging = Splitter::None;
    CDialogEx::OnCaptureChanged(pWnd);
}

// ---------------------------------------------------------------------------
// Volumes

void CLibScopeDlg::LoadVolumes()
{
    m_volumes = EnumerateVolumes();
    m_volumeList.Reset(m_volumes.size());
    m_tree.SetVolumes(m_volumes);

    ULONGLONG total = 0, freeBytes = 0;
    for (const VolumeInfo& v : m_volumes)
    {
        total += v.totalBytes;
        freeBytes += v.freeBytes;
    }
    CString summary;
    summary.Format(_T("%u volume%s  |  Total capacity: %s  |  Free: %s   (click a volume to browse it, double-click to scan all of it)"),
                   unsigned(m_volumes.size()), m_volumes.size() == 1 ? _T("") : _T("s"),
                   fmt::Bytes(double(total)).GetString(), fmt::Bytes(double(freeBytes)).GetString());
    m_volumeSummary.SetWindowText(summary);
}

void CLibScopeDlg::OnVolumeClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    size_t row = 0;
    if (reinterpret_cast<NMITEMACTIVATE*>(pNMHDR)->iItem >= 0 && m_volumeList.GetSelectedRow(row))
        m_tree.NavigateTo(m_volumes[row].root);
}

void CLibScopeDlg::OnVolumeDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    size_t row = 0;
    if (reinterpret_cast<NMITEMACTIVATE*>(pNMHDR)->iItem >= 0 && m_volumeList.GetSelectedRow(row))
        StartScan(m_volumes[row].root, true);
}

void CLibScopeDlg::ShowVolumeMenu(CPoint point)
{
    size_t row = 0;
    if (!m_volumeList.GetSelectedRow(row))
        return;
    const std::wstring root = m_volumes[row].root;

    CMenu menu;
    menu.CreatePopupMenu();
    CString scan;
    scan.Format(_T("&Scan All of %s"), root.c_str());
    menu.AppendMenu(MF_STRING, cmdScan, scan);
    menu.AppendMenu(MF_STRING, cmdGoToFolder, _T("&Browse in Folder Tree"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, cmdOpenLocation, _T("Open in &Explorer"));
    menu.AppendMenu(MF_STRING, cmdCopyPath, _T("&Copy Path"));

    switch (menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, this))
    {
    case cmdScan:
        StartScan(root, true);
        break;
    case cmdGoToFolder:
        m_tree.NavigateTo(root);
        break;
    case cmdOpenLocation:
        fmt::ShowInExplorer(GetSafeHwnd(), root, false);
        break;
    case cmdCopyPath:
        fmt::CopyToClipboard(GetSafeHwnd(), fmt::Wide(root));
        break;
    }
}

// ---------------------------------------------------------------------------
// Scanning

void CLibScopeDlg::ClearLibraries()
{
    m_libraries.clear();
    m_libraryList.Reset(0);
}

void CLibScopeDlg::StartScan(const std::wstring& root, bool recursive)
{
    StopScan();
    ClearLibraries();

    m_listedRoot = root;
    m_listedRecursive = recursive;
    m_scanner = std::make_unique<CLibraryScanner>();
    m_scanner->Start(root, recursive, m_includeExe.GetCheck() == BST_CHECKED);

    m_scanButton.EnableWindow(FALSE);
    m_stopButton.EnableWindow(TRUE);
    m_progress.ShowWindow(SW_SHOW);
    m_progress.SetMarquee(TRUE, 30);
    LayoutControls();

    SetTimer(kScanTimer, kScanTimerMs, nullptr);
    UpdateScanStatus();
}

void CLibScopeDlg::StopScan()
{
    KillTimer(kScanTimer);
    if (!m_scanner)
        return;

    const bool wasDone = m_scanner->IsDone();
    m_scanner->Cancel();   // joins the workers
    PullScanResults();     // keep what was already found
    m_libraryList.Resort();

    if (!wasDone)
        m_lastSummary.Format(_T("Stopped: %s libraries found so far in %s."), fmt::Count(m_libraries.size()).GetString(), m_listedRoot.c_str());
    m_scanner.reset();

    m_scanButton.EnableWindow(TRUE);
    m_stopButton.EnableWindow(FALSE);
    m_progress.SetMarquee(FALSE, 0);
    m_progress.ShowWindow(SW_HIDE);
    LayoutControls();
    UpdateScanStatus();
}

void CLibScopeDlg::PullScanResults()
{
    if (!m_scanner)
        return;
    std::vector<LibraryEntry> batch;
    m_scanner->TakeNew(batch);
    if (batch.empty())
        return;
    m_libraries.insert(m_libraries.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
    m_libraryList.Append(m_libraries.size());
}

void CLibScopeDlg::UpdateScanStatus()
{
    CString text;
    if (m_scanner)
    {
        const auto p = m_scanner->GetProgress();
        text.Format(_T("Scanning: %s libraries, %s files in %s folders  |  %s"),
                    fmt::Count(p.libraries).GetString(), fmt::Count(p.filesSeen).GetString(), fmt::Count(p.folders).GetString(), p.currentPath.c_str());
    }
    else
        text = m_lastSummary.IsEmpty() ? CString(_T("Pick a folder to list its libraries.")) : m_lastSummary;

    CString old;
    m_scanStatus.GetWindowText(old);
    if (old != text)
        m_scanStatus.SetWindowText(text);

    CString status;
    status.Format(_T("  %s libraries listed  |  %s shown  |  Scanned for: %s%s  |  %s"),
                  fmt::Count(m_libraryList.ModelCount()).GetString(), fmt::Count(m_libraryList.VisibleCount()).GetString(),
                  LibraryExtensionList().c_str(), m_includeExe.GetCheck() == BST_CHECKED ? _T(", .exe") : _T(""),
                  theApp.IsElevated() ? _T("Running as administrator") : _T("Standard user: some folders can't be read"));
    m_status.SetWindowText(status);
}

void CLibScopeDlg::OnTimer(UINT_PTR nIDEvent)
{
    switch (nIDEvent)
    {
    case kScanTimer:
        PullScanResults();
        if (m_scanner && m_scanner->IsDone())
        {
            KillTimer(kScanTimer);
            const auto p = m_scanner->GetProgress();
            PullScanResults();
            m_libraryList.Resort();

            m_lastSummary.Format(_T("%s: %s %s among %s files in %s %s, %.1f s."),
                                 m_listedRoot.c_str(), fmt::Count(p.libraries).GetString(), p.libraries == 1 ? _T("library") : _T("libraries"),
                                 fmt::Count(p.filesSeen).GetString(), fmt::Count(p.folders).GetString(), p.folders == 1 ? _T("folder") : _T("folders"),
                                 m_scanner->ElapsedMs() / 1000.0);
            if (p.deniedFolders)
            {
                CString denied;
                denied.Format(_T("  %s unreadable%s."), fmt::Count(p.deniedFolders).GetString(),
                              theApp.IsElevated() ? _T("") : _T(" (run as administrator)"));
                m_lastSummary += denied;
            }
            m_scanner.reset();

            m_scanButton.EnableWindow(TRUE);
            m_stopButton.EnableWindow(FALSE);
            m_progress.SetMarquee(FALSE, 0);
            m_progress.ShowWindow(SW_HIDE);
            LayoutControls();
        }
        if (!m_pendingSelection.empty())
            SelectPendingFile();
        UpdateScanStatus();
        break;

    case kSelectionTimer:
        KillTimer(kSelectionTimer);
        OnLibrarySelected();
        break;

    case kFilterTimer:
    {
        KillTimer(kFilterTimer);
        CString text;
        m_filter.GetWindowText(text);
        text.Trim();
        m_libraryList.SetFilter(text);
        UpdateScanStatus();
        break;
    }

    default:
        CDialogEx::OnTimer(nIDEvent);
    }
}

void CLibScopeDlg::OnScanButton()
{
    std::wstring path = m_tree.SelectedPath();
    if (path.empty())
    {
        AfxMessageBox(_T("Select a drive or folder in the tree first."), MB_ICONINFORMATION);
        return;
    }
    StartScan(path, true);
}

void CLibScopeDlg::OnStopButton()
{
    StopScan();
}

void CLibScopeDlg::OnIncludeExe()
{
    if (!m_listedRoot.empty())
        StartScan(m_listedRoot, m_listedRecursive);
}

void CLibScopeDlg::OnFilterChanged()
{
    SetTimer(kFilterTimer, kFilterDelayMs, nullptr);
}

// ---------------------------------------------------------------------------
// Folder tree

void CLibScopeDlg::OnTreeSelChanged(NMHDR*, LRESULT* pResult)
{
    *pResult = 0;
    const std::wstring path = m_tree.SelectedPath();
    if (path.empty())
        return;

    // Browsing shouldn't throw away a long scan in progress: the list keeps showing it.
    if (m_scanner && m_scanner->IsRecursive())
        return;

    StartScan(path, false);
}

void CLibScopeDlg::ShowTreeMenu(CPoint point)
{
    const HTREEITEM item = m_tree.GetSelectedItem();
    const std::wstring path = m_tree.PathOf(item);
    if (path.empty())
        return;

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, cmdList, _T("&List Libraries in This Folder"));
    menu.AppendMenu(MF_STRING, cmdScan, _T("&Scan This Folder and Subfolders"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, cmdOpenLocation, _T("Open in &Explorer"));
    menu.AppendMenu(MF_STRING, cmdCopyPath, _T("&Copy Path"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, cmdRefreshFolder, _T("&Refresh Subfolders"));

    switch (menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, this))
    {
    case cmdList:
        StartScan(path, false);
        break;
    case cmdScan:
        StartScan(path, true);
        break;
    case cmdOpenLocation:
        fmt::ShowInExplorer(GetSafeHwnd(), path, false);
        break;
    case cmdCopyPath:
        fmt::CopyToClipboard(GetSafeHwnd(), fmt::Wide(path));
        break;
    case cmdRefreshFolder:
        m_tree.Refresh(item);
        break;
    }
}

// ---------------------------------------------------------------------------
// Library list and details

bool CLibScopeDlg::SelectedLibrary(const LibraryEntry*& entry) const
{
    size_t row = 0;
    if (!m_libraryList.GetSelectedRow(row) || row >= m_libraries.size())
        return false;
    entry = &m_libraries[row];
    return true;
}

void CLibScopeDlg::OnLibraryChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    const NMLISTVIEW* nm = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
    if (nm->uChanged & LVIF_STATE)
        SetTimer(kSelectionTimer, kSelectionDelayMs, nullptr);   // settles first, so arrow keys don't load every row
}

void CLibScopeDlg::OnLibrarySelected()
{
    const LibraryEntry* entry = nullptr;
    if (SelectedLibrary(entry))
        RequestDetails(entry->FullPath());
}

void CLibScopeDlg::RequestDetails(const std::wstring& path)
{
    if (_wcsicmp(path.c_str(), m_detailPath.c_str()) == 0)
        return;
    m_detailPath = path;
    m_details.ShowLoading(path);
    m_loader.Request(path, ++m_requestId);
}

LRESULT CLibScopeDlg::OnDetailsReady(WPARAM wParam, LPARAM)
{
    auto details = m_loader.TakeDetails();
    if (details && UINT(wParam) == m_requestId)
        m_details.SetDetails(std::move(details));
    return 0;
}

LRESULT CLibScopeDlg::OnSignatureReady(WPARAM wParam, LPARAM)
{
    auto signature = m_loader.TakeSignature();
    if (signature && UINT(wParam) == m_requestId)
        m_details.SetSignature(std::move(signature));
    return 0;
}

LRESULT CLibScopeDlg::OnDependenciesReady(WPARAM wParam, LPARAM)
{
    auto graph = m_loader.TakeDependencies();
    if (graph && UINT(wParam) == m_requestId)
        m_details.SetDependencies(std::move(graph));
    return 0;
}

void CLibScopeDlg::OnLibraryDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    if (reinterpret_cast<NMITEMACTIVATE*>(pNMHDR)->iItem >= 0)
        m_details.ShowTab(CDetailsPane::tabExports);
}

void CLibScopeDlg::ShowLibraryMenu(CPoint point)
{
    const LibraryEntry* selected = nullptr;
    if (!SelectedLibrary(selected))
        return;
    // Copied: a running scan can reallocate the rows while the menu is open.
    const LibraryEntry entry = *selected;
    const std::wstring path = entry.FullPath();

    const bool isArchive = entry.type == L"Import Library" || entry.type == L"Static Library";
    const bool readable = entry.error.empty();
    const UINT analysable = readable ? 0 : MF_GRAYED;

    const auto withCount = [](LPCTSTR text, long long count) {
        CString label(text);
        if (count >= 0)
            label += _T("  (") + fmt::Count(ULONGLONG(count)) + _T(")");
        return label;
    };

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, cmdSummary, _T("Show S&ummary"));
    menu.SetDefaultItem(cmdSummary);
    menu.AppendMenu(MF_STRING | analysable, cmdExports, withCount(isArchive ? _T("Show Public S&ymbols") : _T("Show E&xported Functions"), entry.exports));
    menu.AppendMenu(MF_STRING | analysable, cmdDependencies, withCount(isArchive ? _T("Show &Members") : _T("Show &Dependencies"), entry.dependencies));
    menu.AppendMenu(MF_STRING | analysable, cmdImports, isArchive ? _T("Show Symbols by Mem&ber") : _T("Show &Imported Functions"));
    if (!isArchive)
    {
        menu.AppendMenu(MF_STRING | analysable, cmdTree, _T("Show Dependency Tr&ee"));
        menu.AppendMenu(MF_STRING | analysable, cmdModules, _T("Show &All Modules in the Chain"));
        menu.AppendMenu(MF_STRING | analysable, cmdProblems, _T("Show Dependency Pro&blems"));
    }
    menu.AppendMenu(MF_STRING, cmdVersion, _T("Show &Version Information"));
    menu.AppendMenu(MF_STRING, cmdSignature, _T("Show Di&gital Signature"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | analysable, cmdSaveReport, _T("Save Full &Report..."));
    menu.AppendMenu(MF_STRING | analysable, cmdSaveExports, isArchive ? _T("Save Public Symbols as C&SV...") : _T("Save Exported Functions as C&SV..."));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, cmdOpenLocation, _T("Open File &Location"));
    menu.AppendMenu(MF_STRING, cmdGoToFolder, _T("Go to Folder in &Tree"));
    menu.AppendMenu(MF_STRING, cmdCopyPath, _T("&Copy Full Path"));
    menu.AppendMenu(MF_STRING, cmdCopyName, _T("Copy File &Name"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, cmdProperties, _T("Windows &Properties"));

    const UINT cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, this);

    // Right-click is itself a way to look at a library, so the load shouldn't wait for the selection timer.
    if (cmd >= cmdSummary && cmd <= cmdSignature)
    {
        KillTimer(kSelectionTimer);
        RequestDetails(path);
    }

    switch (cmd)
    {
    case cmdSummary:      m_details.ShowTab(CDetailsPane::tabSummary); break;
    case cmdExports:      m_details.ShowTab(CDetailsPane::tabExports); break;
    case cmdDependencies: m_details.ShowTab(CDetailsPane::tabDependencies); break;
    case cmdImports:      m_details.ShowTab(CDetailsPane::tabImports); break;
    case cmdTree:         m_details.ShowTab(CDetailsPane::tabTree); break;
    case cmdModules:      m_details.ShowTab(CDetailsPane::tabModules); break;
    case cmdProblems:     m_details.ShowTab(CDetailsPane::tabProblems); break;
    case cmdVersion:     m_details.ShowTab(CDetailsPane::tabVersion); break;
    case cmdSignature:    m_details.ShowTab(CDetailsPane::tabSignature); break;
    case cmdSaveReport:   SaveFullReport(path); break;
    case cmdSaveExports:  SaveExportsCsv(path); break;
    case cmdOpenLocation: fmt::ShowInExplorer(GetSafeHwnd(), path, true); break;
    case cmdCopyPath:     fmt::CopyToClipboard(GetSafeHwnd(), fmt::Wide(path)); break;
    case cmdCopyName:     fmt::CopyToClipboard(GetSafeHwnd(), fmt::Wide(entry.name)); break;
    case cmdProperties:
    {
        SHELLEXECUTEINFO info = { sizeof(info) };
        info.fMask = SEE_MASK_INVOKEIDLIST;
        info.hwnd = GetSafeHwnd();
        info.lpVerb = _T("properties");
        info.lpFile = path.c_str();
        info.nShow = SW_SHOW;
        ShellExecuteEx(&info);
        break;
    }
    case cmdGoToFolder:
        m_tree.NavigateTo(entry.folder);
        break;
    }
}

void CLibScopeDlg::SaveFullReport(const std::wstring& path)
{
    LibraryDetails details;
    SignatureInfo signature;
    std::shared_ptr<DependencyGraph> graph;
    {
        CWaitCursor wait;
        LoadDetails(path, details);
        FileSignatureVerifier::Verify(path, signature);
        if (details.isPe)
        {
            DependencyResolver resolver;
            graph = resolver.Resolve(path);
        }
    }
    CString target;
    if (!AskSavePath(this, DefaultFileName(details.entry.name, _T(".report.txt")), _T("txt"), _T("Text files (*.txt)|*.txt|All files (*.*)|*.*||"), target))
        return;
    if (!report::WriteUtf8File(target, report::FullReport(details, signature, graph.get())))
        AfxMessageBox(_T("The report could not be saved to that location."), MB_ICONERROR);
}

void CLibScopeDlg::SaveExportsCsv(const std::wstring& path)
{
    LibraryDetails details;
    {
        CWaitCursor wait;
        LoadDetails(path, details);
    }
    CString target;
    if (!AskSavePath(this, DefaultFileName(details.entry.name, _T(".exports.csv")), _T("csv"), _T("CSV files (*.csv)|*.csv|All files (*.*)|*.*||"), target))
        return;
    if (!report::WriteUtf8File(target, report::ExportsCsv(details)))
        AfxMessageBox(_T("The file could not be saved to that location."), MB_ICONERROR);
}

// ---------------------------------------------------------------------------
// Opening a path: from the File menu or the command line

bool CLibScopeDlg::SelectPendingFile()
{
    for (size_t r = 0; r < m_libraries.size(); ++r)
        if (_wcsicmp(m_libraries[r].FullPath().c_str(), m_pendingSelection.c_str()) == 0)
        {
            m_pendingSelection.clear();
            m_libraryList.SelectRow(r);
            OnLibrarySelected();
            return true;
        }
    return false;
}

void CLibScopeDlg::OpenPath(const std::wstring& rawPath)
{
    wchar_t full[32768] = {};
    const DWORD length = GetFullPathNameW(rawPath.c_str(), _countof(full), full, nullptr);
    const std::wstring path = length && length < _countof(full) ? std::wstring(full) : rawPath;

    const DWORD attributes = GetFileAttributesW(ToExtendedPath(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        CString message;
        message.Format(_T("This path could not be found:\n%s"), path.c_str());
        AfxMessageBox(message, MB_ICONERROR);
        return;
    }

    // A scan of the whole drive would otherwise ignore the tree selection made below.
    if (m_scanner && m_scanner->IsRecursive())
        StopScan();

    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        m_pendingSelection.clear();
        m_tree.NavigateTo(path);
        if (_wcsicmp(m_listedRoot.c_str(), path.c_str()) != 0)
            StartScan(path, false);
        return;
    }

    const size_t slash = path.find_last_of(L'\\');
    std::wstring folder = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    if (folder.size() == 2)
        folder += L'\\';

    // Show the file right away; the row is selected as soon as the folder listing reaches it.
    m_pendingSelection = path;
    RequestDetails(path);
    m_tree.NavigateTo(folder);

    const bool listedOrListing = _wcsicmp(m_listedRoot.c_str(), folder.c_str()) == 0;
    if (!listedOrListing)
        StartScan(folder, false);
    else if (!SelectPendingFile() && !m_scanner)
        m_pendingSelection.clear();   // listed already and absent: an unusual extension or a filter
}

void CLibScopeDlg::OnFileOpenFolder()
{
    CFolderPickerDialog dialog(nullptr, OFN_PATHMUSTEXIST, this);
    if (dialog.DoModal() == IDOK)
        OpenPath(std::wstring(dialog.GetPathName().GetString()));
}

void CLibScopeDlg::OnFileOpenLibrary()
{
    CFileDialog dialog(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                       _T("Libraries (*.dll;*.ocx;*.cpl;*.drv;*.sys;*.ax;*.acm;*.ime;*.tsp;*.rll;*.pyd;*.node;*.xll;*.winmd;*.lib;*.a)|")
                       _T("*.dll;*.ocx;*.cpl;*.drv;*.sys;*.ax;*.acm;*.ime;*.tsp;*.rll;*.pyd;*.node;*.xll;*.winmd;*.lib;*.a|")
                       _T("Executables (*.exe)|*.exe|All files (*.*)|*.*||"), this);
    if (dialog.DoModal() == IDOK)
        OpenPath(std::wstring(dialog.GetPathName().GetString()));
}

void CLibScopeDlg::OnFileSaveList()
{
    CString target;
    if (!AskSavePath(this, _T("libraries.csv"), _T("csv"), _T("CSV files (*.csv)|*.csv|All files (*.*)|*.*||"), target))
        return;

    CWaitCursor wait;
    CString text(_T("Name,Type,Architecture,Exports,Depends On,Version,Company,Size (bytes),Modified,Folder,Problem\r\n"));
    for (const LibraryEntry& e : m_libraries)
    {
        CString numbers;
        numbers.Format(_T("%I64d,%I64d"), e.exports, e.dependencies);
        CString size;
        size.Format(_T("%I64u"), e.size);
        text += report::CsvField(fmt::Wide(e.name)) + _T(",") + report::CsvField(fmt::Wide(e.type)) + _T(",") +
                report::CsvField(fmt::Wide(e.architecture)) + _T(",") + numbers + _T(",") +
                report::CsvField(fmt::Wide(e.version)) + _T(",") + report::CsvField(fmt::Wide(e.company)) + _T(",") + size + _T(",") +
                report::CsvField(fmt::Wide(FileTimeText(e.modified))) + _T(",") + report::CsvField(fmt::Wide(e.folder)) + _T(",") +
                report::CsvField(fmt::Wide(e.error)) + _T("\r\n");
    }
    if (!report::WriteUtf8File(target, text))
        AfxMessageBox(_T("The list could not be saved to that location."), MB_ICONERROR);
}

void CLibScopeDlg::OnFileExit()
{
    PostMessage(WM_CLOSE);
}

void CLibScopeDlg::OnViewRefresh()
{
    const std::wstring root = m_listedRoot;
    const bool recursive = m_listedRecursive;
    m_detailPath.clear();   // the selected file may have changed on disk
    LoadVolumes();
    LayoutControls();
    if (!root.empty())
        StartScan(root, recursive);
}

void CLibScopeDlg::OnHelpAbout()
{
    CAboutDlg dialog;
    dialog.DoModal();
}
