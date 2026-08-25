#include "system_snapshot.h"

#include <windows.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {
double toGiB(unsigned long long bytes)
{
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    return static_cast<double>(bytes) / gib;
}

double percent(unsigned long long used, unsigned long long total)
{
    if (total == 0) {
        return 0.0;
    }
    return static_cast<double>(used) * 100.0 / static_cast<double>(total);
}

void clearConsole()
{
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console == INVALID_HANDLE_VALUE) {
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO screenBufferInfo{};
    if (!GetConsoleScreenBufferInfo(console, &screenBufferInfo)) {
        return;
    }

    const DWORD cellCount = static_cast<DWORD>(screenBufferInfo.dwSize.X) * screenBufferInfo.dwSize.Y;
    DWORD writtenCount = 0;
    COORD homeCoords{0, 0};
    FillConsoleOutputCharacterW(console, L' ', cellCount, homeCoords, &writtenCount);
    FillConsoleOutputAttribute(console, screenBufferInfo.wAttributes, cellCount, homeCoords, &writtenCount);
    SetConsoleCursorPosition(console, homeCoords);
}

void printSnapshot(const SystemSnapshot& snapshot)
{
    const auto memoryPercent = percent(snapshot.memoryUsedBytes, snapshot.memoryTotalBytes);

    std::wcout << L"Windows PC data monitor" << L"\n";
    std::wcout << L"Press Ctrl+C to exit" << L"\n\n";

    std::wcout << std::fixed << std::setprecision(1);
    std::wcout << L"CPU usage: " << snapshot.cpuUsagePercent << L"%\n";
    std::wcout << L"Memory: " << toGiB(snapshot.memoryUsedBytes) << L" GiB / "
               << toGiB(snapshot.memoryTotalBytes) << L" GiB (" << memoryPercent << L"%)\n\n";

    std::wcout << L"Disks:\n";
    for (const auto& disk : snapshot.disks) {
        const auto usedBytes = disk.totalBytes - disk.freeBytes;
        std::wcout << L"  " << disk.name << L"  " << toGiB(usedBytes) << L" GiB / "
                   << toGiB(disk.totalBytes) << L" GiB (" << percent(usedBytes, disk.totalBytes) << L"%)\n";
    }
}
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SystemCollector collector;

    while (true) {
        const auto snapshot = collector.collect();
        clearConsole();
        printSnapshot(snapshot);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
}