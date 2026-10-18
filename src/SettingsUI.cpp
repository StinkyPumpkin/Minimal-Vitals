#include "SettingsUI.h"
#include "Settings.h"
#include "Vitals.h"

#include "SKSEMenuFramework.h"

namespace {
    void __stdcall RenderLayout() {
        Settings::Config cfg;
        { auto lk = Settings::Lock(); cfg = Settings::Get(); }
        bool dirty = false;

        {
            const auto v = Vitals::Get();
            if (v.valid) {
                ImGuiMCP::Text("Health %.0f / %.0f   Magicka %.0f / %.0f   Stamina %.0f / %.0f",
                               v.hp, v.hpMax, v.mp, v.mpMax, v.sp, v.spMax);
            } else {
                ImGuiMCP::Text("Waiting for the player (values are read in game)");
            }
        }
        ImGuiMCP::Text("");

        if (ImGuiMCP::Checkbox("Enabled", &cfg.enabled)) dirty = true;
        // Not saved: on until unticked or the game restarts.
        ImGuiMCP::Checkbox("Preview (show always, loop a cast and a shout cooldown)", &cfg.preview);
        if (ImGuiMCP::SliderFloat("X", &cfg.x, -50.0f, 3840.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::SliderFloat("Y", &cfg.y, -50.0f, 2160.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::SliderFloat("Size", &cfg.scale, 0.25f, 4.0f, "%.2f")) dirty = true;
        if (ImGuiMCP::Combo("Rotation", &cfg.rotation, "Upright\0" "90 (lying, health right)\0" "180\0" "270 (lying, health left)\0")) dirty = true;
        if (ImGuiMCP::SliderFloat("Frame line width", &cfg.lineWidth, 0.5f, 6.0f, "%.1f")) dirty = true;
        if (ImGuiMCP::SliderFloat("Opacity", &cfg.alpha, 0.05f, 1.0f, "%.2f")) dirty = true;
        if (ImGuiMCP::Checkbox("Show numbers", &cfg.showNumbers)) dirty = true;
        if (ImGuiMCP::SliderFloat("Number size", &cfg.textSize, 6.0f, 64.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::Checkbox("Grain texture on the bars", &cfg.grain)) dirty = true;
        ImGuiMCP::Text("");
        if (ImGuiMCP::Checkbox("Casting indicator (pink glow round the lower box)", &cfg.castIndicator)) dirty = true;
        if (ImGuiMCP::Checkbox("Shout cooldown (white glow down the top edge and left side)", &cfg.shoutIndicator)) dirty = true;
        if (ImGuiMCP::SliderFloat("Shout glow width", &cfg.shoutGlow, 2.0f, 20.0f, "%.1f")) dirty = true;

        if (dirty) Settings::MarkDirty();
        { auto lk = Settings::Lock(); Settings::Get() = cfg; }
    }

    void __stdcall RenderVisibility() {
        Settings::Config cfg;
        { auto lk = Settings::Lock(); cfg = Settings::Get(); }
        bool dirty = false;
        ImGuiMCP::Text("Hide-toggle hotkey (DirectInput scan code)");
        ImGuiMCP::Text("Common: 14=Backspace, 15=Tab, 28=Enter, 57=Space, 0=unbound");
        if (ImGuiMCP::SliderInt("Scan code##hide", &cfg.hideHotkeyDX, 0, 220)) dirty = true;
        ImGuiMCP::Text("");
        if (ImGuiMCP::Checkbox("Follow compass hide (iHUD / Sandbox When Idle / etc.)", &cfg.followCompassHide)) dirty = true;
        ImGuiMCP::Text("Also hides in menus, loading screens and with Photo Mode Hide UI / the tm console command.");
        if (dirty) Settings::MarkDirty();
        { auto lk = Settings::Lock(); Settings::Get() = cfg; }
    }
}

namespace SettingsUI {
    void Register() {
        const bool installed = SKSEMenuFramework::IsInstalled();
        const HMODULE handle = ::GetModuleHandleW(L"SKSEMenuFramework");
        SKSE::log::info("SettingsUI::Register - IsInstalled={} GetModuleHandle={}", installed, static_cast<void*>(handle));
        if (!installed || !handle) {
            SKSE::log::error("SKSE Menu Framework not loadable - settings menu unavailable");
            return;
        }
        SKSEMenuFramework::SetSection("Minimal Vitals");
        SKSEMenuFramework::AddSectionItem("Layout",     RenderLayout);
        SKSEMenuFramework::AddSectionItem("Visibility", RenderVisibility);
        SKSE::log::info("SettingsUI: registered 'Minimal Vitals' section");
    }
}
