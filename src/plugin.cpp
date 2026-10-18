#include "HotkeyHandler.h"
#include "HudUI.h"
#include "Settings.h"
#include "Vitals.h"
#include "SettingsUI.h"
#include "Visibility.h"
#include "iHUDBridge.h"

#include <REL/Relocation.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <atomic>
#include <format>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>

#include <ShlObj.h>
#include <KnownFolders.h>

namespace {
    std::atomic<bool> g_saveRunning{false};
    std::thread       g_saveThread;

    // Marker file dropped at the start of SKSEPluginLoad so the entry point can be
    // proven to have run even if spdlog blows up later.
    void WriteStartupMarker(const char* phase, const std::string& detail = {}) {
        try {
            namespace fs = std::filesystem;
            fs::path p = fs::current_path() / "Data" / "SKSE" / "Plugins";
            std::error_code ec;
            fs::create_directories(p, ec);
            p /= "MinimalVitals-startup.log";
            std::ofstream f(p, std::ios::app);
            const auto t = std::time(nullptr);
            f << t << " [" << phase << "]";
            if (!detail.empty()) f << " " << detail;
            f << "\n";
        } catch (...) {}
    }

    // Documents\My Games\Skyrim Special Edition\SKSE — same place every other
    // plugin logs. (SKSE::log::log_directory() resolves oddly on some setups.)
    std::filesystem::path ResolveLogDirectory() {
        wchar_t* docs = nullptr;
        std::filesystem::path p;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &docs))) {
            p = docs;
            ::CoTaskMemFree(docs);
        } else {
            const wchar_t* up = _wgetenv(L"USERPROFILE");
            if (up) p = std::filesystem::path(up) / "Documents";
        }
        p /= "My Games";
        p /= "Skyrim Special Edition";
        p /= "SKSE";
        return p;
    }

    void InitializeLogging() {
        WriteStartupMarker("InitializeLogging-enter");
        std::filesystem::path logPath = ResolveLogDirectory();
        std::error_code ec;
        std::filesystem::create_directories(logPath, ec);
        logPath /= "MinimalVitals.log";
        try {
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
            auto log  = std::make_shared<spdlog::logger>("global log", std::move(sink));
            log->set_level(spdlog::level::info);
            log->flush_on(spdlog::level::info);
            spdlog::set_default_logger(std::move(log));
            spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
            SKSE::log::info("MinimalVitals v{} - logging initialized at {}", MINIMALVITALS_VERSION, logPath.string());
            WriteStartupMarker("spdlog-init-ok", logPath.string());
        } catch (const std::exception& e) {
            WriteStartupMarker("spdlog-init-FAILED", std::string{e.what()} + " | path=" + logPath.string());
        } catch (...) {
            WriteStartupMarker("spdlog-init-FAILED-unknown");
        }
    }

    void SaveLoop() {
        using namespace std::chrono;
        int tick = 0;
        while (g_saveRunning.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(250ms);
            // v0.1.5: compass-follow state is read on the MAIN thread via an SKSE task
            // (Scaleform GetVariable from the render hook crashed - see Visibility.cpp).
            Visibility::QueueCompassPoll();
            if (++tick % 4 == 0 && Settings::TakeDirty()) {
                Settings::Save();
            }
        }
    }

    bool g_addressLibraryMissing = false;

    // CommonLib fatally terminates the game with a cryptic popup when the Address
    // Library .bin for the RUNNING runtime is absent. Check first; if missing,
    // disable the widget and say exactly what to install.
    bool CheckAddressLibrary() {
        const auto ver = REL::Module::get().version();
        // Same file CommonLib's IDDB::load() opens: VR reads a .csv, AE a versionlib .bin, SE a version .bin.
        // 0.4.0 (CommonLibSSE-NG 9.x): NG classifies minor >= 6 as AE, so 1.7.x is AE too and the file is
        // versionlib-1-7-104-0.bin - the Address Library v5 database (format 5), which NG 9.x parses.
        std::string file;
        if (REL::Module::IsVR()) {
            file = std::format("Data/SKSE/Plugins/version-{}.csv", ver.string());
        } else if (REL::Module::IsAE()) {
            file = std::format("Data/SKSE/Plugins/versionlib-{}.bin", ver.string());
        } else {
            file = std::format("Data/SKSE/Plugins/version-{}.bin", ver.string());
        }
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::current_path() / file, ec)) {
            return true;
        }
        g_addressLibraryMissing = true;
        SKSE::log::error("Address Library file missing for runtime {}.{}.{}.{} ({}) - widget disabled",
                         ver.major(), ver.minor(), ver.patch(), ver.build(), file);
        const std::string text = std::format(
            "Minimal Vitals: the Address Library file for your game version "
            "({}.{}.{}.{}) is not installed, so the widget has been disabled.\n\n"
            "Install \"Address Library for SKSE Plugins\" and pick the edition that "
            "matches your game (1.5.x = SE, 1.6.x / 1.7.x = AE, VR = the VR Address Library).\n\n"
            "The game will continue to run normally.",
            ver.major(), ver.minor(), ver.patch(), ver.build());
        ::MessageBoxA(nullptr, text.c_str(), "Minimal Vitals", MB_OK | MB_ICONWARNING);
        return false;
    }

    void MessageCallback(SKSE::MessagingInterface::Message* msg) {
        if (g_addressLibraryMissing) return;
        switch (msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            SKSE::log::info("kPostLoad - registering SMF section + iHUD bridge");
            if (!CheckAddressLibrary()) return;
            Settings::Load();
            SettingsUI::Register();
            iHUDBridge::Register();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            SKSE::log::info("kDataLoaded - registering event sinks + HUD element");
            Vitals::Register();
            HudUI::Register();
            HotkeyHandler::Register();
            g_saveRunning.store(true);
            g_saveThread = std::thread(SaveLoop);
            break;

        // A cast in progress does not survive a load; the player's animation graph is new.
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            Vitals::OnGameLoaded();
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    WriteStartupMarker("SKSEPluginLoad-enter");
    InitializeLogging();
    SKSE::log::info("MinimalVitals loading... built on CommonLibSSE-NG {}", MINIMALVITALS_COMMONLIB_VERSION);
    // NG 9's Init opens its own <plugin>.log by default and would replace this logger.
    SKSE::Init(skse, { .log = false });

    auto* mi = SKSE::GetMessagingInterface();
    if (!mi || !mi->RegisterListener(MessageCallback)) {
        SKSE::log::error("Failed to register SKSE messaging listener");
        WriteStartupMarker("listener-register-FAILED");
        return false;
    }
    SKSE::log::info("MinimalVitals loaded");
    WriteStartupMarker("SKSEPluginLoad-return-true");
    return true;
}
