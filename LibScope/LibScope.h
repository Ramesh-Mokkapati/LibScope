// LibScope.h : application class
#pragma once

#include "resource.h"

class CLibScopeApp : public CWinAppEx
{
public:
    CLibScopeApp();

    BOOL InitInstance() override;
    bool IsElevated() const { return m_elevated; }

    DECLARE_MESSAGE_MAP()

private:
    static bool QueryElevation();
    bool m_elevated = false;
};

extern CLibScopeApp theApp;
