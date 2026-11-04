#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>  
#include <gdiplus.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

// 画一个圆角矩形路径
void RoundPath(GraphicsPath& p, int l, int t, int w, int h, int r) {
    if (r <= 0) {
        p.AddRectangle(Rect(l, t, w, h));
        return;
    }
    int d = r * 2;
    p.AddArc(l, t, d, d, 180, 90);               // 左上
    p.AddArc(l + w - d, t, d, d, 270, 90);       // 右上
    p.AddArc(l + w - d, t + h - d, d, d, 0, 90); // 右下
    p.AddArc(l, t + h - d, d, d, 90, 90);        // 左下
    p.CloseFigure();
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);

        Graphics g(dc);                               // GDI+ 接到画布上
        g.SetSmoothingMode(SmoothingModeHighQuality); // 抗锯齿
        g.Clear(Color(255, 240, 240, 240));           // 浅灰背景

        // ---- 投影：偏移 4px 画一个黑色半透明圆角矩形 ----
        GraphicsPath shadow;
        RoundPath(shadow, 104, 104, 300, 120, 20);
        SolidBrush sb(Color(80, 0, 0, 0)); // alpha=80 的黑
        g.FillPath(&sb, &shadow);

        // ---- 主体：白色填充 + 蓝色边框 ----
        GraphicsPath path;
        RoundPath(path, 100, 100, 300, 120, 20);
        SolidBrush fill(Color(255, 255, 255, 255)); // 不透明白
        g.FillPath(&fill, &path);
        Pen pen(Color(255, 52, 120, 246), 2.0f); // 蓝色 2px 边框
        g.DrawPath(&pen, &path);

        // ---- 文字（先用 GDI 的 TextOut 凑合，Demo 2 换 DirectWrite）----
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(32, 32, 32));
        DrawTextW(dc,
                  L"你好，圆角矩形",
                  -1,
                  &RECT{120, 140, 400, 200},
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    GdiplusStartupInput gsi;
    ULONG_PTR token;
    GdiplusStartup(&token, &gsi, nullptr);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Demo1";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0,
                                L"Demo1",
                                L"Demo1: GDI+ 圆角矩形",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                540,
                                320,
                                nullptr,
                                nullptr,
                                hInst,
                                nullptr);
    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(token);
    return 0;
}
