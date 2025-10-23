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

  // 设置加是否读档
  void SetLoadStatus(bool status);
  // 是否读档,用于防止出错时第二次还进入读档
  bool ShouldLoadStatus();
  // 设置是否应该加载状态,游戏改变或者有客户端报告不满足读档调用
  void SetShouldLoadStatus(bool should_load);
  // 收到客户端加入的通知,将其状态设为活跃/不活跃
  void activeClientWithPid(int pid);
  void deactiveClientWithPid(int pid);
  // 恢复所有客户端状态,游戏改变的时候调用
  void resetClientsExceptHost();
  // 检查是否所有活跃客户端都满足读档
  bool canLoadStatus();
  // 检查 StateSaves/initial 目录下是否存在指定游戏的初始存档文件
  bool HasInitialStateSave(const std::string& game_id, const std::string& hash8) const;

  bool getClientLoadStatus();
  void setClientLoadStatus(LoadStatus status,int pid);
  bool setClientLoadStatusSuccess(int pid);

public:
  // 客户端状态管理
  mutable std::vector<ClientState> m_client_states;

private:
  // Private constructor for singleton
  NetplayManager();

  // Member variables for thread safety
  mutable std::mutex m_mutex;

  // 客户端状态管理
  bool m_load_status = false;
  // 如果有任何玩家出错,马上调为false,后续同个游戏不加载存档;如果改变为不同游戏,则改为true
  bool m_should_load_status = true;

  // TODO: Add your private member variables here
  // Example:
  // bool m_game_actually_started = false;
  // ClientState m_client_state = ClientState::Disconnected;
};

}  // namespace NetPlay
