// Minimal Vitals - health, magicka and stamina bars for SKSE Menu Framework
// Copyright (C) 2026 StinkyPumpkin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program. If
// not, see <https://www.gnu.org/licenses/>.

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
