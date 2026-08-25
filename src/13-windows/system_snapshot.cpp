#include "system_snapshot.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>

namespace {
std::uint64_t toUInt64(const FILETIME& fileTime)
{
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return value.QuadPart;
}
}

SystemCollector::SystemCollector()
{
    collectCpuUsage();
}

SystemSnapshot SystemCollector::collect()
{
    SystemSnapshot snapshot{};
    snapshot.cpuUsagePercent = collectCpuUsage();
    collectMemory(snapshot);
    collectDisks(snapshot);
    return snapshot;
}

double SystemCollector::collectCpuUsage()
{
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};

    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return 0.0;
    }

    const auto idle = toUInt64(idleTime);
    const auto kernel = toUInt64(kernelTime);
    const auto user = toUInt64(userTime);

    if (!hasLastCpuTimes_) {
        lastIdleTime_ = idle;
        lastKernelTime_ = kernel;
        lastUserTime_ = user;
        hasLastCpuTimes_ = true;
        return 0.0;
    }

    const auto idleDelta = idle - lastIdleTime_;
    const auto kernelDelta = kernel - lastKernelTime_;
    const auto userDelta = user - lastUserTime_;
    const auto totalDelta = kernelDelta + userDelta;

    lastIdleTime_ = idle;
    lastKernelTime_ = kernel;
    lastUserTime_ = user;

    if (totalDelta == 0) {
        return 0.0;
    }

    const auto busyDelta = totalDelta > idleDelta ? totalDelta - idleDelta : 0;
    const auto usage = static_cast<double>(busyDelta) * 100.0 / static_cast<double>(totalDelta);
    return std::clamp(usage, 0.0, 100.0);
}

void SystemCollector::collectMemory(SystemSnapshot& snapshot) const
{
    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);

    if (!GlobalMemoryStatusEx(&memoryStatus)) {
        return;
    }

    snapshot.memoryTotalBytes = memoryStatus.ullTotalPhys;
    snapshot.memoryUsedBytes = memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys;
}

void SystemCollector::collectDisks(SystemSnapshot& snapshot) const
{
    const DWORD bufferLength = GetLogicalDriveStringsW(0, nullptr);
    if (bufferLength == 0) {
        return;
    }

    std::vector<wchar_t> buffer(bufferLength + 1);
    if (GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data()) == 0) {
        return;
    }

    for (const wchar_t* drive = buffer.data(); *drive != L'\0'; drive += std::wcslen(drive) + 1) {
        const UINT driveType = GetDriveTypeW(drive);
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
            continue;
        }

        ULARGE_INTEGER freeBytesAvailable{};
        ULARGE_INTEGER totalBytes{};
        ULARGE_INTEGER totalFreeBytes{};
        if (!GetDiskFreeSpaceExW(drive, &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
            continue;
        }

        DiskSnapshot disk{};
        disk.name = drive;
        disk.totalBytes = totalBytes.QuadPart;
        disk.freeBytes = totalFreeBytes.QuadPart;
        snapshot.disks.push_back(disk);
    }
}