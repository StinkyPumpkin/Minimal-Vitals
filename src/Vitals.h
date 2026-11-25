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

// Player state for the widget. Everything is read on the MAIN thread (an SKSE task the
// render callback queues each frame, one outstanding at a time) and handed to the render
// thread as a copied snapshot - the render hook never touches game objects itself.
namespace Vitals {

    struct Snapshot {
        bool  valid = false;
        float hp = 0, hpMax = 0;
        float mp = 0, mpMax = 0;
        float sp = 0, spMax = 0;

        // Spell charge per hand, same source as zax's Casting Bar:
        // progress = (chargeTime - castingTimer) / chargeTime. "held" = fully charged and
        // not released yet (fire-and-forget held, or a concentration spell being channelled).
        // 0.5.2: both hands tracked (right glows clockwise, left anticlockwise, both = split).
        struct Cast { bool on = false; float progress = 0; bool held = false; };
        Cast  castR, castL;
        bool  castDual = false;   // a real dual cast (perk): both halves follow the one charge
        bool  casting = false;    // either hand

        // Shout recovery: 0 when the cooldown starts, 1 when it is done (then hidden).
        bool  shoutCooling = false;
        float shoutProgress = 0;

        // The horse's stamina while the player rides (mounted = false otherwise).
        bool  mounted = false;
        float horseSp = 0, horseSpMax = 0;
    };

    // kDataLoaded: object-loaded sink (re-attaches the animation sink when the player's 3D reloads).
    void Register();
    // kPostLoadGame / kNewGame: forget any cast in progress and attach the animation sink.
    void OnGameLoaded();
    // Render thread: queue one main-thread read (no-op while one is pending).
    void QueueRead();
    Snapshot Get();
}
