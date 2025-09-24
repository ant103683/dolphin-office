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
    layout->addWidget(CreateGroupBox(tr(info.name), Pad::GetGroup(GetPort(), info.group_id)),
                      info.row, info.col, info.rowspan, info.colspan);
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

  if (index == 1)  // Game 1
  {
    auto* buttons = Pad::GetGroup(GetPort(), PadGroup::Buttons);
    for (auto& control : buttons->controls)
    {
      if (control->name == "A")
        control->ui_name = "Action";
      if (control->name == "B")
        control->ui_name = "Back";
      if (control->name == "X")
        control->ui_name = "Use Item";
      if (control->name == "Y")
        control->ui_name = "Jump";
      if (control->name == "Z")
        control->ui_name = "Camera";
      if (control->name == "START")
        control->ui_name = "Pause";
    }
  }
  else if (index == 2)  // Game 2
  {
    auto* buttons = Pad::GetGroup(GetPort(), PadGroup::Buttons);
    for (auto& control : buttons->controls)
    {
      if (control->name == "A")
        control->ui_name = "Confirm";
      if (control->name == "B")
        control->ui_name = "Cancel";
      if (control->name == "X")
        control->ui_name = "Menu";
      if (control->name == "Y")
        control->ui_name = "Map";
      if (control->name == "Z")
        control->ui_name = "Special";
      if (control->name == "START")
        control->ui_name = "Options";
    }
  }

  auto* l = static_cast<QGridLayout*>(layout());
  for (const auto& info : s_group_box_infos)
  {
    QLayoutItem* old_item = l->itemAtPosition(info.row, info.col);
    if (old_item)
    {
      delete old_item->widget();
    }
    l->addWidget(CreateGroupBox(tr(info.name), Pad::GetGroup(GetPort(), info.group_id)), info.row,
                 info.col, info.rowspan, info.colspan);
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
