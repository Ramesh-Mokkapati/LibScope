// FolderTree.cpp
#include "pch.h"
#include "FolderTree.h"
#include "PeImage.h"

namespace
{
    int ShellIcon(LPCTSTR path, DWORD attributes)
    {
        SHFILEINFO info = {};
        ::SHGetFileInfo(path, attributes, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | (attributes ? SHGFI_USEFILEATTRIBUTES : 0));
        return info.iIcon;
    }

    std::wstring NormalizeFolder(std::wstring path)
    {
        while (path.size() > 3 && path.back() == L'\\')
            path.pop_back();
        return path;
    }

    bool StartsWithFolder(const std::wstring& path, const std::wstring& folder)
    {
        if (path.size() < folder.size() || _wcsnicmp(path.c_str(), folder.c_str(), folder.size()) != 0)
            return false;
        return path.size() == folder.size() || folder.back() == L'\\' || path[folder.size()] == L'\\';
    }
}

BEGIN_MESSAGE_MAP(CFolderTree, CTreeCtrl)
    ON_NOTIFY_REFLECT(TVN_ITEMEXPANDING, &CFolderTree::OnItemExpanding)
    ON_NOTIFY_REFLECT(NM_RCLICK, &CFolderTree::OnRClick)
    ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

BOOL CFolderTree::CreateTree(CWnd* pParent, UINT nID)
{
    const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS;
    if (!Create(style, CRect(0, 0, 10, 10), pParent, nID))
        return FALSE;

    SHFILEINFO info = {};
    const HIMAGELIST images = reinterpret_cast<HIMAGELIST>(
        ::SHGetFileInfo(_T("C:\\"), 0, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
    if (images)
    {
        // The system image list is shared: never let this control destroy it.
        ::SendMessage(GetSafeHwnd(), TVM_SETIMAGELIST, TVSIL_NORMAL, reinterpret_cast<LPARAM>(images));
    }
    m_folderImage = ShellIcon(_T("folder"), FILE_ATTRIBUTE_DIRECTORY);
    return TRUE;
}

HTREEITEM CFolderTree::InsertFolder(HTREEITEM parent, const std::wstring& text, const std::wstring& path, int image)
{
    m_paths.push_back(path);

    TVINSERTSTRUCT insert = {};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN | TVIF_PARAM;
    insert.item.pszText = const_cast<LPTSTR>(text.c_str());
    insert.item.iImage = image;
    insert.item.iSelectedImage = image;
    insert.item.cChildren = 1;   // corrected when the folder is first expanded
    insert.item.lParam = reinterpret_cast<LPARAM>(&m_paths.back());
    return InsertItem(&insert);
}

void CFolderTree::SetVolumes(const std::vector<VolumeInfo>& volumes)
{
    DeleteAllItems();
    m_paths.clear();
    m_loaded.clear();

    for (const VolumeInfo& v : volumes)
    {
        std::wstring text = v.root;
        if (!v.label.IsEmpty())
            text += L"  " + std::wstring(v.label.GetString());
        InsertFolder(TVI_ROOT, text, v.root, ShellIcon(v.root.c_str(), 0));
    }
    for (const std::wstring& root : m_extraRoots)
        InsertFolder(TVI_ROOT, root, root, m_folderImage);
}

HTREEITEM CFolderTree::AddRoot(const std::wstring& path)
{
    const std::wstring normalized = NormalizeFolder(path);
    m_extraRoots.push_back(normalized);
    return InsertFolder(TVI_ROOT, normalized, normalized, m_folderImage);
}

std::wstring CFolderTree::PathOf(HTREEITEM item) const
{
    if (!item)
        return std::wstring();
    const auto* path = reinterpret_cast<const std::wstring*>(GetItemData(item));
    return path ? *path : std::wstring();
}

std::wstring CFolderTree::SelectedPath() const
{
    return PathOf(GetSelectedItem());
}

bool CFolderTree::IsLoaded(HTREEITEM item) const
{
    return m_loaded.count(item) != 0;
}

void CFolderTree::MarkLoaded(HTREEITEM item)
{
    m_loaded.insert(item);
}

void CFolderTree::LoadChildren(HTREEITEM item)
{
    if (IsLoaded(item))
        return;
    MarkLoaded(item);

    const std::wstring folder = PathOf(item);
    const std::wstring pattern = (!folder.empty() && folder.back() == L'\\' ? folder : folder + L"\\") + L"*";

    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW(ToExtendedPath(pattern).c_str(), FindExInfoBasic, &fd,
                                    FindExSearchLimitToDirectories, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    bool denied = false;
    if (hFind == INVALID_HANDLE_VALUE)
    {
        const DWORD err = GetLastError();
        denied = err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES;
    }
    else
    {
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if ((fd.cFileName[0] == L'.' && fd.cFileName[1] == 0) ||
                (fd.cFileName[0] == L'.' && fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))
                continue;
            // Same rule as the scanner: junctions and symlinks only lead somewhere already shown.
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                (fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK))
                continue;
            names.push_back(fd.cFileName);
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });

    SetRedraw(FALSE);
    const std::wstring base = !folder.empty() && folder.back() == L'\\' ? folder : folder + L"\\";
    for (const std::wstring& name : names)
        InsertFolder(item, name, base + name, m_folderImage);
    SetRedraw(TRUE);

    if (names.empty())
    {
        TVITEM tv = {};
        tv.hItem = item;
        tv.mask = TVIF_CHILDREN;
        tv.cChildren = 0;
        SetItem(&tv);
    }
    if (denied)
    {
        CString text = GetItemText(item);
        text += _T("  (access denied)");
        SetItemText(item, text);
    }
}

void CFolderTree::OnItemExpanding(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    const NMTREEVIEW* nm = reinterpret_cast<NMTREEVIEW*>(pNMHDR);
    if (nm->action == TVE_EXPAND)
    {
        CWaitCursor wait;
        LoadChildren(nm->itemNew.hItem);
    }
}

void CFolderTree::Refresh(HTREEITEM item)
{
    if (!item || !IsLoaded(item))
        return;
    SetRedraw(FALSE);
    Expand(item, TVE_COLLAPSE);

    std::function<void(HTREEITEM)> removeBelow = [&](HTREEITEM parent) {
        for (HTREEITEM child = GetChildItem(parent); child;)
        {
            const HTREEITEM next = GetNextSiblingItem(child);
            removeBelow(child);
            m_loaded.erase(child);
            DeleteItem(child);
            child = next;
        }
    };
    removeBelow(item);
    m_loaded.erase(item);

    TVITEM tv = {};
    tv.hItem = item;
    tv.mask = TVIF_CHILDREN;
    tv.cChildren = 1;
    SetItem(&tv);
    SetRedraw(TRUE);
}

HTREEITEM CFolderTree::FindRootFor(const std::wstring& path) const
{
    for (HTREEITEM root = GetRootItem(); root; root = GetNextSiblingItem(root))
        if (StartsWithFolder(path, NormalizeFolder(PathOf(root))))
            return root;
    return nullptr;
}

bool CFolderTree::NavigateTo(const std::wstring& rawPath)
{
    const std::wstring path = NormalizeFolder(rawPath);
    HTREEITEM item = FindRootFor(path);
    if (!item)
        item = AddRoot(path);

    for (;;)
    {
        const std::wstring current = NormalizeFolder(PathOf(item));
        if (_wcsicmp(current.c_str(), path.c_str()) == 0)
            break;

        LoadChildren(item);
        Expand(item, TVE_EXPAND);

        HTREEITEM next = nullptr;
        for (HTREEITEM child = GetChildItem(item); child; child = GetNextSiblingItem(child))
            if (StartsWithFolder(path, PathOf(child)))
            {
                next = child;
                break;
            }
        if (!next)
            break;   // not reachable through the tree (hidden behind a junction): stop at the deepest known folder
        item = next;
    }

    SelectItem(item);
    EnsureVisible(item);
    return true;
}

void CFolderTree::OnRClick(NMHDR*, LRESULT* pResult)
{
    // Without this a tree control doesn't raise WM_CONTEXTMENU at the click position.
    CPoint point;
    GetCursorPos(&point);
    SendMessage(WM_CONTEXTMENU, reinterpret_cast<WPARAM>(GetSafeHwnd()), MAKELPARAM(point.x, point.y));
    *pResult = 1;
}

void CFolderTree::OnContextMenu(CWnd*, CPoint point)
{
    if (!m_onMenu)
        return;

    if (point.x == -1 && point.y == -1)
    {
        CRect rc;
        const HTREEITEM sel = GetSelectedItem();
        if (!sel || !GetItemRect(sel, &rc, TRUE))
            return;
        point = rc.CenterPoint();
        ClientToScreen(&point);
    }
    else
    {
        CPoint client = point;
        ScreenToClient(&client);
        UINT flags = 0;
        const HTREEITEM hit = HitTest(client, &flags);
        if (!hit)
            return;
        SelectItem(hit);
    }
    m_onMenu(point);
}
