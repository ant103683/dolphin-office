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
class QPushButton;

class GCPadEmu final : public MappingWidget
{
  Q_OBJECT
public:
  explicit GCPadEmu(MappingWindow* window);

private slots:
  void OnPresetChanged(int index);
  void OnCustomMappingButtonPressed();

private:
  void CreateMainLayout();
  void LoadSettings() override;
  void SaveSettings() override;
  void LoadPresets();

  InputConfig* GetConfig() override;

  struct PadPreset
  {
    QString title;
    QString id;
    QJsonObject mappings;
  };

  QComboBox* m_preset_combo;
  QPushButton* m_custom_mapping_button;
  QPushButton* m_delete_preset_button;
  std::map<std::string, QGroupBox*> m_group_boxes;
  std::vector<PadPreset> m_presets;
  void OnDeletePresetButtonPressed();
  void UpdateDeleteButtonState();
};
