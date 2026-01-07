// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlayUpload.h"

#include <optional>
#include <string>
#include <vector>

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/NandPaths.h"
#include <fmt/format.h>
#include <fmt/ranges.h>
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/HW/WiiSave.h"
#include "Core/HW/WiiSaveStructs.h"
#include "Core/IOS/IOS.h"
#include "Core/IOS/Uids.h"
#include "Common/SFMLHelper.h"
#include "Core/NetPlayCommon.h"
#include "Core/NetPlayProto.h"

namespace NetPlayUpload
{
namespace FS = IOS::HLE::FS;

static void LogServerHash8(const std::string& s)
{
  const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "serverhash8.txt";
  File::IOFile lf(log_path, "ab");
  if (lf)
    lf.WriteBytes(s.data(), s.size());
}

static std::optional<std::vector<u8>> ReadFileToBuffer(FS::FileSystem* fs,
                                                       const std::string& wii_path)
{
  auto fh = fs->OpenFile(IOS::PID_KERNEL, IOS::PID_KERNEL, wii_path, FS::Mode::Read);
  if (!fh)
    return std::nullopt;
  auto st = fh->GetStatus();
  if (!st)
    return std::nullopt;
  std::vector<u8> buf(st->size);
  auto rd = fh->Read(buf.data(), buf.size());
  if (!rd || *rd != buf.size())
    return std::nullopt;
  return buf;
}

bool BuildClientWiiSaveUploadPacket(FS::FileSystem* temp_fs, const std::vector<u64>& titles,
                                    const std::string& redirect_path, sf::Packet& out_packet)
{
  LogServerHash8(fmt::format("[UPLOAD_BUILD] start titles={} redirect_present={}\n", titles.size(), !redirect_path.empty()));
  out_packet.clear();
  out_packet << NetPlay::MessageID::SyncSaveData;
  out_packet << NetPlay::SyncSaveDataID::WiiData;

  // Mii 数据
  if (auto mii_buf = ReadFileToBuffer(temp_fs, Common::GetMiiDatabasePath()))
  {
    LogServerHash8("[UPLOAD_BUILD] mii=1\n");
    out_packet << true;
    if (!NetPlay::CompressBufferIntoPacket(*mii_buf, out_packet))
      return false;
  }
  else
  {
    LogServerHash8("[UPLOAD_BUILD] mii=0\n");
    out_packet << false;
  }

  // Wii 存档列表
  out_packet << static_cast<u32>(titles.size());
  for (u64 title_id : titles)
  {
    LogServerHash8(fmt::format("[UPLOAD_BUILD] title={:016x}\n", title_id));
    out_packet << u64{title_id};
    auto storage = WiiSave::MakeNandStorage(temp_fs, title_id);
    if (storage && storage->SaveExists())
    {
      LogServerHash8("[UPLOAD_BUILD] exists=1\n");
      const auto header = storage->ReadHeader();
      const auto bk_header = storage->ReadBkHeader();
      const auto files = storage->ReadFiles();
      if (!header || !bk_header || !files)
        return false;

      out_packet << true;

      // Header
      out_packet << u64{header->tid};
      out_packet << header->banner_size << header->permissions << header->unk1;
      for (u8 b : header->md5)
        out_packet << b;
      out_packet << header->unk2;
      for (size_t i = 0; i < header->banner_size; ++i)
        out_packet << header->banner[i];

      // BkHeader
      out_packet << bk_header->size << bk_header->magic << bk_header->ngid
                 << bk_header->number_of_files << bk_header->size_of_files << bk_header->unk1
                 << bk_header->unk2 << bk_header->total_size;
      for (u8 b : bk_header->unk3)
        out_packet << b;
      out_packet << u64{bk_header->tid};
      for (u8 b : bk_header->mac_address)
        out_packet << b;

      // Files
      for (const WiiSave::Storage::SaveFile& file : *files)
      {
        out_packet << file.mode << file.attributes << file.type << file.path;
        if (file.type == WiiSave::Storage::SaveFile::Type::File)
        {
          const auto& data = *file.data;
          if (!data || !NetPlay::CompressBufferIntoPacket(*data, out_packet))
            return false;
        }
      }
    }
    else
    {
      LogServerHash8("[UPLOAD_BUILD] exists=0\n");
      out_packet << false;
    }
  }

  // 重定向目录（可选）
  if (!redirect_path.empty())
  {
    LogServerHash8(fmt::format("[UPLOAD_BUILD] redirect_path='{}'\n", redirect_path));
    out_packet << true;
    if (!NetPlay::CompressFolderIntoPacket(redirect_path, out_packet))
      return false;
  }
  else
  {
    out_packet << false;
  }

  return true;
}

bool ServerDecodeWiiSaveUploadPacketToFS(sf::Packet& packet, FS::FileSystem* target_fs,
                                         const std::string& redirect_target_path,
                                         std::vector<u64>* out_titles)
{
  LogServerHash8(fmt::format("[UPLOAD_DECODE] start redirect_target='{}'\n", redirect_target_path));
  constexpr FS::Modes fs_modes{FS::Mode::ReadWrite, FS::Mode::ReadWrite, FS::Mode::ReadWrite};

  // Mii 数据
  bool has_mii = false;
  packet >> has_mii;
  if (has_mii)
  {
    LogServerHash8("[UPLOAD_DECODE] mii=1\n");
    auto buf = NetPlay::DecompressPacketIntoBuffer(packet);
    if (!buf)
      return false;
    target_fs->CreateFullPath(IOS::PID_KERNEL, IOS::PID_KERNEL, "/shared2/menu/FaceLib/", 0,
                              fs_modes);
    auto fh = target_fs->CreateAndOpenFile(IOS::PID_KERNEL, IOS::PID_KERNEL,
                                           Common::GetMiiDatabasePath(), fs_modes);
    if (!fh || !fh->Write(buf->data(), buf->size()))
      return false;
  }
  else
  {
    LogServerHash8("[UPLOAD_DECODE] mii=0\n");
  }

  // Wii 存档
  u32 save_count = 0;
  packet >> save_count;
  LogServerHash8(fmt::format("[UPLOAD_DECODE] save_count={}\n", save_count));
  for (u32 i = 0; i < save_count; ++i)
  {
    const u64 title_id = Common::PacketReadU64(packet);
    LogServerHash8(fmt::format("[UPLOAD_DECODE] title={:016x}\n", title_id));
    if (out_titles)
      out_titles->push_back(title_id);

    target_fs->CreateFullPath(IOS::PID_KERNEL, IOS::PID_KERNEL,
                              Common::GetTitleDataPathForGame(title_id) + '/', 0, fs_modes);
    auto save = WiiSave::MakeNandStorage(target_fs, title_id);

    bool exists = false;
    packet >> exists;
    LogServerHash8(fmt::format("[UPLOAD_DECODE] exists={}\n", exists));
    if (!exists)
      continue;

    WiiSave::Header header;
    packet >> header.tid;
    packet >> header.banner_size;
    packet >> header.permissions;
    packet >> header.unk1;
    for (u8& b : header.md5)
      packet >> b;
    packet >> header.unk2;
    for (size_t k = 0; k < header.banner_size; ++k)
      packet >> header.banner[k];

    WiiSave::BkHeader bk_header;
    packet >> bk_header.size;
    packet >> bk_header.magic;
    packet >> bk_header.ngid;
    packet >> bk_header.number_of_files;
    packet >> bk_header.size_of_files;
    packet >> bk_header.unk1;
    packet >> bk_header.unk2;
    packet >> bk_header.total_size;
    for (u8& b : bk_header.unk3)
      packet >> b;
    packet >> bk_header.tid;
    for (u8& b : bk_header.mac_address)
      packet >> b;

    std::vector<WiiSave::Storage::SaveFile> files;
    files.reserve(bk_header.number_of_files);
    for (u32 n = 0; n < bk_header.number_of_files; ++n)
    {
      WiiSave::Storage::SaveFile file;
      packet >> file.mode >> file.attributes;
      packet >> file.type;
      packet >> file.path;

      if (file.type == WiiSave::Storage::SaveFile::Type::File)
      {
        auto buf = NetPlay::DecompressPacketIntoBuffer(packet);
        if (!buf)
          return false;
        file.data = std::move(*buf);
      }
      files.push_back(std::move(file));
    }

    if (!save->WriteHeader(header) || !save->WriteBkHeader(bk_header) || !save->WriteFiles(files))
      return false;
    LogServerHash8("[UPLOAD_DECODE] write_ok\n");
  }

  bool has_redirect = false;
  packet >> has_redirect;
  if (has_redirect)
  {
    LogServerHash8("[UPLOAD_DECODE] redirect_present=1\n");
    if (redirect_target_path.empty())
      return false;
    if (!NetPlay::DecompressPacketIntoFolder(packet, redirect_target_path))
      return false;
    LogServerHash8("[UPLOAD_DECODE] redirect_write_ok\n");
  }
  
  LogServerHash8("[UPLOAD_DECODE] done\n");
  return true;
}

static bool CopyNandFile(FS::FileSystem* src_fs, const std::string& src_path,
                         FS::FileSystem* dst_fs, const std::string& dst_path)
{
  auto buf = ReadFileToBuffer(src_fs, src_path);
  if (!buf)
    return false;

  constexpr FS::Modes fs_modes{FS::Mode::ReadWrite, FS::Mode::ReadWrite, FS::Mode::ReadWrite};
  const auto split = FS::SplitPathAndBasename(dst_path);
  dst_fs->CreateFullPath(IOS::PID_KERNEL, IOS::PID_KERNEL, split.parent + '/', 0, fs_modes);
  auto fh = dst_fs->CreateAndOpenFile(IOS::PID_KERNEL, IOS::PID_KERNEL, dst_path, fs_modes);
  if (!fh || !fh->Write(buf->data(), buf->size()))
    return false;
  return true;
}

bool ServerApplyUploadedWiiSaves(FS::FileSystem* source_fs, FS::FileSystem* configured_fs,
                                 const std::vector<u64>& titles, bool backup_existing)
{
  LogServerHash8(fmt::format("[UPLOAD_APPLY] start titles={} backup={}\n", fmt::join(titles, ","), backup_existing));
  // 复制 Mii 数据（若存在）
  CopyNandFile(source_fs, Common::GetMiiDatabasePath(), configured_fs,
               Common::GetMiiDatabasePath());

  IOS::HLE::Kernel ios_local;
  constexpr FS::Modes fs_modes{FS::Mode::ReadWrite, FS::Mode::ReadWrite, FS::Mode::ReadWrite};

  for (u64 title_id : titles)
  {
    const std::string title_path = Common::GetTitleDataPathForGame(title_id);
    configured_fs->CreateFullPath(IOS::PID_KERNEL, IOS::PID_KERNEL, title_path + '/', 0,
                                  fs_modes);

    auto session_save = WiiSave::MakeNandStorage(source_fs, title_id);
    auto user_save = WiiSave::MakeNandStorage(configured_fs, title_id);

    if (backup_existing)
    {
      const std::string backup_path =
          fmt::format("{}/{:016x}.bin", File::GetUserPath(D_BACKUP_IDX), title_id);
      auto backup_save = WiiSave::MakeDataBinStorage(&ios_local.GetIOSC(), backup_path, "w+b");
      WiiSave::Copy(user_save.get(), backup_save.get());
      LogServerHash8(fmt::format("[UPLOAD_APPLY] backup title={:016x} path='{}'\n", title_id, backup_path));
    }

    if (WiiSave::Copy(session_save.get(), user_save.get()) != WiiSave::CopyResult::Success)
    {
      LogServerHash8(fmt::format("[UPLOAD_APPLY] copy_fail title={:016x}\n", title_id));
      return false;
    }
    LogServerHash8(fmt::format("[UPLOAD_APPLY] copy_ok title={:016x}\n", title_id));
  }

  LogServerHash8("[UPLOAD_APPLY] done\n");
  return true;
}

}  // namespace NetPlayUpload
