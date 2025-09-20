// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

#include <array>

class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

class PlayerSelectionDialog : public QDialog
{
  Q_OBJECT

public:
  explicit PlayerSelectionDialog(QWidget* parent = nullptr);

signals:
  void PlayerSelected(int player_port);

private slots:
  void OnPlayerButtonClicked();

private:
  void CreateLayout();
  void ConnectWidgets();
  void SetupPlayerButton(int player_index);

  QVBoxLayout* m_main_layout;
  QGridLayout* m_button_layout;
  QLabel* m_title_label;
  QLabel* m_description_label;
  std::array<QPushButton*, 4> m_player_buttons;
  QPushButton* m_cancel_button;

  static constexpr int MAX_PLAYERS = 4;
};
