#include "Vitals.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string_view>

namespace {
    std::mutex        g_snapMutex;
    Vitals::Snapshot  g_snap{};
    std::atomic<bool> g_readQueued{false};

    // Cast tracking. Written by animation events (which can arrive off the main thread)
    // and by the main-thread read, so plain atomics.
    enum Hand : int { kNoHand = -1, kRight = 0, kLeft = 1 };
    std::atomic<int>  g_castHand{kNoHand};
    std::atomic<bool> g_heldLatch{false};
    int               g_lostReads = 0;     // main thread only: reads with no spell / idle caster

    // Shout recovery (main thread only).
    float g_shoutTotal = 0.0f;
    float g_shoutLast  = 0.0f;

    void EndCast() {
        g_castHand.store(kNoHand, std::memory_order_relaxed);
        g_heldLatch.store(false, std::memory_order_relaxed);
    }

    // Event names and end conditions follow zax's Casting Bar (StateController.cpp):
    // the hand that started casting owns the meter until it fires, the cast stops, or
    // magic is unequipped / weapons drawn or sheathed.
    class AnimSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        static AnimSink* Get() { static AnimSink s; return &s; }

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;
            const std::string_view tag{ a_event->tag.c_str() };
            const int hand = g_castHand.load(std::memory_order_relaxed);

            if (tag == "BeginCastRight" || tag == "BeginCastLeft") {
                if (hand == kNoHand) {
                    g_heldLatch.store(false, std::memory_order_relaxed);
                    g_castHand.store(tag == "BeginCastRight" ? kRight : kLeft, std::memory_order_relaxed);
                }
            } else if (tag == "MRh_SpellFire_Event" || tag == "MRh_Equipped_Event") {
                if (hand == kRight) EndCast();
            } else if (tag == "MLh_SpellFire_Event" || tag == "MLh_Equipped_Event") {
                if (hand == kLeft) EndCast();
            } else if (tag == "MRh_PreAimedCon_to_MRh_AimedCon") {
                if (hand == kRight) g_heldLatch.store(true, std::memory_order_relaxed);
            } else if (tag == "MLh_PreAimedCon_to_MLh_AimedCon") {
                if (hand == kLeft) g_heldLatch.store(true, std::memory_order_relaxed);
            } else if (tag == "CastStop" || tag == "Magic_Unequip_Out" ||
                       tag == "WeaponSheathe" || tag == "weaponDraw") {
                if (hand != kNoHand) EndCast();
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

    void ReadCast(RE::PlayerCharacter* p, Vitals::Snapshot& s) {
        const int hand = g_castHand.load(std::memory_order_relaxed);
        if (hand == kNoHand) { g_lostReads = 0; return; }

        auto* caster = p->GetMagicCaster(hand == kRight ? RE::MagicSystem::CastingSource::kRightHand
                                                        : RE::MagicSystem::CastingSource::kLeftHand);
        RE::MagicItem* spell = caster ? caster->currentSpell : nullptr;
        const bool idle = !caster || !spell || caster->state.get() == RE::MagicCaster::State::kNone;
        // A missed release event must not leave the indicator up forever.
        if (idle) {
            if (++g_lostReads > 15) EndCast();
            return;
        }
        g_lostReads = 0;

        float progress = 1.0f;
        const float chargeTime = spell->GetChargeTime();
        if (chargeTime > 0.0f) {
            progress = std::clamp((chargeTime - caster->castingTimer) / chargeTime, 0.0f, 1.0f);
        }
        if (progress >= 0.999f) g_heldLatch.store(true, std::memory_order_relaxed);

        s.casting      = true;
        s.castHeld     = g_heldLatch.load(std::memory_order_relaxed);
        s.castProgress = s.castHeld ? 1.0f : progress;
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
        EndCast();
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
