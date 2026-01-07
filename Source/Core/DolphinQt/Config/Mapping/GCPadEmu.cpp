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
#include <QSignalBlocker>
#include <QStringList>
#include <QTimer>
#include <QMessageBox>

#include "DolphinQt/Config/Mapping/GCPadCustomPresetDialog.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/Config/Config.h"
#include "Core/Config/UISettings.h"

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
  connect(m_preset_combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
          &GCPadEmu::UpdateDeleteButtonState);
  connect(m_preset_combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
          &GCPadEmu::UpdateDeleteButtonState);
  connect(m_custom_mapping_button, &QPushButton::clicked, this,
          &GCPadEmu::OnCustomMappingButtonPressed);

  QTimer::singleShot(0, this, [this] {
    if (GetController())
      OnPresetChanged(m_preset_combo->currentIndex());
  });
}

void GCPadEmu::LoadPresets()
{
  const std::string user_dir = File::GetUserPath(D_USER_IDX) + std::string("Profiles/");
  const std::string user_path = user_dir + std::string("GCPadPresets.json");
  const std::string sys_path = File::GetSysDirectory() + std::string("/Profiles/GCPadPresets.json");

  if (!File::Exists(user_path) && File::Exists(sys_path))
  {
    File::CreateFullPath(user_path);
    File::CopyRegularFile(sys_path, user_path);
  }

  QFile file(QString::fromStdString(user_path));
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
    preset.id = preset_obj[QString::fromStdString("id")].toString();
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

  {
    const int port = GetPort();
    const auto& info_id = (port == 0) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_0
                          : (port == 1) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_1
                          : (port == 2) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_2
                                         : Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_3;
    const auto& info_title = (port == 0) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_0
                             : (port == 1) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_1
                             : (port == 2) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_2
                                            : Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_3;
    const std::string last_id = Config::Get(info_id);
    const std::string last_title = Config::Get(info_title);
    int found = -1;
    if (!last_id.empty())
    {
      for (int i = 0; i < static_cast<int>(m_presets.size()); ++i)
      {
        if (m_presets[i].id.toStdString() == last_id)
        {
          found = i;
          break;
        }
      }
    }
    if (found == -1 && !last_title.empty())
    {
      for (int i = 0; i < static_cast<int>(m_presets.size()); ++i)
      {
        if (m_presets[i].title.toStdString() == last_title)
        {
          found = i;
          break;
        }
      }
    }
    if (found >= 0)
      m_preset_combo->setCurrentIndex(found + 1);
  }

  auto* preset_label = new QLabel(tr("Custom Game Key Mapping Template:"));
  m_custom_mapping_button = new QPushButton(tr("Custom Key Template"));
  layout->addWidget(preset_label, 0, 0);
  layout->addWidget(m_preset_combo, 0, 1, 1, 2);
  layout->addWidget(m_custom_mapping_button, 0, 3);
  m_delete_preset_button = new QPushButton(tr("Delete"));
  layout->addWidget(m_delete_preset_button, 0, 4);
  connect(m_delete_preset_button, &QPushButton::clicked, this, &GCPadEmu::OnDeletePresetButtonPressed);

  for (const auto& info : s_group_box_infos)
  {
    auto* group_box = CreateGroupBox(tr(info.name), Pad::GetGroup(GetPort(), info.group_id));
    m_group_boxes[info.name] = group_box;
    layout->addWidget(group_box, info.row, info.col, info.rowspan, info.colspan);
  }

  setLayout(layout);

  OnPresetChanged(m_preset_combo->currentIndex());
  UpdateDeleteButtonState();
}

void GCPadEmu::OnPresetChanged(int index)
{
  // Reset all controls to default
  auto* controller = GetController();
  if (!controller)
  {
    UpdateDeleteButtonState();
    return;
  }

  for (auto& group : controller->groups)
  {
    for (auto& control : group->controls)
    {
      control->ui_name = control->name;
    }
  }

  if (index > 0 && index <= static_cast<int>(m_presets.size()))
  {
    const auto& preset = m_presets.at(index - 1);
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

  {
    const int port = GetPort();
    const auto& info = (port == 0) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_0
                         : (port == 1) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_1
                         : (port == 2) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_2
                                        : Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_3;
    const auto& info_id = (port == 0) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_0
                          : (port == 1) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_1
                          : (port == 2) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_2
                                         : Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_3;
    if (index > 0 && index <= static_cast<int>(m_presets.size()))
    {
      const auto& p = m_presets.at(index - 1);
      Config::SetBase(info, p.title.toStdString());
      if (!p.id.isEmpty())
        Config::SetBase(info_id, p.id.toStdString());
    }
    else
    {
      Config::SetBase(info, std::string());
      Config::SetBase(info_id, std::string());
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
  UpdateDeleteButtonState();
}

void GCPadEmu::OnCustomMappingButtonPressed()
{
  GCPadCustomPresetDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted)
  {
    {
      QSignalBlocker blocker(m_preset_combo);
      m_preset_combo->clear();
      m_preset_combo->addItem(tr("Default"));
      m_presets.clear();
      LoadPresets();
      for (const auto& preset : m_presets)
      {
        m_preset_combo->addItem(preset.title);
      }
    }
    m_preset_combo->setCurrentIndex(0);
    UpdateDeleteButtonState();
  }
}

void GCPadEmu::OnDeletePresetButtonPressed()
{
  const int index = m_preset_combo->currentIndex();
  if (index <= 0)
    return;

  const auto title = m_presets.at(index - 1).title;
  const auto id = m_presets.at(index - 1).id;

  QMessageBox confirm(this);
  confirm.setIcon(QMessageBox::Warning);
  confirm.setWindowTitle(tr("Confirm"));
  confirm.setText(tr("Are you sure that you want to delete '%1'?").arg(title));
  confirm.setInformativeText(tr("This cannot be undone!"));
  confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
  if (confirm.exec() != QMessageBox::Yes)
    return;

  const std::string user_dir = File::GetUserPath(D_USER_IDX) + std::string("Profiles/");
  const std::string user_path = user_dir + std::string("GCPadPresets.json");

  QFile file(QString::fromStdString(user_path));
  if (!file.open(QIODevice::ReadOnly))
    return;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();
  if (doc.isNull() || !doc.isObject())
    return;
  QJsonObject root = doc.object();
  QJsonArray presets = root[QStringLiteral("presets")].toArray();

  int remove_index = -1;
  for (int i = 0; i < presets.size(); ++i)
  {
    const auto obj = presets[i].toObject();
    const auto obj_id = obj[QStringLiteral("id")].toString();
    const auto obj_title = obj[QStringLiteral("title")].toString();
    if ((!id.isEmpty() && obj_id == id) || (id.isEmpty() && obj_title == title))
    {
      remove_index = i;
      break;
    }
  }
  if (remove_index == -1)
    return;
  presets.removeAt(remove_index);
  root[QStringLiteral("presets")] = presets;

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;
  file.write(QJsonDocument(root).toJson());
  file.close();

  {
    QSignalBlocker blocker(m_preset_combo);
    m_preset_combo->clear();
    m_preset_combo->addItem(tr("Default"));
    m_presets.clear();
    LoadPresets();
    for (const auto& preset : m_presets)
      m_preset_combo->addItem(preset.title);
  }

  const int port = GetPort();
  const auto& info_id = (port == 0) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_0
                        : (port == 1) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_1
                        : (port == 2) ? Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_2
                                       : Config::MAIN_GCPAD_LAST_PRESET_ID_PORT_3;
  const auto& info_title = (port == 0) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_0
                          : (port == 1) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_1
                          : (port == 2) ? Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_2
                                         : Config::MAIN_GCPAD_LAST_PRESET_TITLE_PORT_3;

  const std::string last_id = Config::Get(info_id);
  const std::string last_title = Config::Get(info_title);

  bool deleted_current = (!id.isEmpty() && last_id == id.toStdString()) ||
                         (id.isEmpty() && last_title == title.toStdString());

  if (deleted_current)
  {
    Config::SetBase(info_id, std::string());
    Config::SetBase(info_title, std::string());
    m_preset_combo->setCurrentIndex(0);
  }
  else
  {
    int found = -1;
    if (!last_id.empty())
    {
      for (int i = 0; i < static_cast<int>(m_presets.size()); ++i)
        if (m_presets[i].id.toStdString() == last_id)
          found = i;
    }
    if (found == -1 && !last_title.empty())
    {
      for (int i = 0; i < static_cast<int>(m_presets.size()); ++i)
        if (m_presets[i].title.toStdString() == last_title)
          found = i;
    }
    m_preset_combo->setCurrentIndex(found >= 0 ? found + 1 : 0);
  }

  OnPresetChanged(m_preset_combo->currentIndex());
  UpdateDeleteButtonState();
}

void GCPadEmu::UpdateDeleteButtonState()
{
  if (m_delete_preset_button)
    m_delete_preset_button->setEnabled(m_preset_combo->currentIndex() > 0);
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
