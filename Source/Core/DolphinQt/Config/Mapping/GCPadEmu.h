// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <string>
#include <vector>

#include <QJsonObject>

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
  void LoadPresets();

  InputConfig* GetConfig() override;

  struct PadPreset
  {
    QString title;
    QJsonObject mappings;
  };

  QComboBox* m_preset_combo;
  std::map<std::string, QGroupBox*> m_group_boxes;
  std::vector<PadPreset> m_presets;
};
