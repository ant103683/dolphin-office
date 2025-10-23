// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "NetplayManager.h"
#include "NetPlayServer.h"

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"

#include <mutex>
#include <vector>

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
    m_client_states[i].pid = i;
  }
  m_client_states[0].is_active = true;
  m_client_states[0].state = LoadStatus::SUCCESS;
}

NetplayManager::~NetplayManager()
{
  // Cleanup code here if needed
}

void NetplayManager::SetLoadStatus(bool status)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_load_status = status;
}

bool NetplayManager::ShouldLoadStatus()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_should_load_status;
}

void NetplayManager::SetShouldLoadStatus(bool should_load)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_should_load_status = should_load;
  if (!m_should_load_status) {
    m_load_status = false;
    resetClientsExceptHost();
  }
}

void NetplayManager::activeClientWithPid(int pid)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_client_states[pid].is_active = true;
  m_client_states[pid].state = LoadStatus::INIT;
}

bool NetplayManager::getClientLoadStatus() {
  return m_load_status;
}

void NetplayManager::setClientLoadStatus(LoadStatus status,int pid) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_client_states[pid].state = status;
}

bool NetplayManager::setClientLoadStatusSuccess(int pid)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_client_states[pid].is_active && m_client_states[pid].state == LoadStatus::INIT)
  {
    m_client_states[pid].state = LoadStatus::SUCCESS;
    return true;
  }
  return false;
}

void NetplayManager::deactiveClientWithPid(int pid)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_client_states[pid].is_active = false;
  m_client_states[pid].state = LoadStatus::INIT;
}

void NetplayManager::resetClientsExceptHost()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto& client : m_client_states)
  {
    if (client.pid == 0)
      continue;
    client.state = LoadStatus::INIT;
  }
}

bool NetplayManager::canLoadStatus()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto& state : m_client_states)
  {
    if (state.pid != 0 && state.is_active && state.state != LoadStatus::SUCCESS)
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
  std::string base_path = File::GetUserPath(D_STATESAVES_IDX);
  std::string full_path = base_path + "initial" + DIR_SEP + game_id + "_" + hash8 + ".sav";
  return File::Exists(full_path);
}

}  // namespace NetPlay
