#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DiskSnapshot {
    std::wstring name;
    unsigned long long totalBytes{};
    unsigned long long freeBytes{};
};

struct SystemSnapshot {
    double cpuUsagePercent{};
    unsigned long long memoryTotalBytes{};
    unsigned long long memoryUsedBytes{};
    std::vector<DiskSnapshot> disks;
};

class SystemCollector {
public:
    SystemCollector();

    SystemSnapshot collect();

private:
    std::uint64_t lastIdleTime_{};
    std::uint64_t lastKernelTime_{};
    std::uint64_t lastUserTime_{};
    bool hasLastCpuTimes_{};

    double collectCpuUsage();
    void collectMemory(SystemSnapshot& snapshot) const;
    void collectDisks(SystemSnapshot& snapshot) const;
};