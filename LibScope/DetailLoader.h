// DetailLoader.h : loads the selected library off the UI thread.
//
// Delivery happens in stages, because signature checks and the full dependency chain can take far
// longer than parsing: the tables appear immediately and the other tabs fill in as each stage ends.
#pragma once

#include "DependencyResolver.h"
#include "FileSignatureVerifier.h"
#include "LibraryAnalyzer.h"

// wParam: request id; the UI ignores any id that is not its latest request.
#define WM_DETAILS_READY      (WM_APP + 30)
#define WM_SIGNATURE_READY    (WM_APP + 31)
#define WM_DEPENDENCIES_READY (WM_APP + 32)

class CDetailLoader
{
public:
    CDetailLoader() = default;
    ~CDetailLoader() { Stop(); }

    void Start(HWND notify);
    void Stop();

    // A newer request replaces one that hasn't started yet.
    void Request(const std::wstring& path, UINT id);

    std::shared_ptr<LibraryDetails> TakeDetails();
    std::shared_ptr<SignatureInfo>  TakeSignature();
    std::shared_ptr<DependencyGraph> TakeDependencies();

private:
    void Run();

    HWND                    m_notify = nullptr;
    std::thread             m_thread;
    std::mutex              m_lock;
    std::condition_variable m_cv;
    bool                    m_stop = false;
    bool                    m_hasRequest = false;
    std::wstring            m_path;
    UINT                    m_id = 0;

    std::shared_ptr<LibraryDetails> m_details;
    std::shared_ptr<SignatureInfo>  m_signature;
    std::shared_ptr<DependencyGraph> m_dependencies;
    DependencyResolver              m_resolver;   // worker thread only; its cache speeds up later requests
};
