#include "HudUI.h"

#include "Settings.h"
#include "Visibility.h"
#include "Vitals.h"

#include "SKSEMenuFramework.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

// "Spire": one thin-line frame split by a diagonal divider. Every diagonal is 30 degrees off
// vertical; the top edge rises to a point on the right, the divider and the bottom edge fall
// to the right. A band of Edge UI gold fading to black lines each box between the frame and
// its bar track (an emptying bar leaves its track clear). Health fills UP from the divider;
// magicka | stamina sit side by side below it and fill DOWN from the divider.
// Charging a spell grows a pink -> purple glow clockwise round the lower box from its
// top-left corner (pulsing once fully charged and held); a shout cooldown grows a white glow
// from the top point down the top edge and the whole left side. Both glow outward from
// behind the frame lines; above the divider the cast glow is drawn over the health section.
// Both disappear when finished. Bars are a fixed size at any level.
namespace {
    namespace DL = ImGuiMCP::ImDrawListManager;
    using ImGuiMCP::ImVec2;
    using ImGuiMCP::ImU32;

    // Frame geometry in reference units; scale 1 = screen px.
    constexpr float XL = 0.0f, XR = 30.0f, W = XR - XL;
    constexpr float K = 1.7320508f;         // tan 60: a diagonal drops K units per unit across
    constexpr float H = W * K;              // drop of a diagonal across the frame
    constexpr float TOP = 0.0f, DIV = 275.0f, BOT = 410.0f;
    constexpr float PAD = 6.0f;             // bar inset from the frame, square to each edge (= the gold band)
    constexpr float GAP = PAD * 2.0f;       // the same inset measured vertically from a diagonal (1 / cos 60)
    constexpr float SPLIT = 2.0f;           // half the gap between magicka and stamina
    constexpr float MARGIN = 56.0f;         // room round the frame for the glows (the window clips)
    constexpr float CAST_GLOW = 3.0f;       // cast glow width outward from the frame line
    constexpr float FRINGE = 0.8f;          // soft edge past the glow ends

    float TopY(float x) { return TOP + H - (x - XL) * K; }
    float DivY(float x) { return DIV + (x - XL) * K; }
    float BotY(float x) { return BOT + (x - XL) * K; }

    struct Rgb { int r, g, b; };
    struct Stat { Rgb hi, mid, lo, rim, label; };
    constexpr Stat kHealth { {242,  83,  75}, {214,  48,  47}, {154,  26,  33}, {255, 125, 114}, {215, 169, 164} };
    constexpr Stat kMagicka{ { 67, 160, 242}, { 31, 112, 210}, { 12,  67, 146}, {130, 198, 255}, {157, 187, 224} };
    constexpr Stat kStamina{ {108, 194,  96}, { 63, 155,  64}, { 34, 102,  42}, {160, 230, 144}, {166, 201, 162} };
    constexpr Rgb  kLine   {  96, 102, 109 };
    constexpr Rgb  kPink   { 255,  92, 207 };
    constexpr Rgb  kPurple { 143,  75, 255 };
    constexpr Rgb  kLilac  { 255, 196, 240 };
    constexpr Rgb  kWhite  { 248, 248, 248 };

    // Edge UI gold (its SkyPrompt icon set: #f6d884 highlight, #7e6024 deep) -> black, by
    // distance in from the frame line. The last stop is a soft edge onto the empty track.
    struct Stop { float d; Rgb c; float a; };
    constexpr Stop kGold[4] = {
        { 0.0f,        { 227, 195, 114 }, 0.92f },
        { PAD * 0.45f, { 126,  96,  36 }, 0.88f },
        { PAD - 0.4f,  {   0,   0,   0 }, 0.88f },
        { PAD + 0.4f,  {   0,   0,   0 }, 0.0f  },
    };

    ImGuiMCP::ImTextureID g_grain = nullptr;
    bool g_registered = false;
    ImVec2 g_white{ 0.0f, 0.0f };   // font atlas white-pixel UV, fetched once per frame

    constexpr int OverlayFlags =
        ImGuiMCP::ImGuiWindowFlags_NoTitleBar
        | ImGuiMCP::ImGuiWindowFlags_NoResize
        | ImGuiMCP::ImGuiWindowFlags_NoMove
        | ImGuiMCP::ImGuiWindowFlags_NoScrollbar
        | ImGuiMCP::ImGuiWindowFlags_NoCollapse
        | ImGuiMCP::ImGuiWindowFlags_NoBackground
        | ImGuiMCP::ImGuiWindowFlags_NoSavedSettings
        | ImGuiMCP::ImGuiWindowFlags_NoInputs
        | ImGuiMCP::ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiMCP::ImGuiWindowFlags_NoNav
        | ImGuiMCP::ImGuiWindowFlags_NoDocking
        | ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize;

    ImU32 Col(Rgb c, float a) {
        const int ai = static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
        return IM_COL32(c.r, c.g, c.b, ai);
    }
    Rgb Mix(Rgb a, Rgb b, float t) {
        return { static_cast<int>(a.r + (b.r - a.r) * t), static_cast<int>(a.g + (b.g - a.g) * t),
                 static_cast<int>(a.b + (b.b - a.b) * t) };
    }
    ImVec2 Lerp(ImVec2 a, ImVec2 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }

    float Seconds() {
        using clock = std::chrono::steady_clock;
        static const clock::time_point s0 = clock::now();
        return std::chrono::duration<float>(clock::now() - s0).count();
    }

    // Reference units -> screen px.
    struct Xf {
        ImVec2 o;
        float  s;
        ImVec2 operator()(float x, float y) const { return { o.x + x * s, o.y + y * s }; }
        ImVec2 operator()(ImVec2 p) const { return (*this)(p.x, p.y); }
    };

    // The SKSEMenuFramework.h wrappers resolve the export with GetProcAddress on every call;
    // these three run a few hundred times a frame, so resolve them once.
    template <class F>
    F Export(const char* name) {
        const HMODULE m = ::GetModuleHandleW(L"SKSEMenuFramework");
        return m ? reinterpret_cast<F>(::GetProcAddress(m, name)) : nullptr;
    }
    using PrimReserveFn  = void (*)(ImGuiMCP::ImDrawList*, int, int);
    using PrimWriteVtxFn = void (*)(ImGuiMCP::ImDrawList*, const ImVec2, const ImVec2, ImU32);
    using PrimWriteIdxFn = void (*)(ImGuiMCP::ImDrawList*, ImGuiMCP::ImDrawIdx);
    PrimReserveFn  g_primReserve  = nullptr;
    PrimWriteVtxFn g_primWriteVtx = nullptr;
    PrimWriteIdxFn g_primWriteIdx = nullptr;

    // Quad tl-tr-br-bl filled with a left->right gradient (hi -> mid at 45% -> lo),
    // then grain and a light rim, like the approved render.
    void FillBar(ImGuiMCP::ImDrawList* dl, ImVec2 tl, ImVec2 tr, ImVec2 br, ImVec2 bl,
                 const Stat& st, float a, float s, bool grain) {
        if (g_primReserve && g_primWriteVtx && g_primWriteIdx) {
            const ImVec2 wp = g_white;
            const ImVec2 mt = Lerp(tl, tr, 0.45f), mb = Lerp(bl, br, 0.45f);
            g_primReserve(dl, 12, 6);
            const auto base = static_cast<ImGuiMCP::ImDrawIdx>(dl->_VtxCurrentIdx);
            // vertices: 0 tl, 1 mt, 2 tr, 3 bl, 4 mb, 5 br
            static constexpr int kIdx[12] = { 0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4 };
            for (int i : kIdx) g_primWriteIdx(dl, static_cast<ImGuiMCP::ImDrawIdx>(base + i));
            g_primWriteVtx(dl, tl, wp, Col(st.hi, a));
            g_primWriteVtx(dl, mt, wp, Col(st.mid, a));
            g_primWriteVtx(dl, tr, wp, Col(st.lo, a));
            g_primWriteVtx(dl, bl, wp, Col(st.hi, a));
            g_primWriteVtx(dl, mb, wp, Col(st.mid, a));
            g_primWriteVtx(dl, br, wp, Col(st.lo, a));
        } else {
            DL::AddQuadFilled(dl, tl, tr, br, bl, Col(st.mid, a));
        }
        if (grain && g_grain) {
            // Screen-space UVs: the 128 px grain tile repeats (the DX11 sampler wraps).
            auto uv = [](ImVec2 p) { return ImVec2{ p.x / 128.0f, p.y / 128.0f }; };
            DL::AddImageQuad(dl, g_grain, tl, tr, br, bl, uv(tl), uv(tr), uv(br), uv(bl), Col({ 255, 255, 255 }, 0.32f * a));
        }
        const ImVec2 rim[4] = { tl, tr, br, bl };
        DL::AddPolyline(dl, rim, 4, Col(st.rim, 0.55f * a), ImGuiMCP::ImDrawFlags_Closed, std::max(0.9f * s, 1.0f));
    }

    // Health: anchored on the divider, grows up toward the top edge. Its two ends follow the
    // top edge and the divider, which slope opposite ways, so each side fills on its own.
    void BarUp(ImGuiMCP::ImDrawList* dl, const Xf& X, float L, float R, float v, const Stat& st, float a, bool grain) {
        if (v <= 0.0f) return;
        v = std::min(v, 1.0f);
        const float tL = TopY(L) + GAP, tR = TopY(R) + GAP, bL = DivY(L) - GAP, bR = DivY(R) - GAP;
        FillBar(dl, X(L, bL - (bL - tL) * v), X(R, bR - (bR - tR) * v), X(R, bR), X(L, bL), st, a, X.s, grain);
    }

    // Magicka / stamina: anchored on the divider, grow down toward the bottom edge.
    void BarDown(ImGuiMCP::ImDrawList* dl, const Xf& X, float L, float R, float v, const Stat& st, float a, bool grain) {
        if (v <= 0.0f) return;
        v = std::min(v, 1.0f);
        const float tL = DivY(L) + GAP, tR = DivY(R) + GAP, bL = BotY(L) - GAP, bR = BotY(R) - GAP;
        FillBar(dl, X(L, tL), X(R, tR), X(R, tR + (bR - tR) * v), X(L, tL + (bL - tL) * v), st, a, X.s, grain);
    }

    // ---- per-vertex coloured meshes (reference units until written) ----

    struct MV { ImVec2 p; Rgb c; float a; };
    struct Row { float t, a; };            // distance outward from the path, alpha there
    enum class Pass { All, Below, Above }; // Below / Above: only the part under / over the split

    // Convex polygon as a triangle fan.
    void Fan(ImGuiMCP::ImDrawList* dl, const Xf& X, const MV* v, int n, float A) {
        if (n < 3) return;
        if (g_primReserve && g_primWriteVtx && g_primWriteIdx) {
            g_primReserve(dl, (n - 2) * 3, n);
            const auto base = static_cast<ImGuiMCP::ImDrawIdx>(dl->_VtxCurrentIdx);
            for (int i = 1; i + 1 < n; ++i) {
                g_primWriteIdx(dl, base);
                g_primWriteIdx(dl, static_cast<ImGuiMCP::ImDrawIdx>(base + i));
                g_primWriteIdx(dl, static_cast<ImGuiMCP::ImDrawIdx>(base + i + 1));
            }
            for (int i = 0; i < n; ++i) g_primWriteVtx(dl, X(v[i].p), g_white, Col(v[i].c, v[i].a * A));
        } else {
            ImVec2 pts[8];
            float a = 0.0f;
            for (int i = 0; i < n; ++i) { pts[i] = X(v[i].p); a += v[i].a; }
            DL::AddConvexPolyFilled(dl, pts, n, Col(v[0].c, a / n * A));
        }
    }

    // The split runs just under the divider line, so the part above it covers the line too.
    float SplitSide(ImVec2 p) { return p.y - (DivY(p.x) + 1.0f); }

    // Part of a convex polygon on one side of the split (Sutherland-Hodgman, one plane).
    int ClipSplit(const MV* in, int n, bool above, MV* out) {
        auto keep = [above](const MV& v) { const float s = SplitSide(v.p); return above ? -s : s; };
        int k = 0;
        for (int i = 0; i < n; ++i) {
            const MV& a = in[i];
            const MV& b = in[(i + 1) % n];
            const float sa = keep(a), sb = keep(b);
            if (sa >= 0.0f) out[k++] = a;
            if ((sa >= 0.0f) != (sb >= 0.0f)) {
                const float t = sa / (sa - sb);
                out[k++] = { Lerp(a.p, b.p, t), Mix(a.c, b.c, t), a.a + (b.a - a.a) * t };
            }
        }
        return k;
    }

    void Quad(ImGuiMCP::ImDrawList* dl, const Xf& X, const MV* q, Pass pass, float A) {
        if (pass == Pass::All) { Fan(dl, X, q, 4, A); return; }
        MV out[8];
        const int n = ClipSplit(q, 4, pass == Pass::Above, out);
        Fan(dl, X, out, n, A);
    }

    ImVec2 Isect(ImVec2 p, ImVec2 d, ImVec2 q, ImVec2 e) {
        const float den = d.x * e.y - d.y * e.x;
        if (std::fabs(den) < 1e-6f) return p;
        const float t = ((q.x - p.x) * e.y - (q.y - p.y) * e.x) / den;
        return { p.x + d.x * t, p.y + d.y * t };
    }

    // Clockwise (on screen) 4-sided convex polygon moved in by d along each edge, corners mitred.
    void Inset(const ImVec2* p, float d, ImVec2* out) {
        ImVec2 o[4], dir[4];
        for (int i = 0; i < 4; ++i) {
            const ImVec2 a = p[i], b = p[(i + 1) % 4];
            const float l = std::hypot(b.x - a.x, b.y - a.y);
            dir[i] = { (b.x - a.x) / l, (b.y - a.y) / l };
            o[i] = { a.x - dir[i].y * d, a.y + dir[i].x * d };
        }
        for (int i = 0; i < 4; ++i) {
            const int h = (i + 3) % 4;
            out[i] = Isect(o[h], dir[h], o[i], dir[i]);
        }
    }

    // Gold -> black band between a box's frame line and its bar track.
    void GoldBand(ImGuiMCP::ImDrawList* dl, const Xf& X, const ImVec2* cell, float A) {
        ImVec2 r[4][4];
        for (int k = 0; k < 4; ++k) Inset(cell, kGold[k].d, r[k]);
        for (int k = 0; k + 1 < 4; ++k) {
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 1) % 4;
                const MV q[4] = { { r[k][i], kGold[k].c, kGold[k].a }, { r[k][j], kGold[k].c, kGold[k].a },
                                  { r[k + 1][j], kGold[k + 1].c, kGold[k + 1].a }, { r[k + 1][i], kGold[k + 1].c, kGold[k + 1].a } };
                Fan(dl, X, q, 4, A);
            }
        }
    }

    Rgb CastRgb(ImVec2 p) {
        const float y0 = DivY(XL), y1 = BotY(XR);
        return Mix(kPink, kPurple, std::clamp((p.y - y0) / (y1 - y0), 0.0f, 1.0f));
    }

    // A glow band along a path on its outward side (away from c): one strip per pair of rows.
    // Corners are mitred. An open path's ends are cut parallel to the frame - a diagonal
    // segment ends on a vertical cut, a vertical one on a cut along the diagonals - with a
    // short soft edge past the cut.
    void Band(ImGuiMCP::ImDrawList* dl, const Xf& X, const ImVec2* in, int nIn, bool closed, ImVec2 c,
              const Row* rows, int nr, bool castColours, Rgb solid, float A, Pass pass) {
        ImVec2 p[8];
        int n = 0;
        for (int i = 0; i < nIn && n < 8; ++i)
            if (n == 0 || std::hypot(in[i].x - p[n - 1].x, in[i].y - p[n - 1].y) > 1e-3f) p[n++] = in[i];
        if (closed && n > 2 && std::hypot(p[0].x - p[n - 1].x, p[0].y - p[n - 1].y) <= 1e-3f) --n;
        if (n < 2 || (closed && n < 3)) return;

        const int ns = closed ? n : n - 1;
        ImVec2 d[8], nm[8], cut[8];
        for (int i = 0; i < ns; ++i) {
            const ImVec2 a = p[i], b = p[(i + 1) % n];
            const float l = std::hypot(b.x - a.x, b.y - a.y);
            d[i] = { (b.x - a.x) / l, (b.y - a.y) / l };
            nm[i] = { -d[i].y, d[i].x };
            const ImVec2 m{ (a.x + b.x) * 0.5f - c.x, (a.y + b.y) * 0.5f - c.y };
            if (nm[i].x * m.x + nm[i].y * m.y < 0.0f) nm[i] = { -nm[i].x, -nm[i].y };
            cut[i] = std::fabs(d[i].x) < 1e-4f ? ImVec2{ 0.5f, 0.8660254f } : ImVec2{ 0.0f, 1.0f };
        }
        auto at = [&](int j, float t) -> ImVec2 {
            auto off = [&](int s) { return ImVec2{ p[j].x + nm[s].x * t, p[j].y + nm[s].y * t }; };
            if (!closed && j == 0) return Isect(off(0), d[0], p[0], cut[0]);
            if (!closed && j == n - 1) return Isect(off(ns - 1), d[ns - 1], p[j], cut[ns - 1]);
            const int s1 = (j + ns - 1) % ns, s2 = j % ns;
            return Isect(off(s1), d[s1], off(s2), d[s2]);
        };
        auto mv = [&](ImVec2 q, float a) { return MV{ q, castColours ? CastRgb(q) : solid, a }; };

        for (int i = 0; i < ns; ++i) {
            const int j0 = i, j1 = (i + 1) % n;
            for (int r = 0; r + 1 < nr; ++r) {
                const MV q[4] = { mv(at(j0, rows[r].t), rows[r].a), mv(at(j1, rows[r].t), rows[r].a),
                                  mv(at(j1, rows[r + 1].t), rows[r + 1].a), mv(at(j0, rows[r + 1].t), rows[r + 1].a) };
                Quad(dl, X, q, pass, A);
            }
        }
        if (closed) return;
        // Soft edges past the two cuts.
        for (int e = 0; e < 2; ++e) {
            const int j = e == 0 ? 0 : n - 1;
            const ImVec2 dir = e == 0 ? ImVec2{ -d[0].x, -d[0].y } : d[ns - 1];
            for (int r = 0; r + 1 < nr; ++r) {
                const ImVec2 a = at(j, rows[r].t), b = at(j, rows[r + 1].t);
                const ImVec2 a2{ a.x + dir.x * FRINGE, a.y + dir.y * FRINGE };
                const ImVec2 b2{ b.x + dir.x * FRINGE, b.y + dir.y * FRINGE };
                const MV q[4] = { mv(a, rows[r].a), mv(b, rows[r + 1].a), mv(b2, 0.0f), mv(a2, 0.0f) };
                Quad(dl, X, q, pass, A);
            }
        }
    }

    // First `f` (0..1) of an open polyline, by length.
    int PartialPath(const ImVec2* pts, int n, float f, ImVec2* out) {
        float total = 0.0f;
        for (int i = 1; i < n; ++i) total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        float left = total * std::clamp(f, 0.0f, 1.0f);
        int k = 0;
        out[k++] = pts[0];
        for (int i = 1; i < n && left > 0.0f; ++i) {
            const float l = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
            if (left >= l) { out[k++] = pts[i]; left -= l; }
            else { out[k++] = Lerp(pts[i - 1], pts[i], left / l); left = 0.0f; }
        }
        return k;
    }

    // Find the widget's own face in SMF's font atlas (SMF loads every .ttf in
    // Data/SKSE/Plugins/Fonts as "<file>, <size>px"). Scanned every frame: an atlas rebuild
    // (SMF font settings) replaces the ImFont objects, so a cached pointer could dangle.
    // Picks the largest baked size so scaled-down numbers stay sharp.
    ImGuiMCP::ImFont* FindFont() {
        auto* io = ImGuiMCP::GetIO();
        if (!io || !io->Fonts) return nullptr;
        ImGuiMCP::ImFont* best = nullptr;
        float bestSize = 0.0f;
        const auto& fonts = io->Fonts->Fonts;
        for (int i = 0; i < fonts.Size; ++i) {
            auto* f = fonts.Data[i];
            if (!f || !f->ConfigData) continue;
            if (_strnicmp(f->ConfigData->Name, "MinimalVitals", 13) != 0) continue;
            if (f->FontSize > bestSize) { best = f; bestSize = f->FontSize; }
        }
        return best ? best : ImGuiMCP::GetFont();
    }

    void Number(ImGuiMCP::ImDrawList* dl, ImGuiMCP::ImFont* font, float px, ImVec2 baseline, float value, Rgb col, float a) {
        char txt[16];
        std::snprintf(txt, sizeof(txt), "%d", static_cast<int>(std::lround(value)));
        const ImVec2 pos{ std::round(baseline.x), std::round(baseline.y - px * 0.78f) };
        DL::AddText(dl, font, px, { pos.x + 1.0f, pos.y + 1.0f }, Col({ 0, 0, 0 }, 0.55f * a), txt);
        DL::AddText(dl, font, px, pos, Col(col, a), txt);
    }

    void __stdcall Render() {
        Settings::Config cfg;
        { auto lk = Settings::Lock(); cfg = Settings::Get(); }
        if (cfg.preview) {
            if (!Visibility::CanPreview()) return;
        } else {
            if (!cfg.enabled || !Visibility::ShouldRender()) return;
        }

        Vitals::QueueRead();
        auto v = Vitals::Get();
        if (cfg.preview) {
            // Loop a fake cast (2 s charge, 1.5 s held) and shout cooldown (3 s) so the
            // indicators can be positioned without casting.
            const float t = std::fmod(Seconds(), 5.0f);
            v.casting = t < 3.5f;
            v.castHeld = t >= 2.0f && t < 3.5f;
            v.castProgress = v.castHeld ? 1.0f : t / 2.0f;
            v.shoutCooling = t < 3.0f;
            v.shoutProgress = t / 3.0f;
            if (!v.valid) { v.hp = 412; v.hpMax = 530; v.mp = 148; v.mpMax = 260; v.sp = 201; v.spMax = 310; }
        } else if (!v.valid) {
            return;
        }

        const float s = std::clamp(cfg.scale, 0.25f, 4.0f);
        const float A = std::clamp(cfg.alpha, 0.0f, 1.0f);
        const float lineW = std::max(cfg.lineWidth * s, 1.0f);
        const float M = MARGIN * s;
        const float textRef = cfg.textSize;
        const float labelRef = cfg.showNumbers ? 10.0f + textRef * 3.2f : 0.0f;

        // The window carries a margin for the glows; the frame stays where it was before
        // the margin grew (x/y + window padding + 10 px).
        ImGuiMCP::SetNextWindowPos({ cfg.x - (M - 10.0f * s), cfg.y - (M - 10.0f * s) }, ImGuiMCP::ImGuiCond_Always, { 0, 0 });
        bool open = true;
        if (!ImGuiMCP::Begin("##minimalvitals", &open, OverlayFlags)) { ImGuiMCP::End(); return; }
        const ImVec2 origin = ImGuiMCP::GetCursorScreenPos();
        ImGuiMCP::Dummy({ (W + labelRef) * s + 2.0f * M, (BOT + H) * s + 2.0f * M });
        auto* dl = ImGuiMCP::GetWindowDrawList();
        if (!dl) { ImGuiMCP::End(); return; }
        g_white = ImGuiMCP::GetFontTexUvWhitePixel();

        const Xf X{ { origin.x + M, origin.y + M }, s };
        auto ratio = [](float cur, float max) { return max > 0.0f ? std::clamp(cur / max, 0.0f, 1.0f) : 0.0f; };

        const ImVec2 upper[4] = { { XL, TopY(XL) }, { XR, TOP }, { XR, DivY(XR) }, { XL, DIV } };
        const ImVec2 lower[4] = { { XL, DIV }, { XR, DivY(XR) }, { XR, BotY(XR) }, { XL, BOT } };
        const ImVec2 frameC{ (XL + XR) * 0.5f, (TOP + BOT + H) * 0.5f };
        const ImVec2 lowerC{ (XL + XR) * 0.5f, (DIV + BOT + H) * 0.5f };
        const float t0 = -0.5f * cfg.lineWidth;   // glows start under the frame line

        // 1. Gold bands.
        GoldBand(dl, X, upper, A);
        GoldBand(dl, X, lower, A);

        // 2. Shout cooldown: from the top point down the top edge, then down the left side.
        if (cfg.shoutIndicator && v.shoutCooling && v.shoutProgress > 0.0f) {
            const float sw = std::clamp(cfg.shoutGlow, 2.0f, 20.0f);
            const Row rows[5] = { { t0 - 0.6f, 0.0f }, { t0 + 0.4f, 1.0f }, { sw - 0.5f, 1.0f }, { sw + 0.5f, 0.5f }, { sw + 7.0f, 0.0f } };
            const ImVec2 edge[3] = { upper[1], upper[0], lower[3] };
            ImVec2 part[4];
            const int n = PartialPath(edge, 3, v.shoutProgress, part);
            Band(dl, X, part, n, false, frameC, rows, 5, false, kWhite, A, Pass::All);
        }

        // Casting: clockwise round the lower box from its top-left corner. Fully charged and
        // held -> the whole outline glows and pulses. Drawn in two passes split at the
        // divider: behind the frame lines below it, over the health section above it.
        const bool casting = cfg.castIndicator && v.casting && (v.castHeld || v.castProgress > 0.0f);
        const float pulse = 0.5f + 0.5f * std::sin(Seconds() * 6.2831853f / 0.9f);
        auto drawCast = [&](Pass pass) {
            const ImVec2 loop[5] = { lower[0], lower[1], lower[2], lower[3], lower[0] };
            if (v.castHeld) {
                const Row glow[5] = { { t0 - 0.6f, 0.0f }, { t0 + 0.4f, 1.0f }, { CAST_GLOW - 0.5f, 1.0f },
                                      { CAST_GLOW + 0.5f, 0.45f + 0.35f * pulse }, { CAST_GLOW + 8.0f + 3.0f * pulse, 0.0f } };
                const Row hot[4] = { { t0 - 0.6f, 0.0f }, { t0 + 0.4f, 0.45f * pulse }, { 1.2f, 0.45f * pulse }, { 2.0f, 0.0f } };
                Band(dl, X, loop, 4, true, lowerC, glow, 5, true, kPink, A, pass);
                Band(dl, X, loop, 4, true, lowerC, hot, 4, false, kLilac, A, pass);
            } else {
                const Row glow[5] = { { t0 - 0.6f, 0.0f }, { t0 + 0.4f, 1.0f }, { CAST_GLOW - 0.5f, 1.0f },
                                      { CAST_GLOW + 0.5f, 0.55f }, { CAST_GLOW + 7.0f, 0.0f } };
                ImVec2 part[8];
                const int n = PartialPath(loop, 5, v.castProgress, part);
                Band(dl, X, part, n, false, lowerC, glow, 5, true, kPink, A, pass);
            }
        };
        if (casting) drawCast(Pass::Below);

        // 3. Bars.
        const float L = XL + PAD, R = XR - PAD, mid = (L + R) * 0.5f;
        BarUp(dl, X, L, R, ratio(v.hp, v.hpMax), kHealth, A, cfg.grain);
        BarDown(dl, X, L, mid - SPLIT, ratio(v.mp, v.mpMax), kMagicka, A, cfg.grain);
        BarDown(dl, X, mid + SPLIT, R, ratio(v.sp, v.spMax), kStamina, A, cfg.grain);

        // 4. Frame: one closed outline + the divider meeting both sides.
        const ImVec2 frame[4] = { X(upper[0]), X(upper[1]), X(lower[2]), X(lower[3]) };
        DL::AddPolyline(dl, frame, 4, Col(kLine, A), ImGuiMCP::ImDrawFlags_Closed, lineW);
        DL::AddLine(dl, X(lower[0]), X(lower[1]), Col(kLine, A), lineW);

        // 5. Cast glow over the health section.
        if (casting) drawCast(Pass::Above);

        // Current values (they grow with level; the bars do not).
        if (cfg.showNumbers) {
            auto* font = FindFont();
            const float px = textRef * s;
            const float tx = XR + 12.0f;
            const float yH = DivY(XR) - 14.0f, yM = yH + textRef * 2.857f, yS = yM + textRef * 1.4286f;
            Number(dl, font, px, X(tx, yH), v.hp, kHealth.label, A);
            Number(dl, font, px, X(tx, yM), v.mp, kMagicka.label, A);
            Number(dl, font, px, X(tx, yS), v.sp, kStamina.label, A);
        }

        ImGuiMCP::End();
    }
}

namespace HudUI {

    void Register() {
        if (g_registered) return;
        const bool installed = SKSEMenuFramework::IsInstalled();
        const HMODULE handle = ::GetModuleHandleW(L"SKSEMenuFramework");
        SKSE::log::info("HudUI::Register - IsInstalled={} GetModuleHandle={}", installed, static_cast<void*>(handle));
        if (!installed || !handle) {
            SKSE::log::error("SKSE Menu Framework not loadable - widget won't render");
            return;
        }
        g_primReserve  = Export<PrimReserveFn>("ImDrawList_PrimReserve");
        g_primWriteVtx = Export<PrimWriteVtxFn>("ImDrawList_PrimWriteVtx");
        g_primWriteIdx = Export<PrimWriteIdxFn>("ImDrawList_PrimWriteIdx");
        SKSE::log::info("HudUI::Register - gradient prims {}", (g_primReserve && g_primWriteVtx && g_primWriteIdx) ? "ok" : "MISSING (flat fills)");

        constexpr const char* kGrain = "Data/Interface/MinimalVitals/grain.dds";
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::current_path() / kGrain, ec)) {
            g_grain = SKSEMenuFramework::LoadTexture(kGrain);
        }
        SKSE::log::info("HudUI::Register - grain texture {}", g_grain ? "loaded" : "not loaded (plain fills)");

        const bool font = std::filesystem::exists(std::filesystem::current_path() / "Data/SKSE/Plugins/Fonts/MinimalVitals.ttf", ec);
        SKSE::log::info("HudUI::Register - number font MinimalVitals.ttf {}", font ? "present" : "missing (SMF default font)");

        SKSEMenuFramework::AddHudElement(Render);
        g_registered = true;
        SKSE::log::info("HudUI::Register - HUD element registered");
    }
}
