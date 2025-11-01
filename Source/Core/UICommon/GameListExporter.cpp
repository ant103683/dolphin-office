// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UICommon/GameListExporter.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "Common/FileUtil.h"
#include "Common/JsonUtil.h"
#include "Common/Crypto/SHA1.h"
#include "Core/TitleDatabase.h"
#include "DiscIO/Enums.h"
#include "UICommon/GameFile.h"

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

} // namespace UICommon