// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace NetPlay
{

enum class LoadStatus
{
  INIT,
  SUCCESS,
  FAILED,
  UNKNOWN
};

struct ClientState
{
  int pid = 0;
  bool is_active = false;
  LoadStatus state = LoadStatus::INIT;
};

class NetplayManager
{
public:
  // Get the singleton instance
  static NetplayManager& GetInstance();

  // Delete copy constructor and assignment operator to ensure singleton
  NetplayManager(const NetplayManager&) = delete;
  NetplayManager& operator=(const NetplayManager&) = delete;

  // Delete move constructor and assignment operator
  NetplayManager(NetplayManager&&) = delete;
  NetplayManager& operator=(NetplayManager&&) = delete;

  // Destructor
  ~NetplayManager();

  // TODO: Add your methods and properties here
  // Example methods that you might want to implement:
  // void SetGameActuallyStarted(bool started);
  // bool IsGameActuallyStarted() const;
  // void SetClientState(ClientState state);
  // ClientState GetClientState() const;

  // Load status management
  LoadStatus GetLoadStatus() const;
  // 设置加是否读档
  void SetLoadStatus(LoadStatus status);
  // 是否读档
  bool ShouldLoadStatus() const;
  // 设置是否应该加载状态
  void SetShouldLoadStatus(bool should_load);
  // 收到客户端加入的通知,将其状态设为活跃
  void activeClientWithPid(int pid);
  void deactiveClientWithPid(int pid);
  void resetAllClient();
  void resetClientsExceptHost();
  // 检查是否所有活跃客户端都满足读档
  bool canLoadStatus();

public:
  // 客户端状态管理
  mutable std::vector<ClientState> m_client_states;

private:
  // Private constructor for singleton
  NetplayManager();

  // Static instance and mutex for thread safety
  static std::unique_ptr<NetplayManager> s_instance;
  static std::mutex s_instance_mutex;

  // Member variables for thread safety
  mutable std::mutex m_mutex;

  // 客户端状态管理
  LoadStatus m_load_status = LoadStatus::UNKNOWN;

  // 如果有任何玩家出错,马上调为false,后续同个游戏不加载存档;如果改变为不同游戏,则改为true
  bool m_should_load_status = true;

  // TODO: Add your private member variables here
  // Example:
  // bool m_game_actually_started = false;
  // ClientState m_client_state = ClientState::Disconnected;
};

}  // namespace NetPlay
