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

// "Spire": one thin-line parallelogram frame, every diagonal at 45 degrees, split by a
// diagonal divider. Health fills UP from the divider; magicka | stamina sit side by side
// below it and fill DOWN from the divider. Charging a spell draws an orange line clockwise
// round the lower box from its top-left corner (pulsing once fully charged and held);
// a shout cooldown draws a white line down the upper right edge from the top corner to
// the divider. Both disappear when finished. Bars are a fixed size at any level.
namespace {
    namespace DL = ImGuiMCP::ImDrawListManager;
    using ImGuiMCP::ImVec2;
    using ImGuiMCP::ImU32;

    // Frame geometry in reference units; scale 1 = screen px.
    constexpr float XL = 0.0f, XR = 30.0f, W = XR - XL;
    constexpr float TOP = 0.0f, DIV = 275.0f, BOT = 410.0f;
    constexpr float PAD = 6.0f;     // bar inset from the vertical sides
    constexpr float GAP = 8.5f;     // vertical inset from a 45-degree edge (= PAD measured square to it)
    constexpr float SPLIT = 2.0f;   // half the gap between magicka and stamina

    struct Rgb { int r, g, b; };
    struct Stat { Rgb hi, mid, lo, rim, label; };
    constexpr Stat kHealth { {242,  83,  75}, {214,  48,  47}, {154,  26,  33}, {255, 125, 114}, {215, 169, 164} };
    constexpr Stat kMagicka{ { 67, 160, 242}, { 31, 112, 210}, { 12,  67, 146}, {130, 198, 255}, {157, 187, 224} };
    constexpr Stat kStamina{ {108, 194,  96}, { 63, 155,  64}, { 34, 102,  42}, {160, 230, 144}, {166, 201, 162} };
    constexpr Rgb  kLine     { 96, 102, 109 };
    constexpr Rgb  kOrange   { 255, 138,  30 };
    constexpr Rgb  kOrangeHi { 255, 196, 120 };
    constexpr Rgb  kWhite    { 244, 244, 244 };

    ImGuiMCP::ImTextureID g_grain = nullptr;
    bool g_registered = false;

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
    };

    // The SKSEMenuFramework.h wrappers resolve the export with GetProcAddress on every call;
    // these three run a few dozen times a frame, so resolve them once.
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

    // Parallelogram tl-tr-br-bl filled with a left->right gradient (hi -> mid at 45% -> lo),
    // then grain and a light rim, like the approved render.
    void FillBar(ImGuiMCP::ImDrawList* dl, ImVec2 tl, ImVec2 tr, ImVec2 br, ImVec2 bl,
                 const Stat& st, float a, float s, bool grain) {
        if (g_primReserve && g_primWriteVtx && g_primWriteIdx) {
            const ImVec2 wp = ImGuiMCP::GetFontTexUvWhitePixel();
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

    // Health: anchored on the divider, grows up toward the top edge.
    void BarUp(ImGuiMCP::ImDrawList* dl, const Xf& X, float L, float R, float v, const Stat& st, float a, bool grain) {
        if (v <= 0.0f) return;
        const float w = R - L, yT = TOP + (L - XL) + GAP, yB = DIV + (L - XL) - GAP;
        const float yl = yB - (yB - yT) * std::min(v, 1.0f);
        FillBar(dl, X(L, yl), X(R, yl + w), X(R, yB + w), X(L, yB), st, a, X.s, grain);
    }

    // Magicka / stamina: anchored on the divider, grow down toward the bottom edge.
    void BarDown(ImGuiMCP::ImDrawList* dl, const Xf& X, float L, float R, float v, const Stat& st, float a, bool grain) {
        if (v <= 0.0f) return;
        const float w = R - L, yT = DIV + (L - XL) + GAP, yB = BOT + (L - XL) - GAP;
        const float yl = yT + (yB - yT) * std::min(v, 1.0f);
        FillBar(dl, X(L, yT), X(R, yT + w), X(R, yl + w), X(L, yl), st, a, X.s, grain);
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
        const float margin = 10.0f * s;
        const float textRef = cfg.textSize;
        const float labelRef = cfg.showNumbers ? 10.0f + textRef * 3.2f : 0.0f;

        ImGuiMCP::SetNextWindowPos({ cfg.x, cfg.y }, ImGuiMCP::ImGuiCond_Always, { 0, 0 });
        bool open = true;
        if (!ImGuiMCP::Begin("##minimalvitals", &open, OverlayFlags)) { ImGuiMCP::End(); return; }
        const ImVec2 origin = ImGuiMCP::GetCursorScreenPos();
        ImGuiMCP::Dummy({ (W + labelRef) * s + 2.0f * margin, (BOT + W) * s + 2.0f * margin });
        auto* dl = ImGuiMCP::GetWindowDrawList();
        if (!dl) { ImGuiMCP::End(); return; }

        const Xf X{ { origin.x + margin, origin.y + margin }, s };
        auto ratio = [](float cur, float max) { return max > 0.0f ? std::clamp(cur / max, 0.0f, 1.0f) : 0.0f; };

        // Bars (under the frame lines).
        const float L = XL + PAD, R = XR - PAD, mid = (L + R) * 0.5f;
        BarUp(dl, X, L, R, ratio(v.hp, v.hpMax), kHealth, A, cfg.grain);
        BarDown(dl, X, L, mid - SPLIT, ratio(v.mp, v.mpMax), kMagicka, A, cfg.grain);
        BarDown(dl, X, mid + SPLIT, R, ratio(v.sp, v.spMax), kStamina, A, cfg.grain);

        // Frame: one closed outline + the divider meeting both sides.
        const ImVec2 frame[4] = { X(XL, TOP), X(XR, TOP + W), X(XR, BOT + W), X(XL, BOT) };
        DL::AddPolyline(dl, frame, 4, Col(kLine, A), ImGuiMCP::ImDrawFlags_Closed, lineW);
        DL::AddLine(dl, X(XL, DIV), X(XR, DIV + W), Col(kLine, A), lineW);

        // Shout cooldown: top-right corner down to the divider junction, opposite to health.
        if (cfg.shoutIndicator && v.shoutCooling && v.shoutProgress > 0.0f) {
            const float y1 = TOP + W + (DIV - TOP) * std::min(v.shoutProgress, 1.0f);
            DL::AddLine(dl, X(XR, TOP + W), X(XR, y1), Col(kWhite, A), std::max(cfg.shoutWidth * s, 1.0f));
        }

        // Casting: clockwise round the lower box from its top-left corner; 1 px wider than
        // the frame line on each side. Fully charged and held -> whole outline pulses.
        if (cfg.castIndicator && v.casting) {
            const float castW = lineW + 2.0f * s;
            const ImVec2 loop[5] = { X(XL, DIV), X(XR, DIV + W), X(XR, BOT + W), X(XL, BOT), X(XL, DIV) };
            if (v.castHeld) {
                const float p = 0.5f + 0.5f * std::sin(Seconds() * 6.2831853f / 0.9f);
                DL::AddPolyline(dl, loop, 4, Col(kOrange, (0.10f + 0.12f * p) * A), ImGuiMCP::ImDrawFlags_Closed, castW + 7.0f * s);
                DL::AddPolyline(dl, loop, 4, Col(kOrange, (0.20f + 0.25f * p) * A), ImGuiMCP::ImDrawFlags_Closed, castW + 3.0f * s);
                DL::AddPolyline(dl, loop, 4, Col(Mix(kOrange, kOrangeHi, p), A), ImGuiMCP::ImDrawFlags_Closed, castW);
            } else if (v.castProgress > 0.0f) {
                ImVec2 part[6];
                const int n = PartialPath(loop, 5, v.castProgress, part);
                if (n >= 2) DL::AddPolyline(dl, part, n, Col(kOrange, A), 0, castW);
            }
        }

        // Current values (they grow with level; the bars do not).
        if (cfg.showNumbers) {
            auto* font = FindFont();
            const float px = textRef * s;
            const float tx = XR + 10.0f;
            const float yH = DIV + 16.0f, yM = yH + textRef * 2.857f, yS = yM + textRef * 1.4286f;
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
