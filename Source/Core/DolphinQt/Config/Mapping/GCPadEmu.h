// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <string>

#include "DolphinQt/Config/Mapping/MappingWidget.h"

class QComboBox;
class QGroupBox;

class GCPadEmu final : public MappingWidget
{
  Q_OBJECT
public:
  explicit GCPadEmu(MappingWindow* window);

private slots:
  void OnPresetChanged(int index);

private:
  void CreateMainLayout();
  void LoadSettings() override;
  void SaveSettings() override;

  InputConfig* GetConfig() override;

  QComboBox* m_preset_combo;
  std::map<std::string, QGroupBox*> m_group_boxes;
};
