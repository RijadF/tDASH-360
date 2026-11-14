#include "tiled-dash-header.h"
#include <ns3/log.h>
#include <ns3/packet.h> // For Buffer::Iterator
#include <sstream>     // For ToString and Print

NS_LOG_COMPONENT_DEFINE("TiledDashHeader");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(TiledDashHeader);

TiledDashHeader::TiledDashHeader()
    : Header(),
      m_messageType(TiledDashHeader::UNDEFINED_MSG),
      m_videoName(""),
      m_horTiles(0),
      m_verTiles(0),
      m_segmentDuration(0.0),
      m_segmentId(0),
      m_tileId(0),
      m_quality(0),      
      m_packetSize(0) {}

TiledDashHeader::~TiledDashHeader() {}

TypeId TiledDashHeader::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::TiledDashHeader").SetParent<Header>().AddConstructor<TiledDashHeader>();
    return tid;
}

TypeId TiledDashHeader::GetInstanceTypeId() const {
    return GetTypeId();
}

uint32_t TiledDashHeader::GetSerializedSize() const {
    // Size of MessageType (enum stored as uint8_t)
    // Size of videoName length (uint16_t) + videoName characters
    // Size of tileConfig length (uint16_t) + tileConfig characters
    // Size of segmentDuration (double)
    // Size of segmentId (uint32_t)
    // Size of tileId (uint32_t)
    // Size of quality (int32_t)
    // Size of horTiles (uint32_t)
    // Size of verTiles (uint32_t)
    // Size of packetSize (uint32_t)
    return sizeof(uint8_t) +                    // m_messageType
           sizeof(uint16_t) + m_videoName.length() + // videoName length + content
           sizeof(uint32_t) +                   // m_horTiles
           sizeof(uint32_t) +                   // m_verTiles
           sizeof(uint64_t) +                   // m_segmentDuration (stored as uint64_t)
           sizeof(uint32_t) +                   // m_segmentId
           sizeof(uint32_t) +                   // m_tileId
           sizeof(uint32_t) +                   // m_quality (stored as uint32_t)
           sizeof(uint32_t);                    // m_packetSize
}

void TiledDashHeader::Serialize(Buffer::Iterator start) const {
    start.WriteU8(static_cast<uint8_t>(m_messageType));

    uint16_t videoNameLen = m_videoName.length();
    start.WriteU16(videoNameLen);
    start.Write((const uint8_t*)m_videoName.c_str(), videoNameLen);
    start.WriteU32(m_horTiles);
    start.WriteU32(m_verTiles);
    // serializing double value on three steps
    uint64_t doubleBytes;
    memcpy(&doubleBytes, &m_segmentDuration, sizeof(m_segmentDuration));
    start.WriteU64 (doubleBytes);    
    start.WriteU32(m_segmentId);
    start.WriteU32(m_tileId);
    // serializing integer value
    uint32_t intBytes = static_cast<uint32_t>(m_quality);
    start.WriteU32 (intBytes); 
    start.WriteU32(m_packetSize);
}

uint32_t TiledDashHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator original = start;

    // Check if we have enough bytes for the minimum header
    uint32_t remainingBytes = start.GetSize();
    if (remainingBytes < sizeof(uint8_t) + sizeof(uint16_t)) {
        NS_LOG_WARN("Packet too short for TiledDashHeader - missing message type or video name length");
        return 0;
    }

    m_messageType = static_cast<MessageType>(start.ReadU8());
    uint16_t videoNameLen = start.ReadU16();
    
    // Check if we have enough bytes for the complete header
    uint32_t expectedSize = sizeof(uint8_t) +           // messageType
                           sizeof(uint16_t) +           // videoNameLen
                           videoNameLen +               // videoName content
                           sizeof(uint32_t) +           // horTiles
                           sizeof(uint32_t) +           // verTiles
                           sizeof(uint64_t) +           // segmentDuration
                           sizeof(uint32_t) +           // segmentId
                           sizeof(uint32_t) +           // tileId
                           sizeof(uint32_t) +           // quality
                           sizeof(uint32_t);            // packetSize

    if (remainingBytes < expectedSize) {
        NS_LOG_WARN("Packet too short for TiledDashHeader, expected " << expectedSize << " bytes, got " << remainingBytes);
        return 0;
    }

    // Read video name
    if (videoNameLen > 0) {
        char videoNameBuffer[videoNameLen + 1];
        start.Read((uint8_t*)videoNameBuffer, videoNameLen);
        videoNameBuffer[videoNameLen] = '\0';
        m_videoName = videoNameBuffer;
    } else {
        m_videoName = "";
    }

    m_horTiles = start.ReadU32();
    m_verTiles = start.ReadU32();

    // deserialize double value from uint64_t
    uint64_t doubleBytes = start.ReadU64();
    memcpy(&m_segmentDuration, &doubleBytes, sizeof(m_segmentDuration));

    m_segmentId = start.ReadU32();
    m_tileId = start.ReadU32();
    m_quality = static_cast<int>(start.ReadU32());
    m_packetSize = start.ReadU32();

    return start.GetDistanceFrom(original);
}

void TiledDashHeader::Print(std::ostream& os) const {
    NS_LOG_FUNCTION(this << &os);
    os << "(Type: ";
    switch (m_messageType) {
        case UNDEFINED_MSG: os << "Undefined"; break;
        case MPD_REQUEST:   os << "MPD_Request"; break;
        case MPD_RESPONSE:  os << "MPD_Response"; break;
        case TILE_REQUEST:  os << "Tile_Request"; break;
        case TILE_RESPONSE: os << "Tile_Response"; break;
        default:            os << "Unknown"; break;
    }
    os << ", Video: " << m_videoName
    << ", HxV Tiles: " << m_horTiles << "x" << m_verTiles
    << ", Duration: " << m_segmentDuration
    << ", Segment: " << m_segmentId
    << ", Tile: " << m_tileId
    << ", Quality: " << m_quality
    << ", PacketSize: " << m_packetSize << ")";
}

std::string TiledDashHeader::ToString() const {
    NS_LOG_FUNCTION(this);
    std::ostringstream oss;
    Print(oss);
    return oss.str();
}

// --- MessageType ---
void TiledDashHeader::SetMessageType(MessageType messageType) { m_messageType = messageType; }
TiledDashHeader::MessageType TiledDashHeader::GetMessageType() const { return m_messageType; }

// --- VideoName ---
void TiledDashHeader::SetVideoName(const std::string& videoName) { m_videoName = videoName; }
std::string TiledDashHeader::GetVideoName() const { return m_videoName; }

// --- SegmentDuration ---
void TiledDashHeader::SetSegmentDuration(double segmentDuration) { m_segmentDuration = segmentDuration; }
double TiledDashHeader::GetSegmentDuration() const { return m_segmentDuration; }

// --- SegmentId ---
void TiledDashHeader::SetSegmentId(uint32_t segmentId) { m_segmentId = segmentId; }
uint32_t TiledDashHeader::GetSegmentId() const { return m_segmentId; }

// --- TileId ---
void TiledDashHeader::SetTileId(uint32_t tileId) { m_tileId = tileId; }
uint32_t TiledDashHeader::GetTileId() const { return m_tileId; }

// --- Quality ---
void TiledDashHeader::SetQuality(int quality) { m_quality = quality; }
int TiledDashHeader::GetQuality() const { return m_quality; }

// --- HorTiles ---
void TiledDashHeader::SetHorTiles(uint32_t horTiles) { m_horTiles = horTiles; }
uint32_t TiledDashHeader::GetHorTiles() const { return m_horTiles; }

// --- verTiles ---
void TiledDashHeader::SetVerTiles(uint32_t verTiles) { m_verTiles = verTiles; }
uint32_t TiledDashHeader::GetVerTiles() const { return m_verTiles; }

// --- PacketSize ---
void TiledDashHeader::SetPacketSize(uint32_t packetSize) { m_packetSize = packetSize; }
uint32_t TiledDashHeader::GetPacketSize() const { return m_packetSize; }

} // namespace ns3