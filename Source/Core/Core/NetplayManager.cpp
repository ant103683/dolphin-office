// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "NetplayManager.h"
#include "NetPlayServer.h"

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Core/State.h"

#include <mutex>
#include <vector>
#include <fmt/format.h>

#define MAX_PLAYERS 4

namespace NetPlay
{

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
}

void NetplayManager::resetClientsExceptHost_NoLock()
{
  for (auto& client : m_client_states)
  {
    if (client.pid == 1) // Host PID is 1
      continue;
    client.state = LoadStatus::INIT;
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
  // 生成完整路径: <UserPath>/StateSaves/initial/<game_id>_<hash8>.sav
  using namespace Common;
  const std::string base_path = File::GetUserPath(D_STATESAVES_IDX);
  const std::string full_path = base_path + "initial" + DIR_SEP + game_id + "_" + hash8 + ".sav";
  return File::Exists(full_path);
}

bool NetplayManager::LoadInitialState(Core::System& system, const std::string& game_id,
                                      const std::string& hash8)
{

  using namespace Common;
  const std::string base_path = File::GetUserPath(D_STATESAVES_IDX);
  const std::string sav_path = base_path + "initial" + DIR_SEP + game_id + "_" + hash8 + ".sav";

  if (!File::Exists(sav_path))
    return false;

  // 调用核心的 State::LoadAs 执行真正的读档
  ::State::LoadAs(system, sav_path);
  return true;
}

}  // namespace NetPlay
