// VolumeEnumerator.h : local disk volumes (the WinMon "Disk Volumes" query, plus removable drives)
#pragma once

struct VolumeInfo
{
    std::wstring root;        // "C:\"
    CString      label;
    CString      fileSystem;
    CString      driveType;
    ULONGLONG    totalBytes = 0;
    ULONGLONG    freeBytes = 0;
};

std::vector<VolumeInfo> EnumerateVolumes();
