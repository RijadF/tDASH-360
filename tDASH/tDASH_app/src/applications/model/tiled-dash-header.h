// tiled-dash-header.h
#ifndef TILED_DASH_HEADER_H
#define TILED_DASH_HEADER_H

#include "ns3/header.h"
#include "ns3/uinteger.h" // For uint32_t, int types if used directly
#include <string>        // For std::string
#include <ostream>       // For Print method

namespace ns3 {

class TiledDashHeader : public Header
{
public:
  static TypeId GetTypeId (void);
  TiledDashHeader ();
  virtual ~TiledDashHeader ();
  std::string ToString() const;

  enum MessageType {
    UNDEFINED_MSG, // Added a default/undefined type
    MPD_REQUEST,
    MPD_RESPONSE,
    TILE_REQUEST,
    TILE_RESPONSE
  };

  // MessageType
  void SetMessageType (MessageType messageType);
  MessageType GetMessageType () const;

  // VideoName
  void SetVideoName(const std::string& videoName);
  std::string GetVideoName() const;

  // TileConfig (e.g., "2x2")
  void SetTileConfig(const std::string& tileConfig);
  std::string GetTileConfig() const;

  // SegmentDuration (in seconds, or appropriate time unit)
  void SetSegmentDuration (double segmentDuration);
  double GetSegmentDuration () const;

  // SegmentId
  void SetSegmentId (uint32_t segmentId);
  uint32_t GetSegmentId () const;

  // TileId
  void SetTileId (uint32_t tileId);
  uint32_t GetTileId () const;

  // Quality
  void SetQuality (int quality); // Changed to int to match member
  int GetQuality () const;      // Changed to int to match member

  // HorTiles
  void SetHorTiles (uint32_t horTiles);
  uint32_t GetHorTiles () const;

  // verTiles
  void SetVerTiles (uint32_t verTiles);
  uint32_t GetVerTiles () const;

  // PacketSize (payload size + header size)
  void SetPacketSize (uint32_t packetSize);
  uint32_t GetPacketSize () const;

  // Overridden from ns3::Header
  virtual TypeId GetInstanceTypeId (void) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
  virtual void Print (std::ostream &os) const; // Declaration for Print

private:
  MessageType m_messageType;
  std::string m_videoName;
  double m_segmentDuration; 
  uint32_t m_horTiles;
  uint32_t m_verTiles;  
  uint32_t m_segmentId;
  uint32_t m_tileId;
  int m_quality;              
  uint32_t m_packetSize;
};

} // namespace ns3

#endif // TILED_DASH_HEADER_H