# curl（libcurl）在 Windows + vcpkg 下的使用

承接 [11-libuser](../11-libuser/readme.md) 里讲的静态库/动态库，这一篇拿 libcurl 当真实例子，看一个成熟的第三方 C 库在 Windows 上到底给我们提供了什么形态的产物。

环境：Windows 11 / MSVC 14.51（VS 2026）/ vcpkg 位于 `D:\vcpkg` / triplet `x64-windows`。

## 1. 安装

### 1.1 基本命令

```shell
vcpkg install curl:x64-windows
```

装完后 vcpkg 会报告实际启用的特性：

```
curl[core,non-http,ssl,sspi]:x64-windows@8.21.0#1
```

| 特性 | 含义 |
| --- | --- |
| `core` | 基础功能 |
| `non-http` | 放开 HTTP/HTTPS 之外的协议（FTP、SMTP 等） |
| `ssl` | 默认 TLS 实现，**Windows 上解析为 Schannel**，不是 OpenSSL |
| `sspi` | 走 Windows 原生认证（NTLM/Kerberos 协商） |

`ssl` 这一条很容易误解，第 4 节会验证它到底链了什么。

### 1.2 下载失败时挂代理

vcpkg 是从源码构建的，要先去 GitHub 拉 tarball。国内网络抖动时会看到：

```
Downloading https://github.com/curl/curl/archive/curl-8_21_0.tar.gz -> curl-curl-curl-8_21_0.tar.gz
Attempt 1 of 3, retrying download.
error: Download timed out.
error: 生成 curl:x64-windows 失败，结果为: BUILD_FAILED
```

挂上本地代理重跑即可（本机 Clash 混合端口为 7897）：

```shell
source ~/proxy.sh          # 见本节末尾
vcpkg install curl:x64-windows
```

> vcpkg 认**大写**的 `HTTP_PROXY`/`HTTPS_PROXY`，而 curl、wget 认**小写**的 `http_proxy`/`https_proxy`。
 写脚本时两套都设，省得来回排查。

不建议把代理变量设成常驻的用户环境变量——代理程序没运行时，git / pip / npm 等一切读这个变量的工具都会变成"连接被拒"，而不是回退直连。用一个手动 source 的脚本更可控：

```bash
# ~/proxy.sh
_PROXY_URL='http://127.0.0.1:7897'
proxy_on() {
    export HTTP_PROXY="$_PROXY_URL" HTTPS_PROXY="$_PROXY_URL"
    export http_proxy="$_PROXY_URL" https_proxy="$_PROXY_URL"
    export NO_PROXY='localhost,127.0.0.1,::1' no_proxy="$NO_PROXY"
}
proxy_off() { unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy NO_PROXY no_proxy; }
proxy_on
```

## 2. 产物：对外只有一组导入库 + DLL

这是本篇最核心的结论。libcurl 是**单体库**，不像 boost、ICU 那样拆成一堆组件库，对外只暴露一组。

`D:\vcpkg\installed\x64-windows\` 下的实际产物：

| 配置 | 导入库 | DLL |
| --- | --- | --- |
| Release | `lib/libcurl.lib`（22 KB） | `bin/libcurl.dll`（717 KB） |
| Debug | `debug/lib/libcurl-d.lib`（22 KB） | `debug/bin/libcurl-d.dll`（1.6 MB） |

除此之外还有：

- `include/curl/curl.h` 等头文件
- `tools/curl/` 下的 `curl.exe` 命令行工具
- `lib/pkgconfig/libcurl.pc`
- `share/curl/vcpkg-cmake-wrapper.cmake`（`find_package` 靠它）

> [Tips]
> Debug 版带 `-d` 后缀。用 `find_package(CURL)` 的话 CMake 会按配置自动选，
> 但如果手写 `target_link_libraries(app libcurl)` 硬编码名字，Debug 构建就会链错。

### 2.1 怎么确认 .lib 是导入库而不是静态库

光看大小（22 KB 对比 717 KB 的 DLL）已经很可疑了，但可以直接查证。导入库的本质是一个**归档文件**，里面只有导入桩，没有任何编译出来的代码：

```shell
dumpbin -nologo -archivemembers lib/libcurl.lib
```

```
Archive member name at 275A: libcurl.dll/
Archive member name at 2984: libcurl.dll/
Archive member name at 2ABA: libcurl.dll/
...
```

103 个归档成员**全部**是 `libcurl.dll/` 条目，一个 `.obj` 都没有 → 确认是纯导入库。

对照一下：如果这是静态库，成员会是 `easy.obj`、`multi.obj`、`http.obj` 这样的目标文件名。这也正是 11-libuser 里 `ar rcs libmylib.a file1.o file2.o` 打包出来的那个形态。

> [Tips]
> Git Bash 下调 dumpbin 要用 `-archivemembers` 而不是 `/archivemembers`，
> 否则 MSYS 的路径转换会把 `/ARCHIVEMEMBERS` 当成一个 Windows 路径，报 LNK1181。

### 2.2 导出符号

```shell
dumpbin -nologo -exports bin/libcurl.dll
```

共 **100 个**导出符号，全是这几族：

```
curl_easy_init      curl_easy_setopt    curl_easy_perform   curl_easy_cleanup
curl_easy_getinfo   curl_easy_escape    curl_easy_header    curl_easy_pause
curl_multi_*        curl_share_*        curl_slist_*        curl_url_*
```

全部是**纯 C 名字，没有 C++ name mangling**。这一点决定了很多事：

- 跨编译器、跨 MSVC 版本调用都不会有 ABI 问题；
- 所以一个 DLL 就够了，不需要像 boost 那样按 `vc145-mt-x64` 编译器标签分出一堆变体（对比看 `lib/` 目录里的 `boost_url-vc145-mt-x64-1_91.lib`，名字里编译器版本、线程模型全带上了）；
- 显式链接（`LoadLibrary` + `GetProcAddress`）时函数名可以直接写 `"curl_easy_init"`，不用像 11-libuser 里那样操心 `extern "C"` 解除名称修饰——libcurl 本身就是 C 库。

## 3. 运行时依赖

```shell
dumpbin -nologo -dependents bin/libcurl.dll
```

```
z.dll                              <- zlib，第三方，要一起分发
bcrypt.dll / CRYPT32.dll / Secur32.dll   <- Schannel，系统自带
WS2_32.dll / IPHLPAPI.DLL / ADVAPI32.dll <- 系统自带
KERNEL32.dll
VCRUNTIME140.dll + api-ms-win-crt-*.dll  <- VC 运行时
```

有两个值得注意的点：

1. **走的是 Windows 原生 Schannel，不是 OpenSSL。** 依赖列表里没有 `libcrypto-3-x64.dll` 和 `libssl-3-x64.dll`——哪怕 vcpkg 树里确实装了 OpenSSL。第 1.1 节那个 `ssl` 特性在 Windows 上默认就解析成 Schannel。好处是少两个几 MB 的 DLL 要分发，证书链直接用系统证书存储；代价是 TLS 行为跟着 Windows 版本走，跨平台项目里跟 Linux 上的 OpenSSL 构建会有细微差异。
2. **brotli 没有被链进去**，压缩只有 zlib。

### 3.1 分发清单

```
libcurl.dll
z.dll
+ VC 运行时（vc_redist.x64.exe，或用 /MT 静态链接 CRT）
```

就两个文件。

## 4. 在 CMake 里用

### 4.1 标准写法

```cmake
find_package(CURL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
```

配合 vcpkg 的 toolchain 文件：

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

`CURL::libcurl` 是个 imported target，头文件路径、库路径、Debug/Release 的 `-d` 后缀选择全都由它带上，不需要再手写 `include_directories` 和 `link_directories`——比 11-libuser 里那套"古代做法"省心得多。

### 4.2 本项目的坑：CMakePresets 依赖 VCPKG_ROOT

本仓库 `CMakePresets.json` 里 toolchain 是这么写的：

```json
"CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
```

它依赖环境变量 `VCPKG_ROOT`，而这个变量在 **Visual Studio 开发者命令行**里会被 `vcvarsall` 覆盖成 VS 自带的那个 vcpkg：

```
用户环境变量（正确）：VCPKG_ROOT = D:\vcpkg                      <- 装了 104 个包
VS 开发者命令行里：   VCPKG_ROOT = D:\SoftWare\VS\VS\VC\vcpkg    <- 一个包都没有
```

后果就是：从 VS 开发者命令行 configure，toolchain 指向那个空的 vcpkg，`find_package(CURL REQUIRED)` 直接失败，而且报错看起来像"curl 没装"，很有迷惑性。

vcpkg.exe 自己不受影响——它会自动探测自身所在目录并忽略不匹配的 `VCPKG_ROOT`，只打一条警告：

```
warning: vcpkg D:\vcpkg\vcpkg.exe 正在使用检测到的 vcpkg 根 D:\vcpkg
并忽略不匹配的 VCPKG_ROOT 环境值 D:\SoftWare\VS\VS\VC\vcpkg。
```

但 CMakePresets 里的 `$env{VCPKG_ROOT}` 没有这个自愈能力。三种应对：

| 做法 | 说明 |
| --- | --- |
| 改用绝对路径 | 把 preset 里的 `$env{VCPKG_ROOT}` 换成 `D:/vcpkg`，最省事最可靠 |
| shell 启动时纠正 | 在 `~/.bashrc` 里 `export VCPKG_ROOT='D:\vcpkg'`，只对 bash 有效 |
| 配置前检查 | configure 前先 `echo $VCPKG_ROOT` 确认一眼 |

本仓库已改为第一种：`CMakePresets.json` 里直接写 `D:/vcpkg/scripts/buildsystems/vcpkg.cmake`。
这个文件没有被 git 跟踪（`git ls-files` 查不到），所以写死本机绝对路径不会污染仓库。

### 4.3 第二个坑：开发者命令行的目标架构决定 triplet

把路径改对之后，`find_package(CURL REQUIRED)` 仍然可能失败：

```
Could NOT find CURL (missing: CURL_LIBRARY CURL_INCLUDE_DIR)
Call Stack:
  D:/vcpkg/scripts/buildsystems/vcpkg.cmake:939 (_find_package)
```

注意调用栈里已经有 `D:/vcpkg/scripts/buildsystems/vcpkg.cmake` 了——说明 toolchain 加载是对的，问题在别处。打印一下就清楚了：

```cmake
message(STATUS "编译器: ${CMAKE_C_COMPILER_ID}")
message(STATUS "triplet: ${VCPKG_TARGET_TRIPLET}")
```

```
编译器: MSVC 19.51.36256.0
triplet: x86-windows          <- 不是 x64-windows！
```

原因链条是这样的：

1. VS 开发者命令行有 x86 和 x64 两种，本机默认那个是 **x86** 目标（`VSCMD_ARG_TGT_ARCH=x86`）；
2. 于是 PATH 上的 `cl.exe` 是 `bin\HostX86\x86\cl.exe`，32 位编译器；
3. vcpkg 的 toolchain 会**根据编译器架构自动推断 triplet**，推出 `x86-windows`；
4. 而 `D:\vcpkg\installed\` 下只有 `x64-windows\` 一个目录——所有包都装在 x64 下；
5. 查找路径变成 `D:/vcpkg/installed/x86-windows`，那儿什么都没有 → 找不到。

> [Tips]
> 这个报错极具迷惑性，看起来像"curl 没装"，实际是"装了但架构对不上"。
> 判断方法：报错时打印 `VCPKG_TARGET_TRIPLET`，跟 `ls D:\vcpkg\installed\` 的目录名比一比。

解决就是让编译环境和已装包的架构一致。用 x64 开发者命令行：

```bat
call "D:\SoftWare\VS\VS\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

这次就对了：

```
[vcvarsall.bat] Environment initialized for: 'x64'
-- Check for working C compiler: .../bin/Hostx64/x64/cl.exe - skipped
-- >>> triplet: x64-windows
-- >>> CURL 版本: 8.21.0
[2/2] Linking C executable demo.exe
D:\vcpkg\installed\x64-windows\bin\libcurl.dll -> build\libcurl.dll 已完成
D:\vcpkg\installed\x64-windows\bin\z.dll       -> build\z.dll 已完成
```

最后两行值得留意：**vcpkg 的 toolchain 会自动把运行时需要的 DLL 拷到 exe 旁边**，正好就是第 3.1 节列的那两个，不用自己手动复制。

### 4.4 最小验证例子

确认整条链路通不通，用这个就够：

```c
#include <curl/curl.h>
#include <stdio.h>
int main(void){ printf("libcurl %s\n", curl_version()); return 0; }
```

```cmake
cmake_minimum_required(VERSION 3.21)
project(curltest C)
find_package(CURL REQUIRED)
add_executable(demo main.c)
target_link_libraries(demo PRIVATE CURL::libcurl)
```

实际输出：

```
libcurl libcurl/8.21.0 Schannel zlib/1.3.2
```

`curl_version()` 把构建配置直接吐出来了——**Schannel**（印证第 3 节：没走 OpenSSL）、**zlib/1.3.2**（印证：brotli 没链进去）。
排查一个陌生的 libcurl 二进制时，跑一下这个函数比翻文档快得多。

## 5. 想要静态链接的话

装另一个 triplet：

```shell
vcpkg install curl:x64-windows-static
```

| | `x64-windows` | `x64-windows-static` |
| --- | --- | --- |
| `libcurl.lib` | 22 KB，导入库 | 数 MB，真正的静态库（内含代码） |
| DLL | 有，`libcurl.dll` | 无 |
| zlib | 单独的 `z.dll` | 一并静态进去 |
| CRT | 动态（`/MD`） | 静态（`/MT`） |
| 分发 | exe + 2 个 DLL | 只有 exe |

两个 triplet 可以在同一个 vcpkg 里共存，装在 `installed/` 下不同子目录，互不干扰。CMake 侧通过 `-DVCPKG_TARGET_TRIPLET=x64-windows-static` 选择。

> [Tips]
> 静态 triplet 用的是 `/MT`，整个项目（包括所有依赖）必须统一，
> 不能一半 `/MD` 一半 `/MT`，否则链接期会撞上 CRT 冲突。

## 6. 常用排查命令小结

```shell
vcpkg list | grep -i curl                    # 看装没装、什么版本什么特性
dumpbin -nologo -archivemembers xxx.lib      # 区分导入库 / 静态库
dumpbin -nologo -exports xxx.dll             # 看导出了哪些符号
dumpbin -nologo -dependents xxx.dll          # 看依赖哪些 DLL
dumpbin -nologo -headers xxx.dll             # 看架构（x86/x64）
```

对应到 11-libuser 里提过的 GNU 工具链，是 `nm` / `objdump -p` / `ldd` 的 MSVC 版本。
