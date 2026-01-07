// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/Config/Config.h"

// This is a temporary solution, although they should be in their respective cpp file in UICommon.
// However, in order for IsSettingSaveable to compile without some issues, this needs to be here.
// Once IsSettingSaveable is removed, then you should able to move these back to UICommon.

namespace Config
{
// Configuration Information

// UI.General

extern const Info<bool> MAIN_USE_DISCORD_PRESENCE;
extern const Info<bool> MAIN_USE_GAME_COVERS;
extern const Info<bool> MAIN_FOCUSED_HOTKEYS;
extern const Info<bool> MAIN_RECURSIVE_ISO_PATHS;
extern const Info<std::string> MAIN_CURRENT_STATE_PATH;

extern const Info<std::string> MAIN_GCPAD_LAST_PRESET_TITLE_PORT_0;
extern const Info<std::string> MAIN_GCPAD_LAST_PRESET_TITLE_PORT_1;
extern const Info<std::string> MAIN_GCPAD_LAST_PRESET_TITLE_PORT_2;
extern const Info<std::string> MAIN_GCPAD_LAST_PRESET_TITLE_PORT_3;
extern const Info<std::string> MAIN_GCPAD_LAST_PRESET_ID_PORT_0;
extern const Info<std::string> MAIN_GCPAD_LAST_PRESET_ID_PORT_1;
extern const Info<std::string> MAIN_GCPAD_LAST_PRESET_ID_PORT_2;
extern const Info<std::string> MAIN_GCPAD_LAST_PRESET_ID_PORT_3;

const Info<std::string>& GetInfoForGCPadLastPresetTitle(int port);

}  // namespace Config
