// DetailsPane.cpp
#include "pch.h"
#include "DetailsPane.h"
#include "Format.h"

namespace
{
    enum { IDC_FILTER = 5001, IDC_TABS, IDC_LIST_FIRST };
    enum { kFilterTimer = 1, kFilterDelayMs = 200 };
    enum { cmdCopy = 1, cmdCopyName, cmdCopyAll, cmdSelectAll, cmdShowFunctions, cmdAnalyse, cmdShowInTree };

    const COLORREF kMuted = RGB(110, 110, 110);
    const COLORREF kBad = RGB(190, 0, 0);
    const COLORREF kGood = RGB(0, 120, 40);
}

BEGIN_MESSAGE_MAP(CDetailsPane, CWnd)
    ON_WM_SIZE()
    ON_WM_CTLCOLOR()
    ON_WM_TIMER()
    ON_EN_CHANGE(IDC_FILTER, &CDetailsPane::OnFilterChanged)
END_MESSAGE_MAP()

BOOL CDetailsPane::CreatePane(CWnd* pParent, UINT nID, CFont* pFont)
{
    m_pFont = pFont;
    LPCTSTR cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), ::GetSysColorBrush(COLOR_BTNFACE), nullptr);
    if (!CWnd::Create(cls, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, CRect(0, 0, 10, 10), pParent, nID))
        return FALSE;

    if (!m_title.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_PATHELLIPSIS, CRect(0, 0, 10, 10), this) ||
        !m_filterLabel.Create(_T("Find in details:"), WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX, CRect(0, 0, 10, 10), this) ||
        !m_filter.CreateEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, IDC_FILTER))
        return FALSE;
    m_filter.SetCueBanner(_T("function or DLL name"));

    if (!m_tabs.Create(CMFCTabCtrl::STYLE_3D_ONENOTE, CRect(0, 0, 10, 10), this, IDC_TABS, CMFCTabCtrl::LOCATION_TOP))
        return FALSE;
    m_tabs.EnableTabSwap(FALSE);
    m_tabs.AutoDestroyWindow(FALSE);   // the lists are members of this pane

    using C = CVirtualList::Column;
    const int R = LVCFMT_RIGHT;
    const std::vector<C> properties = { { _T("Property"), 190 }, { _T("Value"), 760 } };

    for (int i = 0; i < tabCount; ++i)
        if (i != tabTree && !m_lists[i].CreateList(&m_tabs, IDC_LIST_FIRST + i, true))
            return FALSE;
    if (!m_dependencyPanel.CreatePanel(&m_tabs, IDC_LIST_FIRST + tabTree, m_pFont))
        return FALSE;

    m_lists[tabSummary].SetColumns(properties);
    m_lists[tabExports].SetColumns({ { _T("Ordinal"), 70, true, R }, { _T("Name"), 300 }, { _T("Undecorated Name"), 340 },
                                     { _T("RVA"), 90 }, { _T("Forwarded To / Defined In"), 240 } });
    m_lists[tabDependencies].SetColumns({ { _T("DLL or Member"), 340 }, { _T("Functions"), 80, true, R }, { _T("Kind"), 180 } });
    m_lists[tabImports].SetColumns({ { _T("From"), 300 }, { _T("Function"), 380 }, { _T("Ordinal"), 70, true, R }, { _T("Kind"), 150 } });
    m_lists[tabVersion].SetColumns(properties);
    m_lists[tabSignature].SetColumns(properties);
    m_lists[tabModules].SetColumns({ { _T("Module"), 230 }, { _T("Status"), 150 }, { _T("Found Via"), 120 }, { _T("Arch"), 55 },
                                     { _T("Version"), 110 }, { _T("Company"), 170 }, { _T("Full Path"), 420 } });
    m_lists[tabProblems].SetColumns({ { _T("Severity"), 75 }, { _T("Problem"), 900 } });

    m_lists[tabModules].SetSource(
        [this](size_t row, int col) -> CString {
            const DepModule& m = m_graph->modules[row];
            switch (col)
            {
            case 0: return fmt::Wide(m.name);
            case 1: return report::ModuleStatusText(m);
            case 2: return fmt::Wide(m.foundVia);
            case 3: return m.machine ? fmt::Wide(MachineName(m.machine)) : CString();
            case 4: return fmt::Wide(m.version);
            case 5: return fmt::Wide(m.company);
            default: return !m.path.empty() ? fmt::Wide(m.path) : fmt::Wide(m.detail);
            }
        },
        nullptr,
        [this](size_t row) {
            const DepModule& m = m_graph->modules[row];
            if (m.status == DepStatus::Ok)
                return COLORREF(CLR_DEFAULT);
            if (m.status == DepStatus::ApiSetNoHost)
                return m.required ? kBad : kMuted;
            return m.required ? kBad : RGB(190, 100, 0);
        });
    m_lists[tabProblems].SetSource(
        [this](size_t row, int col) -> CString {
            const DepProblem& p = m_graph->problems[row];
            return col == 0 ? report::SeverityText(p.severity) : fmt::Wide(p.text);
        },
        nullptr,
        [this](size_t row) {
            const DepSeverity s = m_graph->problems[row].severity;
            return s == DepSeverity::Error ? kBad : s == DepSeverity::Warning ? RGB(190, 100, 0) : kMuted;
        });

    // Property tabs keep their logical order; the data tabs start sorted the useful way.
    m_lists[tabExports].SetSource(
        [this](size_t row, int col) -> CString {
            const ExportedFunction& e = m_details->exports[row];
            CString s;
            switch (col)
            {
            case 0: if (e.ordinal) s.Format(_T("%u"), e.ordinal); return s;
            case 1: return report::ExportNameText(e);
            case 2: return e.demangledName != e.name ? fmt::Wide(ToWide(e.demangledName)) : CString();
            case 3: if (e.rva && !e.isForwarded) s.Format(_T("0x%08X"), e.rva); return s;
            default: return fmt::Wide(ToWide(e.forwardName));
            }
        },
        [this](size_t row, int) { return double(m_details->exports[row].ordinal); },
        [this](size_t row) { return m_details->exports[row].isForwarded ? RGB(70, 90, 150) : COLORREF(CLR_DEFAULT); });

    m_lists[tabDependencies].SetSource(
        [this](size_t row, int col) -> CString {
            const DllDependency& dep = m_details->dependencies[row];
            switch (col)
            {
            case 0: return fmt::Wide(ToWide(dep.dllName));
            case 1: return fmt::Count(dep.functions.size());
            default:
                if (m_details->isArchive)
                    return m_details->isImportLibrary ? CString(_T("Import target")) : CString(_T("Object member"));
                return dep.isDelayLoaded ? CString(_T("Delay-loaded")) : CString(_T("Loaded at startup"));
            }
        },
        [this](size_t row, int) { return double(m_details->dependencies[row].functions.size()); },
        [this](size_t row) { return m_details->dependencies[row].isDelayLoaded ? RGB(70, 90, 150) : COLORREF(CLR_DEFAULT); });

    m_lists[tabImports].SetSource(
        [this](size_t row, int col) -> CString {
            const ImportRow& ir = m_importRows[row];
            const DllDependency& dep = m_details->dependencies[ir.dependency];
            const ImportedFunction& f = dep.functions[ir.function];
            CString s;
            switch (col)
            {
            case 0: return fmt::Wide(ToWide(dep.dllName));
            case 1: return report::ImportNameText(f);
            case 2: if (f.isOrdinal) s.Format(_T("%u"), f.ordinal); return s;
            default:
                if (m_details->isArchive)
                    return CString(_T("Public symbol"));
                return dep.isDelayLoaded ? CString(_T("Delay-loaded")) : CString(_T("Loaded at startup"));
            }
        },
        [this](size_t row, int) {
            const ImportRow& ir = m_importRows[row];
            return double(m_details->dependencies[ir.dependency].functions[ir.function].ordinal);
        });

    const auto propertyText = [](const std::vector<report::Property>& rows) {
        return [&rows](size_t row, int col) { return col == 0 ? rows[row].first : rows[row].second; };
    };
    m_lists[tabSummary].SetSource(propertyText(m_summaryRows), nullptr, [this](size_t row) {
        const CString& name = m_summaryRows[row].first;
        if (name == _T("Problem"))
            return kBad;
        if (name == _T("Dependency check") && m_graph && m_graph->failure.empty())
            return m_graph->errors ? kBad : m_graph->warnings ? RGB(190, 100, 0) : kGood;
        return COLORREF(CLR_DEFAULT);
    });
    m_lists[tabVersion].SetSource(propertyText(m_versionRows));
    m_lists[tabSignature].SetSource(propertyText(m_signatureRows), nullptr, [this](size_t row) {
        if (m_signatureRows[row].first != _T("Status"))
            return COLORREF(CLR_DEFAULT);
        return !m_signature ? kMuted : m_signature->valid ? kGood : kBad;
    });

    static const LPCTSTR kTitles[tabCount] = { _T("Summary"), _T("Exported Functions"), _T("Dependencies"), _T("Imported Functions"),
                                               _T("Dependency Tree"), _T("All Modules"), _T("Problems"), _T("Version"), _T("Digital Signature") };
    for (int i = 0; i < tabCount; ++i)
    {
        if (i == tabTree)
        {
            m_tabs.AddTab(&m_dependencyPanel, kTitles[i], UINT(-1), FALSE);
            continue;
        }
        CVirtualList& list = m_lists[i];
        const Tab tab = Tab(i);
        list.SetContextMenuHandler([this, &list, tab](CPoint point) { ShowListMenu(list, point, tab); });
        m_tabs.AddTab(&list, kTitles[i], UINT(-1), FALSE);
    }
    m_lists[tabExports].Sort(1, TRUE);
    m_lists[tabDependencies].Sort(0, TRUE);
    m_lists[tabImports].Sort(0, TRUE);
    m_tabs.SetActiveTab(tabSummary);

    m_title.SetFont(m_pFont, FALSE);
    m_filterLabel.SetFont(m_pFont, FALSE);
    m_filter.SetFont(m_pFont, FALSE);
    m_tabs.SetFont(m_pFont);
    for (int i = 0; i < tabCount; ++i)
        if (i != tabTree)
            m_lists[i].SetFont(m_pFont, FALSE);

    LOGFONT lf = {};
    m_pFont->GetLogFont(&lf);
    lf.lfWeight = FW_BOLD;
    m_boldFont.CreateFontIndirect(&lf);
    m_title.SetFont(&m_boldFont, FALSE);

    m_created = true;
    CRect rc;
    GetClientRect(&rc);
    Layout(rc.Width(), rc.Height());
    return TRUE;
}

void CDetailsPane::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    if (m_created && cx > 0 && cy > 0)
        Layout(cx, cy);
}

HBRUSH CDetailsPane::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH hbr = CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
    if (nCtlColor == CTLCOLOR_STATIC)
    {
        pDC->SetBkColor(::GetSysColor(COLOR_BTNFACE));
        return ::GetSysColorBrush(COLOR_BTNFACE);
    }
    return hbr;
}

void CDetailsPane::Layout(int cx, int cy)
{
    const int rowH = fmt::Dpi(24);
    const int filterW = fmt::Dpi(220), labelW = fmt::Dpi(96), gap = fmt::Dpi(6);

    const int filterX = max(cx - filterW, 0);
    const int labelX = max(filterX - gap - labelW, 0);
    m_filter.MoveWindow(filterX, 0, min(filterW, cx), rowH - fmt::Dpi(2));
    m_filterLabel.MoveWindow(labelX, fmt::Dpi(3), labelW, rowH - fmt::Dpi(6));
    m_title.MoveWindow(0, fmt::Dpi(3), max(labelX - gap, 0), rowH - fmt::Dpi(6));
    m_tabs.MoveWindow(0, rowH, cx, max(cy - rowH, 0));
}

void CDetailsPane::ShowPlaceholder(const CString& text)
{
    m_details.reset();
    m_signature.reset();
    m_title.SetWindowText(text);
    ReloadAll();
}

void CDetailsPane::ShowLoading(const std::wstring& path)
{
    // The previous library stays on screen until the new one arrives, which avoids a flicker per row.
    CString title;
    title.Format(_T("%s  (loading...)"), path.c_str());
    m_title.SetWindowText(title);
}

void CDetailsPane::SetDetails(std::shared_ptr<LibraryDetails> details)
{
    m_details = std::move(details);
    m_signature.reset();
    m_title.SetWindowText(fmt::Wide(m_details->entry.FullPath()));
    ReloadAll();
}

void CDetailsPane::SetSignature(std::shared_ptr<SignatureInfo> signature)
{
    m_signature = std::move(signature);
    m_signatureRows = report::SignatureProperties(*m_signature);
    m_lists[tabSignature].Reset(m_signatureRows.size());
}

void CDetailsPane::SetOpenHandler(std::function<void(const std::wstring&)> handler)
{
    m_onOpen = handler;
    m_dependencyPanel.SetOpenHandler(std::move(handler));
}

void CDetailsPane::SetDependencyCheckRow(const CString& value)
{
    for (report::Property& row : m_summaryRows)
        if (row.first == _T("Dependency check"))
        {
            row.second = value;
            m_lists[tabSummary].Invalidate(FALSE);
            return;
        }
}

void CDetailsPane::SetDependencies(std::shared_ptr<DependencyGraph> graph)
{
    m_graph = std::move(graph);
    m_lists[tabModules].Reset(m_graph ? m_graph->modules.size() : 0);
    m_lists[tabProblems].Reset(m_graph ? m_graph->problems.size() : 0);
    m_dependencyPanel.SetGraph(m_graph);
    if (m_graph)
        SetDependencyCheckRow(fmt::Wide(m_graph->SummaryText()));
    UpdateTabLabels();
}

void CDetailsPane::ReloadAll()
{
    m_importRows.clear();
    m_summaryRows.clear();
    m_versionRows.clear();
    m_signatureRows.clear();

    // The rows read m_graph, so the lists are emptied before it goes.
    m_lists[tabModules].Reset(0);
    m_lists[tabProblems].Reset(0);
    m_graph.reset();
    m_dependencyPanel.ShowMessage(m_details && m_details->isPe ? CString(_T("Following the dependency chain..."))
                                  : m_details ? CString(_T("Only PE files such as DLLs have a dependency chain."))
                                              : CString());

    if (m_details)
    {
        m_summaryRows = report::SummaryProperties(*m_details);
        if (m_details->isPe)
            m_summaryRows.emplace_back(CString(_T("Dependency check")), CString(_T("Following the chain...")));
        m_versionRows = report::VersionProperties(*m_details);
        m_signatureRows.emplace_back(CString(_T("Status")), CString(_T("Verifying...")));
        for (size_t d = 0; d < m_details->dependencies.size(); ++d)
            for (size_t f = 0; f < m_details->dependencies[d].functions.size(); ++f)
                m_importRows.push_back({ d, f });
    }

    m_lists[tabSummary].Reset(m_summaryRows.size());
    m_lists[tabExports].Reset(m_details ? m_details->exports.size() : 0);
    m_lists[tabDependencies].Reset(m_details ? m_details->dependencies.size() : 0);
    m_lists[tabImports].Reset(m_importRows.size());
    m_lists[tabVersion].Reset(m_versionRows.size());
    m_lists[tabSignature].Reset(m_signatureRows.size());
    UpdateTabLabels();
}

void CDetailsPane::UpdateTabLabels()
{
    const bool archive = m_details && m_details->isArchive;
    const auto label = [](LPCTSTR name, size_t count, bool show) {
        CString text(name);
        if (show)
            text += _T(" (") + fmt::Count(count) + _T(")");
        return text;
    };
    const bool loaded = m_details != nullptr;
    m_tabs.SetTabLabel(tabExports, label(archive ? _T("Public Symbols") : _T("Exported Functions"), m_details ? m_details->exports.size() : 0, loaded));
    m_tabs.SetTabLabel(tabDependencies, label(archive ? _T("Members") : _T("Dependencies"), m_details ? m_details->dependencies.size() : 0, loaded));
    m_tabs.SetTabLabel(tabImports, label(archive ? _T("Symbols by Member") : _T("Imported Functions"), m_importRows.size(), loaded));
    m_tabs.SetTabLabel(tabModules, label(_T("All Modules"), m_graph ? m_graph->modules.size() : 0, m_graph != nullptr));
    m_tabs.SetTabLabel(tabProblems, label(_T("Problems"), m_graph ? m_graph->errors + m_graph->warnings : 0, m_graph != nullptr));
    m_tabs.RecalcLayout();
    m_tabs.Invalidate(FALSE);
}

void CDetailsPane::ShowTab(Tab tab)
{
    m_tabs.SetActiveTab(tab);
    if (tab == tabTree)
        m_dependencyPanel.SetFocus();
    else
        m_lists[tab].SetFocus();
}

void CDetailsPane::OnFilterChanged()
{
    SetTimer(kFilterTimer, kFilterDelayMs, nullptr);
}

void CDetailsPane::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent != kFilterTimer)
    {
        CWnd::OnTimer(nIDEvent);
        return;
    }
    KillTimer(kFilterTimer);
    CString text;
    m_filter.GetWindowText(text);
    text.Trim();
    // The property tabs are short and always shown in full.
    m_lists[tabExports].SetFilter(text);
    m_lists[tabDependencies].SetFilter(text);
    m_lists[tabImports].SetFilter(text);
    m_lists[tabModules].SetFilter(text);
    m_lists[tabProblems].SetFilter(text);
    m_dependencyPanel.SetFilter(text);
}

void CDetailsPane::ShowListMenu(CVirtualList& list, CPoint point, Tab tab)
{
    size_t row = 0;
    const bool hasRow = list.GetSelectedRow(row);
    const bool isNamedList = tab == tabExports || tab == tabImports || tab == tabDependencies || tab == tabModules;

    CMenu menu;
    menu.CreatePopupMenu();
    if (tab == tabDependencies)
    {
        CString text(_T("&Show Its Functions"));
        menu.AppendMenu(MF_STRING | (hasRow ? 0 : MF_GRAYED), cmdShowFunctions, text);
        menu.AppendMenu(MF_SEPARATOR);
    }
    if (tab == tabModules)
    {
        const bool hasPath = hasRow && !m_graph->modules[row].path.empty();
        menu.AppendMenu(MF_STRING | (hasPath ? 0 : MF_GRAYED), cmdAnalyse, _T("Anal&yse This Module"));
        menu.AppendMenu(MF_SEPARATOR);
    }
    if (tab == tabProblems)
    {
        menu.AppendMenu(MF_STRING | (hasRow ? 0 : MF_GRAYED), cmdShowInTree, _T("Show in Dependency &Tree"));
        menu.AppendMenu(MF_SEPARATOR);
    }
    menu.AppendMenu(MF_STRING | (hasRow ? 0 : MF_GRAYED), cmdCopy, _T("&Copy Selected Rows\tCtrl+C"));
    if (isNamedList)
        menu.AppendMenu(MF_STRING | (hasRow ? 0 : MF_GRAYED), cmdCopyName, _T("Copy &Name"));
    menu.AppendMenu(MF_STRING, cmdCopyAll, _T("Copy &All Visible Rows"));
    menu.AppendMenu(MF_STRING, cmdSelectAll, _T("&Select All\tCtrl+A"));

    switch (menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, this))
    {
    case cmdCopy:
        fmt::CopyToClipboard(GetSafeHwnd(), list.SelectedRowsText());
        break;
    case cmdCopyName:
        fmt::CopyToClipboard(GetSafeHwnd(), list.CellText(row, tab == tabImports ? 1 : tab == tabExports ? 1 : 0));
        break;
    case cmdCopyAll:
        fmt::CopyToClipboard(GetSafeHwnd(), list.AllRowsText());
        break;
    case cmdSelectAll:
        list.SelectAll();
        break;
    case cmdShowFunctions:
        // Filtering by the DLL's name narrows Imported Functions to that DLL.
        m_filter.SetWindowText(list.CellText(row, 0));
        OnTimer(kFilterTimer);
        ShowTab(tabImports);
        break;
    case cmdAnalyse:
        if (m_onOpen)
            m_onOpen(m_graph->modules[row].path);
        break;
    case cmdShowInTree:
    {
        const DepProblem& problem = m_graph->problems[row];
        ShowTab(tabTree);
        m_dependencyPanel.GoTo(problem.importer, problem.edge);
        break;
    }
    }
}
