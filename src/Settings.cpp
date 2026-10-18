#include "Settings.h"
#include "JsonStore.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {
    std::mutex        g_mutex;
    Settings::Config  g_config{};
    std::atomic<bool> g_dirty{false};

    const char* B(bool v) { return v ? "true" : "false"; }

    std::string Serialize(const Settings::Config& c) {
        char buf[900];
        std::snprintf(buf, sizeof(buf),
            "{\n"
            "  \"enabled\": %s,\n  \"x\": %.2f,\n  \"y\": %.2f,\n  \"scale\": %.3f,\n  \"rotation\": %d,\n"
            "  \"lineWidth\": %.2f,\n  \"alpha\": %.3f,\n"
            "  \"showNumbers\": %s,\n  \"textSize\": %.2f,\n  \"grain\": %s,\n  \"trackOpacity\": %.3f,\n"
            "  \"castIndicator\": %s,\n  \"shoutIndicator\": %s,\n  \"shoutGlowWidth\": %.2f,\n"
            "  \"hideHotkeyDX\": %d,\n  \"followCompassHide\": %s\n"
            "}\n",
            B(c.enabled), c.x, c.y, c.scale, c.rotation,
            c.lineWidth, c.alpha,
            B(c.showNumbers), c.textSize, B(c.grain), c.trackOpacity,
            B(c.castIndicator), B(c.shoutIndicator), c.shoutGlow,
            c.hideHotkeyDX, B(c.followCompassHide));
        return buf;
    }

    std::string FindValue(const std::string& json, const std::string& key) {
        std::string needle = "\"" + key + "\"";
        size_t p = json.find(needle);
        if (p == std::string::npos) return "";
        p += needle.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == ':')) ++p;
        size_t end = p;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n') ++end;
        return json.substr(p, end - p);
    }

    void ReadF(const std::string& j, const char* k, float& out) {
        auto v = FindValue(j, k); if (!v.empty()) out = std::strtof(v.c_str(), nullptr);
    }
    void ReadI(const std::string& j, const char* k, int& out) {
        auto v = FindValue(j, k); if (!v.empty()) out = std::atoi(v.c_str());
    }
    void ReadB(const std::string& j, const char* k, bool& out) {
        auto v = FindValue(j, k); if (!v.empty()) out = (v.find("true") != std::string::npos);
    }
}

namespace Settings {

    std::unique_lock<std::mutex> Lock() { return std::unique_lock<std::mutex>(g_mutex); }
    Config& Get() { return g_config; }

    void Load() {
        std::string txt = JsonStore::Load();
        if (txt.empty()) {
            SKSE::log::info("Settings::Load - no config.json yet, using defaults");
            return;
        }
        auto lk = Lock();
        auto& c = g_config;
        ReadB(txt, "enabled", c.enabled);
        ReadF(txt, "x", c.x);                   ReadF(txt, "y", c.y);
        ReadF(txt, "scale", c.scale);
        ReadI(txt, "rotation", c.rotation);
        c.rotation = ((c.rotation % 4) + 4) % 4;
        ReadF(txt, "lineWidth", c.lineWidth);
        ReadF(txt, "alpha", c.alpha);
        ReadB(txt, "showNumbers", c.showNumbers);
        ReadF(txt, "textSize", c.textSize);
        ReadB(txt, "grain", c.grain);
        ReadF(txt, "trackOpacity", c.trackOpacity);
        c.trackOpacity = std::clamp(c.trackOpacity, 0.0f, 1.0f);
        ReadB(txt, "castIndicator", c.castIndicator);
        ReadB(txt, "shoutIndicator", c.shoutIndicator);
        // 0.1.x saved "shoutWidth" (a 3 px line); the glow has its own key so that value is ignored.
        ReadF(txt, "shoutGlowWidth", c.shoutGlow);
        ReadI(txt, "hideHotkeyDX", c.hideHotkeyDX);
        ReadB(txt, "followCompassHide", c.followCompassHide);
        c.scale     = std::clamp(c.scale, 0.25f, 4.0f);
        c.lineWidth = std::clamp(c.lineWidth, 0.5f, 6.0f);
        c.textSize  = std::clamp(c.textSize, 6.0f, 64.0f);
        c.shoutGlow = std::clamp(c.shoutGlow, 2.0f, 20.0f);
        SKSE::log::info("Settings::Load - applied config from disk");
    }

    void Save() {
        std::string txt;
        { auto lk = Lock(); txt = Serialize(g_config); }
        if (JsonStore::Save(txt)) {
            SKSE::log::info("Settings::Save - wrote config.json ({} bytes)", txt.size());
        }
    }

    void MarkDirty() { g_dirty.store(true, std::memory_order_relaxed); }
    bool TakeDirty() { return g_dirty.exchange(false, std::memory_order_relaxed); }
}
