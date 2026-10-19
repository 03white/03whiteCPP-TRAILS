# 可插拔插件设计

## 1. 什么是可插拔

**可插拔（Pluggable）** 指的是一种软件架构：把程序拆成**宿主（Host）** 和**插件（Plugin）** 两部分，
两者通过一份**约定好的接口**通信，接口不变，零件随便换。

- **宿主**：主程序，负责运行主流程、发现并加载插件，但它**不知道**插件的具体实现。
- **插件**：一个个独立的功能模块，实现宿主约定的接口，可以随时增删而不需要重新编译宿主。

判断一套设计是不是"可插拔"，看三点：

1. **接口稳定**：宿主只依赖抽象接口，不依赖任何具体实现。
2. **实现独立**：每个插件单独编译成独立文件（`.dll` / `.so`），互不影响。
3. **运行时装配**：宿主在运行时扫描、加载、调用，插件挂了宿主也能继续跑。

典型例子：VS Code 的扩展、浏览器的插件、Photoshop 的滤镜、游戏 Mod。

### 可插拔 vs 普通多态

普通的 C++ 多态要求所有派生类在**编译期**就参与链接，宿主必须知道每个实现类。
可插拔则要求插件类在**运行期**才出现，宿主编译时甚至不存在这些类，因此必须借助**动态库 + 显式链接**来完成装配。

## 2. C++ 实现可插拔的基础与 API 详解

C++ 实现可插拔，本质是把"面向对象的多态"和"操作系统动态库"结合：

- 用**纯虚接口**定义宿主和插件之间的契约（语言层）。
- 用**动态库**隔离实现，让插件能在运行时被加载（系统层）。
- 用**工厂函数 + `extern "C"`** 把 C++ 对象"导"给宿主（桥梁层）。

### 2.1 纯虚接口：语言层的契约

```cpp
class IPlugin {
public:
    virtual ~IPlugin() {}
    virtual const char* Name() const = 0;
    virtual const char* Description() const = 0;
    virtual double Calculate(double a, double b) const = 0;
};
```

- `= 0` 表示**纯虚函数**，`IPlugin` 是抽象类，不能直接实例化，只能被插件继承实现。
- 宿主只持有 `IPlugin*`，通过**虚函数表(vtable)** 调用，运行期自动派发到插件实现。
- 虚析构 `virtual ~IPlugin()` 不加也可以编译，但 `delete p` 时会走错析构，**必须加**。

### 2.2 导出宏：解决符号可见性

```cpp
#ifdef _WIN32
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
```

三层含义：

| 部分 | 作用 |
| --- | --- |
| `extern "C"` | 关闭 C++ 名称修饰（name mangling），保证符号名就是 `CreatePlugin`，宿主才能 `GetProcAddress("CreatePlugin")` 找到它 |
| `__declspec(dllexport)` | Windows MSVC 下把符号导出到 DLL 的导出表 |
| `__attribute__((visibility("default")))` | GCC/Clang 下把符号标记为外部可见（配合 `-fvisibility=hidden` 时非常关键） |

> 名称修饰：C++ 为了支持重载，会把 `void foo(int)` 修饰成类似 `_Z3fooi` 的名字。
> `extern "C"` 强制用 C 的命名规则，符号名保持原样，这是跨模块查找符号的前提。

### 2.3 工厂函数：跨模块创建对象

```cpp
using CreatePluginFn  = IPlugin*(void);
using DestroyPluginFn = void(IPlugin*);
```

动态库和宿主是两份独立编译的模块，**不能直接 `new` 插件类**，也不能跨模块 `delete`。
解决办法是让插件导出两个 C 函数：

- `CreatePlugin()`：在**插件内部** `new` 出对象并返回接口指针。
- `DestroyPlugin(IPlugin*)`：在**插件内部** `delete`，保证分配和释放用的是同一个堆。

这样内存管理始终留在插件侧，宿主只碰接口，不会因堆不一致而崩溃。

### 2.4 EXPORT_PLUGIN 宏：插件侧的收尾

```cpp
#define EXPORT_PLUGIN(ImplClass)                       \
    PLUGIN_EXPORT IPlugin* CreatePlugin() {            \
        return new ImplClass();                        \
    }                                                  \
    PLUGIN_EXPORT void DestroyPlugin(IPlugin* p) {     \
        delete p;                                      \
    }
```

插件作者只需要写实现类，再写一行 `EXPORT_PLUGIN(AddPlugin)`，导出样板就自动生成。

### 2.5 动态加载 API：系统层

宿主不链接插件，靠操作系统 API 在运行时打开动态库、查找符号：

| 功能 | Windows (MSVC) | POSIX (Linux/macOS) |
| --- | --- | --- |
| 加载库 | `LoadLibraryA(path)` | `dlopen(path, RTLD_NOW \| RTLD_LOCAL)` |
| 取符号 | `GetProcAddress(h, "CreatePlugin")` | `dlsym(h, "CreatePlugin")` |
| 卸载库 | `FreeLibrary(h)` | `dlclose(h)` |
| 库后缀 | `.dll` | `.so` |

注意点：

- Linux 链接时要加 `-ldl`（老 glibc 需要，新 glibc 已并入 libc）。
- `RTLD_NOW`：加载时立即解析全部符号，出错能马上发现；`RTLD_LOCAL`：符号不导出给其他库，避免冲突。
- 返回值是 `void*`，需用 `reinterpret_cast` 转成函数指针类型 `CreatePluginFn`。

## 3. C++ 如何实现可插拔

整体流程：**宿主扫描目录 → 逐个打开动态库 → 查工厂函数 → 创建对象 → 通过接口调用 → 先销毁对象再卸载库。**

### 3.1 插件侧：实现接口 + 导出

每个插件实现 `IPlugin` 并用宏导出，例如加法插件：

```cpp
// plugin_add.cpp
#include "IPlugin.h"

class AddPlugin : public IPlugin {
public:
    const char* Name() const override { return "AddPlugin"; }
    const char* Description() const override { return "加法插件"; }
    double Calculate(double a, double b) const override { return a + b; }
};

EXPORT_PLUGIN(AddPlugin)
```

乘法插件同理，只需换个 `Calculate` 实现。新增插件**不需要改宿主一行代码**，这就是可插拔的价值。

### 3.2 宿主侧：封装跨平台加载

```cpp
#ifdef _WIN32
using LibHandle = HMODULE;
LibHandle LoadLib(const std::string& path) { return LoadLibraryA(path.c_str()); }
void*     GetSym(LibHandle h, const char* name) { return reinterpret_cast<void*>(GetProcAddress(h, name)); }
void      CloseLib(LibHandle h) { FreeLibrary(h); }
const char* LibExt() { return ".dll"; }
#else
using LibHandle = void*;
LibHandle LoadLib(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void*     GetSym(LibHandle h, const char* name) { return dlsym(h, name); }
void      CloseLib(LibHandle h) { dlclose(h); }
const char* LibExt() { return ".so"; }
#endif
```

用 `#ifdef` 把两套系统 API 包成同名函数，后面业务代码就完全平台无关了。

### 3.3 宿主主流程

```cpp
for (auto& entry : fs::directory_iterator(pluginDir)) {
    auto path = entry.path();
    if (path.extension().string() != LibExt()) continue;   // 只认 .dll/.so

    LibHandle h = LoadLib(fs::absolute(path).string());
    if (!h) { /* 跳过加载失败的 */ continue; }

    auto create  = reinterpret_cast<CreatePluginFn>(GetSym(h, "CreatePlugin"));
    auto destroy = reinterpret_cast<DestroyPluginFn>(GetSym(h, "DestroyPlugin"));
    if (!create || !destroy) { CloseLib(h); continue; }     // 缺少入口，不是合法插件

    IPlugin* p = create();
    std::cout << "[加载] " << p->Name() << " - " << p->Description() << "\n";

    handles.push_back(h);
    plugins.push_back(p);
    destroyers.push_back(destroy);
}

for (auto* p : plugins) p->Calculate(3.0, 4.0);             // 统一接口调用

for (size_t i = 0; i < plugins.size(); ++i) destroyers[i](plugins[i]);  // 先销毁对象
for (auto h : handles) CloseLib(h);                        // 再卸载库
```

关键顺序：**必须先 `DestroyPlugin` 销毁对象，再 `CloseLib` 卸载库**。
如果先卸载库，对象的虚函数表已被释放，再 `delete` 就是访问悬空内存，程序直接崩溃。

### 3.4 为什么这样设计

| 设计点 | 目的 |
| --- | --- |
| 纯虚接口 | 宿主只依赖抽象，屏蔽实现细节 |
| `extern "C"` 工厂函数 | 绕过名称修饰，宿主才能按名字查到符号 |
| 创建/销毁成对导出 | 保证对象在同一模块内分配和释放，避免跨堆问题 |
| 目录扫描 | 新增插件只需放入目录，零改动宿主 |
| 跳过非法库 | 单个插件出错不影响宿主，提升健壮性 |

### 3.5 局限与进阶

- **ABI 兼容**：接口一旦有差异（虚表顺序、`std::string` 布局）就不能混用，所以接口传参尽量用 `const char*`、`double` 等 POD 类型。
- **版本管理**：可让插件导出 `PluginApiVersion()` 或在名字里带版本号，宿主校验后再加载。
- **C++ 类跨模块传参**：不同编译器/不同 CRT 版本的 `std::string`、`std::vector` 布局可能不同，跨边界传递易崩，实践中常用纯 C 接口或稳定的 ABI。
- **更多入口**：复杂插件可导出多个工厂函数（如 `CreateFilter`、`CreateRenderer`），或用一张"入口表"一次性返回所有函数指针。

## 4. 编译与运行

### Linux

```bash
mkdir -p plugins

g++ -shared -fPIC -o plugins/libadd.so plugin_add.cpp
g++ -shared -fPIC -o plugins/libmul.so plugin_mul.cpp

g++ -std=c++17 -o host host.cpp -ldl

./host plugins
```

### Windows MSVC

```bat
mkdir plugins

cl /nologo /LD /EHsc /std:c++17 plugin_add.cpp /Fe:plugins\add.dll
cl /nologo /LD /EHsc /std:c++17 plugin_mul.cpp /Fe:plugins\mul.dll

cl /nologo /EHsc /std:c++17 host.cpp /Fe:host.exe

host.exe plugins
```

### Windows MinGW

```bat
mkdir -p plugins

g++ -shared -o plugins/add.dll plugin_add.cpp
g++ -shared -o plugins/mul.dll plugin_mul.cpp

g++ -std=c++17 -o host.exe host.cpp

host.exe plugins
```

预期输出：

```
[加载] AddPlugin - 加法插件
[加载] MulPlugin - 乘法插件

共加载 2 个插件

计算 3 和 4:
  AddPlugin(3, 4) = 7
  MulPlugin(3, 4) = 12
```
