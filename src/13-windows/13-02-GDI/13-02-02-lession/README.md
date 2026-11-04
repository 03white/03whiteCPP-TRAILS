# 13-02-02-lession　四步爬到候选窗的绘制管线

隔壁 [`13-02-01-weasel`](../13-02-01-weasel/) 是终点：一个**分层窗口 + 三套绘图 API
混用**的完整候选窗。单文件四万多字，一上手容易被淹。

这个目录是**通往那里的梯子**。每一级只换掉管线里的一个零件，画面变化肉眼可见，
坏了也能立刻定位是谁坏的。

---

## 终点长什么样

weasel 的 `Panel::DoPaint()`（`candemo.cpp:884`）干的事：

```
        CreateCompatibleBitmap  ─ 一张 32bpp 内存位图
                                      ▲
   ① GDI+       ─────────────────────┤  背景 / 圆角 / 边框 / 高斯投影
   ② D2D+DWrite ─ BindDC(memDC) ─────┤  全部文字
   ③ GDI        ── UpdateLayeredWindow┘ 整张带 alpha 的图交给 DWM 合成
```

三套 API 画在**同一张内存位图**上，桥梁是 `ID2D1DCRenderTarget::BindDC` —— 把
Direct2D 重定向到一个 GDI 的 HDC。顺序是严格的两遍：**先画完所有背景，再画所有
文字**。

对照 `candemo.cpp` 的行号：

| 阶段 | 行 |
|---|---|
| 建内存位图 | `:894-898` |
| 第一遍 GDI+ 画背景 | `:903-911` → `HighlightBox` `:779` |
| 第二遍 D2D 画文字 | `:913-935` → `TextOut` `:842` |
| 合成到屏幕 | `:959` → `LayerUpdate` `:867` |

---

## 为什么非得是三层

这是整条管线唯一的**硬约束**，想通了其余都是枝节：

> **GDI 的文字渲染不写 alpha 通道。**

`TextOut` / `DrawTextW` 画完一片像素，那片的 alpha 还是 0。而 `ULW_ALPHA` 是逐
像素 alpha 合成 —— alpha=0 就是完全透明。所以在分层窗口上用 GDI 画字，**字会整
个消失**。

于是分工被钉死了：

| 任务 | 用谁 | 为什么不能换 |
|---|---|---|
| 背景、圆角、边框、投影 | **GDI+** | `SolidBrush(Color(255,...))` 老老实实写 alpha=255；`GraphicsPath` 画圆角也最省事 |
| 文字 | **D2D / DirectWrite** | GDI 不写 alpha，字会没；顺带拿到 DirectWrite 的排版和字体 fallback |
| 最终合成 | **GDI** | `UpdateLayeredWindow` 本身就是 GDI API |

weasel 拉上 Direct2D **不是**为了"渲染更现代"，是因为别的选择画不出字来。

---

## 四级台阶

| | 文件 | 改什么 | 验证点 |
|---|---|---|---|
| **01** | `Lession01.cpp` ✅ | GDI+ 画在 `WM_PAINT` 的窗口 DC 上 | 圆角矩形 + 投影 + 一行字。最传统的那条路 |
| **02** | `Lession02.cpp` ⬜ | 画布从窗口 DC 换成**内存位图**，再 `BitBlt` 回去 | **画面应该和 01 一模一样**。这级只搬画布，不改任何绘图调用 |
| **03** | `Lession03.cpp` ⬜ | `BitBlt` → `UpdateLayeredWindow` + `ULW_ALPHA`；窗口加 `WS_EX_LAYERED`；**不再走 `WM_PAINT`** | 圆角外面**真的透出桌面**。同时那行字会消失 —— 这个故障要留着，它是 04 的动机 |
| **04** | `Lession04.cpp` ⬜ | 建 `ID2D1DCRenderTarget`，`BindDC` 到同一个 memDC，用 DirectWrite 把字画回来 | 字回来了，抗锯齿明显不同。至此三层拼装成型 |

04 跑通之后，手里就是 weasel 管线的最小骨架。再往上加的全是**量与布局**的事
（`DoLayout` 算矩形、注释列两轮对齐、`Reposition` 按输入点定位并粘住），和绘图
API 无关了 —— 那时候再去读 `candemo.cpp` 就不会被淹。

---

## 每级的要点

### 01 → 02　把画布搬到内存

`BeginPaint` 拿到的是**窗口** DC，画一笔屏幕上就变一下。换成内存位图之后画哪一
笔都看不见，最后 `BitBlt` 一次性甩上去 —— 这就是双缓冲，顺带把闪烁治了。

```cpp
HDC memDC  = CreateCompatibleDC(dc);
HBITMAP bm = CreateCompatibleBitmap(dc, w, h);
HGDIOBJ old = SelectObject(memDC, bm);
// ... Graphics g(memDC); 照 01 原样画
BitBlt(dc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
SelectObject(memDC, old); DeleteObject(bm); DeleteDC(memDC);
```

这一级的陷阱是**对象泄漏**：`HBITMAP` / `HDC` 忘了删，跑几十帧 GDI 句柄就见底
（任务管理器能加"GDI 对象"列盯着）。weasel 在 `:961-963` 老老实实三件套收尾。

### 02 → 03　进分层窗口

三件事一起改，少一件都不工作：

1. 建窗时 `CreateWindowExW` 的第一个参数给 `WS_EX_LAYERED`
2. 不再 `BeginPaint` / `EndPaint`，`WM_PAINT` 直接吞掉；改成一个自己调的
   `DoPaint()`
3. `BitBlt` 换成：

```cpp
POINT src = {0, 0};
POINT dst = {winX, winY};        // 屏幕坐标，不是客户区坐标
SIZE  sz  = {w, h};
BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0xFF, AC_SRC_ALPHA};
UpdateLayeredWindow(hwnd, GetDC(nullptr), &dst, &sz,
                    memDC, &src, RGB(0,0,0), &bf, ULW_ALPHA);
```

`AC_SRC_ALPHA` + `ULW_ALPHA` 才是逐像素 alpha。只给 `SetLayeredWindowAttributes`
是**整窗**透明度，画不出真圆角。

> 为什么要绕开 `WM_PAINT`：真实 weasel 的注释说得很直白 ——「由于某些软件并不依赖
> WM_PAINT 消息来重绘，在消息循环中直接忽略掉了 WM_PAINT 消息」。代价是内容一变
> 就全量重画，没有失效区域优化。

这一级还会撞上一个坐标坑：`UpdateLayeredWindow` 的 `pptDst` 是**屏幕坐标**。给错
了窗口会跑到屏幕角上去。

### 03 → 04　把字请回来

```cpp
D2D1CreateFactory(...);
factory->CreateDCRenderTarget(&props, &rt);   // candemo.cpp:290
// 每帧：
rt->BindDC(memDC, &rcw);                      // candemo.cpp:914
rt->BeginDraw();
rt->DrawTextLayout(...);                      // 或 DrawText
rt->EndDraw();                                // 失败 = 设备丢失，要重建资源
```

三个必须知道的：

- **`BindDC` 每帧都要调**，位图换了就得重绑
- **`EndDraw()` 返回失败不是报错，是"设备丢失"**（显卡驱动重启、分辨率切换都会
  触发）。正确反应是把 D2D 资源整套重建 —— weasel 在 `:933` 调 `InitFontRes(true)`
- 量文字和画文字**必须同一套 API**。用 `IDWriteTextLayout` + `GetMetrics` 量，再
  用同一个 `IDWriteTextFormat` 画，才不会出现"量的宽度和画出来对不上"

从这一级起用到 `<wrl/client.h>` 的 `ComPtr` 和 D2D 的 COM 接口，**MSVC only**
（MinGW 头文件覆盖不全，和隔壁 weasel 一样的限制）。01-03 两套工具链都能跑。

---

## 构建

```bash
cmake --preset msvc-release
cmake --build --preset msvc-release --target 13-02-02-lession
./build/bin/13-02-02-lession.exe
```

可用预设：`msvc-debug` / `msvc-release` / `mingw-debug` / `mingw-release`。

目前 `CMakeLists.txt` 只挂了 `Lession01.cpp` 一个源文件、出一个 exe。加 02 之后
要么改成多个 `add_executable`，要么把当前的目标名带上级数。

---

## 文件

```
Lession01.cpp    ① GDI+ 画在窗口 DC 上（已完成）
CMakeLists.txt   构建
build/bin/       构建产物（被根 .gitignore 的 build/ 规则排除）
```
