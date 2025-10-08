// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/NandPaths.h"

#include <algorithm>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "Common/CommonTypes.h"
#include "Common/Contains.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "Common/IOFile.h"  // for File::IOFile
#include "Core/ConfigManager.h"
#include <filesystem>

namespace Common
{
std::string RootUserPath(FromWhichRoot from)
{
  int idx{};
  switch (from)
  {
  case FromWhichRoot::Configured:
    idx = D_WIIROOT_IDX;
    break;
  case FromWhichRoot::Session:
    idx = D_SESSION_WIIROOT_IDX;
    break;
  case FromWhichRoot::Banners:
    idx = D_BANNERS_WIIROOT_IDX;
    break;
  }
  std::string dir = File::GetUserPath(idx);
  dir.pop_back();  // remove trailing path separator
  return dir;
}

static std::string RootUserPath(std::optional<FromWhichRoot> from)
{
  return from ? RootUserPath(*from) : "";
}

std::string GetImportTitlePath(u64 title_id, std::optional<FromWhichRoot> from)
{
  return RootUserPath(from) + fmt::format("/import/{:08x}/{:08x}", static_cast<u32>(title_id >> 32),
                                          static_cast<u32>(title_id));
}

std::string GetTicketFileName(u64 title_id, std::optional<FromWhichRoot> from)
{
  return fmt::format("{}/ticket/{:08x}/{:08x}.tik", RootUserPath(from),
                     static_cast<u32>(title_id >> 32), static_cast<u32>(title_id));
}

std::string GetV1TicketFileName(u64 title_id, std::optional<FromWhichRoot> from)
{
  return fmt::format("{}/ticket/{:08x}/{:08x}.tv1", RootUserPath(from),
                     static_cast<u32>(title_id >> 32), static_cast<u32>(title_id));
}

std::string GetTitlePath(u64 title_id, std::optional<FromWhichRoot> from)
{
  return fmt::format("{}/title/{:08x}/{:08x}", RootUserPath(from), static_cast<u32>(title_id >> 32),
                     static_cast<u32>(title_id));
}

std::string GetTitleDataPath(u64 title_id, std::optional<FromWhichRoot> from)
{
  return GetTitlePath(title_id, from) + "/data";
}

std::string GetTitleDataPathForGame(u64 title_id, std::optional<FromWhichRoot> from)
{
  const std::string base = GetTitlePath(title_id, from);
  const std::string& hash8 = SConfig::GetInstance().GetSaveHash8();

  
  // Path that will ultimately be returned by this function.
  std::string resolved_path;

  if (!hash8.empty())
  {
    // Construct path including the hash folder
    resolved_path = fmt::format("{}/{}/data", base, hash8);

    // Collapse accidental duplicate hash segment, e.g. /hash8/hash8/data → /hash8/data
    const std::string duplicate = "/" + hash8 + "/" + hash8 + "/";
    const std::string single = "/" + hash8 + "/";
    const size_t dup_pos = resolved_path.find(duplicate);
    if (dup_pos != std::string::npos)
      resolved_path.replace(dup_pos, duplicate.length(), single);
  }
  else
  {
    resolved_path = base + "/data";
  }

  // ------------------------------------------------------------
  // Debug logging block – writes out detailed information about
  // every invocation of this function into User/Logs/savehash8.txt.
  // Each line is prefixed with "[NPDEBUG]" so that the caller can
  // easily filter out logs from previous runs.
  // ------------------------------------------------------------
  const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
  File::CreateFullPath(log_path);
  File::IOFile log_file(log_path, "ab");
  if (log_file)
  {
    static bool s_header_written = false;
    if (!s_header_written)
    {
      const std::string header = fmt::format("[NPDEBUG] ===== New NetPlay debug session =====\n");
      log_file.WriteBytes(header.data(), header.size());
      s_header_written = true;
    }
    const std::string line = fmt::format(
        "[NPDEBUG] GetTitleDataPathForGame: title_id={:016x}, hash8='{}', resolved_path='{}'\n",
        title_id, hash8, resolved_path);
    log_file.WriteBytes(line.data(), line.size());
  }
  // ------------------------------------------------------------

  return resolved_path;
}

std::string GetTitleContentPath(u64 title_id, std::optional<FromWhichRoot> from)
{
  return GetTitlePath(title_id, from) + "/content";
}

std::string GetTMDFileName(u64 title_id, std::optional<FromWhichRoot> from)
{
  return GetTitleContentPath(title_id, from) + "/title.tmd";
}

std::string GetMiiDatabasePath(std::optional<FromWhichRoot> from)
{
  return fmt::format("{}/shared2/menu/FaceLib/RFL_DB.dat", RootUserPath(from));
}

bool IsTitlePath(const std::string& path, std::optional<FromWhichRoot> from, u64* title_id)
{
  std::string expected_prefix = RootUserPath(from) + "/title/";
  if (!path.starts_with(expected_prefix))
  {
    return false;
  }

  // Try to find a title ID in the remaining path.
  std::string subdirectory = path.substr(expected_prefix.size());
  std::vector<std::string> components = SplitString(subdirectory, '/');
  if (components.size() < 2)
  {
    return false;
  }

  u32 title_id_high, title_id_low;
  if (Common::FromChars(components[0], title_id_high, 16).ec != std::errc{} ||
      Common::FromChars(components[1], title_id_low, 16).ec != std::errc{})
  {
    return false;
  }

  if (title_id != nullptr)
  {
    *title_id = (static_cast<u64>(title_id_high) << 32) | title_id_low;
  }
  return true;
}

static bool IsIllegalCharacter(char c)
{
  static constexpr char illegal_chars[] = {'\"', '*', '/', ':', '<', '>', '?', '\\', '|', '\x7f'};
  return static_cast<unsigned char>(c) <= 0x1F || Common::Contains(illegal_chars, c);
}

std::string EscapeFileName(const std::string& filename)
{
  // Prevent paths from containing special names like ., .., ..., ...., and so on
  if (std::ranges::all_of(filename, [](char c) { return c == '.'; }))
    return ReplaceAll(filename, ".", "__2e__");

  // Escape all double underscores since we will use double underscores for our escape sequences
  std::string filename_with_escaped_double_underscores = ReplaceAll(filename, "__", "__5f____5f__");

  // Escape all other characters that need to be escaped
  std::string result;
  result.reserve(filename_with_escaped_double_underscores.size());
  for (char c : filename_with_escaped_double_underscores)
  {
    if (IsIllegalCharacter(c))
      result.append(fmt::format("__{:02x}__", c));
    else
      result.push_back(c);
  }

  return result;
}

std::string EscapePath(const std::string& path)
{
  const std::vector<std::string> split_strings = SplitString(path, '/');

  std::vector<std::string> escaped_split_strings;
  escaped_split_strings.reserve(split_strings.size());
  for (const std::string& split_string : split_strings)
    escaped_split_strings.push_back(EscapeFileName(split_string));

  return fmt::to_string(fmt::join(escaped_split_strings, "/"));
}

std::string UnescapeFileName(const std::string& filename)
{
  std::string result = filename;
  size_t pos = 0;

  // Replace escape sequences of the format "__3f__" with the ASCII
  // character defined by the escape sequence's two hex digits.
  while ((pos = result.find("__", pos)) != std::string::npos)
  {
    u32 character;
    if (pos + 6 <= result.size() && result[pos + 4] == '_' && result[pos + 5] == '_')
      if (Common::FromChars(std::string_view{result}.substr(pos + 2, 2), character, 16).ec ==
          std::errc{})
      {
        result.replace(pos, 6, {static_cast<char>(character)});
      }

    ++pos;
  }

  return result;
}

bool IsFileNameSafe(const std::string_view filename)
{
  return !filename.empty() && !std::ranges::all_of(filename, [](char c) { return c == '.'; }) &&
         std::ranges::none_of(filename, IsIllegalCharacter);
}
}  // namespace Common

namespace Common
{
std::vector<std::string> GetAllHash8ForTitle(u64 title_id, std::optional<FromWhichRoot> from)
{
  std::vector<std::string> hashes;
  namespace fs = std::filesystem;
  const std::string base_title_path = GetTitlePath(title_id, from);
  std::error_code ec;
  for (const auto& dir_entry : fs::directory_iterator(base_title_path, ec))
  {
    if (ec)
      break;
    if (!dir_entry.is_directory())
      continue;
    const std::string name = dir_entry.path().filename().string();
    if (name.size() == 8 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
          return std::isxdigit(c);
        }))
      hashes.push_back(name);
  }

  // Debug logging for hash8 enumeration
  const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
  File::CreateFullPath(log_path);
  File::IOFile log_file(log_path, "ab");
  if (log_file)
  {
    const std::string line = fmt::format(
        "[NPDEBUG] GetAllHash8ForTitle: title_id={:016x}, hash_count={}, list=[{}]\n", title_id,
        hashes.size(), fmt::join(hashes, ","));
    log_file.WriteBytes(line.data(), line.size());
  }
  return hashes;
}
} // namespace Common
