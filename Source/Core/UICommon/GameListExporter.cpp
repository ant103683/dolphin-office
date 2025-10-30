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
    g["title"] = picojson::value{game->GetLongName()};
    g["id"] = picojson::value{game->GetGameID()};
    // SHA1 hash of the game file for integrity identification
    g["hash"] = picojson::value{Common::SHA1::DigestToString(game->GetSyncHash())};
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