#ifndef TILED_DASH_SERVER_H
#define TILED_DASH_SERVER_H

#include "tiled-dash-header.h" // This will include the updated header

#include <ns3/address.h>
#include <ns3/application.h>
#include <ns3/event-id.h>
#include <ns3/nstime.h>
#include <ns3/ptr.h>
#include <ns3/simple-ref-count.h>
#include <ns3/traced-callback.h>
#include "ns3/string.h"

#include <map>
#include <ostream>
#include <vector>

namespace ns3 {

class Socket;
class Packet;

class TiledDashServer : public Application {
public:
    TiledDashServer();
    static TypeId GetTypeId();
    void SetServerFolder(const std::string& folder);
    std::string GetServerFolder() const;

protected:
    void DoDispose() override;
    void StartApplication() override;
    void StopApplication() override;

private:
    bool ConnectionRequestCallback(Ptr<Socket> socket, const Address& address);
    void NewConnectionCreatedCallback(Ptr<Socket> socket, const Address& address);
    void NormalCloseCallback(Ptr<Socket> socket);
    void ErrorCloseCallback(Ptr<Socket> socket);
    void ReceivedDataCallback(Ptr<Socket> socket);
    void SendCallback(Ptr<Socket> socket, uint32_t availableBufferSize);
    void ServeMpdFile(Ptr<Socket> socket, const TiledDashHeader& request, Address from);
    void ServeTileFile(Ptr<Socket> socket, const TiledDashHeader& request, Address from);
    TiledDashHeader ParseRequest(Ptr<Packet> packet);
    std::vector<std::vector<uint32_t>> LoadTileSizes(const std::string& filename);

    std::string m_serverFolder;
    Ptr<Socket> m_initialSocket;
    Address m_localAddress;
    uint16_t m_localPort;

    TracedCallback<Ptr<const Packet>> m_txTrace;
    TracedCallback<Ptr<const Packet>, const Address&> m_rxTrace;
    TracedCallback<const Time&, const Address&> m_rxDelayTrace;
    // Per-socket state for bulk sending
    struct SocketState {
        Ptr<Packet> unsentPacket;
        uint32_t totBytes;
        uint32_t maxBytes;
        uint32_t sendSize;
        bool connected;
        TiledDashHeader responseHeader;
        
        SocketState() : unsentPacket(nullptr), totBytes(0), maxBytes(0), sendSize(1440), connected(true) {} //was 1024
    };

    std::map<Ptr<Socket>, SocketState> m_socketStates;
    void SendTileData(Ptr<Socket> socket, const Address& from);
    
    struct TileLogEntry {
        Time timestamp;
        std::string videoName;
        std::string tileConfig;
        uint32_t segmentId;
        uint32_t tileId;
        int quality;
        uint32_t tileSize;
        Ipv4Address clientIp;
        uint16_t clientPort;
    };
    
    std::vector<TileLogEntry> m_tileLog;
    std::string m_logFilePath;
    
    void FlushTileLog();

};

} // namespace ns3

#endif // TILED_DASH_SERVER_H