// ===========================================================================
//  小狼毫候选窗渲染管线 —— 可运行的最小复刻
//
//  这个 demo 不依赖 Boost / ATL / WTL / librime，单文件可编译。
//  它复刻的是 WeaselUI 的绘制部分，用一个假引擎（硬编码词表）代替 librime。
//
//  复刻了哪些技术：
//    1. 分层窗口（WS_EX_LAYERED + UpdateLayeredWindow + ULW_ALPHA）逐像素 alpha
//    2. 三套绘图 API 画到同一张 GDI 位图上：
//         GDI+     背景圆角矩形、边框、投影
//         D2D/DWrite  全部文字（经 ID2D1DCRenderTarget::BindDC 绑到同一个 DC）
//         GDI      图标、最终合成
//    3. 三次 box blur 近似高斯模糊做投影
//    4. 测量与绘制分离：先用 DirectWrite 量出所有矩形，再照着矩形画
//    5. 不走 WM_PAINT，内容一变就主动全量重绘
//    6. `_InvalidFontName_` + font fallback，实现按 Unicode 区间配置字体
//    7. 按输入点定位，越过屏幕底部就翻到上方并「粘住」
//
//  对照表（demo 里的函数 → 真实代码位置）。
//  下面的路径相对 weasel 仓库根目录，本机在 D:\Project\weasel，版本 0.17.4：
//    Panel::Refresh        → WeaselUI/WeaselPanel.cpp:135
//    Panel::DoPaint        → WeaselUI/WeaselPanel.cpp:988
//    Panel::HighlightBox   → WeaselUI/WeaselPanel.cpp:528  _HighlightText
//    Panel::TextOut        → WeaselUI/WeaselPanel.cpp:1294 _TextOut
//    Panel::LayerUpdate    → WeaselUI/WeaselPanel.cpp:1128 _LayerUpdate
//    Panel::Reposition     → WeaselUI/WeaselPanel.cpp:1222 _RepositionWindow
//    DoLayout              → WeaselUI/VerticalLayout.cpp:6
//    DwRes::Init           → WeaselUI/DirectWriteResources.cpp:17
//    Measure               → WeaselUI/StandardLayout.cpp:14  GetTextSizeDW
//    GaussBlur             → WeaselUI/GdiplusBlur.cpp
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <d2d1.h>
#include <dwrite_2.h>
#include <gdiplus.h>
#include <imm.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "imm32.lib")

using Microsoft::WRL::ComPtr;

// ===========================================================================
//  0. 小工具
// ===========================================================================

// demo 里颜色一律用 0xAARRGGBB（直觉顺序）。
// 注意真实 weasel 的配置色是 0xAABBGGRR（COLORREF 顺序），容易看混。
typedef unsigned int Argb;

static inline int A_(Argb c) { return (c >> 24) & 0xFF; }
static inline int R_(Argb c) { return (c >> 16) & 0xFF; }
static inline int G_(Argb c) { return (c >> 8) & 0xFF; }
static inline int B_(Argb c) { return c & 0xFF; }
static inline bool Opaque(Argb c) { return A_(c) != 0; }

static inline Gdiplus::Color ToGdi(Argb c) {
  return Gdiplus::Color(Gdiplus::Color::MakeARGB((BYTE)A_(c), (BYTE)R_(c),
                                                 (BYTE)G_(c), (BYTE)B_(c)));
}
static inline D2D1_COLOR_F ToD2D(Argb c) {
  return D2D1::ColorF(R_(c) / 255.0f, G_(c) / 255.0f, B_(c) / 255.0f,
                      A_(c) / 255.0f);
}
static inline int Clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// ===========================================================================
//  1. 高斯模糊 —— 三次 box blur 近似
//     真实实现见 WeaselUI/GdiplusBlur.cpp（同样是 boxesForGauss + 3 遍 box）。
//     这里用「索引钳制」写法，O(n) 且边界明显正确，比原版好读。
// ===========================================================================

static void BoxesForGauss(double sigma, int* sizes, int n) {
  double wIdeal = std::sqrt((12.0 * sigma * sigma / n) + 1.0);
  int wl = (int)std::floor(wIdeal);
  if (wl % 2 == 0)
    --wl;
  if (wl < 1)
    wl = 1;
  int wu = wl + 2;
  double mIdeal = (12.0 * sigma * sigma - (double)n * wl * wl -
                   4.0 * n * wl - 3.0 * n) /
                  (-4.0 * wl - 4.0);
  int m = (int)(mIdeal + 0.5);
  for (int i = 0; i < n; ++i)
    sizes[i] = (i < m) ? wl : wu;
}

// 对一维序列做盒式平均。src/dst 指向第 0 个元素的某个通道，step 是元素间字节距离。
static void BoxBlurLine(const BYTE* src,
                        BYTE* dst,
                        int count,
                        int srcStep,
                        int dstStep,
                        int r) {
  if (count <= 0)
    return;
  const int win = 2 * r + 1;
  int sum = 0;
  for (int j = -r; j <= r; ++j)
    sum += src[(size_t)Clampi(j, 0, count - 1) * srcStep];
  for (int i = 0; i < count; ++i) {
    dst[(size_t)i * dstStep] = (BYTE)((sum + win / 2) / win);
    sum += src[(size_t)Clampi(i + r + 1, 0, count - 1) * srcStep];
    sum -= src[(size_t)Clampi(i - r, 0, count - 1) * srcStep];
  }
}

static void GaussBlur(Gdiplus::Bitmap* bmp, float radius) {
  if (!bmp || radius < 1.0f)
    return;
  const int w = (int)bmp->GetWidth(), h = (int)bmp->GetHeight();
  if (w <= 0 || h <= 0)
    return;

  Gdiplus::Rect rc(0, 0, w, h);
  Gdiplus::BitmapData bd;
  if (bmp->LockBits(&rc,
                    Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                    PixelFormat32bppPARGB, &bd) != Gdiplus::Ok)
    return;

  const int stride = bd.Stride;
  const size_t total = (size_t)stride * h;
  std::vector<BYTE> a((BYTE*)bd.Scan0, (BYTE*)bd.Scan0 + total);
  std::vector<BYTE> b(total);

  int boxes[3];
  BoxesForGauss(radius, boxes, 3);
  for (int pass = 0; pass < 3; ++pass) {
    const int r = (boxes[pass] - 1) / 2;
    // 横向：a -> b
    for (int y = 0; y < h; ++y)
      for (int c = 0; c < 4; ++c)
        BoxBlurLine(a.data() + (size_t)y * stride + c,
                    b.data() + (size_t)y * stride + c, w, 4, 4, r);
    // 纵向：b -> a
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 4; ++c)
        BoxBlurLine(b.data() + (size_t)x * 4 + c,
                    a.data() + (size_t)x * 4 + c, h, stride, stride, r);
  }

  memcpy(bd.Scan0, a.data(), total);
  bmp->UnlockBits(&bd);
}

// ===========================================================================
//  2. 数据结构：上下文与样式
//     对应 include/WeaselIPCData.h 的 Context / UIStyle（这里大幅精简）。
// ===========================================================================

struct CandidateInfo {
  std::vector<std::wstring> candies;
  std::vector<std::wstring> comments;
  std::vector<std::wstring> labels;
  int highlighted = 0;
  bool empty() const { return candies.empty(); }
};

struct Ctx {
  std::wstring preedit;  // 拼音串
  CandidateInfo cinfo;
  bool empty() const { return preedit.empty() && cinfo.empty(); }
};

enum LayoutType { LAYOUT_VERTICAL = 0, LAYOUT_HORIZONTAL = 1 };

struct Style {
  int layout_type = LAYOUT_VERTICAL;

  // 字体：逗号分隔多个，按顺序作为 fallback 链
  std::wstring font_face = L"Microsoft YaHei UI, Segoe UI";
  std::wstring label_font_face = L"Consolas, Microsoft YaHei UI";
  std::wstring comment_font_face = L"Microsoft YaHei UI, Segoe UI";
  int font_point = 16;
  int label_font_point = 14;
  int comment_font_point = 12;

  // 度量（96 DPI 下的逻辑像素，绘制时统一乘 dpiScale）
  int margin_x = 12;
  int margin_y = 10;
  int spacing = 8;            // preedit 与候选区之间
  int candidate_spacing = 6;  // 候选之间
  int hilite_spacing = 6;     // label/text/comment 之间
  int hilite_padding_x = 8;
  int hilite_padding_y = 4;
  int round_corner = 6;     // 候选高亮框圆角
  int round_corner_ex = 10; // 窗口背景圆角
  int border = 1;

  int shadow_radius = 10;
  int shadow_offset_x = 0;
  int shadow_offset_y = 3;

  Argb back_color = 0xF8FFFFFF;
  Argb border_color = 0xFFD0D0D0;
  Argb shadow_color = 0x60000000;

  Argb text_color = 0xFF202020;
  Argb label_color = 0xFF909090;
  Argb comment_color = 0xFF909090;

  Argb hilited_back_color = 0xFF3478F6;
  Argb hilited_text_color = 0xFFFFFFFF;
  Argb hilited_label_color = 0xFFD8E4FF;
  Argb hilited_comment_color = 0xFFD8E4FF;

  Argb preedit_text_color = 0xFF404040;

  bool inline_preedit = false;  // true 时拼音交给宿主画，候选窗不画
  bool draw_shadow = true;
  bool draw_round = true;
  bool debug_rects = false;

  std::wstring label_format = L"%s.";
};

// ===========================================================================
//  3. DirectWrite / D2D 资源
//     对应 WeaselUI/DirectWriteResources.cpp。
// ===========================================================================

static std::vector<std::wstring> SplitComma(const std::wstring& in) {
  std::vector<std::wstring> out;
  size_t pos = 0;
  while (pos <= in.size()) {
    size_t comma = in.find(L',', pos);
    if (comma == std::wstring::npos)
      comma = in.size();
    std::wstring t = in.substr(pos, comma - pos);
    size_t a = t.find_first_not_of(L" \t");
    size_t b = t.find_last_not_of(L" \t");
    if (a != std::wstring::npos)
      out.push_back(t.substr(a, b - a + 1));
    pos = comma + 1;
  }
  return out;
}

struct DwRes {
  ComPtr<ID2D1Factory> d2d;
  ComPtr<IDWriteFactory2> dw;
  ComPtr<ID2D1DCRenderTarget> rt;
  ComPtr<ID2D1SolidColorBrush> brush;
  ComPtr<IDWriteTextFormat1> fmtText, fmtLabel, fmtComment, fmtPreedit;
  ComPtr<IDWriteTextLayout2> layout;

  float dpiFont = 96.0f / 72.0f;  // point -> pixel
  float dpiLayout = 1.0f;         // 逻辑像素 -> 物理像素

  bool CreateFactories() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
                                 d2d.ReleaseAndGetAddressOf())))
      return false;
    ComPtr<IUnknown> unk;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory2),
                                   unk.ReleaseAndGetAddressOf())))
      return false;
    if (FAILED(unk.As(&dw)))
      return false;

    // ID2D1DCRenderTarget 是三套 API 能合流的关键：
    // 它能把 Direct2D 的输出重定向到一个 GDI 的 HDC 上。
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(d2d->CreateDCRenderTarget(&props, rt.ReleaseAndGetAddressOf())))
      return false;
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1),
                              brush.ReleaseAndGetAddressOf());
    return true;
  }

  // 把一组字体名建成 fallback 链，挂到 textFormat 上。
  bool SetFallback(ComPtr<IDWriteTextFormat1>& fmt,
                   const std::vector<std::wstring>& fonts) {
    ComPtr<IDWriteFontFallback> sys;
    if (FAILED(dw->GetSystemFontFallback(sys.ReleaseAndGetAddressOf())))
      return false;
    ComPtr<IDWriteFontFallbackBuilder> builder;
    if (FAILED(dw->CreateFontFallbackBuilder(builder.ReleaseAndGetAddressOf())))
      return false;
    for (const auto& f : fonts) {
      DWRITE_UNICODE_RANGE range = {0, 0x10FFFF};
      const WCHAR* fam = f.c_str();
      builder->AddMapping(&range, 1, &fam, 1);
    }
    builder->AddMappings(sys.Get());  // 最后挂系统默认兜底
    ComPtr<IDWriteFontFallback> fb;
    if (FAILED(builder->CreateFontFallback(fb.ReleaseAndGetAddressOf())))
      return false;
    return SUCCEEDED(fmt->SetFontFallback(fb.Get()));
  }

  bool MakeFormat(const std::wstring& faces,
                  int point,
                  ComPtr<IDWriteTextFormat1>& out) {
    auto fonts = SplitComma(faces);
    if (fonts.empty())
      fonts.push_back(L"Segoe UI");

    // ★ 小狼毫的技巧（DirectWriteResources.cpp:89）：
    //   主字体故意写成一个不存在的名字，强迫每个字符都走 fallback 链，
    //   这样「某区间用 A 字体、另一区间用 B 字体」的配置才可能生效。
    const wchar_t* kInvalid = L"_InvalidFontName_";

    auto build = [&](const wchar_t* family) -> bool {
      ComPtr<IDWriteTextFormat> f0;
      if (FAILED(dw->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                      DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL,
                                      point * dpiFont, L"",
                                      f0.ReleaseAndGetAddressOf())))
        return false;
      if (FAILED(f0.As(&out)))
        return false;
      out->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
      out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      return true;
    };

    if (build(kInvalid) && SetFallback(out, fonts))
      return true;
    // fallback 链没建起来就退回用真实字体名，保证总能画出字
    return build(fonts[0].c_str());
  }

  bool Init(const Style& st, UINT dpi) {
    dpiFont = dpi / 72.0f;
    dpiLayout = dpi / 96.0f;
    if (!d2d && !CreateFactories())
      return false;
    MakeFormat(st.font_face, st.font_point, fmtText);
    MakeFormat(st.font_face, st.font_point, fmtPreedit);
    MakeFormat(st.label_font_face, st.label_font_point, fmtLabel);
    MakeFormat(st.comment_font_face, st.comment_font_point, fmtComment);
    return true;
  }

  // 量一段文字的尺寸。对应 StandardLayout.cpp:14 GetTextSizeDW。
  // 关键：量和画用的是同一套 API、同一个 TextFormat，所以不会对不上。
  SIZE Measure(const std::wstring& s, IDWriteTextFormat1* fmt) {
    SIZE sz = {0, 0};
    if (!fmt || s.empty())
      return sz;
    ComPtr<IDWriteTextLayout> l0;
    if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, 100000.0f,
                                    100000.0f, l0.ReleaseAndGetAddressOf())))
      return sz;
    DWRITE_TEXT_METRICS m{};
    if (FAILED(l0->GetMetrics(&m)))
      return sz;
    sz.cx = (LONG)std::ceil(m.widthIncludingTrailingWhitespace);
    sz.cy = (LONG)std::ceil(m.height);
    return sz;
  }
};

// ===========================================================================
//  4. 布局 —— 只算矩形，不碰任何绘图
//     对应 WeaselUI/VerticalLayout.cpp:6 与 HorizontalLayout.cpp。
// ===========================================================================

struct LayoutResult {
  SIZE content = {0, 0};  // 整个窗口大小（含投影外扩）
  RECT bg = {0, 0, 0, 0}; // 背景框（窗口内缩 offset 后的区域）
  RECT preedit = {0, 0, 0, 0};
  std::vector<RECT> cand, label, text, comment;
  int offsetX = 0, offsetY = 0;  // 投影需要的外扩边距
};

static LayoutResult DoLayout(const Ctx& ctx, const Style& st, DwRes& dwr) {
  LayoutResult L;
  const float s = dwr.dpiLayout;
  auto S = [&](int v) { return (int)(v * s); };

  // 投影要画到背景框外面去，所以窗口整体比内容大一圈
  if (st.draw_shadow && Opaque(st.shadow_color) && st.shadow_radius > 0) {
    L.offsetX = S(st.shadow_radius) + std::abs(S(st.shadow_offset_x));
    L.offsetY = S(st.shadow_radius) + std::abs(S(st.shadow_offset_y));
  }

  const int mx = S(st.margin_x), my = S(st.margin_y);
  const int hpx = S(st.hilite_padding_x), hpy = S(st.hilite_padding_y);
  const int hs = S(st.hilite_spacing);

  const int n = (int)ctx.cinfo.candies.size();
  L.cand.assign(n, RECT{});
  L.label.assign(n, RECT{});
  L.text.assign(n, RECT{});
  L.comment.assign(n, RECT{});

  int width = 0;
  int y = L.offsetY + my;

  // ---- preedit ----
  const bool showPreedit = !st.inline_preedit && !ctx.preedit.empty();
  if (showPreedit) {
    SIZE sz = dwr.Measure(ctx.preedit, dwr.fmtPreedit.Get());
    L.preedit = {L.offsetX + mx, y, L.offsetX + mx + sz.cx, y + sz.cy};
    y += sz.cy + S(st.spacing);
    width = (std::max)(width, mx * 2 + (int)sz.cx);
  }

  if (st.layout_type == LAYOUT_VERTICAL) {
    // ---- 竖排：一个候选一行 ----
    int maxLabelW = 0;
    std::vector<SIZE> szL(n), szT(n), szC(n);
    for (int i = 0; i < n; ++i) {
      szL[i] = dwr.Measure(ctx.cinfo.labels[i], dwr.fmtLabel.Get());
      szT[i] = dwr.Measure(ctx.cinfo.candies[i], dwr.fmtText.Get());
      szC[i] = dwr.Measure(ctx.cinfo.comments[i], dwr.fmtComment.Get());
      maxLabelW = (std::max)(maxLabelW, (int)szL[i].cx);
    }

    // 第一轮：定每行的高度与各段 x 位置（label 右对齐到统一列宽）
    int commentShift = 0;
    for (int i = 0; i < n; ++i) {
      if (i > 0)
        y += S(st.candidate_spacing);
      const int rowH =
          (std::max)({(int)szL[i].cy, (int)szT[i].cy, (int)szC[i].cy});
      int x = L.offsetX + mx + hpx;

      L.label[i] = {x + (maxLabelW - (int)szL[i].cx), y + (rowH - szL[i].cy) / 2,
                    x + maxLabelW, y + (rowH + szL[i].cy) / 2};
      x += maxLabelW + hs;

      L.text[i] = {x, y + (rowH - szT[i].cy) / 2, x + (int)szT[i].cx,
                   y + (rowH + szT[i].cy) / 2};
      x += szT[i].cx;

      if (szC[i].cx > 0) {
        commentShift = (std::max)(commentShift, x + hs);
        L.comment[i] = {0, y + (rowH - szC[i].cy) / 2, (int)szC[i].cx,
                        y + (rowH + szC[i].cy) / 2};
      }
      L.cand[i] = {L.offsetX + mx, y - hpy, x + hpx, y + rowH + hpy};
      y += rowH;
    }

    // 第二轮：注释左对齐到「所有带注释的候选中最靠右的那个」之后
    // （对应 VerticalLayout.cpp:170-189 的 Align comments）
    for (int i = 0; i < n; ++i) {
      if (L.comment[i].right > L.comment[i].left) {
        int w = L.comment[i].right - L.comment[i].left;
        L.comment[i].left = commentShift;
        L.comment[i].right = commentShift + w;
        width = (std::max)(width, commentShift - L.offsetX + w + hpx + mx);
      }
      width = (std::max)(width, (int)L.cand[i].right - L.offsetX + mx);
    }
    // 所有高亮框拉到同一宽度（右边界对齐）
    for (int i = 0; i < n; ++i)
      L.cand[i].right = L.offsetX + width - mx;

  } else {
    // ---- 横排：所有候选排在一行 ----
    int x = L.offsetX + mx;
    int rowH = 0;
    for (int i = 0; i < n; ++i) {
      SIZE l = dwr.Measure(ctx.cinfo.labels[i], dwr.fmtLabel.Get());
      SIZE t = dwr.Measure(ctx.cinfo.candies[i], dwr.fmtText.Get());
      SIZE c = dwr.Measure(ctx.cinfo.comments[i], dwr.fmtComment.Get());
      rowH = (std::max)({rowH, (int)l.cy, (int)t.cy, (int)c.cy});

      int inner = (int)l.cx + hs + (int)t.cx + (c.cx ? hs + (int)c.cx : 0);
      L.cand[i] = {x, y - hpy, x + inner + hpx * 2, y + rowH + hpy};

      int cx = x + hpx;
      L.label[i] = {cx, y, cx + (int)l.cx, y + (int)l.cy};
      cx += l.cx + hs;
      L.text[i] = {cx, y, cx + (int)t.cx, y + (int)t.cy};
      cx += t.cx;
      if (c.cx) {
        cx += hs;
        L.comment[i] = {cx, y, cx + (int)c.cx, y + (int)c.cy};
      }
      x = L.cand[i].right + S(st.candidate_spacing);
    }
    // 竖直居中每段
    for (int i = 0; i < n; ++i) {
      auto center = [&](RECT& r) {
        if (r.right <= r.left)
          return;
        int h = r.bottom - r.top;
        r.top = y + (rowH - h) / 2;
        r.bottom = r.top + h;
      };
      center(L.label[i]);
      center(L.text[i]);
      center(L.comment[i]);
      L.cand[i].top = y - hpy;
      L.cand[i].bottom = y + rowH + hpy;
    }
    if (n)
      width = (std::max)(width, (int)L.cand[n - 1].right - L.offsetX + mx);
    y += rowH;
  }

  y += my;
  L.content.cx = width + L.offsetX * 2;
  L.content.cy = y + L.offsetY;
  L.bg = {L.offsetX, L.offsetY, L.offsetX + width, L.content.cy - L.offsetY};
  return L;
}

// ===========================================================================
//  5. 候选窗
// ===========================================================================

class Panel {
 public:
  bool Create(HINSTANCE hInst, HWND owner);
  void Update(const Ctx& ctx);
  void MoveTo(const RECT& caret);  // caret 为屏幕坐标
  void Show();
  void Hide();
  bool IsShown() const { return shown_; }
  HWND Hwnd() const { return hwnd_; }
  Style& style() { return st_; }
  void Invalidate() { dirty_ = true; }
  void Refresh();

  // 鼠标点选回调：参数是候选序号
  void (*on_select)(int) = nullptr;
  void (*on_hover)(int) = nullptr;

  // 最近一次绘制的耗时（毫秒），显示在宿主窗口上
  double last_paint_ms = 0;

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT, WPARAM, LPARAM);

  void InitFontRes(bool forced);
  void Reposition();
  void DoPaint();
  void HighlightBox(HDC dc,
                    const RECT& rc,
                    Argb fill,
                    Argb shadow,
                    int radius,
                    Argb border,
                    bool isBackground);
  void TextOut(const RECT& rc, const std::wstring& s, Argb color,
               IDWriteTextFormat1* fmt);
  void LayerUpdate(HDC memDC, const RECT& rc);
  int HitTest(POINT pt) const;

  HWND hwnd_ = nullptr;
  Ctx ctx_;
  Style st_;
  DwRes dwr_;
  LayoutResult lay_;
  UINT dpi_ = 96;
  Style ostyle_;
  bool styleValid_ = false;
  RECT inputPos_ = {0, 0, 0, 0};
  bool sticky_ = false;
  bool shown_ = false;
  bool dirty_ = true;
  int hover_ = -1;
};

static const wchar_t* kPanelClass = L"DemoCandidatePanel";

bool Panel::Create(HINSTANCE hInst, HWND owner) {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = &Panel::WndProc;
  wc.hInstance = hInst;
  wc.lpszClassName = kPanelClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassExW(&wc);

  // 和 WeaselUI/WeaselUI.cpp:87-90 一致：
  //   TOOLWINDOW  不进 Alt+Tab
  //   TOPMOST     置顶
  //   NOACTIVATE  点击不抢焦点（否则正在打字的输入框会失焦）
  //   LAYERED     分层窗口，逐像素 alpha 的前提
  // 真实代码建窗时带 WS_EX_TRANSPARENT（鼠标穿透），在首次 DoPaint 里
  // 用 ModifyStyleEx 换成 LAYERED（WeaselPanel.cpp:990）。这里直接建成
  // LAYERED，省掉那一步，行为等价。
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
                              WS_EX_NOACTIVATE | WS_EX_LAYERED,
                          kPanelClass, L"", WS_POPUP, 0, 0, 0, 0, owner,
                          nullptr, hInst, this);
  return hwnd_ != nullptr;
}

LRESULT CALLBACK Panel::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  Panel* self = nullptr;
  if (m == WM_NCCREATE) {
    self = (Panel*)((CREATESTRUCTW*)l)->lpCreateParams;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    self->hwnd_ = h;
  } else {
    self = (Panel*)GetWindowLongPtrW(h, GWLP_USERDATA);
  }
  if (self)
    return self->Handle(m, w, l);
  return DefWindowProcW(h, m, w, l);
}

LRESULT Panel::Handle(UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_MOUSEACTIVATE:
      // 绝不抢焦点
      return MA_NOACTIVATE;

    case WM_LBUTTONUP: {
      POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      int i = HitTest(pt);
      if (i >= 0 && on_select)
        on_select(i);
      return 0;
    }

    case WM_MOUSEMOVE: {
      POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      int i = HitTest(pt);
      if (i != hover_) {
        hover_ = i;
        if (i >= 0 && on_hover)
          on_hover(i);
        else
          DoPaint();
      }
      TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd_, 0};
      TrackMouseEvent(&t);
      return 0;
    }

    case WM_MOUSELEAVE:
      if (hover_ != -1) {
        hover_ = -1;
        DoPaint();
      }
      return 0;

    case WM_DPICHANGED:
      InitFontRes(true);
      Refresh();
      return 0;
  }
  return DefWindowProcW(hwnd_, m, w, l);
}

int Panel::HitTest(POINT pt) const {
  for (size_t i = 0; i < lay_.cand.size(); ++i)
    if (PtInRect(&lay_.cand[i], pt))
      return (int)i;
  return -1;
}

void Panel::Update(const Ctx& ctx) {
  ctx_ = ctx;
  hover_ = -1;
  Refresh();
}

// 对应 WeaselPanel.cpp:183 _InitFontRes
void Panel::InitFontRes(bool forced) {
  UINT dx = 96, dy = 96;
  HMONITOR mon = MonitorFromRect(&inputPos_, MONITOR_DEFAULTTONEAREST);
  if (mon)
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);

  // DPI 变了、样式变了、或还没建过 —— 就整个重建 DirectWrite 资源
  bool styleChanged =
      !styleValid_ || memcmp(&ostyle_.font_point, &st_.font_point,
                             sizeof(int)) != 0 ||
      ostyle_.font_face != st_.font_face ||
      ostyle_.label_font_face != st_.label_font_face ||
      ostyle_.comment_font_face != st_.comment_font_face ||
      ostyle_.label_font_point != st_.label_font_point ||
      ostyle_.comment_font_point != st_.comment_font_point;

  if (forced || styleChanged || dx != dpi_) {
    dpi_ = dx;
    dwr_.Init(st_, dpi_);
  }
  ostyle_ = st_;
  styleValid_ = true;
}

// 对应 WeaselPanel.cpp:135 Refresh
void Panel::Refresh() {
  InitFontRes(false);
  lay_ = DoLayout(ctx_, st_, dwr_);

  // 先定大小（_ResizeWindow），再定位置（_RepositionWindow），最后画
  SetWindowPos(hwnd_, nullptr, 0, 0, lay_.content.cx, lay_.content.cy,
               SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);
  Reposition();
  DoPaint();
}

// 对应 WeaselPanel.cpp:1222 _RepositionWindow
void Panel::Reposition() {
  RECT work = {0, 0, 0, 0};
  HMONITOR mon = MonitorFromRect(&inputPos_, MONITOR_DEFAULTTONEAREST);
  if (mon) {
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfo(mon, &mi))
      work = mi.rcWork;
  }
  const int w = lay_.content.cx, h = lay_.content.cy;

  // 默认贴在输入点正下方，并把投影的外扩抵消掉，让背景框而非窗口贴住光标
  int x = inputPos_.left - lay_.offsetX;
  int y = inputPos_.bottom - lay_.offsetY;

  if (x + w > work.right)
    x = work.right - w;
  if (x < work.left)
    x = work.left;

  // 下方放不下就翻到上方，并「粘住」——
  // 否则候选条数变化时窗口会在上下之间反复横跳
  if (y + h > work.bottom || sticky_) {
    sticky_ = true;
    y = inputPos_.top - h + lay_.offsetY;
  }
  if (y < work.top)
    y = work.top;

  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
}

void Panel::MoveTo(const RECT& caret) {
  // 输入点移动得够远就解除「粘在上方」的状态（WeaselPanel.cpp:1181）
  if (std::abs(caret.left - inputPos_.left) > 50 ||
      std::abs(caret.bottom - inputPos_.bottom) > 50)
    sticky_ = false;
  inputPos_ = caret;
  inputPos_.bottom += 4;  // 和光标留一点缝
  Reposition();
}

void Panel::Show() {
  ShowWindow(hwnd_, SW_SHOWNA);  // NA = 不激活
  shown_ = true;
}
void Panel::Hide() {
  ShowWindow(hwnd_, SW_HIDE);
  shown_ = false;
}

// 画一个圆角矩形：可选投影 + 填充 + 边框。全部用 GDI+。
// 对应 WeaselPanel.cpp:528 _HighlightText
void Panel::HighlightBox(HDC dc,
                         const RECT& rc,
                         Argb fill,
                         Argb shadow,
                         int radius,
                         Argb border,
                         bool isBackground) {
  using namespace Gdiplus;
  Graphics g(dc);
  g.SetSmoothingMode(SmoothingModeHighQuality);

  const int w = rc.right - rc.left, h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0)
    return;

  auto roundPath = [&](GraphicsPath& p, int l, int t, int ww, int hh, int r) {
    if (r <= 0) {
      p.AddRectangle(Rect(l, t, ww, hh));
      return;
    }
    const int d = r * 2;
    p.AddArc(l, t, d, d, 180.0f, 90.0f);
    p.AddArc(l + ww - d, t, d, d, 270.0f, 90.0f);
    p.AddArc(l + ww - d, t + hh - d, d, d, 0.0f, 90.0f);
    p.AddArc(l, t + hh - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
  };

  const int sr = (int)(st_.shadow_radius * dwr_.dpiLayout);
  const int sox = (int)(st_.shadow_offset_x * dwr_.dpiLayout);
  const int soy = (int)(st_.shadow_offset_y * dwr_.dpiLayout);

  // ---- 投影：画到一张离屏位图上，高斯模糊，再贴回来 ----
  if (st_.draw_shadow && Opaque(shadow) && sr > 0) {
    const int mx = lay_.offsetX, my = lay_.offsetY;
    Bitmap shadowBmp(w + mx * 2, h + my * 2, PixelFormat32bppPARGB);
    {
      Graphics gs(&shadowBmp);
      gs.SetSmoothingMode(SmoothingModeHighQuality);
      GraphicsPath sp;
      roundPath(sp, mx + sox, my + soy, w, h, radius);
      SolidBrush sb(ToGdi(shadow));
      gs.FillPath(&sb, &sp);
    }
    GaussBlur(&shadowBmp, (float)sr);
    g.DrawImage(&shadowBmp, rc.left - mx, rc.top - my);
  }

  GraphicsPath path;
  roundPath(path, rc.left, rc.top, w, h, radius);

  if (Opaque(fill)) {
    SolidBrush brush(ToGdi(fill));
    g.FillPath(&brush, &path);
  }
  const int bw = (int)(st_.border * dwr_.dpiLayout);
  if (Opaque(border) && bw > 0 && isBackground) {
    Pen pen(ToGdi(border), (REAL)bw);
    g.DrawPath(&pen, &path);
  }
}

// 对应 WeaselPanel.cpp:1294 _TextOut
void Panel::TextOut(const RECT& rc,
                    const std::wstring& s,
                    Argb color,
                    IDWriteTextFormat1* fmt) {
  if (!fmt || s.empty() || !Opaque(color))
    return;
  dwr_.brush->SetColor(ToD2D(color));

  ComPtr<IDWriteTextLayout> l0;
  if (FAILED(dwr_.dw->CreateTextLayout(
          s.c_str(), (UINT32)s.size(), fmt, (float)(rc.right - rc.left),
          (float)(rc.bottom - rc.top), l0.ReleaseAndGetAddressOf())))
    return;

  // 首字符如果有左侧溢出（某些斜体/手写字体），补偿一下，免得被切掉
  float ox = (float)rc.left, oy = (float)rc.top;
  DWRITE_OVERHANG_METRICS om{};
  if (SUCCEEDED(l0->GetOverhangMetrics(&om)) && om.left > 0)
    ox += om.left;

  dwr_.rt->DrawTextLayout(D2D1::Point2F(ox, oy), l0.Get(), dwr_.brush.Get(),
                          D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

// 对应 WeaselPanel.cpp:1128 _LayerUpdate —— 整张带 alpha 的位图交给 DWM 合成
void Panel::LayerUpdate(HDC memDC, const RECT& rc) {
  HDC screen = ::GetDC(nullptr);
  RECT wr;
  GetWindowRect(hwnd_, &wr);
  POINT dst = {wr.left, wr.top};
  POINT src = {0, 0};
  SIZE sz = {rc.right - rc.left, rc.bottom - rc.top};

  // ULW_ALPHA + AC_SRC_ALPHA = 逐像素 alpha（不是整窗统一透明度）。
  // 这就是圆角之外能完全透出桌面、投影能有渐变的原因。
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0xFF, AC_SRC_ALPHA};
  UpdateLayeredWindow(hwnd_, screen, &dst, &sz, memDC, &src, RGB(0, 0, 0), &bf,
                      ULW_ALPHA);
  ::ReleaseDC(nullptr, screen);
}

// 对应 WeaselPanel.cpp:988 DoPaint —— 整个管线的核心
void Panel::DoPaint() {
  LARGE_INTEGER f, t0, t1;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&t0);

  RECT rcw = {0, 0, lay_.content.cx, lay_.content.cy};
  if (rcw.right <= 0 || rcw.bottom <= 0)
    return;

  // ---- 建一张内存位图，三套 API 都往它上面画 ----
  HDC hdc = ::GetDC(hwnd_);
  HDC memDC = ::CreateCompatibleDC(hdc);
  HBITMAP memBmp = ::CreateCompatibleBitmap(hdc, rcw.right, rcw.bottom);
  HGDIOBJ oldBmp = ::SelectObject(memDC, memBmp);
  ::ReleaseDC(hwnd_, hdc);

  const int rcEx = st_.draw_round ? (int)(st_.round_corner_ex * dwr_.dpiLayout) : 0;
  const int rcIn = st_.draw_round ? (int)(st_.round_corner * dwr_.dpiLayout) : 0;

  // ---- 第一遍：GDI+ 画所有背景 ----
  HighlightBox(memDC, lay_.bg, st_.back_color, st_.shadow_color, rcEx,
               st_.border_color, true);

  const int hi = ctx_.cinfo.highlighted;
  const int shown = hover_ >= 0 ? hover_ : hi;
  if (shown >= 0 && shown < (int)lay_.cand.size())
    HighlightBox(memDC, lay_.cand[shown], st_.hilited_back_color, 0, rcIn, 0,
                 false);

  // ---- 第二遍：D2D/DirectWrite 画所有文字，绑到同一个 memDC 上 ----
  if (SUCCEEDED(dwr_.rt->BindDC(memDC, &rcw))) {
    dwr_.rt->BeginDraw();

    if (!st_.inline_preedit && !ctx_.preedit.empty())
      TextOut(lay_.preedit, ctx_.preedit, st_.preedit_text_color,
              dwr_.fmtPreedit.Get());

    for (size_t i = 0; i < ctx_.cinfo.candies.size(); ++i) {
      const bool h = ((int)i == shown);
      TextOut(lay_.label[i], ctx_.cinfo.labels[i],
              h ? st_.hilited_label_color : st_.label_color,
              dwr_.fmtLabel.Get());
      TextOut(lay_.text[i], ctx_.cinfo.candies[i],
              h ? st_.hilited_text_color : st_.text_color, dwr_.fmtText.Get());
      TextOut(lay_.comment[i], ctx_.cinfo.comments[i],
              h ? st_.hilited_comment_color : st_.comment_color,
              dwr_.fmtComment.Get());
    }

    if (FAILED(dwr_.rt->EndDraw()))
      InitFontRes(true);  // 设备丢失，重建资源
  }

  // ---- 调试：把布局算出来的矩形描出来 ----
  if (st_.debug_rects) {
    using namespace Gdiplus;
    Graphics g(memDC);
    auto box = [&](const RECT& r, Argb c) {
      if (r.right <= r.left || r.bottom <= r.top)
        return;
      Pen p(ToGdi(c), 1.0f);
      g.DrawRectangle(&p, r.left, r.top, r.right - r.left - 1,
                      r.bottom - r.top - 1);
    };
    box(lay_.bg, 0xFFFF0000);
    box(lay_.preedit, 0xFF00A000);
    for (size_t i = 0; i < lay_.cand.size(); ++i) {
      box(lay_.cand[i], 0xFFFF00FF);
      box(lay_.label[i], 0xFF0080FF);
      box(lay_.text[i], 0xFF00C0C0);
      box(lay_.comment[i], 0xFFC0A000);
    }
  }

  // ---- 合成到屏幕 ----
  LayerUpdate(memDC, rcw);

  ::SelectObject(memDC, oldBmp);
  ::DeleteObject(memBmp);
  ::DeleteDC(memDC);

  QueryPerformanceCounter(&t1);
  last_paint_ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
}

// ===========================================================================
//  6. 假引擎 —— 代替 librime
// ===========================================================================

struct Engine {
  std::wstring input;
  CandidateInfo cinfo;

  static const std::map<std::wstring, std::vector<std::wstring>>& Dict() {
    static const std::map<std::wstring, std::vector<std::wstring>> d = {
        {L"ni", {L"你", L"尼", L"泥", L"逆", L"匿"}},
        {L"hao", {L"好", L"号", L"豪", L"毫", L"耗"}},
        {L"nihao", {L"你好", L"尼豪", L"泥浩"}},
        {L"wo", {L"我", L"握", L"窝", L"沃", L"卧"}},
        {L"shi", {L"是", L"时", L"事", L"使", L"式"}},
        {L"zhong", {L"中", L"种", L"重", L"众", L"钟"}},
        {L"wen", {L"文", L"闻", L"问", L"温", L"稳"}},
        {L"zhongwen", {L"中文", L"中纹"}},
        {L"shu", {L"输", L"书", L"数", L"树", L"熟"}},
        {L"ru", {L"入", L"如", L"儒", L"乳", L"辱"}},
        {L"fa", {L"法", L"发", L"罚", L"乏", L"伐"}},
        {L"shuru", {L"输入", L"数儒"}},
        {L"shurufa", {L"输入法", L"输入发"}},
        {L"xiao", {L"小", L"笑", L"校", L"晓", L"效"}},
        {L"lang", {L"狼", L"浪", L"朗", L"郎", L"廊"}},
        {L"xiaolang", {L"小狼"}},
        {L"xiaolanghao", {L"小狼毫"}},
        {L"zhou", {L"州", L"周", L"洲", L"舟", L"粥"}},
        {L"yun", {L"韵", L"云", L"运", L"允", L"孕"}},
        {L"zhongzhouyun", {L"中州韵"}},
        {L"ce", {L"测", L"册", L"侧", L"策", L"厕"}},
        {L"shi2", {L"试"}},
        {L"ceshi", {L"测试", L"策士"}},
    };
    return d;
  }

  void Query() {
    cinfo = CandidateInfo{};
    if (input.empty())
      return;

    std::vector<std::wstring> c;
    auto it = Dict().find(input);
    if (it != Dict().end()) {
      c = it->second;
    } else {
      // 查不到就造几个占位候选，保证任何输入都能看到布局
      c = {L"〔" + input + L"〕", L"候選一", L"候選二", L"候選三", L"候選四"};
    }

    const size_t kPageSize = 5;
    if (c.size() > kPageSize)
      c.resize(kPageSize);

    for (size_t i = 0; i < c.size(); ++i) {
      cinfo.candies.push_back(c[i]);
      wchar_t lab[8];
      swprintf_s(lab, L"%d.", (int)i + 1);
      cinfo.labels.push_back(lab);
      // 给第一个候选挂个注释，演示注释列的对齐行为
      cinfo.comments.push_back(i == 0 ? input : std::wstring());
    }
    cinfo.highlighted = 0;
  }
};

// ===========================================================================
//  7. 宿主窗口 —— 模拟「正在打字的应用」
// ===========================================================================

static Panel g_panel;
static Engine g_engine;
static std::wstring g_committed = L"";
static HWND g_host = nullptr;
static HFONT g_hostFont = nullptr;
static HFONT g_hintFont = nullptr;
static int g_theme = 0;

static void ApplyTheme(Style& st, int t) {
  switch (t % 3) {
    case 0:  // 浅色
      st.back_color = 0xF8FFFFFF;
      st.border_color = 0xFFD0D0D0;
      st.shadow_color = 0x60000000;
      st.text_color = 0xFF202020;
      st.label_color = 0xFF909090;
      st.comment_color = 0xFF909090;
      st.hilited_back_color = 0xFF3478F6;
      st.hilited_text_color = 0xFFFFFFFF;
      st.hilited_label_color = 0xFFD8E4FF;
      st.hilited_comment_color = 0xFFD8E4FF;
      st.preedit_text_color = 0xFF404040;
      break;
    case 1:  // 深色
      st.back_color = 0xF02B2B2B;
      st.border_color = 0xFF505050;
      st.shadow_color = 0x80000000;
      st.text_color = 0xFFE8E8E8;
      st.label_color = 0xFF909090;
      st.comment_color = 0xFF808080;
      st.hilited_back_color = 0xFF0A84FF;
      st.hilited_text_color = 0xFFFFFFFF;
      st.hilited_label_color = 0xFFCCE0FF;
      st.hilited_comment_color = 0xFFCCE0FF;
      st.preedit_text_color = 0xFFC0C0C0;
      break;
    case 2:  // 半透明毛玻璃感（低 alpha，看清逐像素 alpha 的效果）
      st.back_color = 0x80202020;
      st.border_color = 0x60FFFFFF;
      st.shadow_color = 0x90000000;
      st.text_color = 0xFFFFFFFF;
      st.label_color = 0xFFB0B0B0;
      st.comment_color = 0xFFB0B0B0;
      st.hilited_back_color = 0xB0FF6B35;
      st.hilited_text_color = 0xFFFFFFFF;
      st.hilited_label_color = 0xFFFFE0D0;
      st.hilited_comment_color = 0xFFFFE0D0;
      st.preedit_text_color = 0xFFE0E0E0;
      break;
  }
}

// 算出「光标」在屏幕上的矩形，交给候选窗定位
static RECT CaretRect() {
  RECT cr;
  GetClientRect(g_host, &cr);
  HDC dc = GetDC(g_host);
  HGDIOBJ old = SelectObject(dc, g_hostFont);

  std::wstring line = g_committed;
  if (g_panel.style().inline_preedit)
    line += g_engine.input;  // 内嵌预编辑：拼音算进文本流

  SIZE sz = {0, 0};
  GetTextExtentPoint32W(dc, line.c_str(), (int)line.size(), &sz);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(g_host, dc);

  const int padX = 20, padY = 24;
  POINT p = {padX + sz.cx, padY};
  ClientToScreen(g_host, &p);
  RECT r = {p.x, p.y, p.x + 2, p.y + tm.tmHeight};
  return r;
}

static void SyncPanel() {
  g_engine.Query();
  Ctx ctx;
  ctx.preedit = g_engine.input;
  ctx.cinfo = g_engine.cinfo;

  if (ctx.empty()) {
    g_panel.Hide();
  } else {
    g_panel.Update(ctx);
    g_panel.MoveTo(CaretRect());
    if (!g_panel.IsShown())
      g_panel.Show();
  }
  InvalidateRect(g_host, nullptr, TRUE);
}

static void CommitAt(int index) {
  if (index < 0 || index >= (int)g_engine.cinfo.candies.size())
    return;
  g_committed += g_engine.cinfo.candies[index];
  g_engine.input.clear();
  SyncPanel();
}

static void OnPanelSelect(int i) {
  CommitAt(i);
}
static void OnPanelHover(int i) {
  g_engine.cinfo.highlighted = i;
  Ctx ctx;
  ctx.preedit = g_engine.input;
  ctx.cinfo = g_engine.cinfo;
  g_panel.Update(ctx);
}

static void DrawHost(HDC dc) {
  RECT cr;
  GetClientRect(g_host, &cr);
  FillRect(dc, &cr, (HBRUSH)GetStockObject(WHITE_BRUSH));

  SetBkMode(dc, TRANSPARENT);
  HGDIOBJ old = SelectObject(dc, g_hostFont);

  // 已上屏文本
  SetTextColor(dc, RGB(0x20, 0x20, 0x20));
  int x = 20;
  TextOutW(dc, x, 24, g_committed.c_str(), (int)g_committed.size());

  SIZE sz = {0, 0};
  GetTextExtentPoint32W(dc, g_committed.c_str(), (int)g_committed.size(), &sz);
  x += sz.cx;

  // 内嵌预编辑：拼音直接画进文本流并加下划线。
  // 真实 weasel 里这一段是应用自己画的，靠 ITfDisplayAttributeProvider
  // 提供下划线样式（WeaselTSF/EditSession.cpp:48 _ShowInlinePreedit）。
  if (g_panel.style().inline_preedit && !g_engine.input.empty()) {
    SetTextColor(dc, RGB(0x30, 0x60, 0xC0));
    TextOutW(dc, x, 24, g_engine.input.c_str(), (int)g_engine.input.size());
    SIZE ps;
    GetTextExtentPoint32W(dc, g_engine.input.c_str(),
                          (int)g_engine.input.size(), &ps);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x30, 0x60, 0xC0));
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, x, 24 + tm.tmHeight - 1, nullptr);
    LineTo(dc, x + ps.cx, 24 + tm.tmHeight - 1);
    SelectObject(dc, op);
    DeleteObject(pen);
    x += ps.cx;
  }

  // 光标
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  RECT caret = {x + 1, 24, x + 3, 24 + tm.tmHeight};
  FillRect(dc, &caret, (HBRUSH)GetStockObject(BLACK_BRUSH));

  // 帮助与状态
  SelectObject(dc, g_hintFont);
  SetTextColor(dc, RGB(0x60, 0x60, 0x60));
  const Style& st = g_panel.style();

  wchar_t buf[512];
  const wchar_t* kHelp[] = {
      L"打字：a-z 输入拼音   空格/回车 上屏   1-9 选词   ↑↓ 移动   退格 删除   Esc 清空",
      L"也可以直接用鼠标点候选、悬停预览",
      L"",
      L"F1 / Ctrl+1 投影    F2 / Ctrl+2 竖排横排   F3 / Ctrl+3 布局矩形",
      L"F4 / Ctrl+4 内嵌    F5 / Ctrl+5 配色      F6 / Ctrl+6 圆角",
      L"（功能键可能被系统输入法抢走 —— 真小狼毫就占了 F4，那就用 Ctrl+数字）",
      L"",
      L"试试这些拼音：nihao / zhongwen / shurufa / xiaolanghao / zhongzhouyun / ceshi",
  };
  int y = 90;
  for (const wchar_t* line : kHelp) {
    TextOutW(dc, 20, y, line, (int)wcslen(line));
    y += 20;
  }

  y += 8;
  SetTextColor(dc, RGB(0x20, 0x60, 0x20));
  swprintf_s(buf,
             L"投影=%s  布局=%s  矩形=%s  内嵌=%s  主题=%d  圆角=%s   |   "
             L"DPI 缩放=%.2fx   上次绘制=%.2f ms",
             st.draw_shadow ? L"开" : L"关",
             st.layout_type == LAYOUT_VERTICAL ? L"竖排" : L"横排",
             st.debug_rects ? L"开" : L"关", st.inline_preedit ? L"开" : L"关",
             g_theme % 3 + 1, st.draw_round ? L"开" : L"关",
             (double)GetDpiForWindow(g_host) / 96.0, g_panel.last_paint_ms);
  TextOutW(dc, 20, y, buf, (int)wcslen(buf));

  y += 24;
  SetTextColor(dc, RGB(0x80, 0x40, 0x00));
  const wchar_t* k =
      L"F3 打开「布局矩形」能看到测量阶段算出的每一个框："
      L"红=背景 绿=预编辑 紫=候选高亮 蓝=序号 青=候选文字 黄=注释";
  TextOutW(dc, 20, y, k, (int)wcslen(k));

  SelectObject(dc, old);
}

static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      DrawHost(dc);
      EndPaint(h, &ps);
      return 0;
    }

    case WM_CHAR: {
      wchar_t c = (wchar_t)w;
      if (c >= L'a' && c <= L'z') {
        g_engine.input += c;
        SyncPanel();
        return 0;
      }
      if (c >= L'1' && c <= L'9' && !g_engine.input.empty() &&
          !(GetKeyState(VK_CONTROL) & 0x8000)) {
        CommitAt(c - L'1');
        return 0;
      }
      if (c == L' ' || c == L'\r') {
        if (!g_engine.input.empty())
          CommitAt(g_engine.cinfo.highlighted);
        else if (c == L' ')
          g_committed += L' ';
        InvalidateRect(h, nullptr, TRUE);
        return 0;
      }
      if (c == L'\b') {
        if (!g_engine.input.empty())
          g_engine.input.pop_back();
        else if (!g_committed.empty())
          g_committed.pop_back();
        SyncPanel();
        return 0;
      }
      if (c == 27) {  // Esc
        g_engine.input.clear();
        SyncPanel();
        return 0;
      }
      return 0;
    }

    case WM_KEYDOWN: {
      Style& st = g_panel.style();
      bool handled = true;

      // 功能键常被别的软件全局抢走 —— 你机器上真正装着的那个小狼毫就占了 F4
      // （方案选单）。所以每个开关都另给一套 Ctrl+数字 的等价键。
      const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      if (ctrl && w >= '1' && w <= '6') {
        switch (w) {
          case '1': st.draw_shadow = !st.draw_shadow; break;
          case '2':
            st.layout_type = st.layout_type == LAYOUT_VERTICAL
                                 ? LAYOUT_HORIZONTAL
                                 : LAYOUT_VERTICAL;
            break;
          case '3': st.debug_rects = !st.debug_rects; break;
          case '4': st.inline_preedit = !st.inline_preedit; break;
          case '5': ApplyTheme(st, ++g_theme); break;
          case '6': st.draw_round = !st.draw_round; break;
        }
        SyncPanel();
        InvalidateRect(h, nullptr, TRUE);
        return 0;
      }

      switch (w) {
        case VK_UP:
        case VK_LEFT:
          if (!g_engine.cinfo.candies.empty()) {
            int& hi = g_engine.cinfo.highlighted;
            hi = (hi + (int)g_engine.cinfo.candies.size() - 1) %
                 (int)g_engine.cinfo.candies.size();
            Ctx c;
            c.preedit = g_engine.input;
            c.cinfo = g_engine.cinfo;
            g_panel.Update(c);
          }
          break;
        case VK_DOWN:
        case VK_RIGHT:
          if (!g_engine.cinfo.candies.empty()) {
            int& hi = g_engine.cinfo.highlighted;
            hi = (hi + 1) % (int)g_engine.cinfo.candies.size();
            Ctx c;
            c.preedit = g_engine.input;
            c.cinfo = g_engine.cinfo;
            g_panel.Update(c);
          }
          break;
        case VK_F1:
          st.draw_shadow = !st.draw_shadow;
          SyncPanel();
          break;
        case VK_F2:
          st.layout_type =
              st.layout_type == LAYOUT_VERTICAL ? LAYOUT_HORIZONTAL : LAYOUT_VERTICAL;
          SyncPanel();
          break;
        case VK_F3:
          st.debug_rects = !st.debug_rects;
          SyncPanel();
          break;
        case VK_F4:
          st.inline_preedit = !st.inline_preedit;
          SyncPanel();
          break;
        case VK_F5:
          ApplyTheme(st, ++g_theme);
          SyncPanel();
          break;
        case VK_F6:
          st.draw_round = !st.draw_round;
          SyncPanel();
          break;
        default:
          handled = false;
      }
      if (handled) {
        InvalidateRect(h, nullptr, TRUE);
        return 0;
      }
      break;
    }

    case WM_MOVE:
    case WM_SIZE:
      if (g_panel.IsShown())
        g_panel.MoveTo(CaretRect());
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// ===========================================================================
//  8. 入口
// ===========================================================================

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
  // Per-Monitor V2：多屏不同 DPI 时窗口能自己拿到正确的缩放
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  Gdiplus::GdiplusStartupInput gsi;
  ULONG_PTR gdiplusToken = 0;
  Gdiplus::GdiplusStartup(&gdiplusToken, &gsi, nullptr);

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = HostProc;
  wc.hInstance = hInst;
  wc.lpszClassName = L"DemoHostWindow";
  wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
  wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  RegisterClassExW(&wc);

  g_host = CreateWindowExW(0, L"DemoHostWindow",
                           L"候选窗渲染 Demo —— 模拟宿主应用",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           1000, 420, nullptr, nullptr, hInst, nullptr);
  if (!g_host)
    return 1;

  // 关掉宿主窗口的系统输入法。否则你真正装着的那个小狼毫会先把 a-z 吃掉，
  // demo 根本收不到 WM_CHAR —— 两个输入法抢同一串按键。
  ImmAssociateContext(g_host, nullptr);

  const UINT dpi = GetDpiForWindow(g_host);
  auto mkFont = [&](int pt, const wchar_t* face) {
    return CreateFontW(-MulDiv(pt, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
  };
  g_hostFont = mkFont(18, L"Microsoft YaHei UI");
  g_hintFont = mkFont(10, L"Microsoft YaHei UI");

  g_panel.Create(hInst, g_host);
  g_panel.on_select = &OnPanelSelect;
  g_panel.on_hover = &OnPanelHover;
  ApplyTheme(g_panel.style(), g_theme);

  ShowWindow(g_host, SW_SHOW);
  UpdateWindow(g_host);

  // 开场就摆一组候选出来，不用打字也能看到效果
  g_engine.input = L"nihao";
  SyncPanel();
  SetFocus(g_host);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (g_hostFont)
    DeleteObject(g_hostFont);
  if (g_hintFont)
    DeleteObject(g_hintFont);
  Gdiplus::GdiplusShutdown(gdiplusToken);
  return 0;
}
