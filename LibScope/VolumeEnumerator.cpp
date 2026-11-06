// VolumeEnumerator.cpp
#include "pch.h"
#include "VolumeEnumerator.h"

std::vector<VolumeInfo> EnumerateVolumes()
{
    std::vector<VolumeInfo> volumes;

    WCHAR drives[512] = {};
    const DWORD len = GetLogicalDriveStringsW(_countof(drives) - 1, drives);
    if (len == 0 || len >= _countof(drives))
        return volumes;

    // Don't let an empty card reader pop up a "no disk" error box.
    DWORD oldMode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode);

    for (const WCHAR* root = drives; *root; root += wcslen(root) + 1)
    {
        // Network shares are left out: a disconnected one would block the UI here for many seconds.
        // File > Open Folder still reaches them.
        const UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
            continue;

        ULARGE_INTEGER avail = {}, total = {}, totalFree = {};
        if (!GetDiskFreeSpaceExW(root, &avail, &total, &totalFree))
            continue;

        VolumeInfo vi;
        vi.root = root;
        vi.driveType = type == DRIVE_FIXED ? _T("Fixed") : _T("Removable");
        vi.totalBytes = total.QuadPart;
        vi.freeBytes = totalFree.QuadPart;

        WCHAR label[MAX_PATH + 1] = {}, fs[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(root, label, _countof(label), nullptr, nullptr, nullptr, fs, _countof(fs)))
        {
            vi.label = label;
            vi.fileSystem = fs;
        }
        volumes.push_back(std::move(vi));
    }

    SetThreadErrorMode(oldMode, nullptr);
    return volumes;
}
