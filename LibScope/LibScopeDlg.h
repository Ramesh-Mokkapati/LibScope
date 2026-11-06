// LibScopeDlg.h : main window - volumes, folder tree, libraries found, details of the selected one.
#pragma once

#include "DetailLoader.h"
#include "DetailsPane.h"
#include "FolderTree.h"
#include "LibraryScanner.h"
#include "VirtualList.h"
#include "VolumeEnumerator.h"
#include "resource.h"

class CLibScopeDlg : public CDialogEx
{
public:
    explicit CLibScopeDlg(const std::wstring& startPath = std::wstring());

protected:
    BOOL OnInitDialog() override;
    BOOL PreTranslateMessage(MSG* pMsg) override;
    void OnOK() override {}       // Enter must not close the window
    void OnCancel() override {}   // nor Escape

    afx_msg void OnClose();
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnCaptureChanged(CWnd* pWnd);

    afx_msg void OnScanButton();
    afx_msg void OnStopButton();
    afx_msg void OnIncludeExe();
    afx_msg void OnFilterChanged();
    afx_msg void OnTreeSelChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLibraryChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLibraryDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnVolumeClick(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnVolumeDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg LRESULT OnDetailsReady(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnSignatureReady(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnDependenciesReady(WPARAM wParam, LPARAM lParam);

    afx_msg void OnFileOpenFolder();
    afx_msg void OnFileOpenLibrary();
    afx_msg void OnFileSaveList();
    afx_msg void OnFileExit();
    afx_msg void OnViewRefresh();
    afx_msg void OnHelpAbout();
    DECLARE_MESSAGE_MAP()

private:
    enum class Splitter { None, TreeList, ListDetails };

    void LayoutControls();
    Splitter HitSplitter(CPoint point) const;

    void LoadVolumes();
    void StartScan(const std::wstring& root, bool recursive);
    void StopScan();
    void PullScanResults();
    void UpdateScanStatus();
    void ClearLibraries();

    void OnLibrarySelected();
    void RequestDetails(const std::wstring& path);
    bool SelectPendingFile();
    void OpenPath(const std::wstring& path);

    void ShowLibraryMenu(CPoint point);
    void ShowTreeMenu(CPoint point);
    void ShowVolumeMenu(CPoint point);

    void SaveFullReport(const std::wstring& path);
    void SaveExportsCsv(const std::wstring& path);
    bool SelectedLibrary(const LibraryEntry*& entry) const;

    HICON         m_hIcon = nullptr;
    HACCEL        m_hAccel = nullptr;
    CFont         m_font;
    std::wstring  m_startPath;

    CStatic       m_volumeSummary;
    CVirtualList  m_volumeList;
    CButton       m_scanButton;
    CButton       m_stopButton;
    CButton       m_includeExe;
    CProgressCtrl m_progress;
    CStatic       m_scanStatus;
    CStatic       m_filterLabel;
    CEdit         m_filter;
    CFolderTree   m_tree;
    CVirtualList  m_libraryList;
    CDetailsPane  m_details;
    CStatic       m_status;

    std::vector<VolumeInfo>          m_volumes;
    std::vector<LibraryEntry>        m_libraries;
    std::unique_ptr<CLibraryScanner> m_scanner;
    std::wstring                     m_listedRoot;     // what the list currently shows
    bool                             m_listedRecursive = false;
    CString                          m_lastSummary;
    std::wstring                     m_pendingSelection;  // file to select once the scan lists it

    CDetailLoader m_loader;
    UINT          m_requestId = 0;
    std::wstring  m_detailPath;     // library shown, or being loaded, in the details pane

    double        m_treeFraction = 0.24;     // of the width
    double        m_detailFraction = 0.42;   // of the height below the toolbar
    CRect         m_treeListGap;
    CRect         m_listDetailsGap;
    Splitter      m_dragging = Splitter::None;
    int           m_contentTop = 0;
    int           m_contentBottom = 0;
};
