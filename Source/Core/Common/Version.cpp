// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/Version.h"

#include <string>

#include "Common/scmrev.h"

namespace Common
{
#define EMULATOR_NAME ""

#ifdef _DEBUG
#define BUILD_TYPE_STR "Debug "
#elif defined DEBUGFAST
#define BUILD_TYPE_STR "DebugFast "
#else
#define BUILD_TYPE_STR ""
#endif

#define DBZ3_VERSION " TO LOVERS OF DBZ3"

const std::string& GetScmRevStr()
{
  static const std::string scm_rev_str = EMULATOR_NAME " "
  // Note this macro can be empty if the master branch does not exist.
#if 1 - SCM_COMMITS_AHEAD_MASTER - 1 != 0
                                                       " "
#endif

#ifdef __INTEL_COMPILER
      BUILD_TYPE_STR SCM_DESC_STR "-ICC";
#else
      BUILD_TYPE_STR DBZ3_VERSION;
#endif
  return scm_rev_str;
}

const std::string& GetScmRevGitStr()
{
  static const std::string scm_rev_git_str = "modified-by-yihe";
  return scm_rev_git_str;
}

const std::string& GetScmDescStr()
{
  static const std::string scm_desc_str = "modified-by-yihe";
  return scm_desc_str;
}

const std::string& GetScmBranchStr()
{
  static const std::string scm_branch_str = SCM_BRANCH_STR;
  return scm_branch_str;
}

const std::string& GetUserAgentStr()
{
  static const std::string user_agent_str = "version/yihe";
  return user_agent_str;
}

const std::string& GetScmDistributorStr()
{
  static const std::string scm_distributor_str = SCM_DISTRIBUTOR_STR;
  return scm_distributor_str;
}

const std::string& GetScmUpdateTrackStr()
{
  static const std::string scm_update_track_str = SCM_UPDATE_TRACK_STR;
  return scm_update_track_str;
}

const std::string& GetNetplayDolphinVer()
{
  //"DBZ3-MODIFIED-BY-YIHE"
 //#ifdef _WIN32
 //  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Win";
 //#elif __APPLE__
 //  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Mac";
 //#else
 //  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Lin";
 //#endif
  static const std::string netplay_dolphin_ver = "modified-by-yihe";
  return netplay_dolphin_ver;
}

int GetScmCommitsAheadMaster()
{
  // Note this macro can be empty if the master branch does not exist.
  return 666;
}

}  // namespace Common
