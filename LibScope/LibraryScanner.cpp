// LibraryScanner.cpp
#include "pch.h"
#include "LibraryScanner.h"

namespace
{
    std::wstring TrimTrailingSlash(std::wstring path)
    {
        while (path.size() > 3 && path.back() == L'\\')
            path.pop_back();
        return path;
    }

    std::wstring JoinPath(const std::wstring& folder, const wchar_t* name)
    {
        return !folder.empty() && folder.back() == L'\\' ? folder + name : folder + L"\\" + name;
    }
}

CLibraryScanner::~CLibraryScanner()
{
    Cancel();
}

void CLibraryScanner::Start(const std::wstring& root, bool recursive, bool includeExecutables)
{
    m_root = TrimTrailingSlash(root);
    m_recursive = recursive;
    m_includeExecutables = includeExecutables;
    m_startTick = GetTickCount64();
    m_coordinator = std::thread(&CLibraryScanner::Coordinator, this);
}

void CLibraryScanner::Cancel()
{
    m_cancel = true;
    m_queueCv.notify_all();
    if (m_coordinator.joinable())
        m_coordinator.join();
}

ULONGLONG CLibraryScanner::ElapsedMs() const
{
    const ULONGLONG end = m_endTick ? m_endTick.load() : GetTickCount64();
    return end - m_startTick;
}

CLibraryScanner::Progress CLibraryScanner::GetProgress() const
{
    Progress p;
    p.folders = m_folders;
    p.filesSeen = m_files;
    p.libraries = m_libraries;
    p.deniedFolders = m_denied;
    std::lock_guard<std::mutex> lock(m_resultLock);
    p.currentPath = m_currentPath;
    return p;
}

void CLibraryScanner::TakeNew(std::vector<LibraryEntry>& out)
{
    std::lock_guard<std::mutex> lock(m_resultLock);
    out = std::move(m_results);
    m_results.clear();
}

void CLibraryScanner::Coordinator()
{
    {
        std::lock_guard<std::mutex> lock(m_queueLock);
        WorkItem item;
        item.folder = m_root;
        m_queue.push_back(std::move(item));
        m_pending = 1;
    }

    // Enumeration and small reads are latency-bound, so a few parallel workers help a lot on SSDs.
    const unsigned workers = max(2u, min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < workers; ++i)
        pool.emplace_back(&CLibraryScanner::Worker, this);
    for (auto& t : pool)
        t.join();

    m_endTick = GetTickCount64();
    m_done = true;
}

void CLibraryScanner::Worker()
{
    // An empty card reader or removable drive mustn't raise a system error box from a worker.
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);

    for (;;)
    {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lock(m_queueLock);
            m_queueCv.wait(lock, [this] { return m_cancel || !m_queue.empty() || m_pending == 0; });
            if (m_cancel || (m_queue.empty() && m_pending == 0))
                return;
            item = std::move(m_queue.front());
            m_queue.pop_front();
        }

        if (item.isFile)
            AnalyzeFile(item.entry);
        else
            ListFolder(item.folder);

        std::lock_guard<std::mutex> lock(m_queueLock);
        if (--m_pending == 0)
            m_queueCv.notify_all();   // everything is done: release the idle workers
    }
}

void CLibraryScanner::ListFolder(const std::wstring& folder)
{
    if ((m_folders.fetch_add(1) & 0x3F) == 0)
    {
        std::lock_guard<std::mutex> lock(m_resultLock);
        m_currentPath = folder;
    }

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW(ToExtendedPath(JoinPath(folder, L"*")).c_str(), FindExInfoBasic, &fd,
                                    FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
            ++m_denied;
        return;
    }

    std::vector<WorkItem> items;
    do
    {
        if (m_cancel)
            break;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!m_recursive ||
                (fd.cFileName[0] == L'.' && fd.cFileName[1] == 0) ||
                (fd.cFileName[0] == L'.' && fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))
                continue;
            // Junctions, symlinks and mount points lead to data found elsewhere (or loop).
            // Other reparse tags, such as OneDrive's on its folders, are ordinary folders here.
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                (fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK))
                continue;

            WorkItem item;
            item.folder = JoinPath(folder, fd.cFileName);
            items.push_back(std::move(item));
        }
        else
        {
            m_files.fetch_add(1, std::memory_order_relaxed);
            if (!IsLibraryFileName(fd.cFileName, m_includeExecutables))
                continue;

            WorkItem item;
            item.isFile = true;
            item.entry.name = fd.cFileName;
            item.entry.folder = folder;
            item.entry.size = (ULONGLONG(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            item.entry.modified = (ULONGLONG(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
            items.push_back(std::move(item));
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (!items.empty() && !m_cancel)
    {
        std::lock_guard<std::mutex> lock(m_queueLock);
        m_pending += items.size();
        for (auto& item : items)
            m_queue.push_back(std::move(item));
        m_queueCv.notify_all();
    }
}

void CLibraryScanner::AnalyzeFile(LibraryEntry& entry)
{
    AnalyzeQuick(entry);
    ++m_libraries;
    std::lock_guard<std::mutex> lock(m_resultLock);
    m_results.push_back(std::move(entry));
}
