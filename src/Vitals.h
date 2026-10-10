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

        // Spell charge, same source as zax's Casting Bar: the hand that started casting,
        // progress = (chargeTime - castingTimer) / chargeTime. "held" = fully charged and
        // not released yet (fire-and-forget held, or a concentration spell being channelled).
        bool  casting = false;
        float castProgress = 0;
        bool  castHeld = false;

        // Shout recovery: 0 when the cooldown starts, 1 when it is done (then hidden).
        bool  shoutCooling = false;
        float shoutProgress = 0;
    };

    // kDataLoaded: object-loaded sink (re-attaches the animation sink when the player's 3D reloads).
    void Register();
    // kPostLoadGame / kNewGame: forget any cast in progress and attach the animation sink.
    void OnGameLoaded();
    // Render thread: queue one main-thread read (no-op while one is pending).
    void QueueRead();
    Snapshot Get();
}
