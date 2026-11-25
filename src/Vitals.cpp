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

#include "Vitals.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string_view>

namespace {
    std::mutex        g_snapMutex;
    Vitals::Snapshot  g_snap{};
    std::atomic<bool> g_readQueued{false};

    // Cast tracking, one slot per hand (0.5.2: both hands, so a dual cast or two spells at
    // once show). Written by animation events (which can arrive off the main thread) and by
    // the main-thread read, so plain atomics.
    enum Hand : int { kRight = 0, kLeft = 1 };
    struct HandTrack {
        std::atomic<bool> on{false};
        std::atomic<bool> held{false};
        int               lostReads = 0;   // main thread only: reads with no spell / idle caster
    };
    HandTrack         g_hands[2];
    std::atomic<bool> g_stopCheck{false}; // CastStop seen: end any hand whose caster is idle now

    // Shout recovery (main thread only).
    float g_shoutTotal = 0.0f;
    float g_shoutLast  = 0.0f;

    void EndHand(int h) {
        g_hands[h].on.store(false, std::memory_order_relaxed);
        g_hands[h].held.store(false, std::memory_order_relaxed);
    }
    void EndAll() { EndHand(kRight); EndHand(kLeft); }
    void BeginHand(int h) {
        if (g_hands[h].on.load(std::memory_order_relaxed)) return;
        g_hands[h].held.store(false, std::memory_order_relaxed);
        g_hands[h].on.store(true, std::memory_order_relaxed);
    }

    // Event names and end conditions follow zax's Casting Bar (StateController.cpp), per hand:
    // a hand's glow runs from its BeginCast until it fires or is re-equipped; unequip / weapons
    // drawn or sheathed end both. CastStop does not say which hand stopped, so it only asks the
    // next read to end whichever hand's caster has gone idle (the other may still be charging).
    class AnimSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        static AnimSink* Get() { static AnimSink s; return &s; }

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;
            const std::string_view tag{ a_event->tag.c_str() };

            if (tag == "BeginCastRight") {
                BeginHand(kRight);
            } else if (tag == "BeginCastLeft") {
                BeginHand(kLeft);
            } else if (tag == "MRh_SpellFire_Event" || tag == "MRh_Equipped_Event") {
                EndHand(kRight);
            } else if (tag == "MLh_SpellFire_Event" || tag == "MLh_Equipped_Event") {
                EndHand(kLeft);
            } else if (tag == "MRh_PreAimedCon_to_MRh_AimedCon") {
                if (g_hands[kRight].on.load(std::memory_order_relaxed)) g_hands[kRight].held.store(true, std::memory_order_relaxed);
            } else if (tag == "MLh_PreAimedCon_to_MLh_AimedCon") {
                if (g_hands[kLeft].on.load(std::memory_order_relaxed)) g_hands[kLeft].held.store(true, std::memory_order_relaxed);
            } else if (tag == "CastStop") {
                g_stopCheck.store(true, std::memory_order_relaxed);
            } else if (tag == "Magic_Unequip_Out" || tag == "WeaponSheathe" || tag == "weaponDraw") {
                EndAll();
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void AttachAnimSink() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        const bool ok = player->AddAnimationGraphEventSink(AnimSink::Get());
        SKSE::log::info("Vitals: animation sink attached to player graph = {}", ok);
    }

    class LoadedSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
    public:
        static LoadedSink* Get() { static LoadedSink s; return &s; }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event,
                                              RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override {
            if (a_event && a_event->loaded && a_event->formID == 0x14) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([]() { AttachAnimSink(); });
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    float MaxAV(RE::Actor* p, RE::ActorValue av) {
        auto* avo = p->AsActorValueOwner();
        return avo->GetPermanentActorValue(av) +
               p->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, av);
    }

    void ReadHand(RE::PlayerCharacter* p, int h, bool stop, Vitals::Snapshot::Cast& out, bool& dual) {
        HandTrack& t = g_hands[h];
        if (!t.on.load(std::memory_order_relaxed)) { t.lostReads = 0; return; }

        auto* caster = p->GetMagicCaster(h == kRight ? RE::MagicSystem::CastingSource::kRightHand
                                                     : RE::MagicSystem::CastingSource::kLeftHand);
        RE::MagicItem* spell = caster ? caster->currentSpell : nullptr;
        const bool idle = !caster || !spell || caster->state.get() == RE::MagicCaster::State::kNone;
        // A missed release event must not leave the indicator up forever.
        if (idle) {
            if (stop || ++t.lostReads > 15) EndHand(h);
            return;
        }
        t.lostReads = 0;

        float progress = 1.0f;
        const float chargeTime = spell->GetChargeTime();
        if (chargeTime > 0.0f) {
            progress = std::clamp((chargeTime - caster->castingTimer) / chargeTime, 0.0f, 1.0f);
        }
        if (progress >= 0.999f) t.held.store(true, std::memory_order_relaxed);

        out.on       = true;
        out.held     = t.held.load(std::memory_order_relaxed);
        out.progress = out.held ? 1.0f : progress;
        if (caster->GetIsDualCasting()) dual = true;
    }

    void ReadCast(RE::PlayerCharacter* p, Vitals::Snapshot& s) {
        const bool stop = g_stopCheck.exchange(false, std::memory_order_relaxed);
        ReadHand(p, kRight, stop, s.castR, s.castDual);
        ReadHand(p, kLeft, stop, s.castL, s.castDual);
        // A dual cast may run on one hand's caster only: both halves follow it.
        if (s.castDual) {
            if (!s.castR.on) s.castR = s.castL;
            if (!s.castL.on) s.castL = s.castR;
        }
        s.casting = s.castR.on || s.castL.on;
    }

    void ReadShout(RE::PlayerCharacter* p, Vitals::Snapshot& s) {
        const float rec = p->GetVoiceRecoveryTime();
        if (rec > 0.0f) {
            // A new cooldown shows up as the timer jumping up; a cooldown already running
            // when the save loaded starts the line from wherever it is.
            if (g_shoutTotal <= 0.0f || rec > g_shoutLast + 0.05f) g_shoutTotal = rec;
            s.shoutCooling  = true;
            s.shoutProgress = std::clamp(1.0f - rec / g_shoutTotal, 0.0f, 1.0f);
        } else {
            g_shoutTotal = 0.0f;
        }
        g_shoutLast = rec;
    }

    void ReadMount(RE::PlayerCharacter* p, Vitals::Snapshot& s) {
        if (!p->IsOnMount()) return;
        RE::NiPointer<RE::Actor> horse;
        if (!p->GetMount(horse) || !horse) return;
        s.horseSp    = horse->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
        s.horseSpMax = MaxAV(horse.get(), RE::ActorValue::kStamina);
        s.mounted    = s.horseSpMax > 0.0f;
    }

    void ReadNow() {
        Vitals::Snapshot s{};
        auto* p = RE::PlayerCharacter::GetSingleton();
        if (p && p->Is3DLoaded()) {
            auto* avo = p->AsActorValueOwner();
            s.hp = avo->GetActorValue(RE::ActorValue::kHealth);
            s.mp = avo->GetActorValue(RE::ActorValue::kMagicka);
            s.sp = avo->GetActorValue(RE::ActorValue::kStamina);
            s.hpMax = MaxAV(p, RE::ActorValue::kHealth);
            s.mpMax = MaxAV(p, RE::ActorValue::kMagicka);
            s.spMax = MaxAV(p, RE::ActorValue::kStamina);
            ReadCast(p, s);
            ReadShout(p, s);
            ReadMount(p, s);
            s.valid = true;
        }
        std::lock_guard lk(g_snapMutex);
        g_snap = s;
    }
}

namespace Vitals {

    void Register() {
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESObjectLoadedEvent>(LoadedSink::Get());
            SKSE::log::info("Vitals: object-loaded sink registered");
        }
    }

    void OnGameLoaded() {
        EndAll();
        g_stopCheck.store(false, std::memory_order_relaxed);
        g_shoutTotal = 0.0f;
        g_shoutLast  = 0.0f;
        AttachAnimSink();
    }

    void QueueRead() {
        // One outstanding task: during loads the main thread stops draining SKSE tasks
        // and the render thread must not pile reads up behind it.
        if (g_readQueued.exchange(true, std::memory_order_acq_rel)) return;
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) { g_readQueued.store(false, std::memory_order_relaxed); return; }
        tasks->AddTask([]() {
            g_readQueued.store(false, std::memory_order_relaxed);
            ReadNow();
        });
    }

    Snapshot Get() {
        std::lock_guard lk(g_snapMutex);
        return g_snap;
    }
}
