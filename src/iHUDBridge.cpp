#include "iHUDBridge.h"

#include <SKSE/SKSE.h>
#include <atomic>
#include <string_view>

namespace {
    // One flag per sender: TFCam's RestoreAll on free-cam exit must not cancel an
    // iHUD Smart Hide that is still in force (and vice versa).
    std::atomic<bool> g_hiddenByIHUD{false};
    std::atomic<bool> g_hiddenByTFCam{false};

    void __cdecl Callback(SKSE::MessagingInterface::Message* msg) {
        if (!msg || !msg->sender) return;
        if (msg->type != iHUDBridge::kHideAll && msg->type != iHUDBridge::kRestoreAll) return;
        const std::string_view sender{ msg->sender };
        auto* flag = sender == "TFCam" ? &g_hiddenByTFCam : &g_hiddenByIHUD;
        const bool hide = msg->type == iHUDBridge::kHideAll;
        if (flag->exchange(hide) != hide) {
            SKSE::log::info("iHUDBridge: {} from {}", hide ? "HideAll" : "RestoreAll", sender);
        }
    }
}

namespace iHUDBridge {

    void Register() {
        auto* mi = SKSE::GetMessagingInterface();
        if (!mi) {
            SKSE::log::error("iHUDBridge: no SKSE messaging interface");
            return;
        }
        const bool a = mi->RegisterListener("iHUDClaude", Callback);
        const bool b = mi->RegisterListener("TFCam", Callback);
        SKSE::log::info("iHUDBridge: listeners iHUDClaude={} TFCam={}", a, b);
    }

    bool IsHiddenByExternal() {
        return g_hiddenByIHUD.load(std::memory_order_relaxed) || g_hiddenByTFCam.load(std::memory_order_relaxed);
    }
}
