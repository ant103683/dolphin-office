// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Core/NetplayManager.h"
#include "NetPlayServer.h"
#include "NetPlayProto.h"
#include "Core/Config/NetplaySettings.h"
#include "Common/Config/Config.h"

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Crypto/SHA1.h"
#include "Core/State.h"
#include "Core/TitleDatabase.h"

#include "DiscIO/Volume.h"
#include "UICommon/GameFile.h"

#include "picojson.h"

#include <fstream>
#include <mutex>
#include <vector>
#include <fmt/format.h>

#define MAX_PLAYERS 4

namespace NetPlay
{
void NetplayManager::UpdateGameInfo(const UICommon::GameFile& game)
{
  const std::string json_path = File::GetUserPath(D_CONFIG_IDX) + "games.json";
  picojson::value games_json;
  std::ifstream file(json_path);
  if (file)
  {
    file >> games_json;
  }

  picojson::array games;
  if (!games_json.is<picojson::null>())
  {
    games = games_json.get<picojson::array>();
  }

  const std::string game_id = game.GetGameID();
  const std::string hash = Common::SHA1::DigestToString(game.GetSyncHash());

  bool found = false;
  for (const auto& entry : games)
  {
    const auto& obj = entry.get<picojson::object>();
    if (obj.at("game_id").get<std::string>() == game_id && obj.at("hash").get<std::string>() == hash)
    {
      found = true;
      break;
    }
  }

  if (!found)
  {
    picojson::object new_game;
    new_game["game_id"] = picojson::value(game_id);
    new_game["hash"] = picojson::value(hash);
    new_game["name"] = picojson::value(game.GetName(UICommon::GameFile::Variant::LongAndPossiblyCustom));
    new_game["revision"] = picojson::value(static_cast<double>(game.GetRevision()));
    new_game["disc_number"] = picojson::value(static_cast<double>(game.GetDiscNumber()));
    games.push_back(picojson::value(new_game));

    std::ofstream outfile(json_path);
    outfile << picojson::value(games).serialize(true);
  }
}

NetplayManager& NetplayManager::GetInstance()
{
  static NetplayManager s_instance;
  return s_instance;
}

NetplayManager::NetplayManager()
{
  m_client_states.resize(MAX_PLAYERS);
  for (int i = 0; i < MAX_PLAYERS; ++i)
  {
    m_client_states[i].pid = i + 1;
  }
  m_client_states[0].is_active = true;
  m_client_states[0].state = LoadStatus::SUCCESS;
}

NetplayManager::~NetplayManager()
{
  // Cleanup code here if needed
}

void NetplayManager::activeClientWithPid(int pid)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_client_states[pid-1].is_active = true;
  m_client_states[pid-1].state = LoadStatus::INIT;
  m_client_states[pid-1].idle_seconds = 0;
}

void NetplayManager::setClientLoadStatus(LoadStatus status,int pid) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_client_states[pid-1].state = status;
}

bool NetplayManager::setClientLoadStatusSuccess(int pid)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_client_states[pid-1].is_active && m_client_states[pid-1].state == LoadStatus::INIT)
  {
    m_client_states[pid-1].state = LoadStatus::SUCCESS;
    return true;
  }
  return false;
}

void NetplayManager::deactiveClientWithPid(int pid)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_client_states[pid-1].is_active = false;
  m_client_states[pid-1].state = LoadStatus::INIT;
  m_client_states[pid-1].idle_seconds = 0;
}

void NetplayManager::resetClientsExceptHost_NoLock()
{
  for (auto& client : m_client_states)
  {
    if (client.pid == 1) // Host PID is 1
      continue;
    client.state = LoadStatus::INIT;
    client.idle_seconds = 0;
  }
}

bool NetplayManager::canLoadStatus()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
  File::IOFile log_file(log_path, "ab");
  if (log_file)
  {
    std::string log_entry = "[NetplayManager::canLoadStatus] Current client states:\n";
    for (const auto& state : m_client_states)
    {
      log_entry += fmt::format("  pid: {}, active: {}, status: {}\n", state.pid, state.is_active,
                                static_cast<int>(state.state));
    }
    log_file.WriteBytes(log_entry.c_str(), log_entry.size());
  }

  for (const auto& state : m_client_states)
  {
    if (state.pid != 1 && state.is_active && state.state != LoadStatus::SUCCESS)
    {
      return false;
    }
  }
  return true;
}

bool NetplayManager::HasInitialStateSave(const std::string& game_id,
                                         const std::string& hash8) const
{
  using namespace Common;
  const std::string base_path = File::GetUserPath(D_STATESAVES_IDX);
  std::string suffix = m_initial_state_suffix;
  std::string filename = game_id + "_" + hash8 + ".sav";
  if (game_id == "RDSJAF" && hash8 == "531c9777" && !suffix.empty())
    filename = game_id + "_" + hash8 + suffix + ".sav";
  const std::string full_path = base_path + "initial" + DIR_SEP + filename;
  return File::Exists(full_path);
}

bool NetplayManager::LoadInitialState(Core::System& system, const std::string& game_id,
                                      const std::string& hash8)
{

  using namespace Common;
  const std::string base_path = File::GetUserPath(D_STATESAVES_IDX);
  std::string suffix = m_initial_state_suffix;
  std::string filename = game_id + "_" + hash8 + ".sav";
  if (game_id == "RDSJAF" && hash8 == "531c9777" && !suffix.empty())
    filename = game_id + "_" + hash8 + suffix + ".sav";
  const std::string sav_path = base_path + "initial" + DIR_SEP + filename;

  if (!File::Exists(sav_path))
    return false;

  ::State::LoadAs(system, sav_path);
  return true;
}

void NetplayManager::SetCurrentGame(const SyncIdentifier& si, const std::string& netplay_name)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  CurrentGame cg;
  cg.game_id = si.game_id;
  cg.revision = si.revision;
  cg.disc_number = si.disc_number;
  cg.is_datel = si.is_datel;
  cg.sync_hash = si.sync_hash;
  cg.hash8 = Common::SHA1::DigestToString(si.sync_hash).substr(0, 8);
  cg.name = netplay_name;
  m_current_game = std::move(cg);
}

void NetplayManager::IdleTick(NetPlayServer& server)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (server.IsRunning())
  {
    for (auto& client : m_client_states)
    {
      if (client.pid == 1)
        continue;
      client.idle_seconds = 0;
    }
    return;
  }
  for (auto& client : m_client_states)
  {
    if (client.pid == 1)
      continue;
    if (!client.is_active)
      continue;
    client.idle_seconds += 1;
    const u32 timeout_sec = Config::Get(Config::NETPLAY_IDLE_TIMEOUT_SEC);
    if (client.idle_seconds >= static_cast<int>(timeout_sec))
    {
      server.SendPrivateChat(static_cast<NetPlay::PlayerId>(client.pid), NetPlay::PlayerId{1},
                             std::string("长时间未进入游戏,已被移出房间"));
      server.KickPlayer(static_cast<NetPlay::PlayerId>(client.pid));
      client.idle_seconds = 0;
      client.state = LoadStatus::INIT;
    }
  }
}

void NetplayManager::SetInitialStateVariantSuffix(const std::string& suffix)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_initial_state_suffix = suffix;
}

std::string NetplayManager::GetInitialStateVariantSuffix() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_initial_state_suffix;
}

}  // namespace NetPlay
