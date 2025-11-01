// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

#include "Core/SyncIdentifier.h"
#include "DolphinQt/GameList/GameListModel.h"

class QVBoxLayout;
class QListWidget;
class QDialogButtonBox;

namespace UICommon
{
class GameFile;
}

class GameListDialog : public QDialog
{
  Q_OBJECT
public:
  explicit GameListDialog(const GameListModel& game_list_model, QWidget* parent);

  int exec() override;
  
  // 新增方法用于获取选中游戏的同步标识符和网络游戏名称
  NetPlay::SyncIdentifier GetSelectedGameSyncIdentifier() const;
  std::string GetSelectedGameNetPlayName() const;

private:
  void CreateWidgets();
  void ConnectWidgets();
  void PopulateGameList();

  const GameListModel& m_game_list_model;
  QVBoxLayout* m_main_layout;
  QListWidget* m_game_list;
  QDialogButtonBox* m_button_box;
};
