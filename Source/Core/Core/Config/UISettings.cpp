// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Config/UISettings.h"

namespace Config
{
// UI.General

const Info<bool> MAIN_USE_DISCORD_PRESENCE{{System::Main, "General", "UseDiscordPresence"}, true};
#ifdef ANDROID
const Info<bool> MAIN_USE_GAME_COVERS{{System::Main, "General", "UseGameCovers"}, true};
#else
const Info<bool> MAIN_USE_GAME_COVERS{{System::Main, "General", "UseGameCovers"}, false};
#endif
const Info<bool> MAIN_FOCUSED_HOTKEYS{{System::Main, "General", "HotkeysRequireFocus"}, true};
const Info<bool> MAIN_RECURSIVE_ISO_PATHS{{System::Main, "General", "RecursiveISOPaths"}, false};
const Info<std::string> MAIN_CURRENT_STATE_PATH{{System::Main, "General", "CurrentStatePath"}, ""};

const Info<std::string> MAIN_GCPAD_LAST_PRESET_TITLE_PORT_0{{System::Main, "Controllers", "GCPadLastPresetTitlePort0"}, ""};
const Info<std::string> MAIN_GCPAD_LAST_PRESET_TITLE_PORT_1{{System::Main, "Controllers", "GCPadLastPresetTitlePort1"}, ""};
const Info<std::string> MAIN_GCPAD_LAST_PRESET_TITLE_PORT_2{{System::Main, "Controllers", "GCPadLastPresetTitlePort2"}, ""};
const Info<std::string> MAIN_GCPAD_LAST_PRESET_TITLE_PORT_3{{System::Main, "Controllers", "GCPadLastPresetTitlePort3"}, ""};
const Info<std::string> MAIN_GCPAD_LAST_PRESET_ID_PORT_0{{System::Main, "Controllers", "GCPadLastPresetIdPort0"}, ""};
const Info<std::string> MAIN_GCPAD_LAST_PRESET_ID_PORT_1{{System::Main, "Controllers", "GCPadLastPresetIdPort1"}, ""};
const Info<std::string> MAIN_GCPAD_LAST_PRESET_ID_PORT_2{{System::Main, "Controllers", "GCPadLastPresetIdPort2"}, ""};
const Info<std::string> MAIN_GCPAD_LAST_PRESET_ID_PORT_3{{System::Main, "Controllers", "GCPadLastPresetIdPort3"}, ""};

const Info<std::string>& GetInfoForGCPadLastPresetTitle(int port)
{
  static const Info<std::string>* const infos[] = {
      &MAIN_GCPAD_LAST_PRESET_TITLE_PORT_0,
      &MAIN_GCPAD_LAST_PRESET_TITLE_PORT_1,
      &MAIN_GCPAD_LAST_PRESET_TITLE_PORT_2,
      &MAIN_GCPAD_LAST_PRESET_TITLE_PORT_3,
  };
  if (port < 0)
    port = 0;
  if (port > 3)
    port = 3;
  return *infos[port];
}

}  // namespace Config
