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

#pragma once

// Hide in step with iHUDClaude's "Universal Hide" and TFCam's free-camera hide.
// Both broadcast over SKSE messaging (receiver = nullptr), so listening to the
// sender name is enough; same contract as ArousedWidget / LilithWidget.
namespace iHUDBridge {

    enum Message : uint32_t {
        kHideAll    = 1,  // hide until RestoreAll
        kRestoreAll = 2,  // restore visibility
        // 3 = RespectArousalThreshold, arousal-widget only; ignored here
    };

    // Register the SKSE messaging listeners. Call from kPostLoad.
    void Register();

    // True while iHUDClaude or TFCam has HideAll in force.
    bool IsHiddenByExternal();
}
