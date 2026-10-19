// IPlugin.h
#pragma once

#ifdef _WIN32
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// 纯虚接口：宿主只认这个
class IPlugin {
public:
    virtual ~IPlugin() {
    }
    virtual const char* Name() const = 0;
    virtual const char* Description() const = 0;
    virtual double Calculate(double a, double b) const = 0;
};

// 每个插件 DLL 必须导出的入口函数类型
using CreatePluginFn = IPlugin*(void);
using DestroyPluginFn = void(IPlugin*);

// 插件实现里用这个宏导出入口
#define EXPORT_PLUGIN(ImplClass)                                               \
    PLUGIN_EXPORT IPlugin* CreatePlugin() {                                    \
        return new ImplClass();                                                \
    }                                                                          \
    PLUGIN_EXPORT void DestroyPlugin(IPlugin* p) {                             \
        delete p;                                                              \
    }