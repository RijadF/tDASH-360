// tiled-dash-client-tile-requester.h
#ifndef TILED_DASH_CLIENT_TILE_REQUESTER_H
#define TILED_DASH_CLIENT_TILE_REQUESTER_H

#include "ns3/object.h"
#include "ns3/socket.h"
#include "ns3/packet.h"
#include "ns3/address.h"
#include "ns3/tcp-socket.h"
#include <map>
#include <vector>
#include "ns3/nstime.h"
#include <utility>
#include <set>
#include "tiled-dash-client-common.h"
#include "tiled-dash-header.h"

namespace ns3 {
  class TiledDashClient;

class TiledDashClientTileRequester : public Object
{
  struct TileRequest
  {
    uint32_t segmentId;
    uint32_t tileId;
    int quality;
    int fovScore;    
    double priority;  
  };

public:
  //PLAYOUT STATES
  enum PlayoutState {
    BUFFERING,
    REBUFFERING,
    PLAYOUT,
    FOVCHANGE
  };
  static TypeId GetTypeId (void);
  TiledDashClientTileRequester ();
  virtual ~TiledDashClientTileRequester (); 

  void Start ();

  void RequestMpdFile();
  //void RequestTile (uint32_t segmentId, uint32_t tileId, int quality);
  void RequestTile (Ptr<TcpSocket> socket,uint32_t segmentId, uint32_t tileId, int quality, int fovScore);
  void SetClient(Ptr<TiledDashClient> client) { m_client = client; } 
  void SetVideoName(std::string videoName){m_videoName = videoName;}
  void SetSegmentDuration (Time segmentDuration){m_segmentDuration = segmentDuration; }
  void SetHorTiles(int horTiles){m_horTiles = horTiles;}
  void SetVerTiles(int verTiles){m_verTiles = verTiles;}

  void UpdateTileRequestQueue(std::vector<int> fovScores, std::vector<uint32_t> segIds);
  void ProcessTileRequestQueue();
  //dodano
  bool TileRequestExists(uint32_t segmentId, uint32_t tileId);
  // 05.08
  bool ParseMpdXmlContent(const std::string& mpdContent); 
  // 11.08
  Time GetAdaptiveTileDelay();
  double ParseIsoDuration(const std::string& isoDuration);
  // 15.09
  void PeriodicQualityUpdate();
  //test
  //void HandleSocketError (Ptr<Socket> socket);
  void OnSocketClosed(Ptr<Socket> socket);
  
private:
  void ReceiveData (Ptr<Socket> socket, Ptr<Packet> packet, const Address &from);
  std::vector<TileRequest> m_tileRequestQueue;  //tile request queue
  // the following maps are created for tracking transmissions over the sockets
  std::map<Ptr<Socket>, uint32_t> m_segId;
  std::map<Ptr<Socket>, TileInfo> m_tileInfo;
  std::map<Ptr<Socket>, TiledDashHeader::MessageType> m_fileTypes;
  std::map<Ptr<Socket>, uint32_t> m_receivedDataSize;
  std::map<Ptr<Socket>, uint32_t> m_packetSizes;
  Ptr<TiledDashClient> m_client;
  std::string m_videoName;
  Time m_segmentDuration;
  int m_horTiles;          // Number of horizontal tiles in the video
  int m_verTiles;          // Number of vertical tiles in the video

  //Added with Parser
  std::vector<uint32_t> m_qualityLevels;
  std::vector<uint32_t> m_bandwidths;
  uint32_t m_startSegmentNumber;
  uint32_t m_endSegmentNumber;
  //26.08
  Time m_mediaDuration;
  size_t valueEnd;
  //added
  // private:
  std::set<std::pair<uint32_t, uint32_t>> m_inFlight;   // (segmentId, tileId) currently downloading
  bool IsTileInFlight(uint32_t segmentId, uint32_t tileId) const;
};

} // namespace ns3

#endif // TILED_DASH_CLIENT_TILE_REQUESTER_H
