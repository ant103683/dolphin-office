// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UICommon/GameListExporter.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <fmt/format.h>
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/JsonUtil.h"
#include "Common/Crypto/SHA1.h"
#include "Core/TitleDatabase.h"
#include "DiscIO/Enums.h"
#include "UICommon/GameFile.h"

#if 1
void WriteDebugLog(const char* fmt, ...)
{
    // Based on the working example from NetPlayServer.cpp
    const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
    File::CreateFullPath(log_path);
    File::IOFile log_file(log_path, "ab");

    if (log_file)
    {
        static bool s_first = true;
        if (s_first)
        {
            s_first = false;
            const char* session_start = "--- NEW SESSION ---\n";
            log_file.WriteBytes(session_start, strlen(session_start));
        }

        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        time_t now = time(0);
        char dt[32];
        ctime_s(dt, sizeof(dt), &now);
        std::string prefix = std::string(dt);
        if (!prefix.empty() && prefix.back() == '\n')
        {
            prefix.pop_back(); // remove trailing newline
        }

        std::string final_log = fmt::format("[{}] {}\n", prefix, buffer);
        log_file.WriteBytes(final_log.c_str(), final_log.size());
    }
}
#else
void WriteDebugLog(const char* fmt, ...)
{
}
#endif

namespace UICommon {

static std::string RegionToString(DiscIO::Region region)
{
  using namespace DiscIO;
  switch (region)
  {
  case Region::NTSC_J: return "NTSC-J";
  case Region::NTSC_U: return "NTSC-U";
  case Region::PAL: return "PAL";
  case Region::NTSC_K: return "NTSC-K";
  case Region::Unknown:
  default: return "Unknown";
  }
}

bool ExportGamesListJson(const std::vector<std::shared_ptr<const GameFile>>& games)
{
  // Resolve output path: <UserConfigDir>/Config/games_list.json
  const std::string output_path = File::GetUserPath(D_CONFIG_IDX) + "games_list.json";

  picojson::object root_obj;

  // Timestamp
  std::time_t now = std::time(nullptr);
  std::tm* tm_now = std::gmtime(&now);
  std::ostringstream oss;
  oss << std::put_time(tm_now, "%Y-%m-%dT%H:%M:%SZ");
  root_obj["generated_at"] = picojson::value{oss.str()};

  // Games array
  picojson::array arr;
  arr.reserve(games.size());
  for (const auto& game : games)
  {
    if (!game || !game->IsValid())
      continue;

    picojson::object g;
    // Use NetPlay name (same as used in host creation)
    g["netplay_name"] = picojson::value{game->GetNetPlayName(Core::TitleDatabase{})};
    g["game_id"] = picojson::value{game->GetGameID()};
    g["revision"] = picojson::value{static_cast<double>(game->GetRevision())};
    g["disc_number"] = picojson::value{static_cast<double>(game->GetDiscNumber())};
    // Sync hash for netplay compatibility verification
    g["sync_hash"] = picojson::value{Common::SHA1::DigestToString(game->GetSyncHash())};
    // Sync identifier components (same as used in netplay)
    auto sync_id = game->GetSyncIdentifier();
    picojson::object sync_obj;
    sync_obj["dol_elf_size"] = picojson::value{static_cast<double>(sync_id.dol_elf_size)};
    sync_obj["game_id"] = picojson::value{sync_id.game_id};
    sync_obj["revision"] = picojson::value{static_cast<double>(sync_id.revision)};
    sync_obj["disc_number"] = picojson::value{static_cast<double>(sync_id.disc_number)};
    sync_obj["is_datel"] = picojson::value{sync_id.is_datel};
    g["sync_identifier"] = picojson::value{sync_obj};
    g["region"] = picojson::value{RegionToString(game->GetRegion())};

    arr.emplace_back(g);
  }

  root_obj["games"] = picojson::value{arr};

  // Ensure directory exists
  File::CreateFullPath(output_path);

  if (!JsonToFile(output_path, picojson::value{root_obj}, true))
  {
    return false;
  }

  return true;
}

picojson::value ImportGamesListJson()
{
  WriteDebugLog("ImportGamesListJson: Starting to import games list from JSON");
  
  // Resolve input path: <UserConfigDir>/Config/games_list.json
  const std::string input_path = File::GetUserPath(D_CONFIG_IDX) + "games_list.json";
  WriteDebugLog(fmt::format("ImportGamesListJson: Looking for file at: {}", input_path).c_str());

  // Check if file exists
  if (!File::Exists(input_path))
  {
    WriteDebugLog("ImportGamesListJson: File does not exist");
    return picojson::value{};
  }

  WriteDebugLog("ImportGamesListJson: File exists, attempting to read and parse");
  
  // Read and parse JSON file
  picojson::value json_data;
  std::string error;
  if (!JsonFromFile(input_path, &json_data, &error))
  {
    WriteDebugLog(fmt::format("ImportGamesListJson: Failed to parse JSON file, error: {}", error).c_str());
    return picojson::value{};
  }

  WriteDebugLog("ImportGamesListJson: Successfully parsed JSON file");
  
  // Log basic structure info
  if (json_data.is<picojson::object>())
  {
    const auto& root_obj = json_data.get<picojson::object>();
    auto games_it = root_obj.find("games");
    if (games_it != root_obj.end() && games_it->second.is<picojson::array>())
    {
      const auto& games_array = games_it->second.get<picojson::array>();
      WriteDebugLog(fmt::format("ImportGamesListJson: Found {} games in JSON file", games_array.size()).c_str());
    }
    else
    {
      WriteDebugLog("ImportGamesListJson: No 'games' array found in JSON");
    }
  }
  else
  {
    WriteDebugLog("ImportGamesListJson: JSON root is not an object");
  }

  return json_data;
}

} // namespace UICommon