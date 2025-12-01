// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <picojson.h>

namespace UICommon {

class GameFile;

// Writes the provided list of GameFile objects to a JSON file located at
// Config/games_list.json under the user's configuration directory.
//
// Each game entry contains (at minimum):
//   - "title": Localized long name of the game (fallbacks to file name if empty)
//   - "id":    6-letter GameID string
//   - "hash":  SHA1 hash string of the game content
//   - "region": Region string (e.g. "NTSC-U", "PAL", "NTSC-J", "Unknown")
//
// The resulting JSON structure is:
// {
//   "generated_at": "ISO-8601 timestamp",
//   "games": [
//       { "title": "Super Mario Sunshine", "version": 1, "region": "NTSC-U" },
//       ...
//   ]
// }
//
// The function returns true on success, false otherwise.
// It performs synchronous file I/O but is expected to be invoked from UI code that
// handles threading if necessary.
bool ExportGamesListJson(const std::vector<std::shared_ptr<const GameFile>>& games);

// Reads the games list from Config/games_list.json under the user's configuration directory.
// Returns a picojson::value containing the parsed JSON data, or an empty value if the file
// doesn't exist or parsing fails.
//
// The returned JSON structure matches the format produced by ExportGamesListJson:
// {
//   "generated_at": "ISO-8601 timestamp",
//   "games": [
//       { "netplay_name": "Game Name", "game_id": "GMSE01", ... },
//       ...
//   ]
// }
picojson::value ImportGamesListJson();

} // namespace UICommon