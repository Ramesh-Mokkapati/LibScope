// DetailsPane.h : everything known about the selected library, one tab per kind of information.
#pragma once

#include "DependencyPanel.h"
#include "DetailLoader.h"
#include "Report.h"
#include "VirtualList.h"

class CDetailsPane : public CWnd
{
public:
    enum Tab { tabSummary, tabExports, tabDependencies, tabImports, tabTree, tabModules, tabProblems, tabVersion, tabSignature, tabCount };

    BOOL CreatePane(CWnd* pParent, UINT nID, CFont* pFont);

    void ShowPlaceholder(const CString& text);
    void ShowLoading(const std::wstring& path);
    void SetDetails(std::shared_ptr<LibraryDetails> details);
    void SetSignature(std::shared_ptr<SignatureInfo> signature);
    void SetDependencies(std::shared_ptr<DependencyGraph> graph);

    // Called to analyse a module chosen in one of the dependency tabs.
    void SetOpenHandler(std::function<void(const std::wstring&)> handler);

    void ShowTab(Tab tab);

    const LibraryDetails* Details() const { return m_details.get(); }

protected:
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnFilterChanged();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    DECLARE_MESSAGE_MAP()

private:
    struct ImportRow
    {
        size_t dependency;
        size_t function;
    };

    void Layout(int cx, int cy);
    void ReloadAll();
    void UpdateTabLabels();
    void ShowListMenu(CVirtualList& list, CPoint point, Tab tab);
    void SetDependencyCheckRow(const CString& value);

    std::function<void(const std::wstring&)> m_onOpen;
    CDependencyPanel m_dependencyPanel;
    std::shared_ptr<DependencyGraph> m_graph;

    CFont*        m_pFont = nullptr;
    CFont         m_boldFont;
    CStatic       m_title;
    CStatic       m_filterLabel;
    CEdit         m_filter;
    CMFCTabCtrl   m_tabs;
    CVirtualList  m_lists[tabCount];
    bool          m_created = false;

    std::shared_ptr<LibraryDetails> m_details;
    std::shared_ptr<SignatureInfo>  m_signature;
    std::vector<report::Property>   m_summaryRows;
    std::vector<report::Property>   m_versionRows;
    std::vector<report::Property>   m_signatureRows;
    std::vector<ImportRow>          m_importRows;
};
