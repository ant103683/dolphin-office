// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/Mapping/GCPadEmu.h"

#include <QComboBox>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>

#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"

#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"

#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"
#include "InputCommon/InputConfig.h"

namespace
{
struct GroupBoxInfo
{
  PadGroup group_id;
  const char* name;
  int row, col, rowspan, colspan;
};

const std::vector<GroupBoxInfo> s_group_box_infos = {
    {PadGroup::Buttons, QT_TR_NOOP("Buttons"), 1, 0, 1, 1},
    {PadGroup::DPad, QT_TR_NOOP("D-Pad"), 2, 0, 1, 1},
    {PadGroup::MainStick, QT_TR_NOOP("Control Stick"), 1, 1, 2, 1},
    {PadGroup::CStick, QT_TR_NOOP("C Stick"), 1, 2, 2, 1},
    {PadGroup::Triggers, QT_TR_NOOP("Triggers"), 1, 3, 1, 1},
    {PadGroup::Rumble, QT_TR_NOOP("Rumble"), 2, 3, 1, 1},
    {PadGroup::Options, QT_TR_NOOP("Options"), 3, 3, 1, 1},
};
}  // namespace

GCPadEmu::GCPadEmu(MappingWindow* window) : MappingWidget(window)
{
  CreateMainLayout();
  connect(m_preset_combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
          &GCPadEmu::OnPresetChanged);
  connect(m_custom_mapping_button, &QPushButton::clicked, this,
          &GCPadEmu::OnCustomMappingButtonPressed);
}

void GCPadEmu::LoadPresets()
{
  std::string path = File::GetSysDirectory() + "/Profiles/GCPadPresets.json";
  QFile file(QString::fromStdString(path));
  if (!file.open(QIODevice::ReadOnly))
  {
    //ERROR_LOG(COMMON, "Failed to open GCPadPresets.json at %s", path.c_str());
    return;
  }

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (doc.isNull())
  {
    //ERROR_LOG(COMMON, "Failed to parse GCPadPresets.json. It might be malformed.");
    return;
  }

  QJsonObject root = doc.object();
  QJsonArray presets = root[QString::fromStdString("presets")].toArray();

  //NOTICE_LOG(COMMON, "Found %d presets in GCPadPresets.json", presets.size());

  for (const auto& preset_value : presets)
  {
    QJsonObject preset_obj = preset_value.toObject();
    PadPreset preset;
    preset.title = preset_obj[QString::fromStdString("title")].toString();
    preset.mappings = preset_obj[QString::fromStdString("mappings")].toObject();
    m_presets.push_back(preset);
    //NOTICE_LOG(COMMON, "Loaded preset: %s", preset.title.toUtf8().constData());
  }
}

void GCPadEmu::CreateMainLayout()
{
  auto* layout = new QGridLayout;

  m_preset_combo = new QComboBox;
  m_preset_combo->addItem(tr("Default"));

  LoadPresets();

  for (const auto& preset : m_presets)
  {
    m_preset_combo->addItem(preset.title);
  }

  auto* preset_label = new QLabel(tr("Custom Game Key Mapping Template:"));
  m_custom_mapping_button = new QPushButton(tr("Custom Key Template"));
  layout->addWidget(preset_label, 0, 0);
  layout->addWidget(m_preset_combo, 0, 1, 1, 2);
  layout->addWidget(m_custom_mapping_button, 0, 3);

  for (const auto& info : s_group_box_infos)
  {
    auto* group_box = CreateGroupBox(tr(info.name), Pad::GetGroup(GetPort(), info.group_id));
    m_group_boxes[info.name] = group_box;
    layout->addWidget(group_box, info.row, info.col, info.rowspan, info.colspan);
  }

  setLayout(layout);
}

void GCPadEmu::OnPresetChanged(int index)
{
  // Reset all controls to default
  auto* controller = GetController();
  if (!controller)
    return;

  for (auto& group : controller->groups)
  {
    for (auto& control : group->controls)
    {
      control->ui_name = control->name;
    }
  }

  if (index == 0)  // Default
  {
    // UI is already reset to default, so just refresh it
  }
  else
  {
    const auto& preset = m_presets[index - 1];
    const auto& mappings = preset.mappings;

    for (auto& group : controller->groups)
    {
      if (mappings.contains(QString::fromStdString(group->name)))
      {
        QJsonArray mapping_array = mappings[QString::fromStdString(group->name)].toArray();
        for (int i = 0; i < mapping_array.size() && i < group->controls.size(); ++i)
        {
          group->controls[i]->ui_name = mapping_array[i].toString().toStdString();
        }
      }
    }
  }

  auto* grid_layout = static_cast<QGridLayout*>(layout());
  for (auto& pair : m_group_boxes)
  {
    if (pair.second)
    {
      grid_layout->removeWidget(pair.second);
      pair.second->deleteLater();
    }
  }
  m_group_boxes.clear();

  for (const auto& info : s_group_box_infos)
  {
    auto* group_box = CreateGroupBox(tr(info.name), Pad::GetGroup(GetPort(), info.group_id));
    m_group_boxes[info.name] = group_box;
    grid_layout->addWidget(group_box, info.row, info.col, info.rowspan, info.colspan);
  }
}

void GCPadEmu::OnCustomMappingButtonPressed()
{
}

void GCPadEmu::LoadSettings()
{
  Pad::LoadConfig();
}

void GCPadEmu::SaveSettings()
{
  Pad::GetConfig()->SaveConfig();
}

InputConfig* GCPadEmu::GetConfig()
{
  return Pad::GetConfig();
}
