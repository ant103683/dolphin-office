// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/NetPlayServer.h"
#include "UICommon/GameListExporter.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "Common/CommonPaths.h"
#include "Common/ENet.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/JsonUtil.h"
#include "Common/HttpRequest.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/SFMLHelper.h"
#include "Common/StringUtil.h"
#include "Common/Crypto/SHA1.h"
#include "Common/UPnP.h"
#include "Common/Version.h"

#include "Core/AchievementManager.h"
#include "Core/ActionReplay.h"
#include "Core/Boot/Boot.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/NetplaySettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/Config/SessionSettings.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/GeckoCode.h"
#include "Core/GeckoCodeConfig.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/IOS/FS/HostBackend/FS.h"
#include "Core/NetPlayUpload.h"
#include "NetplayManager.h"
#ifdef HAS_LIBMGBA
#include "Core/HW/GBACore.h"
#endif
#include "Core/HW/GCMemcard/GCMemcard.h"
#include "Core/HW/GCMemcard/GCMemcardDirectory.h"
#include "Core/HW/GCMemcard/GCMemcardRaw.h"
#include "Core/HW/Sram.h"
#include "Core/HW/WiiSave.h"
#include "Core/HW/WiiSaveStructs.h"
#include "Core/HW/WiimoteEmu/DesiredWiimoteState.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/IOS/ES/ES.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/IOS/IOS.h"
#include "Core/IOS/Uids.h"
#include "Core/NetPlayClient.h"  //for NetPlayUI
#include "Core/NetPlayCommon.h"
#include "Core/NetplayManager.h"
#include "Core/SyncIdentifier.h"

#include "DiscIO/Enums.h"
#include "DiscIO/RiivolutionPatcher.h"

#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/GCPadStatus.h"
#include "InputCommon/InputConfig.h"

#include "UICommon/GameFile.h"

#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/types.h>
#ifdef __HAIKU__
#define _BSD_SOURCE
#include <bsd/ifaddrs.h>
#elif !defined ANDROID
#include <ifaddrs.h>
#endif
#include <arpa/inet.h>
#else
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace NetPlay
{
NetPlayServer::~NetPlayServer()
{
  if (is_connected)
  {
    m_do_loop = false;
    m_chunked_data_event.Set();
    m_chunked_data_complete_event.Set();
    if (m_chunked_data_thread.joinable())
      m_chunked_data_thread.join();
    m_thread.join();
    enet_host_destroy(m_server);

    if (Common::g_MainNetHost.get() == m_server)
    {
      Common::g_MainNetHost.release();
    }

    if (m_traversal_client)
    {
      Common::g_TraversalClient->m_Client = nullptr;
      Common::ReleaseTraversalClient();
    }
  }

#ifdef USE_UPNP
  Common::UPnP::StopPortmapping();
#endif
}

// called from ---GUI--- thread
NetPlayServer::NetPlayServer(const u16 port, const bool forward_port, NetPlayUI* dialog,
                             const NetTraversalConfig& traversal_config)
    : m_dialog(dialog)
{
  NetPlay::NetplayManager::GetInstance();
  //--use server time
  if (enet_initialize() != 0)
  {
    PanicAlertFmtT("Enet Didn't Initialize");
  }

  m_pad_map.fill(0);
  m_gba_config.fill({});
  m_wiimote_map.fill(0);

  if (traversal_config.use_traversal)
  {
    if (!Common::EnsureTraversalClient(traversal_config.traversal_host,
                                       traversal_config.traversal_port,
                                       traversal_config.traversal_port_alt, port))
    {
      return;
    }

    Common::g_TraversalClient->m_Client = this;
    m_traversal_client = Common::g_TraversalClient.get();

    m_server = Common::g_MainNetHost.get();

    if (Common::g_TraversalClient->HasFailed())
      Common::g_TraversalClient->ReconnectToServer();
  }
  else
  {
    ENetAddress serverAddr;
    serverAddr.host = ENET_HOST_ANY;
    serverAddr.port = port;
    m_server = enet_host_create(&serverAddr, 10, CHANNEL_COUNT, 0, 0);
    if (m_server != nullptr)
    {
      m_server->mtu = std::min(m_server->mtu, NetPlay::MAX_ENET_MTU);
      m_server->intercept = Common::ENet::InterceptCallback;
    }

    SetupIndex();
  }
  if (m_server != nullptr)
  {
    is_connected = true;
    m_do_loop = true;
    m_thread = std::thread(&NetPlayServer::ThreadFunc, this);
    m_target_buffer_size = 5;
    m_chunked_data_thread = std::thread(&NetPlayServer::ChunkedDataThreadFunc, this);

#ifdef USE_UPNP
    if (forward_port && !traversal_config.use_traversal)
      Common::UPnP::TryPortmapping(port);
#endif
  }
}

static PlayerId* PeerPlayerId(ENetPeer* peer)
{
  return static_cast<PlayerId*>(peer->data);
}

static void ClearPeerPlayerId(ENetPeer* peer)
{
  if (peer->data)
  {
    delete PeerPlayerId(peer);
    peer->data = nullptr;
  }
}

void NetPlayServer::SetupIndex()
{
  if (!Config::Get(Config::NETPLAY_USE_INDEX) || Config::Get(Config::NETPLAY_INDEX_NAME).empty() ||
      Config::Get(Config::NETPLAY_INDEX_REGION).empty())
  {
    return;
  }

  NetPlaySession session;

  session.name = Config::Get(Config::NETPLAY_INDEX_NAME);
  session.region = Config::Get(Config::NETPLAY_INDEX_REGION);
  session.has_password = !Config::Get(Config::NETPLAY_INDEX_PASSWORD).empty();
  session.method = m_traversal_client ? "traversal" : "direct";
  session.game_id = m_selected_game_name.empty() ? "UNKNOWN" : m_selected_game_name;
  session.player_count = static_cast<int>(m_players.size());
  session.in_game = m_is_running;
  session.port = GetPort();

  if (m_traversal_client)
  {
    if (!m_traversal_client->IsConnected())
      return;

    session.server_id = std::string(Common::g_TraversalClient->GetHostID().data(), 8);
  }
  else
  {
    Common::HttpRequest request;
    // ENet does not support IPv6, so IPv4 has to be used
    request.UseIPv4();
    Common::HttpRequest::Response response =
        request.Get("https://ip.dolphin-emu.org/", {{"X-Is-Dolphin", "1"}});

    if (!response.has_value())
      return;

    session.server_id = std::string(response->begin(), response->end());
  }

  session.EncryptID(Config::Get(Config::NETPLAY_INDEX_PASSWORD));

  bool success = m_index.Add(session);
  if (m_dialog != nullptr)
    m_dialog->OnIndexAdded(success, success ? "" : m_index.GetLastError());

  m_index.SetErrorCallback([this] {
    if (m_dialog != nullptr)
      m_dialog->OnIndexRefreshFailed(m_index.GetLastError());
  });
}

// called from ---NETPLAY--- thread
void NetPlayServer::ThreadFunc()
{
  INFO_LOG_FMT(NETPLAY, "NetPlayServer starting.");

  while (m_do_loop)
  {
    // update pings every so many seconds
    if ((m_ping_timer.ElapsedMs() > 1000) || m_update_pings)
    {
      // only used as an identifier, not time value, so truncation is fine
      m_ping_key = static_cast<u32>(Common::Timer::NowMs());

      sf::Packet spac;
      spac << MessageID::Ping;
      spac << m_ping_key;

      m_ping_timer.Start();
      SendToClients(spac);

      m_index.SetPlayerCount(static_cast<int>(m_players.size()));
      m_index.SetGame(m_selected_game_name);
      m_index.SetInGame(m_is_running);

      m_update_pings = false;
    }

    ENetEvent netEvent;
    int net;
    if (m_traversal_client)
      m_traversal_client->HandleResends();
    net = enet_host_service(m_server, &netEvent, 1000);
    while (!m_async_queue.Empty())
    {
      INFO_LOG_FMT(NETPLAY, "Processing async queue event.");
      {
        std::lock_guard lkp(m_crit.players);
        INFO_LOG_FMT(NETPLAY, "Locked player mutex.");
        auto& e = m_async_queue.Front();
        if (e.target_mode == TargetMode::Only)
        {
          if (const auto it = m_players.find(e.target_pid); it != m_players.end())
            Send(it->second.socket, e.packet, e.channel_id);
        }
        else
        {
          SendToClients(e.packet, e.target_pid, e.channel_id);
        }
      }
      INFO_LOG_FMT(NETPLAY, "Processing async queue event done.");
      m_async_queue.Pop();
    }
    if (net > 0)
    {
      switch (netEvent.type)
      {
      case ENET_EVENT_TYPE_CONNECT:
      {
        // Actual client initialization is deferred to the receive event, so here
        // we'll just log the new connection.
        INFO_LOG_FMT(NETPLAY, "Peer connected from: {:x}:{}", netEvent.peer->address.host,
                     netEvent.peer->address.port);
      }
      break;
      case ENET_EVENT_TYPE_RECEIVE:
      {
        INFO_LOG_FMT(NETPLAY, "enet_host_service: receive event");

        sf::Packet rpac;
        rpac.append(netEvent.packet->data, netEvent.packet->dataLength);

        if (!netEvent.peer->data)
        {
          // uninitialized client, we'll assume this is their initialization packet
          ConnectionError error;
          {
            INFO_LOG_FMT(NETPLAY, "Initializing peer {:x}:{}", netEvent.peer->address.host,
                         netEvent.peer->address.port);
            std::lock_guard lkg(m_crit.game);
            error = OnConnect(netEvent.peer, rpac);
          }

          if (error != ConnectionError::NoError)
          {
            INFO_LOG_FMT(NETPLAY, "Error {} initializing peer {:x}:{}", u8(error),
                         netEvent.peer->address.host, netEvent.peer->address.port);

            sf::Packet spac;
            spac << error;
            // don't need to lock, this client isn't in the client map
            Send(netEvent.peer, spac);

            ClearPeerPlayerId(netEvent.peer);
            enet_peer_disconnect_later(netEvent.peer, 0);
          }
        }
        else
        {
          auto it = m_players.find(*PeerPlayerId(netEvent.peer));
          Client& client = it->second;
          if (OnData(rpac, client) != 0)
          {
            INFO_LOG_FMT(NETPLAY, "Invalid packet from client {}, disconnecting.", client.pid);

            // if a bad packet is received, disconnect the client
            std::lock_guard lkg(m_crit.game);
            OnDisconnect(client);

            ClearPeerPlayerId(netEvent.peer);
          }
          else
          {
            INFO_LOG_FMT(NETPLAY, "successfully handled packet from client {}", client.pid);
          }
        }
        enet_packet_destroy(netEvent.packet);
      }
      break;
      case ENET_EVENT_TYPE_DISCONNECT:
      {
        INFO_LOG_FMT(NETPLAY, "enet_host_service: disconnect event");

        std::lock_guard lkg(m_crit.game);
        if (!netEvent.peer->data)
        {
          ERROR_LOG_FMT(NETPLAY, "enet_host_service: no peer data");
          break;
        }
        const auto player_id = *PeerPlayerId(netEvent.peer);
        auto it = m_players.find(player_id);
        if (it != m_players.end())
        {
          Client& client = it->second;
          INFO_LOG_FMT(NETPLAY, "Disconnecting client {}.", client.pid);
          OnDisconnect(client);

          ClearPeerPlayerId(netEvent.peer);
        }
        else
        {
          ERROR_LOG_FMT(NETPLAY, "Invalid player {} to disconnect.", player_id);
        }
      }
      break;
      default:
        // not a valid switch case due to not technically being part of the enum
        if (static_cast<int>(netEvent.type) == Common::ENet::SKIPPABLE_EVENT)
          INFO_LOG_FMT(NETPLAY, "enet_host_service: skippable packet event");
        else
          ERROR_LOG_FMT(NETPLAY, "enet_host_service: unknown event type: {}", int(netEvent.type));
        break;
      }
    }
    else if (net == 0)
    {
      INFO_LOG_FMT(NETPLAY, "enet_host_service: no event occurred");
    }
    else
    {
      ERROR_LOG_FMT(NETPLAY, "enet_host_service error: {}", net);
    }
  }

  INFO_LOG_FMT(NETPLAY, "NetPlayServer shutting down.");

  // close listening socket and client sockets
  for (const auto& player_entry : std::views::values(m_players))
  {
    ClearPeerPlayerId(player_entry.socket);
    enet_peer_disconnect(player_entry.socket, 0);
  }
  m_players.clear();
}

static void SendSyncIdentifier(sf::Packet& spac, const SyncIdentifier& sync_identifier)
{
  // We cast here due to a potential long vs long long mismatch
  spac << static_cast<u64>(sync_identifier.dol_elf_size);

  spac << sync_identifier.game_id;
  spac << sync_identifier.revision;
  spac << sync_identifier.disc_number;
  spac << sync_identifier.is_datel;

  for (const u8& x : sync_identifier.sync_hash)
    spac << x;
}

// called from ---NETPLAY--- thread
ConnectionError NetPlayServer::OnConnect(ENetPeer* incoming_connection, sf::Packet& received_packet)
{
  std::string netplay_version;
  received_packet >> netplay_version;
  if (netplay_version != Common::GetScmRevGitStr())
    return ConnectionError::VersionMismatch;

  if (m_is_running || m_start_pending)
    return ConnectionError::GameRunning;

  if (m_players.size() >= 255)
    return ConnectionError::ServerFull;

  Client new_player{};
  new_player.pid = GiveFirstAvailableIDTo(incoming_connection);
  new_player.socket = incoming_connection;

  received_packet >> new_player.revision;
  received_packet >> new_player.name;

  if (StringUTF8CodePointCount(new_player.name) > MAX_NAME_LENGTH)
    return ConnectionError::NameTooLong;

  // Update time in milliseconds of no acknowledgment of
  // sent packets before a connection is deemed disconnected
  enet_peer_timeout(incoming_connection, 0, PEER_TIMEOUT.count(), PEER_TIMEOUT.count());

  // force a ping on first netplay loop
  m_update_pings = true;

  AssignNewUserAPad(new_player);

  // tell other players a new player joined
  SendResponseToAllPlayers(MessageID::PlayerJoin, new_player.pid, new_player.name,
                           new_player.revision);

  // tell new client they connected and their ID
  SendResponseToPlayer(new_player, MessageID::ConnectionSuccessful, new_player.pid);

  // tell new client the selected game
  if (!m_selected_game_name.empty())
  {
    sf::Packet send_packet;
    send_packet << MessageID::ChangeGame;
    SendSyncIdentifier(send_packet, m_selected_game_identifier);
    send_packet << m_selected_game_name;
    Send(new_player.socket, send_packet);
  }

  if (!m_host_input_authority)
    SendResponseToPlayer(new_player, MessageID::PadBuffer, m_target_buffer_size);

  SendResponseToPlayer(new_player, MessageID::HostInputAuthority, m_host_input_authority);

  for (const auto& existing_player : std::views::values(m_players))
  {
    SendResponseToPlayer(new_player, MessageID::PlayerJoin, existing_player.pid,
                         existing_player.name, existing_player.revision);

    SendResponseToPlayer(new_player, MessageID::GameStatus, existing_player.pid,
                         static_cast<u8>(existing_player.game_status));
  }

  if (Config::Get(Config::NETPLAY_ENABLE_QOS))
    new_player.qos_session = Common::QoSSession(new_player.socket);

  {
    std::lock_guard lkp(m_crit.players);
    // add new player to list of players
  m_players.emplace(*PeerPlayerId(new_player.socket), std::move(new_player));
  NetPlay::NetplayManager::GetInstance().activeClientWithPid(new_player.pid);
  // sync pad mappings with everyone
    UpdatePadMapping();
    UpdateGBAConfig();
    UpdateWiimoteMapping();
  }

  return ConnectionError::NoError;
}

// called from ---NETPLAY--- thread
unsigned int NetPlayServer::OnDisconnect(const Client& player)
{
  const PlayerId pid = player.pid;

  if (m_is_running)
  {
    for (PlayerId& mapping : m_pad_map)
    {
      if (mapping == pid && pid != 1)
      {
        std::lock_guard lkg(m_crit.game);
        m_is_running = false;

        sf::Packet spac;
        spac << MessageID::DisableGame;
        // this thread doesn't need players lock
        SendToClients(spac);
        break;
      }
    }
  }

  if (m_start_pending)
  {
    ChunkedDataAbort();
    m_dialog->OnGameStartAborted();
    m_start_pending = false;
  }

  sf::Packet spac;
  spac << MessageID::PlayerLeave;
  spac << pid;

  enet_peer_disconnect(player.socket, 0);

  std::lock_guard lkp(m_crit.players);
  auto it = m_players.find(player.pid);
  if (it != m_players.end())
    m_players.erase(it);

  NetPlay::NetplayManager::GetInstance().deactiveClientWithPid(pid);
  if (m_players.size() <= 1)
  {
    NetPlay::NetplayManager::GetInstance().resetClientsExceptHost_NoLock();
  }

  // alert other players of disconnect
  SendToClients(spac);

  for (size_t i = 0; i < m_pad_map.size(); ++i)
  {
    if (m_pad_map[i] == pid)
    {
      m_pad_map[i] = 0;
      m_gba_config[i].enabled = false;
      UpdatePadMapping();
      UpdateGBAConfig();
    }
  }

  for (PlayerId& mapping : m_wiimote_map)
  {
    if (mapping == pid)
    {
      mapping = 0;
      UpdateWiimoteMapping();
    }
  }

  return 0;
}

// called from ---GUI--- thread
PadMappingArray NetPlayServer::GetPadMapping() const
{
  return m_pad_map;
}

GBAConfigArray NetPlayServer::GetGBAConfig() const
{
  return m_gba_config;
}

PadMappingArray NetPlayServer::GetWiimoteMapping() const
{
  return m_wiimote_map;
}

// called from ---GUI--- thread
void NetPlayServer::SetPadMapping(const PadMappingArray& mappings)
{
  m_pad_map = mappings;
  UpdatePadMapping();
}

// called from ---GUI--- thread
void NetPlayServer::SetGBAConfig(const GBAConfigArray& mappings, bool update_rom)
{
#ifdef HAS_LIBMGBA
  m_gba_config = mappings;
  if (update_rom)
  {
    for (size_t i = 0; i < m_gba_config.size(); ++i)
    {
      auto& config = m_gba_config[i];
      if (!config.enabled)
        continue;
      std::string rom_path = Config::Get(Config::MAIN_GBA_ROM_PATHS[i]);
      config.has_rom = HW::GBA::Core::GetRomInfo(rom_path.c_str(), config.hash, config.title);
    }
  }
#endif
  UpdateGBAConfig();
}

// called from ---GUI--- thread
void NetPlayServer::SetWiimoteMapping(const PadMappingArray& mappings)
{
  m_wiimote_map = mappings;
  UpdateWiimoteMapping();
}

// called from ---GUI--- thread and ---NETPLAY--- thread
void NetPlayServer::UpdatePadMapping()
{
  sf::Packet spac;
  spac << MessageID::PadMapping;
  for (PlayerId mapping : m_pad_map)
  {
    spac << mapping;
  }
  SendToClients(spac);
}

// called from ---GUI--- thread and ---NETPLAY--- thread
void NetPlayServer::UpdateGBAConfig()
{
  sf::Packet spac;
  spac << MessageID::GBAConfig;
  for (const auto& config : m_gba_config)
  {
    spac << config.enabled << config.has_rom << config.title;
    for (auto& data : config.hash)
      spac << data;
  }
  SendToClients(spac);
}

// called from ---NETPLAY--- thread
void NetPlayServer::UpdateWiimoteMapping()
{
  sf::Packet spac;
  spac << MessageID::WiimoteMapping;
  for (PlayerId mapping : m_wiimote_map)
  {
    spac << mapping;
  }
  SendToClients(spac);
}

// called from ---GUI--- thread and ---NETPLAY--- thread
void NetPlayServer::AdjustPadBufferSize(unsigned int size)
{
  std::lock_guard lkg(m_crit.game);

  m_target_buffer_size = size;

  // not needed on clients with host input authority
  if (!m_host_input_authority)
  {
    // tell clients to change buffer size
    sf::Packet spac;
    spac << MessageID::PadBuffer;
    spac << m_target_buffer_size;

    SendAsyncToClients(std::move(spac));
  }
}

void NetPlayServer::SetHostInputAuthority(const bool enable)
{
  std::lock_guard lkg(m_crit.game);

  m_host_input_authority = enable;

  // tell clients about the new value
  sf::Packet spac;
  spac << MessageID::HostInputAuthority;
  spac << m_host_input_authority;

  SendAsyncToClients(std::move(spac));

  // resend pad buffer to clients when disabled
  if (!m_host_input_authority)
    AdjustPadBufferSize(m_target_buffer_size);
}

void NetPlayServer::SendAsync(sf::Packet&& packet, const PlayerId pid, const u8 channel_id)
{
  {
    std::lock_guard lkq(m_crit.async_queue_write);
    m_async_queue.Push(AsyncQueueEntry{std::move(packet), pid, TargetMode::Only, channel_id});
  }
  Common::ENet::WakeupThread(m_server);
}

void NetPlayServer::SendAsyncToClients(sf::Packet&& packet, const PlayerId skip_pid,
                                       const u8 channel_id)
{
  {
    std::lock_guard lkq(m_crit.async_queue_write);
    m_async_queue.Push(
        AsyncQueueEntry{std::move(packet), skip_pid, TargetMode::AllExcept, channel_id});
  }
  Common::ENet::WakeupThread(m_server);
}

void NetPlayServer::SendChunked(sf::Packet&& packet, const PlayerId pid, const std::string& title)
{
  {
    std::lock_guard lkq(m_crit.chunked_data_queue_write);
    m_chunked_data_queue.Push(
        ChunkedDataQueueEntry{std::move(packet), pid, TargetMode::Only, title});
  }
  m_chunked_data_event.Set();
}

void NetPlayServer::SendChunkedToClients(sf::Packet&& packet, const PlayerId skip_pid,
                                         const std::string& title)
{
  {
    std::lock_guard lkq(m_crit.chunked_data_queue_write);
    m_chunked_data_queue.Push(
        ChunkedDataQueueEntry{std::move(packet), skip_pid, TargetMode::AllExcept, title});
  }
  m_chunked_data_event.Set();
}

void NetPlayServer::SendPauseCommand()
{
  sf::Packet spac;
  spac << MessageID::PauseSimulation;
  SendToClients(spac);
}

void NetPlayServer::SendResumeCommand()
{
  sf::Packet spac;
  spac << MessageID::ResumeSimulation;
  SendToClients(spac);
}

void NetPlayServer::SendPrivateChat(PlayerId target_pid, PlayerId author_pid, const std::string& msg)
{
  sf::Packet pac;
  pac << MessageID::ChatMessage;
  pac << author_pid;
  pac << msg;
  if (const auto it = m_players.find(target_pid); it != m_players.end())
    Send(it->second.socket, pac);
}

// called from ---NETPLAY--- thread
unsigned int NetPlayServer::OnData(sf::Packet& packet, Client& player)
{
  MessageID mid;
  packet >> mid;

  INFO_LOG_FMT(NETPLAY, "Got client message: {:x} from client {}", static_cast<u8>(mid),
               player.pid);

  // don't need lock because this is the only thread that modifies the players
  // only need locks for writes to m_players in this thread

  switch (mid)
  {
  case MessageID::ChatMessage:
  {
    std::string msg;
    packet >> msg;

    // send msg to other clients
    sf::Packet spac;
    spac << MessageID::ChatMessage;
    spac << player.pid;
    spac << msg;

    SendToClients(spac, player.pid);
  }
  break;

  case MessageID::RequestChangeGame:
  {
    // Minimal payload: game_id + sync_hash (20 bytes)
    std::string req_game_id;
    packet >> req_game_id;
    std::array<u8, 20> req_hash{};
    for (u8& b : req_hash)
      packet >> b;

    bool exists = false;
    std::string netplay_name;

    const picojson::value json_data = UICommon::ImportGamesListJson();
    SyncIdentifier si{};
    if (json_data.is<picojson::object>())
    {
      const auto& root_obj = json_data.get<picojson::object>();
      auto games_it = root_obj.find("games");
      if (games_it != root_obj.end() && games_it->second.is<picojson::array>())
      {
        const auto& games_array = games_it->second.get<picojson::array>();
        const std::string req_hash_hex = Common::SHA1::DigestToString(req_hash);
        for (const auto& v : games_array)
        {
          if (!v.is<picojson::object>())
            continue;
          const auto& g = v.get<picojson::object>();
          const auto id_it = g.find("game_id");
          const auto hash_it = g.find("sync_hash");
          if (id_it != g.end() && id_it->second.is<std::string>() &&
              hash_it != g.end() && hash_it->second.is<std::string>())
          {
            std::string json_id = id_it->second.get<std::string>();
            std::string json_hash = hash_it->second.get<std::string>();
            std::string json_hash_upper = json_hash;
            Common::ToUpper(&json_hash_upper);
            std::string req_hash_upper = req_hash_hex;
            Common::ToUpper(&req_hash_upper);
            if (json_id == req_game_id && json_hash_upper == req_hash_upper)
            {
              exists = true;
              const auto name_it = g.find("netplay_name");
              netplay_name = (name_it != g.end() && name_it->second.is<std::string>()) ?
                                 name_it->second.get<std::string>() :
                                 json_id;

              si.game_id = req_game_id;
              si.sync_hash = req_hash;

              const auto dol_elf_size_it = g.find("dol_elf_size");
              si.dol_elf_size =
                  (dol_elf_size_it != g.end() && dol_elf_size_it->second.is<double>()) ?
                      static_cast<u32>(dol_elf_size_it->second.get<double>()) :
                      0;

              const auto revision_it = g.find("revision");
              si.revision = (revision_it != g.end() && revision_it->second.is<double>()) ?
                                static_cast<u16>(revision_it->second.get<double>()) :
                                0;

              const auto disc_number_it = g.find("disc_number");
              si.disc_number =
                  (disc_number_it != g.end() && disc_number_it->second.is<double>()) ?
                      static_cast<u8>(disc_number_it->second.get<double>()) :
                      0;

              const auto is_datel_it = g.find("is_datel");
              si.is_datel = (is_datel_it != g.end() && is_datel_it->second.is<bool>()) ?
                                is_datel_it->second.get<bool>() :
                                false;
              break;
            }
          }
        }
      }
    }

    if (exists)
    {
      ChangeGame(si, netplay_name);
    }
    else
    {
      sf::Packet resp;
      resp << MessageID::ChangeGameNotFound;
      resp << req_game_id;
      Send(player.socket, resp);
    }

  }
  break;

  case MessageID::RequestChangeGameFull:
  {
    std::string game_id;
    packet >> game_id;

    std::array<u8, 20> sync_hash{};
    for (u8& b : sync_hash)
      packet >> b;

    std::string netplay_name;
    packet >> netplay_name;

    u64 dol_elf_size = Common::PacketReadU64(packet);
    u16 revision;
    packet >> revision;
    u8 disc_number;
    packet >> disc_number;
    bool is_datel;
    packet >> is_datel;
    u32 region_u32;
    packet >> region_u32;
    u32 platform_u32;
    packet >> platform_u32;
    bool has_wii_data;
    packet >> has_wii_data;

    std::vector<u8> tmd;
    std::vector<u8> ticket;
    std::vector<u8> cert;

    if (has_wii_data)
    {
       u32 size;
       packet >> size;
       tmd.resize(size);
       for(auto& b : tmd) packet >> b;

       packet >> size;
       ticket.resize(size);
       for(auto& b : ticket) packet >> b;

       packet >> size;
       cert.resize(size);
       for(auto& b : cert) packet >> b;
    }

    // JSON Update Logic
    picojson::value json_data = UICommon::ImportGamesListJson();
    picojson::object root_obj;
    picojson::array games_array;

    if (json_data.is<picojson::object>())
    {
      root_obj = json_data.get<picojson::object>();
      auto games_it = root_obj.find("games");
      if (games_it != root_obj.end() && games_it->second.is<picojson::array>())
      {
        games_array = games_it->second.get<picojson::array>();
      }
    }

    const std::string req_hash_hex = Common::SHA1::DigestToString(sync_hash);
    
    // Check if game exists
    bool exists = false;
    for (auto& v : games_array)
    {
        if (!v.is<picojson::object>()) continue;
        const auto& g = v.get<picojson::object>();
        const auto id_it = g.find("game_id");
        const auto hash_it = g.find("sync_hash");
        if (id_it != g.end() && id_it->second.is<std::string>() &&
            hash_it != g.end() && hash_it->second.is<std::string>())
        {
            std::string json_id = id_it->second.get<std::string>();
            std::string json_hash = hash_it->second.get<std::string>();
            std::string json_hash_upper = json_hash;
            Common::ToUpper(&json_hash_upper);
            std::string req_hash_upper = req_hash_hex;
            Common::ToUpper(&req_hash_upper);

            if (json_id == game_id && json_hash_upper == req_hash_upper)
            {
                exists = true;
                break;
            }
        }
    }

    if (!exists)
    {
        picojson::object g;
        g["netplay_name"] = picojson::value(netplay_name);
        g["game_id"] = picojson::value(game_id);
        g["revision"] = picojson::value(static_cast<double>(revision));
        g["disc_number"] = picojson::value(static_cast<double>(disc_number));
        g["sync_hash"] = picojson::value(req_hash_hex);
        
        std::string region_str = "Unknown";
        switch(static_cast<DiscIO::Region>(region_u32)) {
            case DiscIO::Region::NTSC_J: region_str = "NTSC-J"; break;
            case DiscIO::Region::NTSC_U: region_str = "NTSC-U"; break;
            case DiscIO::Region::PAL: region_str = "PAL"; break;
            case DiscIO::Region::NTSC_K: region_str = "NTSC-K"; break;
            default: break;
        }
        g["region"] = picojson::value(region_str);

        picojson::object sync_obj;
        sync_obj["dol_elf_size"] = picojson::value(static_cast<double>(dol_elf_size));
        sync_obj["game_id"] = picojson::value(game_id);
        sync_obj["revision"] = picojson::value(static_cast<double>(revision));
        sync_obj["disc_number"] = picojson::value(static_cast<double>(disc_number));
        sync_obj["is_datel"] = picojson::value(is_datel);
        g["sync_identifier"] = picojson::value(sync_obj);

        if (has_wii_data) {
            if (!tmd.empty()) {
                std::string tmd_hex = ArrayToString(tmd.data(), static_cast<u32>(tmd.size()), std::numeric_limits<int>::max(), false);
                g["tmd"] = picojson::value(tmd_hex);
                g["tmd_size"] = picojson::value(static_cast<double>(tmd.size()));
            }
            if (!ticket.empty()) {
                std::string ticket_hex = ArrayToString(ticket.data(), static_cast<u32>(ticket.size()), std::numeric_limits<int>::max(), false);
                g["ticket"] = picojson::value(ticket_hex);
            }
            if (!cert.empty()) {
                 std::string cert_hex = ArrayToString(cert.data(), static_cast<u32>(cert.size()), std::numeric_limits<int>::max(), false);
                 g["cert"] = picojson::value(cert_hex);
            }
        }
        
        games_array.push_back(picojson::value(g));
        root_obj["games"] = picojson::value(games_array);
        
        // Timestamp
        std::time_t now = std::time(nullptr);
        std::tm* tm_now = std::gmtime(&now);
        std::ostringstream oss;
        oss << std::put_time(tm_now, "%Y-%m-%dT%H:%M:%SZ");
        root_obj["generated_at"] = picojson::value(oss.str());

        const std::string output_path = File::GetUserPath(D_CONFIG_IDX) + "games_list.json";
        File::CreateFullPath(output_path);
        JsonToFile(output_path, picojson::value(root_obj), true);
        
        INFO_LOG_FMT(NETPLAY, "Added new game to persistent list: {} (Hash: {})", game_id, req_hash_hex);
    }
    else
    {
        INFO_LOG_FMT(NETPLAY, "Game already exists in persistent list: {} (Hash: {})", game_id, req_hash_hex);
    }

    SyncIdentifier si;
    si.game_id = game_id;
    si.sync_hash = sync_hash;
    si.dol_elf_size = dol_elf_size;
    si.revision = revision;
    si.disc_number = disc_number;
    si.is_datel = is_datel;
    
    ChangeGame(si, netplay_name);
  }
  break;

  case MessageID::ChunkedDataProgress:
  {
    u32 cid;
    packet >> cid;
    u64 progress = Common::PacketReadU64(packet);

    m_dialog->SetChunkedProgress(player.pid, progress);
  }
  break;

  case MessageID::ChunkedDataComplete:
  {
    u32 cid;
    packet >> cid;

    if (const auto it = m_chunked_data_complete_count.find(cid);
        it != m_chunked_data_complete_count.end())
    {
      it->second++;
      m_chunked_data_complete_event.Set();
    }
  }
  break;

  case MessageID::PadData:
  {
    // if this is pad data from the last game still being received, ignore it
    if (player.current_game != m_current_game)
      break;

    sf::Packet spac;
    spac << (m_host_input_authority ? MessageID::PadHostData : MessageID::PadData);

    while (!packet.endOfPacket())
    {
      PadIndex map;
      packet >> map;

      // 如果发现控制器映射与玩家 PID 不匹配，则进入「宽限 N 帧」容错逻辑。
      // N = 22 帧（约 0.37 s）以涵盖 PadBuffer 深度与网络 RTT 抖动。
      const bool mapping_match = (m_pad_map.at(map) == player.pid);

      // 先无条件读取完整 PadData，保证 packet 对齐
      GCPadStatus pad;
      packet >> pad.button;
      if (!m_gba_config.at(map).enabled)
      {
        packet >> pad.analogA >> pad.analogB >> pad.stickX >> pad.stickY >> pad.substickX >>
            pad.substickY >> pad.triggerLeft >> pad.triggerRight >> pad.isConnected;
      }

      if (!mapping_match)
      {
        if (m_pad_mapping_grace_counter < PAD_MAPPING_GRACE_FRAMES)
        {
          ++m_pad_mapping_grace_counter; // 仍在宽限期 —— 继续转发，避免卡顿
        }
        else
        {
          // 超过宽限期仍不匹配，认为客户端未按要求切换端口；终止处理本条消息
          WARN_LOG_FMT(NETPLAY, "Pad mapping mismatch beyond grace period (map {} expect {}, got {}).", static_cast<int>(map), m_pad_map.at(map), player.pid);
          break;
        }
      }
      else
      {
        // 匹配正确，重置计数器
        m_pad_mapping_grace_counter = 0;
      }

      // 无论是否处于宽限期，只要没有超期，都把 PadData 转发给其他客户端
      spac << map << pad.button;
      if (!m_gba_config.at(map).enabled)
      {
        spac << pad.analogA << pad.analogB << pad.stickX << pad.stickY << pad.substickX
             << pad.substickY << pad.triggerLeft << pad.triggerRight << pad.isConnected;
      }
    }

    if (m_host_input_authority)
    {
      // Prevent crash before game stop if the golfer disconnects
      if (m_current_golfer != 0)
      {
        if (const auto it = m_players.find(m_current_golfer); it != m_players.end())
          Send(it->second.socket, spac);
      }
    }
    else
    {
      SendToClients(spac, player.pid);
    }
  }
  break;

  case MessageID::PadHostData:
  {
    // Kick player if they're not the golfer.
    if (m_current_golfer != 0 && player.pid != m_current_golfer)
      return 1;

    sf::Packet spac;
    spac << MessageID::PadData;

    while (!packet.endOfPacket())
    {
      PadIndex map;
      packet >> map;

      GCPadStatus pad;
      packet >> pad.button;
      spac << map << pad.button;
      if (!m_gba_config.at(map).enabled)
      {
        packet >> pad.analogA >> pad.analogB >> pad.stickX >> pad.stickY >> pad.substickX >>
            pad.substickY >> pad.triggerLeft >> pad.triggerRight >> pad.isConnected;

        spac << pad.analogA << pad.analogB << pad.stickX << pad.stickY << pad.substickX
             << pad.substickY << pad.triggerLeft << pad.triggerRight << pad.isConnected;
      }
    }

    SendToClients(spac, player.pid);
  }
  break;

  case MessageID::WiimoteData:
  {
    // if this is Wiimote data from the last game still being received, ignore it
    if (player.current_game != m_current_game)
      break;

    sf::Packet spac;
    spac << MessageID::WiimoteData;

    while (!packet.endOfPacket())
    {
      PadIndex map;
      packet >> map;

      // If the data is not from the correct player,
      // then disconnect them.
      if (m_wiimote_map.at(map) != player.pid)
      {
        return 1;
      }

      WiimoteEmu::SerializedWiimoteState pad;
      packet >> pad.length;
      if (pad.length > pad.data.size())
        return 1;
      for (size_t i = 0; i < pad.length; ++i)
        packet >> pad.data[i];

      spac << map;
      spac << pad.length;
      for (size_t i = 0; i < pad.length; ++i)
        spac << pad.data[i];
    }

    SendToClients(spac, player.pid);
  }
  break;

  case MessageID::GolfRequest:
  {
    PlayerId pid;
    packet >> pid;

    // Check if player ID is valid and sender isn't a spectator
    if (!m_players.contains(pid) || !PlayerHasControllerMapped(player.pid))
      break;

    if (m_host_input_authority && m_settings.golf_mode && m_pending_golfer == 0 &&
        m_current_golfer != pid && PlayerHasControllerMapped(pid))
    {
      m_pending_golfer = pid;

      sf::Packet spac;
      spac << MessageID::GolfPrepare;
      Send(m_players[pid].socket, spac);
    }
  }
  break;

  case MessageID::GolfRelease:
  {
    if (m_pending_golfer == 0)
      break;

    sf::Packet spac;
    spac << MessageID::GolfSwitch;
    spac << m_pending_golfer;
    SendToClients(spac);
  }
  break;

  case MessageID::GolfAcquire:
  {
    if (m_pending_golfer == 0)
      break;

    m_current_golfer = m_pending_golfer;
    m_pending_golfer = 0;
  }
  break;

  case MessageID::GolfPrepare:
  {
    if (m_pending_golfer == 0)
      break;

    m_current_golfer = 0;

    sf::Packet spac;
    spac << MessageID::GolfSwitch;
    spac << PlayerId{0};
    SendToClients(spac);
  }
  break;

  case MessageID::Pong:
  {
    // truncation (> ~49 days elapsed) should never happen here
    const u32 ping = static_cast<u32>(m_ping_timer.ElapsedMs());
    u32 ping_key = 0;
    packet >> ping_key;

    if (m_ping_key == ping_key)
    {
      player.ping = ping;
    }

    sf::Packet spac;
    spac << MessageID::PlayerPingData;
    spac << player.pid;
    spac << player.ping;

    SendToClients(spac);
  }
  break;

  case MessageID::StartGame:
  {
    packet >> player.current_game;
  }
  break;

  case MessageID::StopGame:
  {
    if (!m_is_running)
      break;

    m_is_running = false;

    // tell clients to stop game
    sf::Packet spac;
    spac << MessageID::StopGame;

    NetPlay::NetplayManager::GetInstance().resetClientsExceptHost_NoLock();

    std::lock_guard lkp(m_crit.players);
    SendToClients(spac);
  }
  break;

  case MessageID::GameStatus:
  {
    SyncIdentifierComparison status;
    packet >> status;

    m_players[player.pid].game_status = status;

    // send msg to other clients
    sf::Packet spac;
    spac << MessageID::GameStatus;
    spac << player.pid;
    spac << status;

    SendToClients(spac);
  }
  break;

  case MessageID::ClientCapabilities:
  {
    packet >> m_players[player.pid].has_ipl_dump;
    packet >> m_players[player.pid].has_hardware_fma;
  }
  break;

  case MessageID::ClientInitialStateAck:
  {
    auto& netPlayManager = NetplayManager::GetInstance();
    bool has_initial_state = false;
    packet >> has_initial_state;
    const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
    File::IOFile log_file(log_path, "ab");
    if (log_file)
    {
      const std::string& r = fmt::format("server has_initial_state:{}, player:{}\n",
                                           has_initial_state, player.pid);
      log_file.WriteBytes(r.c_str(), r.size());
    }
    if (log_file)
    {
      const std::string& r = fmt::format("Player {} reported HasInitialStateSave = {}\n",
                                           player.pid, has_initial_state);
      log_file.WriteBytes(r.c_str(), r.size());
    }
    if (has_initial_state) {
      if (log_file)
      {
        const std::string& r = fmt::format("Player {} has initial state save.\n", player.pid);
        log_file.WriteBytes(r.c_str(), r.size());
      }
      if (netPlayManager.setClientLoadStatusSuccess(player.pid)) {
        if (log_file)
        {
          const std::string& r = fmt::format("Player {} set load status success.\n", player.pid);
          log_file.WriteBytes(r.c_str(), r.size());
        }
        if (netPlayManager.canLoadStatus()) {
          if (log_file)
          {
            const std::string& r = fmt::format("All clients ready, now can send pause order.\n");
            log_file.WriteBytes(r.c_str(), r.size());
          }
            if (log_file)
            {
                const std::string& r = "All clients ready, starting to load state.\n";
                log_file.WriteBytes(r.c_str(), r.size());
            }
            SendPauseCommand();

            // Launch a detached thread that will resume the simulation after a 1-second delay.
            std::thread([this]() {
              // Sleep for 1 second to give clients time to process the pause and load state.
              std::this_thread::sleep_for(std::chrono::seconds(1));
              // After the delay, send the resume command.
              SendResumeCommand();
            }).detach();

            if (log_file)
            {
                const std::string& r = "Pause command sent.\n";
                log_file.WriteBytes(r.c_str(), r.size());
            }
        }
      } else {
        if (log_file)
        {
          const std::string& r = fmt::format("Player {} set load status failed.\n", player.pid);
          log_file.WriteBytes(r.c_str(), r.size());
        }
      }
    } else {
        if (log_file)
        {
          const std::string& r = fmt::format("Player {} has no initial state save.\n", player.pid);
          log_file.WriteBytes(r.c_str(), r.size());
        }
    }
  }
  break;

  case MessageID::PowerButton:
  {
    sf::Packet spac;
    spac << MessageID::PowerButton;
    SendToClients(spac, player.pid);
  }
  break;

  case MessageID::RequestStartGameClient:
  {
    // if (m_dialog && m_dialog->IsSharingEnabled())
    if (m_dialog)
    {
      OnRequestStartGameClient(player);
    }
    else
    {
      
    }
  }
  break;

  case MessageID::TimeBase:
  {
    u64 timebase = Common::PacketReadU64(packet);
    u32 frame;
    packet >> frame;

    if (m_desync_detected)
      break;

    std::vector<std::pair<PlayerId, u64>>& timebases = m_timebase_by_frame[frame];
    timebases.emplace_back(player.pid, timebase);
    if (timebases.size() >= m_players.size())
    {
      // we have all records for this frame

      if (!std::ranges::all_of(timebases, [&](std::pair<PlayerId, u64> pair) {
            return pair.second == timebases[0].second;
          }))
      {
        int pid_to_blame = 0;
        for (auto pair : timebases)
        {
          if (std::ranges::all_of(timebases, [&](std::pair<PlayerId, u64> other) {
                return other.first == pair.first || other.second != pair.second;
              }))
          {
            // we are the only outlier
            pid_to_blame = pair.first;
            break;
          }
        }

        sf::Packet spac;
        spac << MessageID::DesyncDetected;
        spac << pid_to_blame;
        spac << frame;
        SendToClients(spac);

        m_desync_detected = true;
      }
      m_timebase_by_frame.erase(frame);
    }
  }
  break;

  case MessageID::GameDigestProgress:
  {
    int progress;
    packet >> progress;

    sf::Packet spac;
    spac << MessageID::GameDigestProgress;
    spac << player.pid;
    spac << progress;

    SendToClients(spac);
  }
  break;

  case MessageID::GameDigestResult:
  {
    std::string result;
    packet >> result;

    sf::Packet spac;
    spac << MessageID::GameDigestResult;
    spac << player.pid;
    spac << result;

    SendToClients(spac);
  }
  break;

  case MessageID::REQUEST_PAD_MAPPING_CHANGE_ID: // Ensure this ID matches the one in NetPlayProto.h
  {
    INFO_LOG_FMT(NETPLAY, "Received REQUEST_PAD_MAPPING_CHANGE_ID from player {} ({})", player.pid, player.name);
    PadMappingArray gc_map_from_client;
    GBAConfigArray gba_cfg_from_client;
    PadMappingArray wii_map_from_client;

    // Deserialize PadMappingArray (gc_map_from_client)
    for (PlayerId& mapping : gc_map_from_client)
    {
      if (!(packet >> mapping))
      {
        WARN_LOG_FMT(NETPLAY, "Packet for REQUEST_PAD_MAPPING_CHANGE_ID too short for GC map (PlayerId) from PID {}.", player.pid);
        return 1; // Indicate error / disconnect
      }
    }

    // Deserialize GBAConfigArray (gba_cfg_from_client)
    for (GBAConfig& config : gba_cfg_from_client)
    {
      if (!(packet >> config.enabled >> config.has_rom >> config.title))
      {
        WARN_LOG_FMT(NETPLAY, "Packet for REQUEST_PAD_MAPPING_CHANGE_ID too short for GBA config (metadata) from PID {}.", player.pid);
        return 1; // Indicate error / disconnect
      }
      for (u8& hash_byte : config.hash)
      {
        if (!(packet >> hash_byte))
        {
          WARN_LOG_FMT(NETPLAY, "Packet for REQUEST_PAD_MAPPING_CHANGE_ID too short for GBA config (hash) from PID {}.", player.pid);
          return 1; // Indicate error / disconnect
        }
      }
    }

    // Deserialize PadMappingArray (wii_map_from_client)
    for (PlayerId& mapping : wii_map_from_client)
    {
      if (!(packet >> mapping))
      {
        WARN_LOG_FMT(NETPLAY, "Packet for REQUEST_PAD_MAPPING_CHANGE_ID too short for Wii map (PlayerId) from PID {}.", player.pid);
        return 1; // Indicate error / disconnect
      }
    }

    // TODO: Validate the data if necessary (e.g., PIDs are valid)
    SetPadMapping(gc_map_from_client);
    SetGBAConfig(gba_cfg_from_client, true); // Assuming 'true' triggers broadcast
    SetWiimoteMapping(wii_map_from_client);
    // The Set... methods should already handle broadcasting the update to all clients.
  }
  break;

  case MessageID::REQUEST_BUFFER_CHANGE_ID:
  {
    s32 requested_buffer_value;
    // Check if there's enough data for s32
    if (packet.getReadPosition() + sizeof(s32) > packet.getDataSize()) {
        WARN_LOG_FMT(NETPLAY, "Packet for REQUEST_BUFFER_CHANGE_ID too short to contain buffer value from PID {}.", player.pid);
        return 1; // Indicate error / disconnect client
    }
    if (!(packet >> requested_buffer_value)) {
        WARN_LOG_FMT(NETPLAY, "Failed to deserialize buffer value from REQUEST_BUFFER_CHANGE_ID (PID: {}).", player.pid);
        return 1; // Indicate error / disconnect client
    }

    INFO_LOG_FMT(NETPLAY, "Player PID {} ({}) requested buffer change to: {}", player.pid, player.name, requested_buffer_value);
    
    // As per user request, skipping min/max validation if not already part of AdjustPadBufferSize.
    // AdjustPadBufferSize takes unsigned int.
    if (requested_buffer_value < 0) {
        WARN_LOG_FMT(NETPLAY, "Player PID {} ({}) requested negative buffer value {}. Clamping to 0.", player.pid, player.name, requested_buffer_value);
        requested_buffer_value = 0;
    }
    this->AdjustPadBufferSize(static_cast<unsigned int>(requested_buffer_value));
    // AdjustPadBufferSize will handle broadcasting if not in HIA mode.
  }
  break;

  case MessageID::GameDigestError:
  {
    std::string error;
    packet >> error;

    sf::Packet spac;
    spac << MessageID::GameDigestError;
    spac << player.pid;
    spac << error;

    SendToClients(spac);
  }
  break;

  case MessageID::SyncSaveData:
  {
    SyncSaveDataID sub_id;
    packet >> sub_id;

    INFO_LOG_FMT(NETPLAY, "Got client SyncSaveData message: {:x} from client {}", u8(sub_id),
                 player.pid);

    switch (sub_id)
    {
    case SyncSaveDataID::UploadIntent:
    {
      const bool allowed = m_settings.savedata_write;
      {
        const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "serverhash8.txt";
        File::IOFile lf(log_path, "ab");
        if (lf)
        {
          const std::string line = fmt::format("[SERVER] UploadIntent allowed={} pid={}\n", allowed, player.pid);
          lf.WriteBytes(line.data(), line.size());
        }
      }
      if (m_dialog)
        m_dialog->AppendChat(allowed ? "允许上传存档，开始等待数据..."
                                     : "拒绝上传存档：未启用写回主机存档");
      sf::Packet resp;
      resp << MessageID::SyncSaveData;
      resp << (allowed ? SyncSaveDataID::AllowUpload : SyncSaveDataID::Failure);
      Send(player.socket, resp);
    }
    break;

    case SyncSaveDataID::WiiData:
    {
      std::vector<u64> titles;
      const std::string redirect_target = File::GetUserPath(D_USER_IDX) + "RedirectSession" DIR_SEP;
      const std::string temp_root = File::GetUserPath(D_USER_IDX) + "WiiUploadSession" DIR_SEP;
      {
        const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "serverhash8.txt";
        File::IOFile lf(log_path, "ab");
        if (lf)
        {
          const std::string line = fmt::format("[SERVER] WiiData recv temp_root='{}' redirect='{}'\n", temp_root, redirect_target);
          lf.WriteBytes(line.data(), line.size());
        }
      }
      if (m_dialog)
        m_dialog->AppendChat(Common::FmtFormatT("收到上传数据，开始解包到：{0}，重定向：{1}", temp_root,
                                               redirect_target));
      auto temp_fs = std::make_unique<IOS::HLE::FS::HostFileSystem>(temp_root);

      const bool decode_ok = NetPlayUpload::ServerDecodeWiiSaveUploadPacketToFS(
          packet, temp_fs.get(), redirect_target, &titles);

      if (!decode_ok)
      {
        const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "serverhash8.txt";
        File::IOFile lf(log_path, "ab");
        if (lf)
        {
          const std::string line = "[SERVER] WiiData decode failed\n";
          lf.WriteBytes(line.data(), line.size());
        }
        if (m_dialog)
          m_dialog->AppendChat("上传解包失败");
        sf::Packet resp;
        resp << MessageID::SyncSaveData;
        resp << SyncSaveDataID::Failure;
        Send(player.socket, resp);
        break;
      }

      ENetPeer* sock = player.socket;
      if (m_dialog)
        m_dialog->AppendChat("解包成功，开始覆盖主机存档...");
      std::thread([this, sock, titles = std::move(titles), temp_fs = std::move(temp_fs)]() mutable {
        {
          const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "serverhash8.txt";
          File::IOFile lf(log_path, "ab");
          if (lf)
          {
            const std::string line = fmt::format("[SERVER] Apply start titles={}\n", fmt::join(titles, ","));
            lf.WriteBytes(line.data(), line.size());
          }
        }
        auto configured_fs = IOS::HLE::FS::MakeFileSystem(IOS::HLE::FS::Location::Configured);
        const bool apply_ok = NetPlayUpload::ServerApplyUploadedWiiSaves(
            temp_fs.get(), configured_fs.get(), titles, true);

        if (m_dialog)
          m_dialog->AppendChat(apply_ok ? "上传存档应用成功" : "上传存档应用失败");
        {
          const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "serverhash8.txt";
          File::IOFile lf(log_path, "ab");
          if (lf)
          {
            const std::string line = fmt::format("[SERVER] Apply done ok={}\n", apply_ok);
            lf.WriteBytes(line.data(), line.size());
          }
        }
        sf::Packet resp;
        resp << MessageID::SyncSaveData;
        resp << (apply_ok ? SyncSaveDataID::Success : SyncSaveDataID::Failure);
        Send(sock, resp);
      }).detach();
    }
    break;

    case SyncSaveDataID::Success:
    {
      if (m_start_pending)
      {
        m_save_data_synced_players++;
        if (m_save_data_synced_players >= m_players.size() - 1)
        {
          INFO_LOG_FMT(NETPLAY, "SyncSaveData: All players synchronized. ({} >= {})",
                       m_save_data_synced_players, m_players.size() - 1);
          m_dialog->AppendChat(Common::GetStringT("All players' saves synchronized."));

          // Saves are synced, check if codes are as well and attempt to start the game
          m_saves_synced = true;
          CheckSyncAndStartGame();
        }
        else
        {
          INFO_LOG_FMT(NETPLAY, "SyncSaveData: Not all players synchronized. ({} < {})",
                       m_save_data_synced_players, m_players.size() - 1);
        }
      }
      else
      {
        INFO_LOG_FMT(NETPLAY, "SyncSaveData: Start not pending.");
      }
    }
    break;

    case SyncSaveDataID::Failure:
    {
      m_dialog->AppendChat(Common::FmtFormatT("{0} failed to synchronize.", player.name));
      m_dialog->OnGameStartAborted();
      ChunkedDataAbort();
      m_start_pending = false;
    }
    break;

    default:
      PanicAlertFmtT(
          "Unknown SYNC_SAVE_DATA message with id:{0} received from player:{1} Kicking player!",
          static_cast<u8>(sub_id), player.pid);
      return 1;
    }
  }
  break;

  case MessageID::SyncCodes:
  {
    // Receive Status of Code Sync
    SyncCodeID sub_id;
    packet >> sub_id;

    INFO_LOG_FMT(NETPLAY, "Got client SyncCodes message: {:x} from client {}", u8(sub_id),
                 player.pid);

    // Check If Code Sync was successful or not
    switch (sub_id)
    {
    case SyncCodeID::Success:
    {
      if (m_start_pending)
      {
        if (++m_codes_synced_players >= m_players.size() - 1)
        {
          INFO_LOG_FMT(NETPLAY, "SyncCodes: All players synchronized. ({} >= {})",
                       m_codes_synced_players, m_players.size() - 1);

          m_dialog->AppendChat(Common::GetStringT("All players' codes synchronized."));

          // Codes are synced, check if saves are as well and attempt to start the game
          m_codes_synced = true;
          CheckSyncAndStartGame();
        }
        else
        {
          INFO_LOG_FMT(NETPLAY, "SyncCodes: Not all players synchronized. ({} < {})",
                       m_codes_synced_players, m_players.size() - 1);
        }
      }
      else
      {
        INFO_LOG_FMT(NETPLAY, "SyncCodes: Start not pending.");
      }
    }
    break;

    case SyncCodeID::Failure:
    {
      m_dialog->AppendChat(Common::FmtFormatT("{0} failed to synchronize codes.", player.name));
      m_dialog->OnGameStartAborted();
      m_start_pending = false;
    }
    break;

    default:
      PanicAlertFmtT(
          "Unknown SYNC_GECKO_CODES message with id:{0} received from player:{1} Kicking player!",
          static_cast<u8>(sub_id), player.pid);
      return 1;
    }
  }
  break;

  default:
    PanicAlertFmtT("Unknown message with id:{0} received from player:{1} Kicking player!",
                   static_cast<u8>(mid), player.pid);
    // unknown message, kick the client
    return 1;
  }

  return 0;
}

void NetPlayServer::OnTraversalStateChanged()
{
  const Common::TraversalClient::State state = m_traversal_client->GetState();

  if (Common::g_TraversalClient->GetHostID()[0] != '\0')
    SetupIndex();

  if (!m_dialog)
    return;

  if (state == Common::TraversalClient::State::Failure)
    m_dialog->OnTraversalError(m_traversal_client->GetFailureReason());

  m_dialog->OnTraversalStateChanged(state);
}

void NetPlayServer::OnTtlDetermined(u8 ttl)
{
  m_dialog->OnTtlDetermined(ttl);
}

// called from ---GUI--- thread
void NetPlayServer::SendChatMessage(const std::string& msg)
{
  sf::Packet spac;
  spac << MessageID::ChatMessage;
  spac << PlayerId{0};  // server ID always 0
  spac << msg;

  SendAsyncToClients(std::move(spac));
}

// called from ---GUI--- thread
bool NetPlayServer::ChangeGame(const SyncIdentifier& sync_identifier,
                               const std::string& netplay_name)
{
  std::lock_guard lkg(m_crit.game);

  INFO_LOG_FMT(NETPLAY, "Changing game to {} ({:02x}).", netplay_name,
               fmt::join(sync_identifier.sync_hash, ""));

  m_selected_game_identifier = sync_identifier;
  m_selected_game_name = netplay_name;

  // Reset client states except host when game changes
  NetPlay::NetplayManager::GetInstance().resetClientsExceptHost_NoLock();

  // Compute and set SaveHash8 early so that server-only host (not entering game)
  // can still resolve correct NAND save paths for Wii titles.
  const std::string hash_str = Common::SHA1::DigestToString(sync_identifier.sync_hash);
  const std::string hash8 = hash_str.substr(0, 8);
  SConfig::GetInstance().SetSaveHash8(hash8);

  NetplayManager::GetInstance().SetCurrentGame(m_selected_game_identifier, m_selected_game_name);

  // Debug logging similar to previous instrumentation
  const std::string log_path = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
  File::WriteStringToFile(log_path, "[NPDEBUG] ChangeGame set SaveHash8=" + hash8 + "\n");

  {
    const std::string gid = m_selected_game_identifier.game_id;
    if (gid.size() == 6)
    {
      const u32 type_high = 0x00010000u;
      const u32 id_low = (static_cast<u8>(gid[0]) << 24) | (static_cast<u8>(gid[1]) << 16) |
                         (static_cast<u8>(gid[2]) << 8) | static_cast<u8>(gid[3]);
      const u64 title_id = (static_cast<u64>(type_high) << 32) | static_cast<u64>(id_low);

      {
        const std::string tmd_dir_dbg = Common::GetTitleContentPath(title_id);
        const std::string tmd_path_dbg = Common::GetTMDFileName(title_id);
        const std::string log_path2 = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
        File::IOFile lf(log_path2, "ab");
        if (lf)
        {
          const std::string line = fmt::format("[NPDEBUG] ChangeGame: title_id={:016x}, tmd_dir='{}', tmd_path='{}'\n",
                                              title_id, tmd_dir_dbg, tmd_path_dbg);
          lf.WriteBytes(line.data(), line.size());
        }
      }

      IOS::HLE::Kernel ios;
      if (!ios.GetESCore().FindInstalledTMD(title_id).IsValid())
      {
        {
          const std::string log_path2 = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
          File::IOFile lf(log_path2, "ab");
          if (lf)
          {
            const std::string line = fmt::format("[NPDEBUG] ChangeGame: installed_tmd_exists=false for {:016x}\n",
                                                title_id);
            lf.WriteBytes(line.data(), line.size());
          }
        }
        std::vector<u8> tmd_bytes;
        {
          const picojson::value json_data = UICommon::ImportGamesListJson();
          if (json_data.is<picojson::object>())
          {
            const auto& root_obj = json_data.get<picojson::object>();
            auto games_it = root_obj.find("games");
            if (games_it != root_obj.end() && games_it->second.is<picojson::array>())
            {
              const auto& games_array = games_it->second.get<picojson::array>();
              const std::string req_hash_hex = Common::SHA1::DigestToString(m_selected_game_identifier.sync_hash);
              std::string req_hash_upper = req_hash_hex;
              Common::ToUpper(&req_hash_upper);
              for (const auto& v : games_array)
              {
                if (!v.is<picojson::object>())
                  continue;
                const auto& g = v.get<picojson::object>();
                const auto id_it = g.find("game_id");
                const auto hash_it = g.find("sync_hash");
                const auto tmd_it = g.find("tmd");
                if (id_it != g.end() && id_it->second.is<std::string>() &&
                    hash_it != g.end() && hash_it->second.is<std::string>() &&
                    tmd_it != g.end() && tmd_it->second.is<std::string>())
                {
                  std::string json_id = id_it->second.get<std::string>();
                  std::string json_hash = hash_it->second.get<std::string>();
                  std::string json_hash_upper = json_hash;
                  Common::ToUpper(&json_hash_upper);
                  if (json_id == gid && json_hash_upper == req_hash_upper)
                  {
                    const std::string tmd_hex = tmd_it->second.get<std::string>();
                    auto hex_to_bytes = [](const std::string& s) {
                      std::vector<u8> out;
                      int hi = -1;
                      for (char c : s)
                      {
                        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                          continue;
                        u8 v = 0;
                        if (c >= '0' && c <= '9')
                          v = static_cast<u8>(c - '0');
                        else if (c >= 'a' && c <= 'f')
                          v = static_cast<u8>(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F')
                          v = static_cast<u8>(c - 'A' + 10);
                        else
                          continue;
                        if (hi < 0)
                          hi = v;
                        else
                        {
                          out.push_back(static_cast<u8>((hi << 4) | v));
                          hi = -1;
                        }
                      }
                      return out;
                    };
                    tmd_bytes = hex_to_bytes(tmd_hex);
                    break;
                  }
                }
              }
            }
          }
        }

        std::vector<u8> ticket_bytes;
        std::vector<u8> cert_bytes;
        {
          const picojson::value json_data2 = UICommon::ImportGamesListJson();
          if (json_data2.is<picojson::object>())
          {
            const auto& root_obj2 = json_data2.get<picojson::object>();
            auto games_it2 = root_obj2.find("games");
            if (games_it2 != root_obj2.end() && games_it2->second.is<picojson::array>())
            {
              const auto& games_array2 = games_it2->second.get<picojson::array>();
              const std::string req_hash_hex2 = Common::SHA1::DigestToString(m_selected_game_identifier.sync_hash);
              std::string req_hash_upper2 = req_hash_hex2;
              Common::ToUpper(&req_hash_upper2);
              for (const auto& v2 : games_array2)
              {
                if (!v2.is<picojson::object>())
                  continue;
                const auto& g2 = v2.get<picojson::object>();
                const auto id_it2 = g2.find("game_id");
                const auto hash_it2 = g2.find("sync_hash");
                const auto ticket_it2 = g2.find("ticket");
                const auto cert_it2 = g2.find("cert");
                if (id_it2 != g2.end() && id_it2->second.is<std::string>() &&
                    hash_it2 != g2.end() && hash_it2->second.is<std::string>())
                {
                  std::string json_id2 = id_it2->second.get<std::string>();
                  std::string json_hash2 = hash_it2->second.get<std::string>();
                  std::string json_hash_upper2 = json_hash2;
                  Common::ToUpper(&json_hash_upper2);
                  if (json_id2 == gid && json_hash_upper2 == req_hash_upper2)
                  {
                    auto hex_to_bytes2 = [](const std::string& s) {
                      std::vector<u8> out;
                      int hi = -1;
                      for (char c : s)
                      {
                        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                          continue;
                        u8 v = 0;
                        if (c >= '0' && c <= '9')
                          v = static_cast<u8>(c - '0');
                        else if (c >= 'a' && c <= 'f')
                          v = static_cast<u8>(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F')
                          v = static_cast<u8>(c - 'A' + 10);
                        else
                          continue;
                        if (hi < 0)
                          hi = v;
                        else
                        {
                          out.push_back(static_cast<u8>((hi << 4) | v));
                          hi = -1;
                        }
                      }
                      return out;
                    };
                    if (ticket_it2 != g2.end() && ticket_it2->second.is<std::string>())
                      ticket_bytes = hex_to_bytes2(ticket_it2->second.get<std::string>());
                    if (cert_it2 != g2.end() && cert_it2->second.is<std::string>())
                      cert_bytes = hex_to_bytes2(cert_it2->second.get<std::string>());
                    break;
                  }
                }
              }
            }
          }
        }

        bool strict_done = false;
        if (!tmd_bytes.empty() && !cert_bytes.empty() && !ticket_bytes.empty())
        {
          IOS::HLE::ESCore& es = ios.GetESCore();
          const auto ticket_rc = es.ImportTicket(ticket_bytes, cert_bytes,
                                                IOS::HLE::ESCore::TicketImportType::PossiblyPersonalised,
                                                IOS::HLE::ESCore::VerifySignature::Yes);
          if (ticket_rc == IOS::HLE::IPC_SUCCESS)
          {
            IOS::HLE::ESCore::Context ctx;
            ctx.uid = IOS::SYSMENU_UID;
            ctx.gid = IOS::SYSMENU_GID;
            const auto init_rc = es.ImportTitleInit(ctx, tmd_bytes, cert_bytes,
                                                    IOS::HLE::ESCore::VerifySignature::Yes);
            if (init_rc == IOS::HLE::IPC_SUCCESS)
            {
              const auto done_rc = es.ImportTitleDone(ctx);
              strict_done = (done_rc == IOS::HLE::IPC_SUCCESS);
            }
          }
          {
            const std::string log_path2 = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
            File::IOFile lf(log_path2, "ab");
            if (lf)
            {
              const std::string line = fmt::format(
                  "[NPDEBUG] ChangeGame: strict_import ticket_rc={} done={}\n",
                  static_cast<int>(ticket_rc), strict_done ? 1 : 0);
              lf.WriteBytes(line.data(), line.size());
            }
          }
        }

        if (tmd_bytes.empty())
        {
          tmd_bytes.resize(sizeof(IOS::ES::TMDHeader));
          auto put_u32 = [&tmd_bytes](size_t off, u32 v) {
            tmd_bytes[off + 0] = static_cast<u8>((v >> 24) & 0xff);
            tmd_bytes[off + 1] = static_cast<u8>((v >> 16) & 0xff);
            tmd_bytes[off + 2] = static_cast<u8>((v >> 8) & 0xff);
            tmd_bytes[off + 3] = static_cast<u8>(v & 0xff);
          };
          auto put_u64 = [&put_u32](size_t off, u64 v) {
            put_u32(off + 0, static_cast<u32>((v >> 32) & 0xffffffffu));
            put_u32(off + 4, static_cast<u32>(v & 0xffffffffu));
          };
          auto put_u16 = [&tmd_bytes](size_t off, u16 v) {
            tmd_bytes[off + 0] = static_cast<u8>((v >> 8) & 0xff);
            tmd_bytes[off + 1] = static_cast<u8>(v & 0xff);
          };

          put_u32(0, static_cast<u32>(IOS::SignatureType::RSA2048));
          tmd_bytes[offsetof(IOS::ES::TMDHeader, tmd_version)] = 1;
          tmd_bytes[offsetof(IOS::ES::TMDHeader, ca_crl_version)] = 0;
          tmd_bytes[offsetof(IOS::ES::TMDHeader, signer_crl_version)] = 0;
          tmd_bytes[offsetof(IOS::ES::TMDHeader, is_vwii)] = 0;
          put_u64(offsetof(IOS::ES::TMDHeader, ios_id), 0x000000010000003aULL);
          put_u64(offsetof(IOS::ES::TMDHeader, title_id), title_id);
          put_u32(offsetof(IOS::ES::TMDHeader, title_flags), IOS::ES::TITLE_TYPE_DEFAULT);
          tmd_bytes[offsetof(IOS::ES::TMDHeader, group_id) + 0] = static_cast<u8>(gid[4]);
          tmd_bytes[offsetof(IOS::ES::TMDHeader, group_id) + 1] = static_cast<u8>(gid[5]);
          put_u16(offsetof(IOS::ES::TMDHeader, region), 0);
          put_u32(offsetof(IOS::ES::TMDHeader, access_rights), 0);
          put_u16(offsetof(IOS::ES::TMDHeader, title_version), 1);
          put_u16(offsetof(IOS::ES::TMDHeader, num_contents), 0);
          put_u16(offsetof(IOS::ES::TMDHeader, boot_index), 0);
        }

        constexpr IOS::HLE::FS::Modes modes{IOS::HLE::FS::Mode::ReadWrite, IOS::HLE::FS::Mode::ReadWrite,
                                            IOS::HLE::FS::Mode::None};
        const std::string tmp_path = "/tmp/title.tmd";
        bool wrote_tmp = false;
        {
          auto tmp_file = ios.GetFS()->CreateAndOpenFile(IOS::PID_KERNEL, IOS::PID_KERNEL, tmp_path, modes);
          if (!strict_done && tmp_file)
          {
            auto wr = tmp_file->Write(tmd_bytes.data(), tmd_bytes.size());
            if (wr && *wr == tmd_bytes.size())
              wrote_tmp = true;
          }
        }
        if (wrote_tmp)
        {
          const std::string tmd_dir = Common::GetTitleContentPath(title_id);
          const std::string tmd_path = Common::GetTMDFileName(title_id);
          ios.GetFS()->CreateFullPath(IOS::PID_KERNEL, IOS::PID_KERNEL, tmd_path, 0,
                                      {IOS::HLE::FS::Mode::ReadWrite, IOS::HLE::FS::Mode::ReadWrite,
                                       IOS::HLE::FS::Mode::Read});
          ios.GetFS()->SetMetadata(IOS::PID_KERNEL, tmd_dir, IOS::PID_KERNEL, IOS::PID_KERNEL, 0,
                                   modes);
          {
            const std::string log_path2 = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
            File::IOFile lf(log_path2, "ab");
            if (lf)
            {
              const std::string line = fmt::format("[NPDEBUG] ChangeGame: renaming '{}' -> '{}'\n", tmp_path, tmd_path);
              lf.WriteBytes(line.data(), line.size());
            }
          }
          const auto rename_rc = ios.GetFS()->Rename(IOS::PID_KERNEL, IOS::PID_KERNEL, tmp_path, tmd_path);
          {
            const std::string log_path2 = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
            File::IOFile lf(log_path2, "ab");
            if (lf)
            {
              const std::string line = fmt::format("[NPDEBUG] ChangeGame: rename result={} to '{}'\n",
                                                   static_cast<int>(rename_rc), tmd_path);
              lf.WriteBytes(line.data(), line.size());
            }
          }
        }
      }
      else
      {
        const std::string log_path2 = File::GetUserPath(D_LOGS_IDX) + "savehash8.txt";
        File::IOFile lf(log_path2, "ab");
        if (lf)
        {
          const std::string line = fmt::format("[NPDEBUG] ChangeGame: installed_tmd_exists=true for {:016x}\n",
                                              title_id);
          lf.WriteBytes(line.data(), line.size());
        }
      }
    }
  }

  // send changed game to clients
  sf::Packet spac;
  spac << MessageID::ChangeGame;
  SendSyncIdentifier(spac, m_selected_game_identifier);
  spac << m_selected_game_name;

  SendAsyncToClients(std::move(spac));

  return true;
}

// called from ---GUI--- thread
bool NetPlayServer::ComputeGameDigest(const SyncIdentifier& sync_identifier)
{
  sf::Packet spac;
  spac << MessageID::ComputeGameDigest;
  SendSyncIdentifier(spac, sync_identifier);

  SendAsyncToClients(std::move(spac));

  return true;
}

// called from ---GUI--- thread
bool NetPlayServer::AbortGameDigest()
{
  sf::Packet spac;
  spac << MessageID::GameDigestAbort;

  SendAsyncToClients(std::move(spac));
  return true;
}

// called from ---GUI--- thread
bool NetPlayServer::SetupNetSettings()
{
  INFO_LOG_FMT(NETPLAY, "Loading game settings for {:02x}.",
               fmt::join(m_selected_game_identifier.sync_hash, ""));

  NetPlay::NetSettings settings;

#if IS_SERVER
  // Server mode: use selected identifier directly, do not depend on GameFile
  Config::AddLayer(ConfigLoaders::GenerateGlobalGameConfigLoader(
      m_selected_game_identifier.game_id, m_selected_game_identifier.revision));
  Config::AddLayer(ConfigLoaders::GenerateLocalGameConfigLoader(
      m_selected_game_identifier.game_id, m_selected_game_identifier.revision));
#else
  const auto game = m_dialog->FindGameFile(m_selected_game_identifier);
  if (game == nullptr)
  {
    ERROR_LOG_FMT(NETPLAY, "Game {:02x} not found in game list.",
                  fmt::join(m_selected_game_identifier.sync_hash, ""));
    PanicAlertFmtT("Selected game doesn't exist in game list!");
    return false;
  }

  // Load GameINI so we can sync the settings from it
  Config::AddLayer(
      ConfigLoaders::GenerateGlobalGameConfigLoader(game->GetGameID(), game->GetRevision()));
  Config::AddLayer(
      ConfigLoaders::GenerateLocalGameConfigLoader(game->GetGameID(), game->GetRevision()));
#endif

  // Copy all relevant settings
  settings.cpu_thread = Config::Get(Config::MAIN_CPU_THREAD);
  settings.cpu_core = Config::Get(Config::MAIN_CPU_CORE);
  settings.enable_cheats = Config::AreCheatsEnabled();
  settings.enable_hardcore = AchievementManager::GetInstance().IsHardcoreModeActive();
  settings.selected_language = Config::Get(Config::MAIN_GC_LANGUAGE);
  settings.override_region_settings = Config::Get(Config::MAIN_OVERRIDE_REGION_SETTINGS);
  settings.dsp_hle = Config::Get(Config::MAIN_DSP_HLE);
  settings.dsp_enable_jit = Config::Get(Config::MAIN_DSP_JIT);
  settings.ram_override_enable = Config::Get(Config::MAIN_RAM_OVERRIDE_ENABLE);
  settings.mem1_size = Config::Get(Config::MAIN_MEM1_SIZE);
  settings.mem2_size = Config::Get(Config::MAIN_MEM2_SIZE);
  settings.fallback_region = Config::Get(Config::MAIN_FALLBACK_REGION);
  settings.allow_sd_writes = Config::Get(Config::MAIN_ALLOW_SD_WRITES);
  settings.oc_enable = Config::Get(Config::MAIN_OVERCLOCK_ENABLE);
  settings.oc_factor = Config::Get(Config::MAIN_OVERCLOCK);
  settings.vi_oc_enable = Config::Get(Config::MAIN_VI_OVERCLOCK_ENABLE);
  settings.vi_oc_factor = Config::Get(Config::MAIN_VI_OVERCLOCK);

  for (ExpansionInterface::Slot slot : ExpansionInterface::SLOTS)
  {
    ExpansionInterface::EXIDeviceType device;
    if (slot == ExpansionInterface::Slot::SP1)
    {
      // There's no way the BBA is going to sync, disable it
      device = ExpansionInterface::EXIDeviceType::None;
    }
    else
    {
      device = Config::Get(Config::GetInfoForEXIDevice(slot));
    }
    settings.exi_device[slot] = device;
  }

  settings.memcard_size_override = Config::Get(Config::MAIN_MEMORY_CARD_SIZE);

  for (size_t i = 0; i < Config::SYSCONF_SETTINGS.size(); ++i)
  {
    std::visit(
        [&](auto* info) {
          static_assert(sizeof(info->GetDefaultValue()) <= sizeof(u32));
          settings.sysconf_settings[i] = static_cast<u32>(Config::Get(*info));
        },
        Config::SYSCONF_SETTINGS[i].config_info);
  }

  settings.efb_access_enable = Config::Get(Config::GFX_HACK_EFB_ACCESS_ENABLE);
  settings.bbox_enable = Config::Get(Config::GFX_HACK_BBOX_ENABLE);
  settings.force_progressive = Config::Get(Config::GFX_HACK_FORCE_PROGRESSIVE);
  settings.efb_to_texture_enable = Config::Get(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM);
  settings.xfb_to_texture_enable = Config::Get(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM);
  settings.disable_copy_to_vram = Config::Get(Config::GFX_HACK_DISABLE_COPY_TO_VRAM);
  settings.immediate_xfb_enable = Config::Get(Config::GFX_HACK_IMMEDIATE_XFB);
  settings.efb_emulate_format_changes = Config::Get(Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES);
  settings.safe_texture_cache_color_samples =
      Config::Get(Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES);
  settings.perf_queries_enable = Config::Get(Config::GFX_PERF_QUERIES_ENABLE);
  settings.float_exceptions = Config::Get(Config::MAIN_FLOAT_EXCEPTIONS);
  settings.divide_by_zero_exceptions = Config::Get(Config::MAIN_DIVIDE_BY_ZERO_EXCEPTIONS);
  settings.fprf = Config::Get(Config::MAIN_FPRF);
  settings.accurate_nans = Config::Get(Config::MAIN_ACCURATE_NANS);
  settings.disable_icache = Config::Get(Config::MAIN_DISABLE_ICACHE);
  settings.sync_on_skip_idle = Config::Get(Config::MAIN_SYNC_ON_SKIP_IDLE);
  settings.sync_gpu = Config::Get(Config::MAIN_SYNC_GPU);
  settings.sync_gpu_max_distance = Config::Get(Config::MAIN_SYNC_GPU_MAX_DISTANCE);
  settings.sync_gpu_min_distance = Config::Get(Config::MAIN_SYNC_GPU_MIN_DISTANCE);
  settings.sync_gpu_overclock = Config::Get(Config::MAIN_SYNC_GPU_OVERCLOCK);
  settings.jit_follow_branch = Config::Get(Config::MAIN_JIT_FOLLOW_BRANCH);
  settings.fast_disc_speed = Config::Get(Config::MAIN_FAST_DISC_SPEED);
  settings.mmu = Config::Get(Config::MAIN_MMU);
  settings.fastmem = Config::Get(Config::MAIN_FASTMEM);
  settings.skip_ipl = Config::Get(Config::MAIN_SKIP_IPL) || !DoAllPlayersHaveIPLDump();
  settings.load_ipl_dump = Config::Get(Config::SESSION_LOAD_IPL_DUMP) && DoAllPlayersHaveIPLDump();
  settings.vertex_rounding = Config::Get(Config::GFX_HACK_VERTEX_ROUNDING);
  settings.internal_resolution = Config::Get(Config::GFX_EFB_SCALE);
  settings.efb_scaled_copy = Config::Get(Config::GFX_HACK_COPY_EFB_SCALED);
  settings.fast_depth_calc = Config::Get(Config::GFX_FAST_DEPTH_CALC);
  settings.enable_pixel_lighting = Config::Get(Config::GFX_ENABLE_PIXEL_LIGHTING);
  settings.widescreen_hack = Config::Get(Config::GFX_WIDESCREEN_HACK);
  settings.force_texture_filtering = Config::Get(Config::GFX_ENHANCE_FORCE_TEXTURE_FILTERING);
  settings.max_anisotropy = Config::Get(Config::GFX_ENHANCE_MAX_ANISOTROPY);
  settings.force_true_color = Config::Get(Config::GFX_ENHANCE_FORCE_TRUE_COLOR);
  settings.disable_copy_filter = Config::Get(Config::GFX_ENHANCE_DISABLE_COPY_FILTER);
  settings.disable_fog = Config::Get(Config::GFX_DISABLE_FOG);
  settings.arbitrary_mipmap_detection = Config::Get(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION);
  settings.arbitrary_mipmap_detection_threshold =
      Config::Get(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION_THRESHOLD);
  settings.enable_gpu_texture_decoding = Config::Get(Config::GFX_ENABLE_GPU_TEXTURE_DECODING);
  settings.defer_efb_copies = Config::Get(Config::GFX_HACK_DEFER_EFB_COPIES);
  settings.efb_access_tile_size = Config::Get(Config::GFX_HACK_EFB_ACCESS_TILE_SIZE);
  settings.efb_access_defer_invalidation = Config::Get(Config::GFX_HACK_EFB_DEFER_INVALIDATION);

  settings.savedata_load = Config::Get(Config::NETPLAY_SAVEDATA_LOAD);
  settings.savedata_write = settings.savedata_load && Config::Get(Config::NETPLAY_SAVEDATA_WRITE);
  settings.savedata_sync_all_wii =
      settings.savedata_load && Config::Get(Config::NETPLAY_SAVEDATA_SYNC_ALL_WII);

  settings.strict_settings_sync = Config::Get(Config::NETPLAY_STRICT_SETTINGS_SYNC);
  settings.sync_codes = Config::Get(Config::NETPLAY_SYNC_CODES);
  settings.golf_mode = Config::Get(Config::NETPLAY_NETWORK_MODE) == "golf";
  settings.use_fma = DoAllPlayersHaveHardwareFMA();
  settings.hide_remote_gbas = Config::Get(Config::NETPLAY_HIDE_REMOTE_GBAS);

  // Unload GameINI to restore things to normal
  Config::RemoveLayer(Config::LayerType::GlobalGame);
  Config::RemoveLayer(Config::LayerType::LocalGame);

  m_settings = settings;

  return true;
}

bool NetPlayServer::DoAllPlayersHaveIPLDump() const
{
  return std::ranges::all_of(m_players, [](const auto& p) { return p.second.has_ipl_dump; });
}

bool NetPlayServer::DoAllPlayersHaveHardwareFMA() const
{
  return std::ranges::all_of(m_players, [](const auto& p) { return p.second.has_hardware_fma; });
}

struct SaveSyncInfo
{
  u8 save_count = 0;
  std::shared_ptr<const UICommon::GameFile> game;
  bool has_wii_save = false;
  std::unique_ptr<IOS::HLE::FS::FileSystem> configured_fs;
  std::optional<std::vector<u8>> mii_data;
  std::vector<std::pair<u64, WiiSave::StoragePointer>> wii_saves;
  std::optional<DiscIO::Riivolution::SavegameRedirect> redirected_save;
};

// called from ---GUI--- thread
bool NetPlayServer::RequestStartGame()
{
  INFO_LOG_FMT(NETPLAY, "Start Game requested.");

  if (!SetupNetSettings())
    return false;

  bool start_now = true;

  if (m_settings.savedata_load)
  {
    auto save_sync_info = CollectSaveSyncInfo();
    if (!save_sync_info)
    {
      PanicAlertFmtT("Error collecting save data!");
      m_start_pending = false;
      return false;
    }

    if (save_sync_info->has_wii_save)
    {
      // Set titles for host-side loading in WiiRoot
      std::vector<u64> titles;
      for (const auto& title_id : std::views::keys(save_sync_info->wii_saves))
        titles.push_back(title_id);
      m_dialog->SetHostWiiSyncData(
          std::move(titles),
          save_sync_info->redirected_save ? save_sync_info->redirected_save->m_target_path : "");
    }

    if (m_players.size() > 1)
    {
      start_now = false;
      m_start_pending = true;
      if (!SyncSaveData(*save_sync_info))
      {
        PanicAlertFmtT("Error synchronizing save data!");
        m_start_pending = false;
        return false;
      }
    }
  }

  // Check To Send Codes to Clients
  if (m_settings.sync_codes && m_players.size() > 1)
  {
    start_now = false;
    m_start_pending = true;
    if (!SyncCodes())
    {
      PanicAlertFmtT("Error synchronizing cheat codes!");
      m_start_pending = false;
      return false;
    }
  }

  if (start_now)
  {
    return StartGame();
  }

  INFO_LOG_FMT(NETPLAY, "Waiting for data sync with clients.");
  return true;
}

// called from multiple threads
bool NetPlayServer::StartGame()
{
  INFO_LOG_FMT(NETPLAY, "Starting game.");

  m_timebase_by_frame.clear();
  m_desync_detected = false;
  std::lock_guard lkg(m_crit.game);
  // only used as an identifier, not time value, so truncation is fine
  m_current_game = static_cast<u32>(Common::Timer::NowMs());

  NetPlay::NetplayManager::GetInstance().resetClientsExceptHost_NoLock();

  // no change, just update with clients
  if (!m_host_input_authority)
    AdjustPadBufferSize(m_target_buffer_size);

  m_current_golfer = 1;
  m_pending_golfer = 0;

  const u64 initial_rtc = GetInitialNetPlayRTC();

#if IS_SERVER
  // Server mode: derive region directory without relying on GameFile.
  // Prefer configured fallback; region-specific handling is not required for hosting.
  const DiscIO::Region used_region = Config::Get(Config::MAIN_FALLBACK_REGION);
  const std::string region =
      Config::GetDirectoryForRegion(Config::ToGameCubeRegion(used_region));
#else
  const std::string region = Config::GetDirectoryForRegion(
      Config::ToGameCubeRegion(m_dialog->FindGameFile(m_selected_game_identifier)->GetRegion()));
#endif

  // load host's GC SRAM
  SConfig::GetInstance().m_strSRAM = File::GetUserPath(F_GCSRAM_IDX);
  InitSRAM(&m_settings.sram, SConfig::GetInstance().m_strSRAM);

  // tell clients to start game
  sf::Packet spac;
  spac << MessageID::StartGame;
  spac << m_current_game;
  spac << m_settings.cpu_thread;
  spac << m_settings.cpu_core;
  spac << m_settings.enable_cheats;
  spac << m_settings.enable_hardcore;
  spac << m_settings.selected_language;
  spac << m_settings.override_region_settings;
  spac << m_settings.dsp_enable_jit;
  spac << m_settings.dsp_hle;
  spac << m_settings.ram_override_enable;
  spac << m_settings.mem1_size;
  spac << m_settings.mem2_size;
  spac << m_settings.fallback_region;
  spac << m_settings.allow_sd_writes;
  spac << m_settings.oc_enable;
  spac << m_settings.oc_factor;
  spac << m_settings.vi_oc_enable;
  spac << m_settings.vi_oc_factor;

  for (auto slot : ExpansionInterface::SLOTS)
    spac << static_cast<int>(m_settings.exi_device[slot]);

  spac << m_settings.memcard_size_override;

  for (u32 value : m_settings.sysconf_settings)
    spac << value;

  spac << m_settings.efb_access_enable;
  spac << m_settings.bbox_enable;
  spac << m_settings.force_progressive;
  spac << m_settings.efb_to_texture_enable;
  spac << m_settings.xfb_to_texture_enable;
  spac << m_settings.disable_copy_to_vram;
  spac << m_settings.immediate_xfb_enable;
  spac << m_settings.efb_emulate_format_changes;
  spac << m_settings.safe_texture_cache_color_samples;
  spac << m_settings.perf_queries_enable;
  spac << m_settings.float_exceptions;
  spac << m_settings.divide_by_zero_exceptions;
  spac << m_settings.fprf;
  spac << m_settings.accurate_nans;
  spac << m_settings.disable_icache;
  spac << m_settings.sync_on_skip_idle;
  spac << m_settings.sync_gpu;
  spac << m_settings.sync_gpu_max_distance;
  spac << m_settings.sync_gpu_min_distance;
  spac << m_settings.sync_gpu_overclock;
  spac << m_settings.jit_follow_branch;
  spac << m_settings.fast_disc_speed;
  spac << m_settings.mmu;
  spac << m_settings.fastmem;
  spac << m_settings.skip_ipl;
  spac << m_settings.load_ipl_dump;
  spac << m_settings.vertex_rounding;
  spac << m_settings.internal_resolution;
  spac << m_settings.efb_scaled_copy;
  spac << m_settings.fast_depth_calc;
  spac << m_settings.enable_pixel_lighting;
  spac << m_settings.widescreen_hack;
  spac << m_settings.force_texture_filtering;
  spac << m_settings.max_anisotropy;
  spac << m_settings.force_true_color;
  spac << m_settings.disable_copy_filter;
  spac << m_settings.disable_fog;
  spac << m_settings.arbitrary_mipmap_detection;
  spac << m_settings.arbitrary_mipmap_detection_threshold;
  spac << m_settings.enable_gpu_texture_decoding;
  spac << m_settings.defer_efb_copies;
  spac << m_settings.efb_access_tile_size;
  spac << m_settings.efb_access_defer_invalidation;
  spac << m_settings.savedata_load;
  spac << m_settings.savedata_write;
  spac << m_settings.savedata_sync_all_wii;
  spac << m_settings.strict_settings_sync;
  spac << initial_rtc;
  spac << region;
  spac << m_settings.sync_codes;

  spac << m_settings.golf_mode;
  spac << m_settings.use_fma;
  spac << m_settings.hide_remote_gbas;

  for (size_t i = 0; i < sizeof(m_settings.sram); ++i)
    spac << m_settings.sram[i];

  SendAsyncToClients(std::move(spac));

  m_start_pending = false;
  m_is_running = true;

  return true;
}

void NetPlayServer::AbortGameStart()
{
  if (m_start_pending)
  {
    INFO_LOG_FMT(NETPLAY, "Aborting game start.");
    m_dialog->OnGameStartAborted();
    ChunkedDataAbort();
    m_start_pending = false;
  }
  else
  {
    INFO_LOG_FMT(NETPLAY, "Aborting game start but no game start pending.");
  }
}

// called from ---GUI--- thread
std::optional<SaveSyncInfo> NetPlayServer::CollectSaveSyncInfo()
{
  INFO_LOG_FMT(NETPLAY, "Collecting saves.");

  SaveSyncInfo sync_info;

  sync_info.save_count = 0;
  for (ExpansionInterface::Slot slot : ExpansionInterface::MEMCARD_SLOTS)
  {
    if (m_settings.exi_device[slot] == ExpansionInterface::EXIDeviceType::MemoryCard)
    {
      INFO_LOG_FMT(NETPLAY, "Adding memory card (raw) in slot {}.", slot);
      ++sync_info.save_count;
    }
    else if (Config::Get(Config::GetInfoForEXIDevice(slot)) ==
             ExpansionInterface::EXIDeviceType::MemoryCardFolder)
    {
      INFO_LOG_FMT(NETPLAY, "Adding memory card (folder) in slot {}.", slot);
      ++sync_info.save_count;
    }
  }

#if !IS_SERVER
  sync_info.game = m_dialog->FindGameFile(m_selected_game_identifier);
  if (sync_info.game == nullptr)
  {
    PanicAlertFmtT("Selected game doesn't exist in game list!");
    return std::nullopt;
  }
#endif

  sync_info.has_wii_save = false;
#if !IS_SERVER
  if (m_settings.savedata_load && (sync_info.game->GetPlatform() == DiscIO::Platform::WiiDisc ||
                                   sync_info.game->GetPlatform() == DiscIO::Platform::WiiWAD ||
                                   sync_info.game->GetPlatform() == DiscIO::Platform::ELFOrDOL))
  {
    INFO_LOG_FMT(NETPLAY, "Adding Wii saves.");

    sync_info.has_wii_save = true;
    ++sync_info.save_count;

    sync_info.configured_fs = IOS::HLE::FS::MakeFileSystem(IOS::HLE::FS::Location::Configured);
    if (m_settings.savedata_sync_all_wii)
    {
      IOS::HLE::Kernel ios;
      for (const u64 title : ios.GetESCore().GetInstalledTitles())
      {
        auto save = WiiSave::MakeNandStorage(sync_info.configured_fs.get(), title);
        if (save && save->ReadHeader().has_value() && save->ReadBkHeader().has_value() &&
            save->ReadFiles().has_value())
        {
          sync_info.wii_saves.emplace_back(title, std::move(save));
        }
        else
        {
          INFO_LOG_FMT(NETPLAY, "Skipping Wii save of title {:016x}.", title);
        }
      }
    }
    else if (sync_info.game->GetPlatform() == DiscIO::Platform::WiiDisc ||
             sync_info.game->GetPlatform() == DiscIO::Platform::WiiWAD)
    {
      auto save =
          WiiSave::MakeNandStorage(sync_info.configured_fs.get(), sync_info.game->GetTitleID());
      sync_info.wii_saves.emplace_back(sync_info.game->GetTitleID(), std::move(save));
    }

    {
      auto file = sync_info.configured_fs->OpenFile(
          IOS::PID_KERNEL, IOS::PID_KERNEL, Common::GetMiiDatabasePath(), IOS::HLE::FS::Mode::Read);
      if (file)
      {
        std::vector<u8> file_data(file->GetStatus()->size);
        if (!file->Read(file_data.data(), file_data.size()))
          return std::nullopt;
        sync_info.mii_data = std::move(file_data);
      }
    }

    if (sync_info.game->GetBlobType() == DiscIO::BlobType::MOD_DESCRIPTOR)
    {
      auto boot_params = BootParameters::GenerateFromFile(sync_info.game->GetFilePath());
      if (boot_params)
      {
        sync_info.redirected_save =
            DiscIO::Riivolution::ExtractSavegameRedirect(boot_params->riivolution_patches);
      }
    }
  }
#else
  if (m_settings.savedata_load)
  {
    INFO_LOG_FMT(NETPLAY, "Adding Wii saves (server mode).");

    sync_info.has_wii_save = true;
    ++sync_info.save_count;

    sync_info.configured_fs = IOS::HLE::FS::MakeFileSystem(IOS::HLE::FS::Location::Configured);
    {
      IOS::HLE::Kernel ios;
      if (m_settings.savedata_sync_all_wii)
      {
        for (const u64 title : ios.GetESCore().GetInstalledTitles())
        {
          auto save = WiiSave::MakeNandStorage(sync_info.configured_fs.get(), title);
          if (save && save->ReadHeader().has_value() && save->ReadBkHeader().has_value() &&
              save->ReadFiles().has_value())
          {
            sync_info.wii_saves.emplace_back(title, std::move(save));
          }
          else
          {
            INFO_LOG_FMT(NETPLAY, "Skipping Wii save of title {:016x}.", title);
          }
        }
      }
      else
      {
        for (const u64 title : ios.GetESCore().GetInstalledTitles())
        {
          const auto tmd = ios.GetESCore().FindInstalledTMD(title);
          if (!tmd.IsValid())
            continue;
          if (tmd.GetGameID() != m_selected_game_identifier.game_id)
            continue;

          auto save = WiiSave::MakeNandStorage(sync_info.configured_fs.get(), title);
          if (save && save->ReadHeader().has_value() && save->ReadBkHeader().has_value() &&
              save->ReadFiles().has_value())
          {
            sync_info.wii_saves.emplace_back(title, std::move(save));
          }
          else
          {
            INFO_LOG_FMT(NETPLAY, "Skipping Wii save of title {:016x}.", title);
          }
        }
      }
    }

    {
      auto file = sync_info.configured_fs->OpenFile(
          IOS::PID_KERNEL, IOS::PID_KERNEL, Common::GetMiiDatabasePath(), IOS::HLE::FS::Mode::Read);
      if (file)
      {
        std::vector<u8> file_data(file->GetStatus()->size);
        if (!file->Read(file_data.data(), file_data.size()))
          return std::nullopt;
        sync_info.mii_data = std::move(file_data);
      }
    }
  }
#endif

  for (size_t i = 0; i < m_gba_config.size(); ++i)
  {
    const auto& config = m_gba_config[i];
    if (config.enabled && config.has_rom)
    {
      INFO_LOG_FMT(NETPLAY, "Adding GBA save in slot {}.", i);
      ++sync_info.save_count;
    }
  }

  return sync_info;
}

// called from ---GUI--- thread
bool NetPlayServer::SyncSaveData(const SaveSyncInfo& sync_info)
{
  INFO_LOG_FMT(NETPLAY, "Sending {} savegame chunks to clients.", sync_info.save_count);

  // We're about to sync saves, so set m_saves_synced to false (waits to start game)
  m_saves_synced = false;

  m_save_data_synced_players = 0;

  {
    sf::Packet pac;
    pac << MessageID::SyncSaveData;
    pac << SyncSaveDataID::Notify;
    pac << sync_info.save_count;

    // send this on the chunked data channel to ensure it's sequenced properly
    SendAsyncToClients(std::move(pac), 0, CHUNKED_DATA_CHANNEL);
  }

  if (sync_info.save_count == 0)
    return true;

#if IS_SERVER
  // Server mode: compute region without GameFile
  const DiscIO::Region game_region = Config::Get(Config::MAIN_FALLBACK_REGION);
  const auto gamecube_region = Config::ToGameCubeRegion(game_region);
  const std::string region = Config::GetDirectoryForRegion(gamecube_region);
#else
  const auto game_region = sync_info.game->GetRegion();
  const auto gamecube_region = Config::ToGameCubeRegion(game_region);
  const std::string region = Config::GetDirectoryForRegion(gamecube_region);
#endif

  for (ExpansionInterface::Slot slot : ExpansionInterface::MEMCARD_SLOTS)
  {
    const bool is_slot_a = slot == ExpansionInterface::Slot::A;

    if (m_settings.exi_device[slot] == ExpansionInterface::EXIDeviceType::MemoryCard)
    {
      const int size_override = m_settings.memcard_size_override;
      const u16 card_size_mbits =
          size_override >= 0 && size_override <= 4 ?
              static_cast<u16>(Memcard::MBIT_SIZE_MEMORY_CARD_59 << size_override) :
              Memcard::MBIT_SIZE_MEMORY_CARD_2043;
      const std::string path = Config::GetMemcardPath(slot, game_region, card_size_mbits);

      sf::Packet pac;
      pac << MessageID::SyncSaveData;
      pac << SyncSaveDataID::RawData;
      pac << is_slot_a << region << size_override;

      if (File::Exists(path))
      {
        INFO_LOG_FMT(NETPLAY, "Sending data of raw memcard {} in slot {}.", path,
                     is_slot_a ? 'A' : 'B');
        if (!CompressFileIntoPacket(path, pac))
          return false;
      }
      else
      {
        // No file, so we'll say the size is 0
        INFO_LOG_FMT(NETPLAY, "Sending empty marker for raw memcard {} in slot {}.", path,
                     is_slot_a ? 'A' : 'B');
        pac << u64{0};
      }

      SendChunkedToClients(std::move(pac), 1,
                           fmt::format("Memory Card {} Synchronization", is_slot_a ? 'A' : 'B'));
    }
    else if (Config::Get(Config::GetInfoForEXIDevice(slot)) ==
             ExpansionInterface::EXIDeviceType::MemoryCardFolder)
    {
      const std::string path = Config::GetGCIFolderPath(slot, gamecube_region);

      sf::Packet pac;
      pac << MessageID::SyncSaveData;
      pac << SyncSaveDataID::GCIData;
      pac << is_slot_a;

      if (File::IsDirectory(path))
      {
        std::vector<std::string> files =
#if IS_SERVER
            GCMemcardDirectory::GetFileNamesForGameID(path + DIR_SEP, m_selected_game_identifier.game_id);
#else
            GCMemcardDirectory::GetFileNamesForGameID(path + DIR_SEP, sync_info.game->GetGameID());
#endif

        INFO_LOG_FMT(NETPLAY, "Sending data of GCI memcard {} in slot {} ({} files).", path,
                     is_slot_a ? 'A' : 'B', files.size());

        pac << static_cast<u8>(files.size());

        for (const std::string& file : files)
        {
          const std::string filename = file.substr(file.find_last_of('/') + 1);
          INFO_LOG_FMT(NETPLAY, "Sending GCI {}.", filename);
          pac << filename;
          if (!CompressFileIntoPacket(file, pac))
            return false;
        }
      }
      else
      {
        INFO_LOG_FMT(NETPLAY, "Sending empty marker for GCI memcard {} in slot {}.", path,
                     is_slot_a ? 'A' : 'B');

        pac << static_cast<u8>(0);
      }

      SendChunkedToClients(std::move(pac), 1,
                           fmt::format("GCI Folder {} Synchronization", is_slot_a ? 'A' : 'B'));
    }
  }

  if (sync_info.has_wii_save)
  {
    sf::Packet pac;
    pac << MessageID::SyncSaveData;
    pac << SyncSaveDataID::WiiData;

    // Shove the Mii data into the start the packet
    if (sync_info.mii_data)
    {
      INFO_LOG_FMT(NETPLAY, "Sending Mii data.");
      pac << true;
      if (!CompressBufferIntoPacket(*sync_info.mii_data, pac))
        return false;
    }
    else
    {
      INFO_LOG_FMT(NETPLAY, "Not sending Mii data.");
      pac << false;  // no mii data
    }

    // Carry on with the save files
    INFO_LOG_FMT(NETPLAY, "Sending {} Wii saves.", sync_info.wii_saves.size());
    pac << static_cast<u32>(sync_info.wii_saves.size());

    for (const auto& [title_id, storage] : sync_info.wii_saves)
    {
      pac << u64{title_id};

      if (storage->SaveExists())
      {
        const std::optional<WiiSave::Header> header = storage->ReadHeader();
        const std::optional<WiiSave::BkHeader> bk_header = storage->ReadBkHeader();
        const std::optional<std::vector<WiiSave::Storage::SaveFile>> files = storage->ReadFiles();
        if (!header || !bk_header || !files)
        {
          INFO_LOG_FMT(NETPLAY, "Wii save of title {:016x} is corrupted.", title_id);
          return false;
        }

        INFO_LOG_FMT(NETPLAY, "Sending Wii save of title {:016x}.", title_id);
        pac << true;  // save exists

        // Header
        pac << u64{header->tid};
        pac << header->banner_size << header->permissions << header->unk1;
        for (u8 byte : header->md5)
          pac << byte;
        pac << header->unk2;
        for (size_t i = 0; i < header->banner_size; i++)
          pac << header->banner[i];

        // BkHeader
        pac << bk_header->size << bk_header->magic << bk_header->ngid << bk_header->number_of_files
            << bk_header->size_of_files << bk_header->unk1 << bk_header->unk2
            << bk_header->total_size;
        for (u8 byte : bk_header->unk3)
          pac << byte;
        pac << u64{bk_header->tid};
        for (u8 byte : bk_header->mac_address)
          pac << byte;

        // Files
        for (const WiiSave::Storage::SaveFile& file : *files)
        {
          INFO_LOG_FMT(NETPLAY, "Sending Wii save data of type {} at {}",
                       static_cast<u8>(file.type), file.path);

          pac << file.mode << file.attributes << file.type << file.path;

          if (file.type == WiiSave::Storage::SaveFile::Type::File)
          {
            const std::optional<std::vector<u8>>& data = *file.data;
            if (!data || !CompressBufferIntoPacket(*data, pac))
              return false;
          }
        }
      }
      else
      {
        INFO_LOG_FMT(NETPLAY, "No data for Wii save of title {:016x}.", title_id);
        pac << false;  // save does not exist
      }
    }

    if (sync_info.redirected_save)
    {
      INFO_LOG_FMT(NETPLAY, "Sending redirected save at {}.",
                   sync_info.redirected_save->m_target_path);
      pac << true;
      if (!CompressFolderIntoPacket(sync_info.redirected_save->m_target_path, pac))
        return false;
    }
    else
    {
      INFO_LOG_FMT(NETPLAY, "Not sending redirected save.");
      pac << false;  // no redirected save
    }

    SendChunkedToClients(std::move(pac), 1, "Wii Save Synchronization");
  }

  for (size_t i = 0; i < m_gba_config.size(); ++i)
  {
    if (m_gba_config[i].enabled && m_gba_config[i].has_rom)
    {
      sf::Packet pac;
      pac << MessageID::SyncSaveData;
      pac << SyncSaveDataID::GBAData;
      pac << static_cast<u8>(i);

      std::string path;
#ifdef HAS_LIBMGBA
      path = HW::GBA::Core::GetSavePath(Config::Get(Config::MAIN_GBA_ROM_PATHS[i]),
                                        static_cast<int>(i));
#endif
      if (File::Exists(path))
      {
        INFO_LOG_FMT(NETPLAY, "Sending data of GBA save at {} for slot {}.", path, i);
        if (!CompressFileIntoPacket(path, pac))
          return false;
      }
      else
      {
        // No file, so we'll say the size is 0
        INFO_LOG_FMT(NETPLAY, "Sending empty marker for GBA save at {} for slot {}.", path, i);
        pac << u64{0};
      }

      SendChunkedToClients(std::move(pac), 1,
                           fmt::format("GBA{} Save File Synchronization", i + 1));
    }
  }

  return true;
}

bool NetPlayServer::SyncCodes()
{
  INFO_LOG_FMT(NETPLAY, "Sending codes to clients.");

  // Sync Codes is ticked, so set m_codes_synced to false
  m_codes_synced = false;

  // Find all INI files
#if IS_SERVER
  const auto& game_id = m_selected_game_identifier.game_id;
  const auto revision = m_selected_game_identifier.revision;
#else
  // Get Game Path
  const auto game = m_dialog->FindGameFile(m_selected_game_identifier);
  if (game == nullptr)
  {
    PanicAlertFmtT("Selected game doesn't exist in game list!");
    return false;
  }
  const auto game_id = game->GetGameID();
  const auto revision = game->GetRevision();
#endif
  Common::IniFile globalIni;
  for (const std::string& filename : ConfigLoaders::GetGameIniFilenames(game_id, revision))
    globalIni.Load(File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + filename, true);
  Common::IniFile localIni;
  for (const std::string& filename : ConfigLoaders::GetGameIniFilenames(game_id, revision))
    localIni.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + filename, true);

  // Initialize Number of Synced Players
  m_codes_synced_players = 0;

  // Notify Clients of Incoming Code Sync
  {
    sf::Packet pac;
    pac << MessageID::SyncCodes;
    pac << SyncCodeID::Notify;
    SendAsyncToClients(std::move(pac));
  }
  // Sync Gecko Codes
  {
    std::vector<Gecko::GeckoCode> codes = Gecko::LoadCodes(globalIni, localIni);

#ifdef USE_RETRO_ACHIEVEMENTS
    AchievementManager::GetInstance().FilterApprovedGeckoCodes(codes, game_id, revision);
#endif  // USE_RETRO_ACHIEVEMENTS

    // Create a Gecko Code Vector with just the active codes
    std::vector<Gecko::GeckoCode> active_codes = Gecko::SetAndReturnActiveCodes(codes);

    // Determine Codelist Size
    u16 codelines = 0;
    for (const Gecko::GeckoCode& active_code : active_codes)
    {
      INFO_LOG_FMT(NETPLAY, "Indexing {}", active_code.name);
      for (const Gecko::GeckoCode::Code& code : active_code.codes)
      {
        INFO_LOG_FMT(NETPLAY, "{:08x} {:08x}", code.address, code.data);
        codelines++;
      }
    }

    // Output codelines to send
    INFO_LOG_FMT(NETPLAY, "Sending {} Gecko codelines", codelines);

    // Send initial packet. Notify of the sync operation and total number of lines being sent.
    {
      sf::Packet pac;
      pac << MessageID::SyncCodes;
      pac << SyncCodeID::NotifyGecko;
      pac << codelines;
      SendAsyncToClients(std::move(pac));
    }

    // Send entire codeset in the second packet
    {
      sf::Packet pac;
      pac << MessageID::SyncCodes;
      pac << SyncCodeID::GeckoData;
      // Iterate through the active code vector and send each codeline
      for (const Gecko::GeckoCode& active_code : active_codes)
      {
        INFO_LOG_FMT(NETPLAY, "Sending {}", active_code.name);
        for (const Gecko::GeckoCode::Code& code : active_code.codes)
        {
          INFO_LOG_FMT(NETPLAY, "{:08x} {:08x}", code.address, code.data);
          pac << code.address;
          pac << code.data;
        }
      }
      SendAsyncToClients(std::move(pac));
    }
  }

  // Sync AR Codes
  {
    std::vector<ActionReplay::ARCode> codes = ActionReplay::LoadCodes(globalIni, localIni);
#ifdef USE_RETRO_ACHIEVEMENTS
    AchievementManager::GetInstance().FilterApprovedARCodes(codes, game_id, revision);
#endif  // USE_RETRO_ACHIEVEMENTS
    // Create an AR Code Vector with just the active codes
    std::vector<ActionReplay::ARCode> active_codes = ActionReplay::ApplyAndReturnCodes(codes);

    // Determine Codelist Size
    u16 codelines = 0;
    for (const ActionReplay::ARCode& active_code : active_codes)
    {
      INFO_LOG_FMT(NETPLAY, "Indexing {}", active_code.name);
      for (const ActionReplay::AREntry& op : active_code.ops)
      {
        INFO_LOG_FMT(NETPLAY, "{:08x} {:08x}", op.cmd_addr, op.value);
        codelines++;
      }
    }

    // Output codelines to send
    INFO_LOG_FMT(NETPLAY, "Sending {} AR codelines", codelines);

    // Send initial packet. Notify of the sync operation and total number of lines being sent.
    {
      sf::Packet pac;
      pac << MessageID::SyncCodes;
      pac << SyncCodeID::NotifyAR;
      pac << codelines;
      SendAsyncToClients(std::move(pac));
    }

    // Send entire codeset in the second packet
    {
      sf::Packet pac;
      pac << MessageID::SyncCodes;
      pac << SyncCodeID::ARData;
      // Iterate through the active code vector and send each codeline
      for (const ActionReplay::ARCode& active_code : active_codes)
      {
        INFO_LOG_FMT(NETPLAY, "Sending {}", active_code.name);
        for (const ActionReplay::AREntry& op : active_code.ops)
        {
          INFO_LOG_FMT(NETPLAY, "{:08x} {:08x}", op.cmd_addr, op.value);
          pac << op.cmd_addr;
          pac << op.value;
        }
      }
      SendAsyncToClients(std::move(pac));
    }
  }

  return true;
}

void NetPlayServer::CheckSyncAndStartGame()
{
  if (m_saves_synced && m_codes_synced)
  {
    INFO_LOG_FMT(NETPLAY, "Synchronized, starting game.");
    StartGame();
  }
  else
  {
    INFO_LOG_FMT(NETPLAY, "Not synchronized.");
  }
}

u64 NetPlayServer::GetInitialNetPlayRTC() const
{
  if (Config::Get(Config::MAIN_CUSTOM_RTC_ENABLE))
    return Config::Get(Config::MAIN_CUSTOM_RTC_VALUE);

  return Common::Timer::GetLocalTimeSinceJan1970();
}

// called from multiple threads
void NetPlayServer::SendToClients(const sf::Packet& packet, const PlayerId skip_pid,
                                  const u8 channel_id)
{
  for (auto& p : std::views::values(m_players))
  {
    if (p.pid && p.pid != skip_pid)
    {
      Send(p.socket, packet, channel_id);
    }
  }
}

void NetPlayServer::Send(ENetPeer* socket, const sf::Packet& packet, const u8 channel_id)
{
  Common::ENet::SendPacket(socket, packet, channel_id);
}

void NetPlayServer::KickPlayer(PlayerId player)
{
  for (auto& current_player : std::views::values(m_players))
  {
    if (current_player.pid == player)
    {
      enet_peer_disconnect(current_player.socket, 0);
      return;
    }
  }
}

bool NetPlayServer::PlayerHasControllerMapped(const PlayerId pid) const
{
  const auto mapping_matches_player_id = [pid](const PlayerId& mapping) { return mapping == pid; };

  return std::ranges::any_of(m_pad_map, mapping_matches_player_id) ||
         std::ranges::any_of(m_wiimote_map, mapping_matches_player_id);
}

void NetPlayServer::AssignNewUserAPad(const Client& player)
{
  for (PlayerId& mapping : m_pad_map)
  {
    // 0 means unmapped
    if (mapping == 0)
    {
      mapping = player.pid;
      break;
    }
  }
}

PlayerId NetPlayServer::GiveFirstAvailableIDTo(ENetPeer* player)
{
  PlayerId pid = 1;
  for (auto i = m_players.begin(); i != m_players.end(); ++i)
  {
    if (i->second.pid == pid)
    {
      pid++;
      i = m_players.begin();
    }
  }
  player->data = new PlayerId(pid);
  return pid;
}

template <typename... Data>
void NetPlayServer::SendResponseToPlayer(const Client& player, const MessageID message_id,
                                         Data&&... data_to_send)
{
  sf::Packet response;
  response << message_id;
  // this is a C++17 fold expression used to call the << operator for all of the data
  (response << ... << std::forward<Data>(data_to_send));

  Send(player.socket, response);
}

template <typename... Data>
void NetPlayServer::SendResponseToAllPlayers(const MessageID message_id, Data&&... data_to_send)
{
  sf::Packet response;
  response << message_id;
  // this is a C++17 fold expression used to call the << operator for all of the data
  (response << ... << std::forward<Data>(data_to_send));

  SendToClients(response);
}

u16 NetPlayServer::GetPort() const
{
  return m_server->address.port;
}

// called from ---GUI--- thread
std::unordered_set<std::string> NetPlayServer::GetInterfaceSet() const
{
  std::unordered_set<std::string> result;
  for (const auto& list_entry : GetInterfaceListInternal())
    result.emplace(list_entry.first);
  return result;
}

// called from ---GUI--- thread
std::string NetPlayServer::GetInterfaceHost(const std::string& inter) const
{
  char buf[16]{};
  fmt::format_to_n(buf, sizeof(buf) - 1, ":{}", GetPort());

  auto lst = GetInterfaceListInternal();
  for (const auto& list_entry : lst)
  {
    if (list_entry.first == inter)
    {
      return list_entry.second + buf;
    }
  }
  return "?";
}

// called from ---GUI--- thread
std::vector<std::pair<std::string, std::string>> NetPlayServer::GetInterfaceListInternal() const
{
  std::vector<std::pair<std::string, std::string>> result;
#if defined(_WIN32)
  ULONG buffer_size = 0;
  GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &buffer_size);

  std::vector<char> buffer(buffer_size);
  auto* const adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
  const ULONG adapters_result = GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &buffer_size);
  if (adapters_result == NO_ERROR)
  {
    for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next)
    {
      if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
        continue;

      auto* const unicast = adapter->FirstUnicastAddress;
      if (!unicast)
        continue;

      char addr_str[INET_ADDRSTRLEN] = {};
      inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr)->sin_addr,
                addr_str, sizeof(addr_str));
      result.emplace_back(WStringToUTF8(adapter->FriendlyName), addr_str);
    }
  }
  else
  {
    WARN_LOG_FMT(NETPLAY, "GetAdaptersAddresses: {}", adapters_result);
  }
#elif defined(ANDROID)
// Android has no getifaddrs for some stupid reason.  If this
// functionality ends up actually being used on Android, fix this.
#else
  ifaddrs* ifp = nullptr;
  char buf[512];
  if (getifaddrs(&ifp) != -1)
  {
    for (ifaddrs* curifp = ifp; curifp; curifp = curifp->ifa_next)
    {
      sockaddr* sa = curifp->ifa_addr;

      if (sa == nullptr)
        continue;
      if (sa->sa_family != AF_INET)
        continue;
      sockaddr_in* sai = (struct sockaddr_in*)sa;
      if (ntohl(((struct sockaddr_in*)sa)->sin_addr.s_addr) == 0x7f000001)
        continue;
      const char* ip = inet_ntop(sa->sa_family, &sai->sin_addr, buf, sizeof(buf));
      if (ip == nullptr)
        continue;
      result.emplace_back(std::make_pair(curifp->ifa_name, ip));
    }
    freeifaddrs(ifp);
  }
#endif
  if (result.empty())
    result.emplace_back(std::make_pair("!local!", "127.0.0.1"));
  return result;
}

// called from ---Chunked Data--- thread
void NetPlayServer::ChunkedDataThreadFunc()
{
  INFO_LOG_FMT(NETPLAY, "Starting Chunked Data Thread.");

  while (m_do_loop)
  {
    m_chunked_data_event.Wait();

    if (m_abort_chunked_data)
    {
      // thread-safe clear
      while (!m_chunked_data_queue.Empty())
        m_chunked_data_queue.Pop();

      m_abort_chunked_data = false;
    }

    while (!m_chunked_data_queue.Empty())
    {
      if (!m_do_loop)
        return;
      if (m_abort_chunked_data)
        break;
      auto& e = m_chunked_data_queue.Front();
      const u32 id = m_next_chunked_data_id++;

      m_chunked_data_complete_count[id] = 0;
      size_t player_count;
      {
        std::vector<int> players;
        if (e.target_mode == TargetMode::Only)
        {
          players.push_back(e.target_pid);
        }
        else
        {
          for (auto& pl : std::views::values(m_players))
          {
            if (pl.pid != e.target_pid)
              players.push_back(pl.pid);
          }
        }
        player_count = players.size();

        INFO_LOG_FMT(NETPLAY, "Informing players {} of data chunk {} start.",
                     fmt::join(players, ", "), id);

        sf::Packet pac;
        pac << MessageID::ChunkedDataStart;
        pac << id << e.title << u64{e.packet.getDataSize()};

        ChunkedDataSend(std::move(pac), e.target_pid, e.target_mode);

        if (e.target_mode == TargetMode::AllExcept && e.target_pid == 1)
          m_dialog->ShowChunkedProgressDialog(e.title, e.packet.getDataSize(), players);
      }

      const bool enable_limit = Config::Get(Config::NETPLAY_ENABLE_CHUNKED_UPLOAD_LIMIT);
      const float bytes_per_second =
          (std::max(Config::Get(Config::NETPLAY_CHUNKED_UPLOAD_LIMIT), 1u) / 8.0f) * 1024.0f;
      const std::chrono::duration<double> send_interval(CHUNKED_DATA_UNIT_SIZE / bytes_per_second);
      bool skip_wait = false;
      size_t index = 0;
      do
      {
        if (!m_do_loop)
          return;
        if (m_abort_chunked_data)
        {
          INFO_LOG_FMT(NETPLAY, "Informing players of data chunk {} abort.", id);

          sf::Packet pac;
          pac << MessageID::ChunkedDataAbort;
          pac << id;
          ChunkedDataSend(std::move(pac), e.target_pid, e.target_mode);
          break;
        }
        if (e.target_mode == TargetMode::Only)
        {
          if (!m_players.contains(e.target_pid))
          {
            skip_wait = true;
            break;
          }
        }

        auto start = std::chrono::steady_clock::now();

        sf::Packet pac;
        pac << MessageID::ChunkedDataPayload;
        pac << id;
        size_t len = std::min(CHUNKED_DATA_UNIT_SIZE, e.packet.getDataSize() - index);
        pac.append(static_cast<const u8*>(e.packet.getData()) + index, len);

        INFO_LOG_FMT(NETPLAY, "Sending data chunk of {} ({} bytes at {}/{}).", id, len, index,
                     e.packet.getDataSize());

        ChunkedDataSend(std::move(pac), e.target_pid, e.target_mode);
        index += CHUNKED_DATA_UNIT_SIZE;

        if (enable_limit)
        {
          std::chrono::duration<double> delta = std::chrono::steady_clock::now() - start;
          std::this_thread::sleep_for(send_interval - delta);
        }
      } while (index < e.packet.getDataSize());

      if (!m_abort_chunked_data)
      {
        INFO_LOG_FMT(NETPLAY, "Informing players of data chunk {} end.", id);

        sf::Packet pac;
        pac << MessageID::ChunkedDataEnd;
        pac << id;
        ChunkedDataSend(std::move(pac), e.target_pid, e.target_mode);
      }

      while (m_chunked_data_complete_count[id] < player_count && m_do_loop &&
             !m_abort_chunked_data && !skip_wait)
        m_chunked_data_complete_event.Wait();
      m_chunked_data_complete_count.erase(id);
      m_dialog->HideChunkedProgressDialog();

      m_chunked_data_queue.Pop();
    }
  }

  INFO_LOG_FMT(NETPLAY, "Stopping Chunked Data Thread.");
}

// called from ---Chunked Data--- thread
void NetPlayServer::ChunkedDataSend(sf::Packet&& packet, const PlayerId pid,
                                    const TargetMode target_mode)
{
  if (target_mode == TargetMode::Only)
  {
    SendAsync(std::move(packet), pid, CHUNKED_DATA_CHANNEL);
  }
  else
  {
    SendAsyncToClients(std::move(packet), pid, CHUNKED_DATA_CHANNEL);
  }
}

void NetPlayServer::ChunkedDataAbort()
{
  m_abort_chunked_data = true;
  m_chunked_data_event.Set();
  m_chunked_data_complete_event.Set();
}

// --- 实现新消息的处理函数 ---
void NetPlayServer::OnRequestStartGameClient(Client& player)
{

  // if (m_dialog && m_dialog->IsSharingEnabled())
  if (m_dialog)
  {
    // 如果是共享服务器，允许客户端启动游戏
    RequestStartGame();
  }
  else
  {
    // 如果不是共享服务器，忽略请求或发送错误消息
  }

  // RequestStartGame();
}

}  // namespace NetPlay
