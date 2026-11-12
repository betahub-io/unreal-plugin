// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_SteamKeyboard.h"
#include "BH_Log.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_LINUX
#include <dlfcn.h>
#endif

namespace
{
    // Steamworks flat API (steam_api_flat.h). ISteamUtils010 is the interface version that
    // added the floating keyboard (SDK 1.53) and is still current.
    using FGetUtils = void* (*)();
    using FShow = bool (*)(void* Utils, int32 Mode, int32 X, int32 Y, int32 Width, int32 Height);
    using FDismiss = bool (*)(void* Utils);
    const char* GetUtilsName = "SteamAPI_SteamUtils_v010";
    const char* ShowName = "SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput";
    const char* DismissName = "SteamAPI_ISteamUtils_DismissFloatingGamepadTextInput";

    // EFloatingGamepadTextInputMode
    constexpr int32 SteamSingleLine = 0;
    constexpr int32 SteamMultipleLines = 1;
    constexpr int32 SteamEmail = 2;

    /** steam_api, only if the game has loaded it. */
    void* LoadedSteamApi()
    {
#if PLATFORM_WINDOWS
        // Unreal 5 builds only 64-bit games.
        return ::GetModuleHandleW(L"steam_api64.dll");
#elif PLATFORM_LINUX
        return dlopen("libsteam_api.so", RTLD_NOW | RTLD_NOLOAD);
#else
        return nullptr;
#endif
    }

    void* Export(void* Module, const char* Name)
    {
#if PLATFORM_WINDOWS
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(Module), Name));
#elif PLATFORM_LINUX
        return dlsym(Module, Name);
#else
        return nullptr;
#endif
    }

    /** ISteamUtils of the game's Steamworks; Utils is null when Steamworks is not running. */
    struct FSteamUtils
    {
        void* Module = LoadedSteamApi();
        void* Utils = nullptr;

        FSteamUtils()
        {
            const FGetUtils GetUtils = Module ? reinterpret_cast<FGetUtils>(Export(Module, GetUtilsName)) : nullptr;
            Utils = GetUtils ? GetUtils() : nullptr;
        }

        ~FSteamUtils()
        {
#if PLATFORM_LINUX
            // RTLD_NOLOAD still counts a reference.
            if (Module)
            {
                dlclose(Module);
            }
#endif
        }

        void* Function(const char* Name) const
        {
            return Utils ? Export(Module, Name) : nullptr;
        }
    };
}

bool BH_SteamKeyboard::Show(EBH_SteamKeyboardMode Mode, const FIntRect& Field)
{
    const FSteamUtils Steam;
    void* Utils = Steam.Utils;
    const FShow ShowKeyboard = reinterpret_cast<FShow>(Steam.Function(ShowName));
    if (!ShowKeyboard)
    {
        return false;
    }
    const int32 SteamMode = Mode == EBH_SteamKeyboardMode::Email ? SteamEmail
        : Mode == EBH_SteamKeyboardMode::MultipleLines ? SteamMultipleLines : SteamSingleLine;
    // False outside Steam Deck and Big Picture.
    const bool bShown = ShowKeyboard(Utils, SteamMode, Field.Min.X, Field.Min.Y, Field.Width(), Field.Height());
    UE_LOG(LogBetaHub, Verbose, TEXT("Steam floating keyboard %s"), bShown ? TEXT("shown") : TEXT("not available"));
    return bShown;
}

void BH_SteamKeyboard::Dismiss()
{
    const FSteamUtils Steam;
    if (const FDismiss DismissKeyboard = reinterpret_cast<FDismiss>(Steam.Function(DismissName)))
    {
        DismissKeyboard(Steam.Utils);
    }
}
