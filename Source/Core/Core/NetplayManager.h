// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <optional>

#include "Core/SyncIdentifier.h"

#define IS_SERVER 0
#define IS_CLIENT 1

namespace Core {
class System;
}

namespace UICommon
{
class GameFile;
}

namespace NetPlay
{
class NetPlayServer;

enum class LoadStatus
{
  INIT,
  SUCCESS,
  FAILED,
  UNKNOWN
};

struct ClientState
{
  int pid;
  bool is_active = false;
  LoadStatus state = LoadStatus::INIT;
  int idle_seconds = 0;
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

  void activeClientWithPid(int pid);
  void deactiveClientWithPid(int pid);
  void UpdateGameInfo(const UICommon::GameFile& game);
    // 恢复所有客户端状态,游戏改变的时候调用
  void resetClientsExceptHost_NoLock();
  // 检查是否所有活跃客户端都满足读档
  bool canLoadStatus();
  // 检查 StateSaves/initial 目录下是否存在指定游戏的初始存档文件
  bool HasInitialStateSave(const std::string& game_id, const std::string& hash8) const;

  // 触发加载 initial 目录下的即时存档 (game_id_hash8.sav)。
  // 返回值表示是否成功开始加载（文件存在且允许加载）。
  bool LoadInitialState(Core::System& system, const std::string& game_id,
                        const std::string& hash8);

  bool getClientLoadStatus();
  void setClientLoadStatus(LoadStatus status,int pid);
  bool setClientLoadStatusSuccess(int pid);

  void SetCurrentGame(const SyncIdentifier& si, const std::string& netplay_name);

  void IdleTick(NetPlayServer& server);

public:
  // 客户端状态管理
  mutable std::vector<ClientState> m_client_states;

private:

  // Private constructor for singleton
  NetplayManager();

  // Member variables for thread safety
  mutable std::mutex m_mutex;

  struct CurrentGame
  {
    std::string game_id;
    u16 revision = 0;
    u8 disc_number = 0;
    bool is_datel = false;
    std::array<u8, 20> sync_hash{};
    std::string hash8;
    std::string name;
  };

  std::optional<CurrentGame> m_current_game;
};

}  // namespace NetPlay
