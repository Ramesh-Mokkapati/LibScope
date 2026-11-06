// DetailLoader.cpp
#include "pch.h"
#include "DetailLoader.h"

void CDetailLoader::Start(HWND notify)
{
    m_notify = notify;
    m_stop = false;
    m_thread = std::thread(&CDetailLoader::Run, this);
}

void CDetailLoader::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void CDetailLoader::Request(const std::wstring& path, UINT id)
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_path = path;
        m_id = id;
        m_hasRequest = true;
    }
    m_cv.notify_all();
}

std::shared_ptr<LibraryDetails> CDetailLoader::TakeDetails()
{
    std::lock_guard<std::mutex> lock(m_lock);
    return std::move(m_details);
}

std::shared_ptr<SignatureInfo> CDetailLoader::TakeSignature()
{
    std::lock_guard<std::mutex> lock(m_lock);
    return std::move(m_signature);
}

std::shared_ptr<DependencyGraph> CDetailLoader::TakeDependencies()
{
    std::lock_guard<std::mutex> lock(m_lock);
    return std::move(m_dependencies);
}

void CDetailLoader::Run()
{
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);

    for (;;)
    {
        std::wstring path;
        UINT id = 0;
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_cv.wait(lock, [this] { return m_stop || m_hasRequest; });
            if (m_stop)
                return;
            path = m_path;
            id = m_id;
            m_hasRequest = false;
        }

        auto details = std::make_shared<LibraryDetails>();
        LoadDetails(path, *details);
        const bool canVerify = details->entry.error.empty() || details->isPe;
        const bool hasChain = details->isPe;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_stop)
                return;
            if (id == m_id)
            {
                m_details = std::move(details);
                ::PostMessage(m_notify, WM_DETAILS_READY, id, 0);
            }
            // Arrow-keying through a list: skip the slow check for rows already left behind.
            if (m_hasRequest)
                continue;
        }

        auto signature = std::make_shared<SignatureInfo>();
        if (canVerify)
            FileSignatureVerifier::Verify(path, *signature);
        else
            signature->message = L"Not checked, because the file could not be read as a library.";

        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_stop)
                return;
            if (id == m_id)
            {
                m_signature = std::move(signature);
                ::PostMessage(m_notify, WM_SIGNATURE_READY, id, 0);
            }
            if (m_hasRequest || !hasChain)
                continue;
        }

        // Stops early as soon as the selection moves on, or the window closes.
        auto graph = m_resolver.Resolve(path, [this] {
            std::lock_guard<std::mutex> lock(m_lock);
            return m_stop || m_hasRequest;
        });

        std::lock_guard<std::mutex> lock(m_lock);
        if (m_stop)
            return;
        if (id == m_id && !graph->cancelled)
        {
            m_dependencies = std::move(graph);
            ::PostMessage(m_notify, WM_DEPENDENCIES_READY, id, 0);
        }
    }
}
