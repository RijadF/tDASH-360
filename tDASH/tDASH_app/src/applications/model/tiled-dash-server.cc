#include "tiled-dash-server.h"
#include "ns3/callback.h"
#include "ns3/config.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/tcp-socket-factory.h"
// #include "ns3/tcp-socket.h" // Already included via tiled-dash-server.h indirectly or not strictly needed here
#include "ns3/uinteger.h"
#include "ns3/buffer.h" // For Buffer::Iterator if used, though not directly here
#include <fstream>
#include <sstream>
#include <vector>
#include <string> // For std::to_string

NS_LOG_COMPONENT_DEFINE("TiledDashServer");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(TiledDashServer);

TiledDashServer::TiledDashServer() : m_initialSocket(nullptr) {m_logFilePath = "tile_server_log.csv";}

TypeId TiledDashServer::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::TiledDashServer")
            .SetParent<Application>()
            .AddConstructor<TiledDashServer>()
            .AddAttribute("ServerFolder", "The folder containing video files.", StringValue("."),
                          MakeStringAccessor(&TiledDashServer::m_serverFolder), 
                          MakeStringChecker())
            .AddAttribute("LocalAddress", "The local address of the server.", AddressValue(),
                          MakeAddressAccessor(&TiledDashServer::m_localAddress), MakeAddressChecker())
            .AddAttribute("LocalPort", "Port on which the application listens for incoming packets.",
                          UintegerValue(80), MakeUintegerAccessor(&TiledDashServer::m_localPort),
                          MakeUintegerChecker<uint16_t>())
            .AddTraceSource("Tx", "A packet has been sent.", MakeTraceSourceAccessor(&TiledDashServer::m_txTrace),
                              "ns3::Packet::TracedCallback")
            .AddTraceSource("Rx", "A packet has been received.", MakeTraceSourceAccessor(&TiledDashServer::m_rxTrace),
                              "ns3::Packet::PacketAddressTracedCallback")
            .AddTraceSource("RxDelay", "A packet has been received with delay information.",
                              MakeTraceSourceAccessor(&TiledDashServer::m_rxDelayTrace),
                              "ns3::Application::DelayAddressCallback");
    return tid;
}

void TiledDashServer::SetServerFolder(const std::string& folder) {
    m_serverFolder = folder;
}

std::string TiledDashServer::GetServerFolder() const {
    return m_serverFolder;
}

void TiledDashServer::DoDispose() {
    NS_LOG_FUNCTION(this);
    if (!Simulator::IsFinished()) {
        StopApplication();
    }
    Application::DoDispose();
}

void TiledDashServer::StartApplication() {
    NS_LOG_FUNCTION(this);
    if (!m_initialSocket) {
        m_initialSocket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());

        if (Ipv4Address::IsMatchingType(m_localAddress)) {
            const Ipv4Address ipv4 = Ipv4Address::ConvertFrom(m_localAddress);
            const InetSocketAddress inetSocket = InetSocketAddress(ipv4, m_localPort);
            m_initialSocket->Bind(inetSocket);
        } else if (Ipv6Address::IsMatchingType(m_localAddress)) { //needs to be tested with ipv6 ip address
            const Ipv6Address ipv6 = Ipv6Address::ConvertFrom(m_localAddress);
            const Inet6SocketAddress inet6Socket = Inet6SocketAddress(ipv6, m_localPort);
            m_initialSocket->Bind(inet6Socket);
        }
        m_initialSocket->Listen();
        m_initialSocket->SetAcceptCallback(
            MakeCallback(&TiledDashServer::ConnectionRequestCallback, this),
            MakeCallback(&TiledDashServer::NewConnectionCreatedCallback, this));
    }
}
// Add FlushTileLog implementation
void TiledDashServer::FlushTileLog() {
    std::ofstream logFile(m_logFilePath);
    
    if (!logFile.is_open()) {
        NS_LOG_ERROR("Failed to open log file: " << m_logFilePath);
        return;
    }
    
    // Write CSV header
    logFile << "Timestamp_s,VideoName,TileConfig,SegmentId,TileId,Quality,TileSize_bytes,ClientIP,ClientPort\n";
    
    // Write all entries
    for (const auto& entry : m_tileLog) {
        logFile << entry.timestamp.GetSeconds() << ","
                << entry.videoName << ","
                << entry.tileConfig << ","
                << entry.segmentId << ","
                << entry.tileId << ","
                << entry.quality << ","
                << entry.tileSize << ","
                << entry.clientIp << ","
                << entry.clientPort << "\n";
    }
    
    logFile.close();
    NS_LOG_INFO("Flushed " << m_tileLog.size() << " tile log entries to " << m_logFilePath);
}
void TiledDashServer::StopApplication() {
    NS_LOG_FUNCTION(this);
    if (m_initialSocket) {
        m_initialSocket->Close();
        m_initialSocket->SetAcceptCallback(
            MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
            MakeNullCallback<void, Ptr<Socket>, const Address&>());
    }
    FlushTileLog();
}

bool TiledDashServer::ConnectionRequestCallback(Ptr<Socket> socket, const Address& address) {
    NS_LOG_FUNCTION(this << socket << address);
    return true; // Accept all connections
}

void TiledDashServer::NewConnectionCreatedCallback(Ptr<Socket> socket, const Address& address) {
    NS_LOG_FUNCTION(this << socket << address);
    socket->SetCloseCallbacks(MakeCallback(&TiledDashServer::NormalCloseCallback, this),
                               MakeCallback(&TiledDashServer::ErrorCloseCallback, this));
    socket->SetRecvCallback(MakeCallback(&TiledDashServer::ReceivedDataCallback, this));
    socket->SetSendCallback(MakeCallback(&TiledDashServer::SendCallback, this)); // Optional: handle flow control
}


void TiledDashServer::NormalCloseCallback(Ptr<Socket> socket) {
    NS_LOG_FUNCTION(this << socket);
    // Clean up socket state
    m_socketStates.erase(socket);
    socket->Close(); 
}

void TiledDashServer::ErrorCloseCallback(Ptr<Socket> socket) {
    NS_LOG_FUNCTION(this << socket);
    // Clean up socket state
    m_socketStates.erase(socket);
    socket->Close();
}



void TiledDashServer::ReceivedDataCallback(Ptr<Socket> socket) {
    NS_LOG_FUNCTION(this << socket);
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from))) { // RecvFrom for compatibility, though TCP is connection-oriented
        if (packet->GetSize() == 0) { // Peer closed connection gracefully
            break;
        }
        m_rxTrace(packet, from); // Trace received packet
        // Add delay trace if timestamp is enabled on packet
        //Time rcvTime = Simulator::Now();
        //m_rxDelayTrace(rcvTime - packet->GetCreationTime(), from); // Example, if packet has creation time. Packet still does not have it thats why Callback in main is not working

        TiledDashHeader request = ParseRequest(packet);
        NS_LOG_INFO("Received request: " << request.ToString());


        if (request.GetMessageType() == TiledDashHeader::MPD_REQUEST) {
            ServeMpdFile(socket, request,from);
        } else if (request.GetMessageType() == TiledDashHeader::TILE_REQUEST) {
            ServeTileFile(socket, request, from);
        } else {
            NS_LOG_WARN(this << " Received unknown message type: " << static_cast<int>(request.GetMessageType()));
            // Optionally send an error response or close socket
        }
    }
}

void TiledDashServer::SendCallback(Ptr<Socket> socket, uint32_t availableBufferSize) {
    NS_LOG_FUNCTION(this << socket << availableBufferSize);
    
    // Check if we have pending data to send for this socket
    auto it = m_socketStates.find(socket);
    if (it != m_socketStates.end()) {
        SocketState& state = it->second;
        if (state.unsentPacket || state.totBytes < state.maxBytes) {
            // Continue sending data
            SendTileData(socket, Address()); // from address not needed for continuation
             //Simulator::Schedule(Seconds(0.1), &TiledDashServer::SendTileData, this, socket, Address());
        }
    }
}


TiledDashHeader TiledDashServer::ParseRequest(Ptr<Packet> packet) {
    TiledDashHeader header;
    packet->RemoveHeader(header); // Deserialize header from packet
    return header;
}

void TiledDashServer::ServeMpdFile(Ptr<Socket> socket, const TiledDashHeader& request, Address from) {
    NS_LOG_FUNCTION(this << socket << " VideoName: " << request.GetVideoName());
    
    // Try multiple common MPD file patterns
    std::vector<std::string> possiblePaths = {
        m_serverFolder + "/" + request.GetVideoName() + ".mpd",
        m_serverFolder + "/" + request.GetVideoName() + "/" + request.GetVideoName() + ".mpd",
        m_serverFolder + "/" + request.GetVideoName() + "_dash.mpd",
        m_serverFolder + "/" + request.GetVideoName() + "/" + request.GetVideoName() + "_dash.mpd"
    };
    
    std::string filename;
    std::ifstream file;
    
    // Find the first existing file
    for (const auto& path : possiblePaths) {
        file.open(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            filename = path;
            NS_LOG_INFO("Found MPD file: " << filename);
            break;
        }
    }
    
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read actual MPD content
        std::vector<char> buffer(size);
        if (file.read(buffer.data(), size)) {
            file.close();
            
            // Convert to string and optionally modify content
            std::string mpdContent(buffer.begin(), buffer.end());
            
            // Update media templates to use correct video name if needed
            size_t pos = 0;
            while ((pos = mpdContent.find("media=\"", pos)) != std::string::npos) {
                pos += 7;
                size_t endPos = mpdContent.find("\"", pos);
                if (endPos != std::string::npos) {
                    std::string mediaTemplate = mpdContent.substr(pos, endPos - pos);
                    
                    // Replace video name in template if it doesn't match
                    size_t firstUnderscore = mediaTemplate.find("_");
                    if (firstUnderscore != std::string::npos) {
                        std::string templateVideoName = mediaTemplate.substr(0, firstUnderscore);
                        if (templateVideoName != request.GetVideoName()) {
                            std::string newTemplate = request.GetVideoName() + mediaTemplate.substr(firstUnderscore);
                            mpdContent.replace(pos, endPos - pos, newTemplate);
                            endPos = pos + newTemplate.length();
                        }
                    }
                }
                pos = endPos;
            }
            
            // Do the same for initialization templates
            pos = 0;
            while ((pos = mpdContent.find("initialization=\"", pos)) != std::string::npos) {
                pos += 16;
                size_t endPos = mpdContent.find("\"", pos);
                if (endPos != std::string::npos) {
                    std::string initTemplate = mpdContent.substr(pos, endPos - pos);
                    
                    size_t firstUnderscore = initTemplate.find("_");
                    if (firstUnderscore != std::string::npos) {
                        std::string templateVideoName = initTemplate.substr(0, firstUnderscore);
                        if (templateVideoName != request.GetVideoName()) {
                            std::string newTemplate = request.GetVideoName() + initTemplate.substr(firstUnderscore);
                            mpdContent.replace(pos, endPos - pos, newTemplate);
                            endPos = pos + newTemplate.length();
                        }
                    }
                }
                pos = endPos;
            }
            
            // Create response with actual MPD content
            TiledDashHeader responseHeader;
            responseHeader.SetMessageType(TiledDashHeader::MPD_RESPONSE);
            responseHeader.SetVideoName(request.GetVideoName());
            responseHeader.SetPacketSize(mpdContent.size() + responseHeader.GetSerializedSize());
            
            // Create packet with real MPD data
            Ptr<Packet> payloadPacket = Create<Packet>(reinterpret_cast<const uint8_t*>(mpdContent.c_str()), 
                                                       mpdContent.size());
            
            Ptr<Packet> responsePacket = Create<Packet>();
            responsePacket->AddHeader(responseHeader);
            responsePacket->AddAtEnd(payloadPacket);
            
            socket->Send(responsePacket);
            m_txTrace(responsePacket);
            
            NS_LOG_INFO(this << " Sent MPD file " << request.GetVideoName() << ".mpd to "
                             << InetSocketAddress::ConvertFrom(from).GetIpv4()
                             << " size: " << mpdContent.size() << " bytes"
                             << " total packet size: " << responsePacket->GetSize());
        } else {
            file.close();
            NS_LOG_ERROR(this << " Failed to read MPD file: " << filename);
        }
    } else {
        NS_LOG_ERROR(this << " Could not find MPD file for video: " << request.GetVideoName());
        NS_LOG_ERROR("Searched paths:");
        for (const auto& path : possiblePaths) {
            NS_LOG_ERROR("  " << path);
        }
    }
}

void TiledDashServer::ServeTileFile(Ptr<Socket> socket, const TiledDashHeader& request, Address from) {
    std::string tileConfigStr = std::to_string(request.GetHorTiles()) + "X" + std::to_string(request.GetVerTiles());
    
    NS_LOG_FUNCTION(this << socket << " VideoName: " << request.GetVideoName() 
                         << " TileConfig: " << tileConfigStr
                         << " SegDuration: " << request.GetSegmentDuration() 
                         << " SegId: " << request.GetSegmentId() 
                         << " TileId: " << request.GetTileId() 
                         << " Quality: " << request.GetQuality());
    
    std::string filename = m_serverFolder + "/" + request.GetVideoName() + "/T" + tileConfigStr +
                           "-D" + std::to_string(static_cast<int>(request.GetSegmentDuration())) + 
                           "-S" + std::to_string(request.GetSegmentId()) + ".csv";
    
    std::vector<std::vector<uint32_t>> tileSizes = LoadTileSizes(filename);
    
    if (request.GetTileId() < tileSizes.size()) {
        if (request.GetQuality() >= 0 && static_cast<uint32_t>(request.GetQuality()) < tileSizes[request.GetTileId()].size()) {
            uint32_t tileSize = tileSizes[request.GetTileId()][request.GetQuality()];
            
            // LOG THE TILE REQUEST
            TileLogEntry entry;
            entry.timestamp = Simulator::Now();
            entry.videoName = request.GetVideoName();
            entry.tileConfig = tileConfigStr;
            entry.segmentId = request.GetSegmentId();
            entry.tileId = request.GetTileId();
            entry.quality = request.GetQuality();
            //tileSize=2500;//testing 2500
            entry.tileSize = tileSize; //tileSize;
            
            InetSocketAddress inetAddr = InetSocketAddress::ConvertFrom(from);
            entry.clientIp = inetAddr.GetIpv4();
            entry.clientPort = inetAddr.GetPort();
            
            m_tileLog.push_back(entry);
            
            // Initialize socket state for this transfer
            SocketState& state = m_socketStates[socket];
            state.maxBytes = tileSize;
            state.totBytes = 0;
            state.sendSize = std::min(static_cast<uint32_t>(1460), tileSize);//
            //state.sendSize = std::min(static_cast<uint32_t>(536), tileSize); //default Ns-3
            state.connected = true;
            
            // Setup response header
            state.responseHeader.SetMessageType(TiledDashHeader::TILE_RESPONSE);
            state.responseHeader.SetVideoName(request.GetVideoName());
            state.responseHeader.SetHorTiles(request.GetHorTiles());
            state.responseHeader.SetVerTiles(request.GetVerTiles());
            state.responseHeader.SetSegmentDuration(request.GetSegmentDuration());
            state.responseHeader.SetSegmentId(request.GetSegmentId());
            state.responseHeader.SetTileId(request.GetTileId());
            state.responseHeader.SetQuality(request.GetQuality());
            state.responseHeader.SetPacketSize(tileSize + state.responseHeader.GetSerializedSize());
            
            // Send the header first
            Ptr<Packet> headerPacket = Create<Packet>();
            headerPacket->AddHeader(state.responseHeader);
            socket->Send(headerPacket);
            
            // Start bulk sending the tile data
            SendTileData(socket, from);
            
            NS_LOG_INFO(this << " Started sending Tile file for " << request.GetVideoName()
                             << " Config: " << tileConfigStr
                             << " Seg: " << request.GetSegmentId()
                             << " Tile: " << request.GetTileId()
                             << " Quality: " << request.GetQuality()
                             << " to " << entry.clientIp 
                             << " size: " << tileSize << " (payload)");
        } else {
            NS_LOG_ERROR(this << " Quality index out of range: " << request.GetQuality() 
                              << " for TileID " << request.GetTileId() 
                              << ". Available qualities: " << (tileSizes[request.GetTileId()].empty() ? 0 : tileSizes[request.GetTileId()].size()));
            TiledDashHeader errHeader;
            errHeader.SetMessageType(TiledDashHeader::TILE_RESPONSE);
            errHeader.SetVideoName(request.GetVideoName());
            errHeader.SetSegmentId(request.GetSegmentId());
            errHeader.SetTileId(request.GetTileId());
            errHeader.SetQuality(request.GetQuality());
            errHeader.SetPacketSize(errHeader.GetSerializedSize()); // zero payload, header only
            Ptr<Packet> errPacket = Create<Packet>();
            errPacket->AddHeader(errHeader);
            socket->Send(errPacket);
        }
    } else {
        NS_LOG_ERROR(this << " Tile ID " << request.GetTileId() << " out of range for file " << filename 
                          << ". Available tiles: " << tileSizes.size());
        TiledDashHeader errHeader;
        errHeader.SetMessageType(TiledDashHeader::TILE_RESPONSE);
        errHeader.SetVideoName(request.GetVideoName());
        errHeader.SetSegmentId(request.GetSegmentId());
        errHeader.SetTileId(request.GetTileId());
        errHeader.SetQuality(request.GetQuality());
        errHeader.SetPacketSize(errHeader.GetSerializedSize()); // zero payload, header only
        Ptr<Packet> errPacket = Create<Packet>();
        errPacket->AddHeader(errHeader);
        socket->Send(errPacket);
    }
}

void TiledDashServer::SendTileData(Ptr<Socket> socket, const Address& from) {
    NS_LOG_FUNCTION(this << socket);
    
    auto it = m_socketStates.find(socket);
    if (it == m_socketStates.end()) {
        NS_LOG_ERROR("Socket state not found for bulk sending");
        return;
    }
    
    SocketState& state = it->second;
    
    //  #1: Proper condition - only check if we have more bytes to send
    while (state.totBytes < state.maxBytes) {  // Removed the "maxBytes == 0" condition
        
        uint64_t toSend = state.sendSize;
        
        // Calculate remaining bytes to send
        uint64_t remaining = state.maxBytes - state.totBytes;
        toSend = std::min(toSend, remaining);  // Simplified logic
        
        if (toSend == 0) {
            break; // Nothing more to send
        }
        
        NS_LOG_LOGIC("sending packet at " << Simulator::Now() << 
                    " - bytes " << state.totBytes << "/" << state.maxBytes);
        
        Ptr<Packet> packet;
        
        if (state.unsentPacket) {
            packet = state.unsentPacket;
            toSend = std::min(toSend, static_cast<uint64_t>(packet->GetSize()));
        } else {
            packet = Create<Packet>(toSend);
        }
        
        int actual = socket->Send(packet);
        
        if (actual == -1) {
            // Socket buffer full, cache packet and wait for SendCallback
            NS_LOG_DEBUG("Socket buffer full, caching packet for later");
            if (!state.unsentPacket) {
                state.unsentPacket = packet;
            }
            break; // Exit and wait for SendCallback
            
        } else if (actual > 0) {
            // Some or all data was sent
            if ((unsigned)actual == packet->GetSize()) {
                // Full packet sent
                state.totBytes += actual;
                m_txTrace(packet);
                state.unsentPacket = nullptr;
                
            } else {
                // Partial send - fragment the packet
                NS_LOG_DEBUG("Partial send: " << actual << "/" << packet->GetSize());
                Ptr<Packet> sent = packet->CreateFragment(0, actual);
                Ptr<Packet> unsent = packet->CreateFragment(actual, packet->GetSize() - actual);
                
                state.totBytes += actual;
                m_txTrace(sent);
                state.unsentPacket = unsent;
                break; // Wait for more buffer space
            }
        } else {
            NS_FATAL_ERROR("Unexpected return value from socket->Send(): " << actual);
        }
        
        // Add explicit check to prevent infinite loops
        if (state.totBytes >= state.maxBytes) {
            break;
        }
    }
    
    //  #2: Always check for completion, with better logging
    if (state.totBytes >= state.maxBytes) {
        NS_LOG_INFO("Tile transfer completed: " << state.totBytes << "/" << state.maxBytes << 
                   " bytes sent for socket " << socket);
        
        // Clean up this transfer's state
        m_socketStates.erase(socket);
        
        // Don't close the socket - it may be reused for more requests
    } else {
        NS_LOG_DEBUG("Tile transfer in progress: " << state.totBytes << "/" << state.maxBytes << 
                    " bytes sent, waiting for buffer space");
    }
}


std::vector<std::vector<uint32_t>> TiledDashServer::LoadTileSizes(const std::string& filename) {
    std::vector<std::vector<uint32_t>> tileSizesVector; // Renamed to avoid conflict
    std::ifstream file(filename);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::vector<uint32_t> row;
            std::stringstream ss(line);
            std::string value;
            while (std::getline(ss, value, ',')) {
                try {
                    if (!value.empty()) { // Ensure value is not empty
                        row.push_back(std::stoul(value));
                    }
                } catch (const std::invalid_argument& e) {
                    NS_LOG_ERROR("Invalid argument in CSV file '" << filename << "': " << e.what() << " for value '" << value << "'");
                } catch (const std::out_of_range& e) {
                    NS_LOG_ERROR("Out of range in CSV file '" << filename << "': " << e.what() << " for value '" << value << "'");
                }
            }
            if (!row.empty()) { // Only add row if it has some valid numbers
                tileSizesVector.push_back(row);
            }
        }
        file.close();
        if (tileSizesVector.empty()) {
             NS_LOG_WARN("Tile size file '" << filename << "' was empty or contained no valid data.");
        }
    } else {
        NS_LOG_ERROR("Could not open tile size file: " << filename);
    }
    return tileSizesVector;
}


} // namespace ns3