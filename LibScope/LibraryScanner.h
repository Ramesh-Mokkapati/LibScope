// LibraryScanner.h : multi-threaded search for libraries under a folder.
// Same worker design as WinMon's CFolderScanner; files are analysed on the pool as well,
// so one huge folder such as System32 still uses every thread.
#pragma once

#include "LibraryAnalyzer.h"

class CLibraryScanner
{
public:
    struct Progress
    {
        ULONGLONG    folders = 0;
        ULONGLONG    filesSeen = 0;
        ULONGLONG    libraries = 0;
        ULONGLONG    deniedFolders = 0;
        std::wstring currentPath;
    };

    CLibraryScanner() = default;
    ~CLibraryScanner();
    CLibraryScanner(const CLibraryScanner&) = delete;
    CLibraryScanner& operator=(const CLibraryScanner&) = delete;

    void Start(const std::wstring& root, bool recursive, bool includeExecutables);
    void Cancel();

    bool IsDone() const { return m_done; }
    bool IsRecursive() const { return m_recursive; }
    const std::wstring& Root() const { return m_root; }
    ULONGLONG ElapsedMs() const;
    Progress GetProgress() const;

    // Moves out the rows found since the previous call.
    void TakeNew(std::vector<LibraryEntry>& out);

private:
    struct WorkItem
    {
        std::wstring folder;     // set for a folder to list
        LibraryEntry entry;      // set for a file to analyse
        bool         isFile = false;
    };

    void Coordinator();
    void Worker();
    void ListFolder(const std::wstring& folder);
    void AnalyzeFile(LibraryEntry& entry);

    std::wstring            m_root;
    bool                    m_recursive = false;
    bool                    m_includeExecutables = false;

    std::thread             m_coordinator;
    std::mutex              m_queueLock;
    std::condition_variable m_queueCv;
    std::deque<WorkItem>    m_queue;
    size_t                  m_pending = 0;      // queued + being worked on

    std::atomic<bool>       m_cancel { false };
    std::atomic<bool>       m_done { false };
    std::atomic<ULONGLONG>  m_folders { 0 };
    std::atomic<ULONGLONG>  m_files { 0 };
    std::atomic<ULONGLONG>  m_libraries { 0 };
    std::atomic<ULONGLONG>  m_denied { 0 };
    std::atomic<ULONGLONG>  m_startTick { 0 };
    std::atomic<ULONGLONG>  m_endTick { 0 };

    mutable std::mutex      m_resultLock;
    std::vector<LibraryEntry> m_results;
    std::wstring            m_currentPath;
};
