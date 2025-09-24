// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/Mapping/GCPadEmu.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>

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
}

void GCPadEmu::CreateMainLayout()
{
  auto* layout = new QGridLayout;

  m_preset_combo = new QComboBox;
  m_preset_combo->addItem(tr("Default"));
  m_preset_combo->addItem(tr("Game 1 Preset"));
  m_preset_combo->addItem(tr("Game 2 Preset"));

  auto* preset_label = new QLabel(tr("Button Preset:"));
  layout->addWidget(preset_label, 0, 0);
  layout->addWidget(m_preset_combo, 0, 1, 1, 3);

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

  // Hardcoded presets for testing
  if (index == 1)  // Game 1 Preset
  {
    for (auto& group : controller->groups)
    {
      if (group->name == "Buttons")
      {
        group->controls[0]->ui_name = "Action";
        group->controls[1]->ui_name = "Back";
        group->controls[2]->ui_name = "Use Item";
        group->controls[3]->ui_name = "Jump";
        group->controls[4]->ui_name = "Camera";
        group->controls[5]->ui_name = "Start";
      }
      else if (group->name == "D-Pad")
      {
        group->controls[0]->ui_name = "\xE4\xB8\x8A";  // "上"
        group->controls[1]->ui_name = "\xE4\xB8\x8B";  // "下"
        group->controls[2]->ui_name = "\xE5\xB7\xA6";  // "左"
        group->controls[3]->ui_name = "\xE5\x8F\xB3";  // "右"
      }
      else if (group->name == "Control Stick")
      {
        group->controls[0]->ui_name = "\xE4\xB8\x8A";  // "上"
        group->controls[1]->ui_name = "\xE4\xB8\x8B";  // "下"
        group->controls[2]->ui_name = "\xE5\xB7\xA6";  // "左"
        group->controls[3]->ui_name = "\xE5\x8F\xB3";  // "右"
        group->controls[4]->ui_name = "\xE4\xBF\xAE\xE9\xA5\xB0\xE9\x94\xAE";  // "修饰键"
      }
      else if (group->name == "C Stick")
      {
        group->controls[0]->ui_name = "\xE4\xB8\x8A";  // "上"
        group->controls[1]->ui_name = "\xE4\xB8\x8B";  // "下"
        group->controls[2]->ui_name = "\xE5\xB7\xA6";  // "左"
        group->controls[3]->ui_name = "\xE5\x8F\xB3";  // "右"
        group->controls[4]->ui_name = "\xE4\xBF\xAE\xE9\xA5\xB0\xE9\x94\xAE";  // "修饰键"
      }
      else if (group->name == "Triggers")
      {
        group->controls[0]->ui_name = "L-Analog";
        group->controls[1]->ui_name = "R-Analog";
        group->controls[2]->ui_name = "L";
        group->controls[3]->ui_name = "R";
        // group->controls[4]->ui_name = "L-\xE6\xA8\xA1\xE6\x8B\x9F";  // "L-模拟"
        // group->controls[5]->ui_name = "R-\xE6\xA8\xA1\xE6\x8B\x9F";  // "R-模拟"
      }
    }
  }
  else if (index == 2)  // Game 2 Preset
  {
    for (auto& group : controller->groups)
    {
      if (group->name == "Buttons")
      {
        group->controls[0]->ui_name = "A";
        group->controls[1]->ui_name = "B";
        group->controls[2]->ui_name = "X";
        group->controls[3]->ui_name = "Y";
        group->controls[4]->ui_name = "Z";
        group->controls[5]->ui_name = "Start";
      }
      else if (group->name == "D-Pad")
      {
        group->controls[0]->ui_name = "D-Up";
        group->controls[1]->ui_name = "D-Down";
        group->controls[2]->ui_name = "D-Left";
        group->controls[3]->ui_name = "D-Right";
      }
      else if (group->name == "Control Stick")
      {
        group->controls[0]->ui_name = "M-Up";
        group->controls[1]->ui_name = "M-Down";
        group->controls[2]->ui_name = "M-Left";
        group->controls[3]->ui_name = "M-Right";
        group->controls[4]->ui_name = "M-Mod";
      }
      else if (group->name == "C Stick")
      {
        group->controls[0]->ui_name = "C-Up";
        group->controls[1]->ui_name = "C-Down";
        group->controls[2]->ui_name = "C-Left";
        group->controls[3]->ui_name = "C-Right";
        group->controls[4]->ui_name = "C-Mod";
      }
      else if (group->name == "Triggers")
      {
        group->controls[0]->ui_name = "L-An";
        group->controls[1]->ui_name = "R-An";
        group->controls[2]->ui_name = "L-Tr";
        group->controls[3]->ui_name = "R-Tr";
        // group->controls[4]->ui_name = "L-An-Btn";
        // group->controls[5]->ui_name = "R-An-Btn";
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
