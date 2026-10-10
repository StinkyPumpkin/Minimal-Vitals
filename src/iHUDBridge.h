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
