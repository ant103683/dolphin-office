1 日志参考:
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
2 在我没有要求你执行终端命令的前提下,禁止私自尝试让我执行任何终端命令,比如编译运行
3 每次开始对话前,回顾一下我们最初的需求,最好是用中文总结一下