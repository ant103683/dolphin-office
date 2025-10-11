// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <vector>

#include "Core/NetPlayProto.h"

class QCheckBox;
class QGridLayout;
class QComboBox;
class QDialogButtonBox;

namespace NetPlay
{
class Player;
}

class PadMappingDialog : public QDialog
{
  Q_OBJECT
public:
  explicit PadMappingDialog(QWidget* widget);

  int exec() override;

  void SetInitialData(const NetPlay::PadMappingArray& initial_gc_maps,
                      const NetPlay::GBAConfigArray& initial_gba_config,
                      const NetPlay::PadMappingArray& initial_wii_maps,
                      const std::vector<const NetPlay::Player*>& players);

  NetPlay::PadMappingArray GetGCPadArray();
  NetPlay::GBAConfigArray GetGBAArray();
  NetPlay::PadMappingArray GetWiimoteArray();

private:
  void CreateWidgets();
  void ConnectWidgets();

  void OnMappingChanged();

  NetPlay::PadMappingArray m_pad_mapping;
  NetPlay::GBAConfigArray m_gba_config;
  NetPlay::PadMappingArray m_wii_mapping;

  QGridLayout* m_main_layout;
  std::array<QComboBox*, 4> m_gc_boxes;
  std::array<QCheckBox*, 4> m_gba_boxes;
  std::array<QComboBox*, 4> m_wii_boxes;
  std::vector<const NetPlay::Player*> m_players;
  QDialogButtonBox* m_button_box;
};
