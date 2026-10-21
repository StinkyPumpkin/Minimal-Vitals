#pragma once
#include <mutex>

namespace Settings {

    struct Config {
        bool  enabled      = true;
        float x            = 24.0f;    // top-left of the frame, screen px
        float y            = 1380.0f;
        // Fixed size: 1.0 = frame 30 x 462 px. The bars never grow with the player's
        // max health/magicka/stamina (unlike TrueHUD's "scale bars"); only the numbers do.
        float scale        = 1.0f;
        // Quarter turns clockwise: 0 = upright, 1 = 90 (lying, health on the right),
        // 2 = 180, 3 = 270 (lying, health on the left). The numbers stay upright.
        int   rotation     = 0;
        float lineWidth    = 1.5f;     // frame line thickness at scale 1, px
        float alpha        = 1.0f;     // whole-widget opacity
        bool  showNumbers  = true;     // current values beside the frame
        float textSize     = 16.0f;    // number size at scale 1, px
        bool  grain        = true;     // fine grain over the fills (Interface/MinimalVitals/grain.dds)
        // Dark backing behind each bar track. 0 = clear: an emptying bar leaves empty space.
        float trackOpacity = 0.0f;
        bool  castIndicator  = true;   // pink glow round the lower box while charging a spell
        bool  shoutIndicator = true;   // white glow down the top edge and left side while the shout recovers
        float shoutGlow    = 10.0f;    // shout glow width outward from the frame at scale 1, px
        bool  horseStamina = true;     // thin dark green line down the right side while riding
        // DirectInput scan code for the manual hide-toggle key. 0 = unbound.
        int   hideHotkeyDX = 0;
        // Mirror the vanilla compass _alpha (iHUD, Sandbox When Idle, ...).
        bool  followCompassHide = true;

        // Runtime only (never saved): draw even when the HUD would hide it, and loop a fake
        // cast + shout cooldown so position and size can be set without casting.
        bool  preview      = false;
    };

    std::unique_lock<std::mutex> Lock();
    Config& Get();

    void Load();
    void Save();

    void MarkDirty();
    bool TakeDirty();
}
