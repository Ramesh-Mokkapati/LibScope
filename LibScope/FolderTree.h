// FolderTree.h : drives and folders, with each level loaded when it is first expanded.
#pragma once

#include "VolumeEnumerator.h"

class CFolderTree : public CTreeCtrl
{
public:
    using MenuFn = std::function<void(CPoint screenPoint)>;

    BOOL CreateTree(CWnd* pParent, UINT nID);
    void SetContextMenuHandler(MenuFn handler) { m_onMenu = std::move(handler); }

    // Rebuilds the top level. Roots added with AddRoot are kept.
    void SetVolumes(const std::vector<VolumeInfo>& volumes);

    // Adds a top-level folder, for a place outside the local volumes such as a network share.
    HTREEITEM AddRoot(const std::wstring& path);

    // Path of an item or of the selection; empty if there is none.
    std::wstring PathOf(HTREEITEM item) const;
    std::wstring SelectedPath() const;

    // Expands down to `path` and selects it. Unknown locations become a new root.
    bool NavigateTo(const std::wstring& path);

    // Re-reads the children of a loaded item (and clears cached children below it).
    void Refresh(HTREEITEM item);

protected:
    afx_msg void OnItemExpanding(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    afx_msg void OnRClick(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    HTREEITEM InsertFolder(HTREEITEM parent, const std::wstring& text, const std::wstring& path, int image);
    void      LoadChildren(HTREEITEM item);
    bool      IsLoaded(HTREEITEM item) const;
    void      MarkLoaded(HTREEITEM item);
    HTREEITEM FindRootFor(const std::wstring& path) const;

    int                       m_folderImage = 0;
    std::deque<std::wstring>  m_paths;      // stable storage behind each item's data pointer
    std::unordered_set<void*> m_loaded;     // items whose children have been read
    std::vector<std::wstring> m_extraRoots;
    MenuFn                    m_onMenu;
};
