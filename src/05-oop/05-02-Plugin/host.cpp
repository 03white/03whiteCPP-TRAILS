// host.cpp
#include "IPlugin.h"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

// ---------- 跨平台动态库封装 ----------
#ifdef _WIN32
using LibHandle = HMODULE;
LibHandle LoadLib(const std::string& path) {
    return LoadLibraryA(path.c_str());
}
void* GetSym(LibHandle h, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(h, name));
}
void CloseLib(LibHandle h) {
    FreeLibrary(h);
}
const char* LibExt() {
    return ".dll";
}
#else
using LibHandle = void*;
LibHandle LoadLib(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* GetSym(LibHandle h, const char* name) {
    return dlsym(h, name);
}
void CloseLib(LibHandle h) {
    dlclose(h);
}
const char* LibExt() {
    return ".so";
}
#endif

int main(int argc, char** argv) {
    std::string pluginDir = (argc > 1) ? argv[1] : "plugins";

    if (!fs::exists(pluginDir) || !fs::is_directory(pluginDir)) {
        std::cerr << "插件目录不存在: " << pluginDir << "\n";
        return 1;
    }

    std::vector<LibHandle> handles;
    std::vector<IPlugin*> plugins;
    std::vector<DestroyPluginFn> destroyers;

    // 扫描目录，加载所有插件
    for (auto& entry : fs::directory_iterator(pluginDir)) {
        if (!entry.is_regular_file())
            continue;
        auto path = entry.path();
        if (path.extension().string() != LibExt())
            continue;

        std::string full = fs::absolute(path).string();
        LibHandle h = LoadLib(full);
        if (!h) {
            std::cerr << "[跳过] 加载失败: " << path.string() << "\n";
            continue;
        }

        auto create =
            reinterpret_cast<CreatePluginFn>(GetSym(h, "CreatePlugin"));
        auto destroy =
            reinterpret_cast<DestroyPluginFn>(GetSym(h, "DestroyPlugin"));

        if (!create || !destroy) {
            std::cerr << "[跳过] 缺少入口: " << path.string() << "\n";
            CloseLib(h);
            continue;
        }

        IPlugin* p = create();
        std::cout << "[加载] " << p->Name() << " - " << p->Description()
                  << "\n";

        handles.push_back(h);
        plugins.push_back(p);
        destroyers.push_back(destroy);
    }

    std::cout << "\n共加载 " << plugins.size() << " 个插件\n\n";

    double a = 3.0, b = 4.0;
    std::cout << "计算 " << a << " 和 " << b << ":\n";
    for (size_t i = 0; i < plugins.size(); ++i) {
        std::cout << "  " << plugins[i]->Name() << "(" << a << ", " << b
                  << ") = " << plugins[i]->Calculate(a, b) << "\n";
    }

    // 先销毁对象，再卸载库
    for (size_t i = 0; i < plugins.size(); ++i)
        destroyers[i](plugins[i]);
    for (auto h : handles)
        CloseLib(h);

    return 0;
}