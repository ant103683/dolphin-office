// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include <SFML/Network/Packet.hpp>

#include "Core/IOS/FS/FileSystem.h"

namespace NetPlayUpload
{
// 客户端侧：从临时会话文件系统读取 Wii 存档，构建与服务器下发一致的数据包格式
// 参数：
//  - temp_fs: 临时会话 FS 根，用于读取 Mii 数据与各标题存档
//  - titles: 需要打包的 Wii 标题 ID 列表
//  - redirect_path: 可选的重定向存档目录（Riivolution），为空则不包含
//  - out_packet: 结果数据包（未发送，仅构建）
// 返回：构建成功与否
bool BuildClientWiiSaveUploadPacket(IOS::HLE::FS::FileSystem* temp_fs,
                                    const std::vector<u64>& titles,
                                    const std::string& redirect_path,
                                    sf::Packet& out_packet);

// 服务器侧：接收客户端上传的数据包并写入指定目标 FS（可为暂存或直接配置 FS）
// 参数：
//  - packet: 客户端上传的 Wii 存档数据包
//  - target_fs: 写入目标文件系统（由调用方决定是暂存还是配置 FS）
//  - redirect_target_path: 若包含重定向存档，解压到该主机目录
//  - out_titles: 可选返回解包到的标题 ID 列表（用于后续覆盖）
// 返回：写入成功与否
bool ServerDecodeWiiSaveUploadPacketToFS(sf::Packet& packet,
                                         IOS::HLE::FS::FileSystem* target_fs,
                                         const std::string& redirect_target_path,
                                         std::vector<u64>* out_titles);

// 服务器侧：将来源 FS 中的 Wii 存档覆盖写入到配置 FS，并按需备份旧存档
// 参数：
//  - source_fs: 来源文件系统（可为暂存 FS 或会话 FS）
//  - configured_fs: 服务器配置 NAND 文件系统
//  - titles: 需要覆盖写入的标题 ID 列表
//  - backup_existing: 是否备份旧存档到用户备份目录
// 返回：覆盖成功与否（任一标题失败则返回 false）
bool ServerApplyUploadedWiiSaves(IOS::HLE::FS::FileSystem* source_fs,
                                 IOS::HLE::FS::FileSystem* configured_fs,
                                 const std::vector<u64>& titles,
                                 bool backup_existing);
}