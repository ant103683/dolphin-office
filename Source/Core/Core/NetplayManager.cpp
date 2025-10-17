// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "NetplayManager.h"
#include "Core/NetPlayServer.h"

#include <mutex>
#include <vector>

#define MAX_PLAYERS 4

namespace NetPlay
{

// Static member definitions
std::unique_ptr<NetplayManager> NetplayManager::s_instance = nullptr;
std::mutex NetplayManager::s_instance_mutex;

NetplayManager& NetplayManager::GetInstance()
{
  // Double-checked locking pattern for thread-safe singleton
  if (s_instance == nullptr)
  {
    std::lock_guard<std::mutex> lock(s_instance_mutex);
    if (s_instance == nullptr)
    {
      s_instance = std::unique_ptr<NetplayManager>(new NetplayManager());
    }
  }
  return *s_instance;
}

NetplayManager::NetplayManager()
{
  // TODO: Initialize your member variables here
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
  // TODO: Cleanup code here if needed
}

// Load status management implementation
LoadStatus NetplayManager::GetLoadStatus() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_load_status;
}

void NetplayManager::SetLoadStatus(LoadStatus status)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_load_status = status;
}

bool NetplayManager::ShouldLoadStatus() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_should_load_status;
}

void NetplayManager::SetShouldLoadStatus(bool should_load)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_should_load_status = should_load;
}

void NetplayManager::activeClientWithPid(int pid)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_client_states[pid].is_active = true;
  m_client_states[pid].state = LoadStatus::INIT;
}

void NetplayManager::deactiveClientWithPid(int pid)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_client_states[pid].is_active = false;
  m_client_states[pid].state = LoadStatus::INIT;
}

void NetplayManager::resetAllClient()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto& client : m_client_states)
  {
    client.is_active = false;
    client.state = LoadStatus::INIT;
  }
}

void NetplayManager::resetClientsExceptHost()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto& client : m_client_states)
  {
    if (client.pid == 0)
      continue; // host 保持原状态
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
      m_should_load_status = false;
      return false;
    }
  }
  // 若全部满足条件则保持现有 should_load_status 不变，返回 true
  return true;
}

// TODO: Implement your additional methods here
// Example implementations:
//
// void NetplayManager::SetGameActuallyStarted(bool started)
// {
//   std::lock_guard<std::mutex> lock(m_mutex);
//   m_game_actually_started = started;
// }
//
// bool NetplayManager::IsGameActuallyStarted() const
// {
//   std::lock_guard<std::mutex> lock(m_mutex);
//   return m_game_actually_started;
// }

}  // namespace NetPlay
