// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/NetPlay/GameListDialog.h"

#include <memory>

#include <fmt/format.h>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QVBoxLayout>
#include <QVariant>

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/JsonUtil.h"
#include "Core/NetplayManager.h"
#include "UICommon/GameFile.h"
#include "UICommon/GameListExporter.h"

// 声明picojson::value为QVariant可转换类型
Q_DECLARE_METATYPE(picojson::value)

// 调试日志函数
static void WriteDebugLog(const std::string& message)
{
  const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
  File::CreateFullPath(log_path);
  File::IOFile log_file(log_path, "ab");
  if (log_file)
  {
    static bool s_header_written = false;
    if (!s_header_written)
    {
      const std::string header = fmt::format("[202511] ===== GameListDialog Debug Session =====\n");
      log_file.WriteBytes(header.data(), header.size());
      s_header_written = true;
    }
    const std::string line = fmt::format("[202511] {}\n", message);
    log_file.WriteBytes(line.data(), line.size());
  }
}

GameListDialog::GameListDialog(const GameListModel& game_list_model, QWidget* parent)
    : QDialog(parent), m_game_list_model(game_list_model)
{
  WriteDebugLog("GameListDialog constructor called");
  setWindowTitle(tr("Select a game"));

  CreateWidgets();
  ConnectWidgets();
  WriteDebugLog("GameListDialog constructor completed");
}

void GameListDialog::CreateWidgets()
{
  m_main_layout = new QVBoxLayout;
  m_game_list = new QListWidget;
  m_button_box = new QDialogButtonBox(QDialogButtonBox::Ok);
  m_button_box->setEnabled(false);

  m_main_layout->addWidget(m_game_list);
  m_main_layout->addWidget(m_button_box);

  setLayout(m_main_layout);
}

void GameListDialog::ConnectWidgets()
{
  connect(m_game_list, &QListWidget::itemSelectionChanged,
          [this] { m_button_box->setEnabled(m_game_list->currentRow() != -1); });

  connect(m_game_list, &QListWidget::itemDoubleClicked, this, &GameListDialog::accept);
  connect(m_button_box, &QDialogButtonBox::accepted, this, &GameListDialog::accept);
}

void GameListDialog::PopulateGameList()
{
  std::string log_path = File::GetUserPath(D_LOGS_IDX);
  log_path += "savehash8.txt";
  File::CreateFullPath(log_path);
  std::ofstream fp;
  File::OpenFStream(fp, log_path, std::ios_base::app);
  if (fp)
  {
    fp << "PopulateGameList: Starting to populate game list.\n";
  }
  m_game_list->clear();
  if (fp)
  {
    fp << "PopulateGameList: IS_SERVER is true, loading from JSON.\n";
  }
  
  // 当IS_SERVER为真时，从JSON文件获取游戏列表
  picojson::value json_data = UICommon::ImportGamesListJson();
  
  if (json_data.is<picojson::object>())
  {
    if (fp)
    {
      fp << "PopulateGameList: JSON data is a valid object.\n";
    }
    const auto& root_obj = json_data.get<picojson::object>();
    auto games_it = root_obj.find("games");
    
    if (games_it == root_obj.end())
    {
      if (fp)
      {
        fp << "PopulateGameList: 'games' key not found in JSON root object.\n";
      }
    }
    else if (!games_it->second.is<picojson::array>())
    {
      if (fp)
      {
        fp << "PopulateGameList: 'games' value is not an array.\n";
      }
    }
    else
    {
      const auto& games_array = games_it->second.get<picojson::array>();
      if (fp)
      {
        fp << fmt::format("PopulateGameList: Found {} games in JSON\n", games_array.size());
      }
      
      for (const auto& game_value : games_array)
      {
        if (game_value.is<picojson::object>())
        {
          const auto& game_obj = game_value.get<picojson::object>();
          
          // 获取netplay_name字段
          auto name_it = game_obj.find("netplay_name");
          if (name_it != game_obj.end() && name_it->second.is<std::string>())
          {
            QString game_name = QString::fromStdString(name_it->second.get<std::string>());
            if (fp)
            {
              fp << fmt::format("PopulateGameList: Adding game: {}\n", name_it->second.get<std::string>());
            }
            auto* item = new QListWidgetItem(game_name);
            
            // 将JSON对象存储在UserRole中，以便后续使用
            QVariant json_variant;
            json_variant.setValue(game_value);
            item->setData(Qt::UserRole, json_variant);
            
            m_game_list->addItem(item);
          }
          else
          {
            if (fp)
            {
              fp << "PopulateGameList: Game object missing netplay_name field\n";
            }
          }
        }
        else
        {
          if (fp)
          {
            fp << "PopulateGameList: Invalid game object in array\n";
          }
        }
      }
    }
  }
  else
  {
    if (fp)
    {
      fp << "PopulateGameList: JSON data is not a valid object\n";
    }
  }
  fp.close();

}

NetPlay::SyncIdentifier GameListDialog::GetSelectedGameSyncIdentifier() const
{
  WriteDebugLog("GetSelectedGameSyncIdentifier: Starting to get sync identifier");
  auto items = m_game_list->selectedItems();
  if (items.isEmpty())
  {
    WriteDebugLog("GetSelectedGameSyncIdentifier: No items selected");
    return {};
  }

#if IS_SERVER
  WriteDebugLog("GetSelectedGameSyncIdentifier: IS_SERVER is true, extracting from JSON");
  // 当IS_SERVER为真时，从JSON数据重构SyncIdentifier
  QVariant json_variant = items[0]->data(Qt::UserRole);
  if (json_variant.canConvert<picojson::value>())
  {
    WriteDebugLog("GetSelectedGameSyncIdentifier: JSON variant is convertible");
    picojson::value game_json = json_variant.value<picojson::value>();
    if (game_json.is<picojson::object>())
    {
      WriteDebugLog("GetSelectedGameSyncIdentifier: Game JSON is valid object");
      const auto& game_obj = game_json.get<picojson::object>();
      NetPlay::SyncIdentifier sync_id;
      
      // 从sync_identifier对象中提取信息
      auto sync_id_it = game_obj.find("sync_identifier");
      if (sync_id_it != game_obj.end() && sync_id_it->second.is<picojson::object>())
      {
        WriteDebugLog("GetSelectedGameSyncIdentifier: Found sync_identifier object");
        const auto& sync_obj = sync_id_it->second.get<picojson::object>();
        
        auto dol_elf_size_it = sync_obj.find("dol_elf_size");
        if (dol_elf_size_it != sync_obj.end() && dol_elf_size_it->second.is<double>())
        {
          sync_id.dol_elf_size = static_cast<u64>(dol_elf_size_it->second.get<double>());
          WriteDebugLog(fmt::format("GetSelectedGameSyncIdentifier: dol_elf_size = {}", sync_id.dol_elf_size));
        }
          
        auto game_id_it = sync_obj.find("game_id");
        if (game_id_it != sync_obj.end() && game_id_it->second.is<std::string>())
        {
          sync_id.game_id = game_id_it->second.get<std::string>();
          WriteDebugLog(fmt::format("GetSelectedGameSyncIdentifier: game_id = {}", sync_id.game_id));
        }
          
        auto revision_it = sync_obj.find("revision");
        if (revision_it != sync_obj.end() && revision_it->second.is<double>())
        {
          sync_id.revision = static_cast<u16>(revision_it->second.get<double>());
          WriteDebugLog(fmt::format("GetSelectedGameSyncIdentifier: revision = {}", sync_id.revision));
        }
          
        auto disc_number_it = sync_obj.find("disc_number");
        if (disc_number_it != sync_obj.end() && disc_number_it->second.is<double>())
        {
          sync_id.disc_number = static_cast<u8>(disc_number_it->second.get<double>());
          WriteDebugLog(fmt::format("GetSelectedGameSyncIdentifier: disc_number = {}", sync_id.disc_number));
        }
          
        auto is_datel_it = sync_obj.find("is_datel");
        if (is_datel_it != sync_obj.end() && is_datel_it->second.is<bool>())
        {
          sync_id.is_datel = is_datel_it->second.get<bool>();
          WriteDebugLog(fmt::format("GetSelectedGameSyncIdentifier: is_datel = {}", sync_id.is_datel));
        }
      }
      else
      {
        WriteDebugLog("GetSelectedGameSyncIdentifier: sync_identifier object not found or invalid");
      }
      
      // 从sync_hash数组中提取哈希值
      auto sync_hash_it = game_obj.find("sync_hash");
      if (sync_hash_it != game_obj.end() && sync_hash_it->second.is<picojson::array>())
      {
        WriteDebugLog("GetSelectedGameSyncIdentifier: Found sync_hash array");
        const auto& hash_array = sync_hash_it->second.get<picojson::array>();
        for (size_t i = 0; i < std::min(hash_array.size(), sync_id.sync_hash.size()); ++i)
        {
          if (hash_array[i].is<double>())
            sync_id.sync_hash[i] = static_cast<u8>(hash_array[i].get<double>());
        }
      }
      else if (sync_hash_it != game_obj.end() && sync_hash_it->second.is<std::string>())
      {
        WriteDebugLog("GetSelectedGameSyncIdentifier: Found sync_hash string, converting from hex");
        const std::string hash_str = sync_hash_it->second.get<std::string>();
        WriteDebugLog(fmt::format("GetSelectedGameSyncIdentifier: sync_hash string = {}", hash_str));
        // 将十六进制字符串转换为字节数组
        if (hash_str.length() == 40) // SHA1 hash is 40 hex characters
        {
          for (size_t i = 0; i < 20; ++i)
          {
            std::string byte_str = hash_str.substr(i * 2, 2);
            sync_id.sync_hash[i] = static_cast<u8>(std::stoul(byte_str, nullptr, 16));
          }
        }
      }
      else
      {
        WriteDebugLog("GetSelectedGameSyncIdentifier: sync_hash not found or invalid format");
      }
      
      WriteDebugLog("GetSelectedGameSyncIdentifier: Successfully constructed SyncIdentifier from JSON");
      return sync_id;
    }
    else
    {
      WriteDebugLog("GetSelectedGameSyncIdentifier: Game JSON is not an object");
    }
  }
  else
  {
    WriteDebugLog("GetSelectedGameSyncIdentifier: JSON variant is not convertible");
  }
  WriteDebugLog("GetSelectedGameSyncIdentifier: Returning empty SyncIdentifier");
  return {};
#else
  WriteDebugLog("GetSelectedGameSyncIdentifier: IS_SERVER is false, using original logic");
  // 当IS_SERVER为假时，使用原有逻辑
  const UICommon::GameFile& game = *items[0]->data(Qt::UserRole).value<std::shared_ptr<const UICommon::GameFile>>();
  auto sync_id = game.GetSyncIdentifier();
  WriteDebugLog(fmt::format("GetSelectedGameSyncIdentifier: Got sync identifier from GameFile: game_id={}", sync_id.game_id));
  return sync_id;
#endif
}

std::string GameListDialog::GetSelectedGameNetPlayName() const
{
  WriteDebugLog("GetSelectedGameNetPlayName: Starting to get netplay name");
  auto items = m_game_list->selectedItems();
  if (items.isEmpty())
  {
    WriteDebugLog("GetSelectedGameNetPlayName: No items selected");
    return "";
  }

#if IS_SERVER
  WriteDebugLog("GetSelectedGameNetPlayName: IS_SERVER is true, extracting from JSON");
  // 当IS_SERVER为真时，从JSON数据获取netplay_name
  QVariant json_variant = items[0]->data(Qt::UserRole);
  if (json_variant.canConvert<picojson::value>())
  {
    WriteDebugLog("GetSelectedGameNetPlayName: JSON variant is convertible");
    picojson::value game_json = json_variant.value<picojson::value>();
    if (game_json.is<picojson::object>())
    {
      WriteDebugLog("GetSelectedGameNetPlayName: Game JSON is valid object");
      const auto& game_obj = game_json.get<picojson::object>();
      auto name_it = game_obj.find("netplay_name");
      if (name_it != game_obj.end() && name_it->second.is<std::string>())
      {
        std::string netplay_name = name_it->second.get<std::string>();
        WriteDebugLog(fmt::format("GetSelectedGameNetPlayName: Found netplay_name = {}", netplay_name));
        return netplay_name;
      }
      else
      {
        WriteDebugLog("GetSelectedGameNetPlayName: netplay_name field not found or invalid");
      }
    }
    else
    {
      WriteDebugLog("GetSelectedGameNetPlayName: Game JSON is not an object");
    }
  }
  else
  {
    WriteDebugLog("GetSelectedGameNetPlayName: JSON variant is not convertible");
  }
  WriteDebugLog("GetSelectedGameNetPlayName: Returning empty string");
  return "";
#else
  WriteDebugLog("GetSelectedGameNetPlayName: IS_SERVER is false, using original logic");
  // 当IS_SERVER为假时，使用原有逻辑
  const UICommon::GameFile& game = *items[0]->data(Qt::UserRole).value<std::shared_ptr<const UICommon::GameFile>>();
  std::string netplay_name = m_game_list_model.GetNetPlayName(game);
  WriteDebugLog(fmt::format("GetSelectedGameNetPlayName: Got netplay name from GameFile: {}", netplay_name));
  return netplay_name;
#endif
}

int GameListDialog::exec()
{
  WriteDebugLog("GameListDialog::exec() called - about to populate game list");
  PopulateGameList();
  WriteDebugLog("GameListDialog::exec() - PopulateGameList completed, showing dialog");
  return QDialog::exec();
}
