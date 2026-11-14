// tiled-dash-client-tile-requester.cc
#include "tiled-dash-client-tile-requester.h"
#include "tiled-dash-client.h"
#include "tiled-dash-client-network-manager.h"
#include "tiled-dash-client-media-buffer.h" 
#include "tiled-dash-client-abr-engine.h" 
#include "tiled-dash-client-network-tracker.h"
#include "tiled-dash-client-fov-tracker.h"
#include "tiled-dash-client-media-decoder.h"

#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/packet.h"
#include "ns3/tcp-socket.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/simulator.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex> //31.08

#include <cctype>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TiledDashClientTileRequester");

NS_OBJECT_ENSURE_REGISTERED (TiledDashClientTileRequester);

TypeId
TiledDashClientTileRequester::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TiledDashClientTileRequester")
    .SetParent<Object> ()
    .SetGroupName("Applications")
    .AddConstructor<TiledDashClientTileRequester> ()
    ;
  return tid;
}

TiledDashClientTileRequester::TiledDashClientTileRequester ()
{
  NS_LOG_FUNCTION (this);
}

TiledDashClientTileRequester::~TiledDashClientTileRequester ()
{
  NS_LOG_FUNCTION (this);
}


void
TiledDashClientTileRequester::Start ()
{
  NS_LOG_FUNCTION (this);  
  m_client->GetNetworkManager()->SetDataReceivedCallback (MakeCallback (&TiledDashClientTileRequester::ReceiveData, this));
  //m_client->GetNetworkManager()->SetSocketErrorCallback(MakeCallback(&TiledDashClientTileRequester::HandleSocketError, this));
  RequestMpdFile();
}

bool TiledDashClientTileRequester::IsTileInFlight(uint32_t segmentId, uint32_t tileId) const
{
    return m_inFlight.count({segmentId, tileId}) > 0;
}


void TiledDashClientTileRequester::PeriodicQualityUpdate()
{
    NS_LOG_FUNCTION(this);
    
    if (!m_client || !m_client->GetMediaDecoder()) {
        return;
    }
    
    uint32_t currentSegmentId = m_client->GetMediaDecoder()->GetCurrentSegmentId();
    uint32_t totalNumSegments = m_client->GetVideoSegments();
    
    // Update qualities for the next few segments
    std::vector<uint32_t> segmentsToUpdate;
    for (uint32_t i = 1; i < 3; ++i) { 
        uint32_t segId = currentSegmentId + i;
        if (segId <= totalNumSegments) {
            segmentsToUpdate.push_back(segId);
        }
    }
    
    if (!segmentsToUpdate.empty()) {
        std::vector<int> currentFovScores = m_client->GetFovTracker()->GetCurrentFovScores();
        //std::vector<int> currentFovScores=m_client->GetFovTracker()->GetFinalPredicted();
        UpdateTileRequestQueue(currentFovScores, segmentsToUpdate);
    }

}


void TiledDashClientTileRequester::RequestMpdFile() {
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("Requesting MPD file at time: " << Simulator::Now().GetSeconds());
    
    Ptr<Packet> packet = Create<Packet>();
    TiledDashHeader header;
    header.SetMessageType(TiledDashHeader::MPD_REQUEST);
    
    // For MPD request, we only need the video name - other parameters are unknown yet!
    // The video name should be known beforehand (e.g., from configuration or user input)
    //std::string videoName = "testVideo"; // This should match your server's MPD filename
    //std::string videoName="360_Google_DoodlesSpotlight_Stories_Back_to_the_Moon_6x4_1000_dash";
    std::string videoName=m_client->GetVideoName();
    //std::string videoName="Pomerelle_Mountain_Resort-360_Video_5x5_1000_dash";
    header.SetVideoName(videoName);
    
    // Set minimal/default values for other fields since we don't know them yet
    // The server doesn't actually need these for serving the MPD file
    header.SetSegmentDuration(0.0);  // Unknown yet
    header.SetHorTiles(0);           // Unknown yet  
    header.SetVerTiles(0);           // Unknown yet
    
    packet->AddHeader(header);
    
    Ptr<TcpSocket> socket = m_client->GetNetworkManager()->GetIdleSocket();
    NS_LOG_INFO("Socket for MPD request: " << socket);
    
    if (socket) {
        NS_LOG_INFO("Sending MPD request for video: " << videoName);
        m_client->GetNetworkManager()->SendData(socket, packet);
    } else {
        NS_LOG_ERROR("No idle socket available to request MPD file");
    }
}

void TiledDashClientTileRequester::RequestTile(Ptr<TcpSocket> socket, uint32_t segmentId, uint32_t tileId, int quality, int fovScore) {
    //NS_LOG_WARN("Entered RequestTile function at time: " << Simulator::Now().GetSeconds());
    NS_LOG_FUNCTION(this << socket << segmentId << tileId << quality);
    NS_LOG_WARN("Component identity: " << this << ", m_client: " << m_client << ", Entered RequestTile function at time: " << Simulator::Now().GetSeconds());
    if (!m_client || !m_client->GetNetworkManager()) {
        NS_LOG_ERROR("Network manager is null - cannot request tile S" << segmentId << " T" << tileId);
        return;
    }
    
    // The socket is now a function parameter, so no need to get it here.
    if (!socket) {
        NS_LOG_ERROR("Provided socket is null - cannot request tile S" << segmentId << " T" << tileId << " Q" << quality);
        return;
    }
    
    // Create and prepare the packet
    Ptr<Packet> packet = Create<Packet>();
    TiledDashHeader header;
    header.SetMessageType(TiledDashHeader::TILE_REQUEST);
    header.SetSegmentId(segmentId);
    header.SetTileId(tileId);
    header.SetQuality(quality);

    // Use parsed MPD parameters
    header.SetVideoName(m_videoName);
    header.SetSegmentDuration(m_segmentDuration.GetSeconds());
    header.SetHorTiles(m_horTiles);
    header.SetVerTiles(m_verTiles);
    
    packet->AddHeader(header);
    
    // Store request information for tracking
    m_segId[socket] = segmentId;
    m_tileInfo[socket].tileId = tileId;
    m_tileInfo[socket].quality = quality;
    m_tileInfo[socket].fetchDuration = Simulator::Now();
    m_tileInfo[socket].requestTime=Simulator::Now();
    m_inFlight.insert({segmentId, tileId}); 
    //m_tileInfo[socket].fovScore=fovScore; 
    // Send the request

    NS_LOG_INFO("Sending tile request on socket " << socket 
                << " for S" << segmentId << " T" << tileId << " Q" << quality);
    
    m_client->GetNetworkManager()->SendData(socket, packet);
}

void TiledDashClientTileRequester::ReceiveData(Ptr<Socket> socket, Ptr<Packet> packet, const Address &from) {
    NS_LOG_INFO("Entered ReceiveData");

    NS_LOG_DEBUG("ReceiveData at " << Simulator::Now().GetSeconds()
               << " socket=" << socket);

    NS_LOG_FUNCTION(this << socket << packet << from);
    TiledDashHeader header;
    //NS_LOG_ERROR("=== ReceiveData CALLED ===");
    //NS_LOG_ERROR("Message type: " << header.GetMessageType() << " (TILE_RESPONSE=" << TiledDashHeader::TILE_RESPONSE << ")");
    packet->PeekHeader(header);
    
    // Initialize tracking for new downloads
    if (m_packetSizes.find(socket) == m_packetSizes.end() || m_packetSizes[socket] == 0) {
        TiledDashHeader responseHeader;
        //NS_LOG_ERROR("Message type: " << responseHeader.GetMessageType() << " (TILE_RESPONSE=" << TiledDashHeader::TILE_RESPONSE << ")");
        packet->PeekHeader(responseHeader);

        m_packetSizes[socket] = responseHeader.GetPacketSize();
        m_fileTypes[socket] = responseHeader.GetMessageType();
        
        if (m_fileTypes[socket] == TiledDashHeader::TILE_RESPONSE) {
            m_tileInfo[socket].size = responseHeader.GetPacketSize() - responseHeader.GetSerializedSize();
        }
        m_receivedDataSize[socket] = 0;
        NS_LOG_INFO("First packet for download on socket " << socket << ". Expected total size: " << m_packetSizes[socket]
                    << ", FileType: " << m_fileTypes[socket]);
    }

    uint32_t currentPacketPayloadSize = packet->GetSize();
    m_receivedDataSize[socket] += currentPacketPayloadSize;

    NS_LOG_DEBUG("Socket " << socket << " received chunk of size " << currentPacketPayloadSize
                 << ". Total received for this download: " << m_receivedDataSize[socket]
                 << " / " << m_packetSizes[socket]);

    // Check if complete file/tile has been received
    if (m_packetSizes.count(socket) && m_packetSizes[socket] > 0 && m_receivedDataSize[socket] >= m_packetSizes[socket]) {
        NS_LOG_INFO("Complete file/tile received on socket " << socket);

        if (m_fileTypes.count(socket)) {
            TiledDashHeader::MessageType fileType = m_fileTypes[socket];
            if (fileType == TiledDashHeader::MPD_RESPONSE) {
                NS_LOG_INFO("MPD file fully received on socket " << socket);
                
                // Remove the header to access the payload
                TiledDashHeader mpdHeader;
                packet->RemoveHeader(mpdHeader);
                
                // Extract the actual MPD XML content from the packet payload
                uint32_t payloadSize = packet->GetSize();
                uint8_t* buffer = new uint8_t[payloadSize + 1];
                packet->CopyData(buffer, payloadSize);
                buffer[payloadSize] = '\0'; // Null terminate for string conversion
                
                std::string mpdContent(reinterpret_cast<char*>(buffer));
                delete[] buffer;
                
                NS_LOG_INFO("MPD content received, payload size: " << payloadSize << " bytes");
                NS_LOG_DEBUG("MPD content preview: " << mpdContent.substr(0, std::min(200, (int)mpdContent.length())));
                
                // Parse the MPD XML content (not the header!)
                if (ParseMpdXmlContent(mpdContent)) {
                    NS_LOG_INFO("MPD XML parsing successful - Parameters extracted from XML");
                    
                    // Log the extracted parameters
                    NS_LOG_INFO("Extracted MPD parameters from XML: "
                               << "videoName=" << m_videoName
                               << ", horTiles=" << m_horTiles
                               << ", verTiles=" << m_verTiles
                               << ", segmentDuration=" << m_segmentDuration.GetSeconds()
                               << ", segmentRange=" << m_startSegmentNumber << "-" << m_endSegmentNumber
                               << ", qualityLevels=" << m_qualityLevels.size());

                    // Now proceed with your existing tile request logic
                    std::vector<int> fovScores(m_client->GetHorTiles()*m_client->GetVerTiles(), 3); // Your existing FOV scores
                    //std::vector<int> fovScores(25, 3); 
                    //std::vector<int> fovScores={1,1,1,1,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0};
                    // Use the parsed segment range or your predefined segments
                    //std::vector<uint32_t> segIds = {1, 2, 3, 4, 5, 6, 7, 8, 9};//test for 10seg
                    //std::vector<uint32_t> segIds = {1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19};//test for 20seg
                    std::vector<uint32_t> segIds;
                    for (uint32_t i = 1; i <m_client->GetMediaDecoder()->GetInitialBufferingDuration()+1; ++i) { 
                        segIds.push_back(i);
                    }

                    UpdateTileRequestQueue(fovScores, segIds);
                    ProcessTileRequestQueue();
                    
                } else {
                    NS_LOG_ERROR("Failed to parse MPD XML content");
                }
            }
            else if (fileType == TiledDashHeader::TILE_RESPONSE) {
                NS_LOG_INFO("Tile fully received on socket " << socket);
                NS_LOG_INFO("count "<<m_tileInfo.count(socket)<<" seg "<<m_segId.count(socket)<<" client "<<m_client<<" buffer "<<m_client->GetMediaBuffer());
                if (m_tileInfo.count(socket) && m_segId.count(socket)) {
                    NS_LOG_INFO("USAO OVDJE--ne ulazi");
                    m_inFlight.erase({m_segId[socket], m_tileInfo[socket].tileId});
                    Time currentTime = Simulator::Now();
                    m_tileInfo[socket].fetchDuration = currentTime - m_tileInfo[socket].fetchDuration;
                    m_tileInfo[socket].receiveTime=m_tileInfo[socket].requestTime+m_tileInfo[socket].fetchDuration;
                    if (m_tileInfo[socket].fetchDuration.GetSeconds() > 0) {
                        //NS_LOG_WARN("Size of the packet: "<<m_tileInfo[socket].size);
                        double throughputBps = (static_cast<double>(m_tileInfo[socket].size) * 8.0) / m_tileInfo[socket].fetchDuration.GetSeconds(); //added number of users in the chanell
                        m_tileInfo[socket].throughputBps = throughputBps;//num of users
                        NS_LOG_INFO("Tile S" << m_segId[socket] << " T" << m_tileInfo[socket].tileId 
                                    << " Q" << m_tileInfo[socket].quality << " Size: " << m_tileInfo[socket].size
                                    << " bytes, FetchTime: " << m_tileInfo[socket].fetchDuration.GetSeconds() << "s, Throughput: " << throughputBps << " bps");
                        if (m_client && m_client->GetNetworkTracker()) {
                            NS_LOG_INFO("Usao da submit");
                            m_client->GetNetworkTracker()->SubmitThroughput(socket, throughputBps);
                        }
                    } else {
                        NS_LOG_WARN("Fetch duration for tile on socket " << socket << " is zero. Cannot calculate throughput.");
                    }
                    double netRate = m_client->GetNetworkTracker()->EstimateOverallThroughput();
                    m_tileInfo[socket].netRate = netRate;
                    // ADD THESE DEBUG LINES:
                    NS_LOG_INFO("=== BEFORE StoreTile() ===");
                    NS_LOG_INFO("Component identity: " << this << ", m_client: " << m_client << ", Time: " << Simulator::Now().GetSeconds());
                    if (m_client && m_client->GetMediaBuffer()) {
                        NS_LOG_INFO("Usao da storam");
                        m_client->GetMediaBuffer()->StoreTile(m_segId[socket], m_tileInfo[socket]);
                        
                    }
                } else {
                    NS_LOG_WARN("Missing m_tileInfo or m_segId for completed tile download on socket " << socket);
                }
            }
        } 
        else {
             NS_LOG_WARN("File type not known for completed download on socket " << socket);
        }

        
        // Cleanup tracking maps before releasing the socket and processing the next request.
        NS_LOG_DEBUG("Cleaning up tracking maps for socket " << socket);

        
        // Release the socket back to the pool
        if (m_client && m_client->GetNetworkManager()) {
            NS_LOG_DEBUG("Inside the release socket function");
            m_client->GetNetworkManager()->ReleaseSocket(DynamicCast<TcpSocket>(socket));
        }
        //Release the socket
        m_segId.erase(socket);
        m_tileInfo.erase(socket);
        m_fileTypes.erase(socket);
        m_receivedDataSize.erase(socket);
        m_packetSizes.erase(socket);
        // Process the next request only after the cleanup
        ProcessTileRequestQueue(); 
    }
}

void TiledDashClientTileRequester::UpdateTileRequestQueue(std::vector<int> fovScores, std::vector<uint32_t> segmentIds)
{
    double netRate = m_client->GetNetworkTracker()->EstimateOverallThroughput();
    NS_LOG_WARN("USAO U UPDATE "<<Simulator::Now());
    NS_LOG_WARN("NET RATE JE "<<netRate);
    for (uint32_t segmentId : segmentIds)
    {
        std::vector<int> updatedFoVScores = fovScores; // Copy of fovScores
        for (uint32_t tileId = 0; tileId < fovScores.size(); ++tileId)
        {
            if (fovScores[tileId] >= 0)
            {
                if (m_client->GetMediaBuffer()->IsTileBuffered(segmentId, tileId) || IsTileInFlight(segmentId, tileId))
                {
                    updatedFoVScores[tileId] = -1; // Case 1: Tile downloaded
                }
                else
                {
                    bool inQueue = false;
                    for (auto it = m_tileRequestQueue.begin(); it != m_tileRequestQueue.end(); ++it)
                    {
                        if (it->segmentId == segmentId && it->tileId == tileId)
                        {
                            m_tileRequestQueue.erase(it); // Case 2: Request in queue
                            inQueue = true;
                            break;
                        }
                    }
                    if (!inQueue)
                    {
                        // Case 3: Not requested before, FoV score unchanged
                    }
                }
            }
        }

        //Time deadline = m_client->GetMediaDecoder()->GetSegmentPlayoutTime(segmentId);
        Time absolutePlayoutTime = m_client->GetMediaDecoder()->GetSegmentPlayoutTime(segmentId);
        Time currentTime = Simulator::Now();
        Time deadline = absolutePlayoutTime - currentTime;
        
        std::map<uint32_t, std::vector<int>> tileQualities = m_client->GetAbrEngine()->GetSegmentTileQuality(netRate, segmentId, updatedFoVScores, deadline);
        
        for (uint32_t tileId = 0; tileId < fovScores.size(); ++tileId)
        {
            //if (fovScores[tileId] >= -1 && tileQualities[segmentId][tileId] >= 0)
            if (updatedFoVScores[tileId] >= 0 && tileQualities[segmentId][tileId] >= 0)
            {
                double priority = static_cast<double>(segmentId) + static_cast<double>(fovScores[tileId]) / 1000.0;
                NS_LOG_WARN("segmentId "<<segmentId<<" Priority: "<<priority<<" quality "<<tileQualities[segmentId][tileId]<< " fovscore "<<fovScores[tileId]<<" tileID "<<tileId<<" Current segment "<<m_client->GetMediaDecoder()->GetCurrentSegmentId());
                m_tileRequestQueue.push_back(TileRequest{segmentId, tileId, tileQualities[segmentId][tileId], fovScores[tileId], priority});
            }
        }
    }

    std::sort(m_tileRequestQueue.begin(), m_tileRequestQueue.end(), [](const TileRequest& a, const TileRequest& b) {
        return a.priority < b.priority;
    });
    ProcessTileRequestQueue();
}

void TiledDashClientTileRequester::ProcessTileRequestQueue()
{
    NS_LOG_INFO("=== ProcessTileRequestQueue START ===");
    NS_LOG_INFO("Queue size: " << m_tileRequestQueue.size());
    
    uint32_t totalNumSegments = m_client->GetVideoSegments();
    
    // Populate queue if empty
    if (m_tileRequestQueue.empty())
    {
        uint32_t firstSeg = m_client->GetMediaBuffer()->GetNextSegmentId();
        if (firstSeg == 0) firstSeg = 1;
        const uint32_t kLookahead = 2;   // how many segments ahead we may look
        double netRate = m_client->GetNetworkTracker()->EstimateOverallThroughput();
        std::vector<int> fovScores = m_client->GetFovTracker()->GetCurrentFovScores();

        for (uint32_t segmentId = firstSeg;segmentId < firstSeg + kLookahead && segmentId < totalNumSegments;++segmentId)
        {
            // Hide tiles that are already buffered or downloading, so the ABR
            // only budgets bandwidth for tiles we actually still need.
            //std::vector<int> abrScores = fovScores;
            bool anythingNeeded = false;
            for (uint32_t tileId = 0; tileId < fovScores.size(); ++tileId) {
                if (m_client->GetMediaBuffer()->IsTileBuffered(segmentId, tileId) || IsTileInFlight(segmentId, tileId)) {
                    fovScores[tileId] = -1;
                } else if (fovScores[tileId] >= 0) {
                    anythingNeeded = true;
                }
            }
            if (!anythingNeeded) continue;   // whole segment covered -> look at next one

            Time deadline = m_client->GetMediaDecoder()->GetSegmentPlayoutTime(segmentId) - Simulator::Now();
            std::map<uint32_t, std::vector<int>> tileQualities = m_client->GetAbrEngine()->GetSegmentTileQuality(netRate, segmentId, fovScores, deadline);

            for (uint32_t tileId = 0; tileId < fovScores.size(); ++tileId) {
                if (fovScores[tileId] >= 0 && tileQualities[segmentId][tileId] >= 0 && !TileRequestExists(segmentId, tileId)) {
                    float priority = static_cast<float>(segmentId) + static_cast<float>(fovScores[tileId]) / 1000.0f;
                    m_tileRequestQueue.push_back(TileRequest{
                        segmentId, tileId, tileQualities[segmentId][tileId],
                        fovScores[tileId], priority});
                }
            }
            if (!m_tileRequestQueue.empty()) break;   // found work, stop looking ahead
        
            std::sort(m_tileRequestQueue.begin(), m_tileRequestQueue.end(), 
                     [](const TileRequest& a, const TileRequest& b) {
                return a.priority < b.priority;
            });
            for (uint32_t i = 0; i < m_tileRequestQueue.size(); ++i) {
                NS_LOG_WARN("Queue Pos: " << i << " fovScore: " << m_tileRequestQueue[i].fovScore<<" tileId: "<<m_tileRequestQueue[i].tileId<<" quality: "<<m_tileRequestQueue[i].quality<<" segment: "<<segmentId);
            }
            NS_LOG_INFO("Queue populated with " << m_tileRequestQueue.size() << " tiles");
        }
    }

    // Process ALL tiles with available sockets
    while (!m_tileRequestQueue.empty())
    {
        // Skip out-of-range segments
        if (m_tileRequestQueue.front().segmentId >= totalNumSegments) {
            NS_LOG_INFO("Skipping out-of-range segment " << m_tileRequestQueue.front().segmentId);
            m_tileRequestQueue.erase(m_tileRequestQueue.begin());
            continue;
        }
        
        Ptr<TcpSocket> socket = m_client->GetNetworkManager()->GetIdleSocket();
        //Ptr<TcpSocket> socket = m_client->GetNetworkManager()->GetSocket(0);
        if (socket)
        {
            auto request = m_tileRequestQueue.front();
            m_tileRequestQueue.erase(m_tileRequestQueue.begin());
            
            // Double-check not already buffered
            if (m_client->GetMediaBuffer()->IsTileBuffered(request.segmentId, request.tileId)) {
                NS_LOG_INFO("Tile S" << request.segmentId << " T" << request.tileId 
                           << " already buffered, releasing socket");
                //m_client->GetNetworkManager()->ReleaseSocket(socket);
                m_client->GetNetworkManager()->ReleaseSocket(DynamicCast<TcpSocket>(socket));
                continue; // Try next tile
            }
            
            if (IsTileInFlight(request.segmentId, request.tileId)) {
                NS_LOG_INFO("Tile S" << request.segmentId << " T" << request.tileId << " already downloading, skipping");
                m_client->GetNetworkManager()->ReleaseSocket(DynamicCast<TcpSocket>(socket));
                continue;
            }
            NS_LOG_INFO("Requesting tile S" << request.segmentId << " T" << request.tileId 
                       << " Q" << request.quality);
            
            RequestTile(socket, request.segmentId, request.tileId, request.quality, request.fovScore);
        }
        else
        {
            NS_LOG_WARN("No idle sockets available, stopping (queue size: " 
                       << m_tileRequestQueue.size() << ")");

            break; // Exit while loop
        }
    }
    
    NS_LOG_INFO("=== ProcessTileRequestQueue END (remaining: " 
               << m_tileRequestQueue.size() << ") ===");
}


// Helper function to check if a tile request already exists in the queue
bool TiledDashClientTileRequester::TileRequestExists(uint32_t segmentId, uint32_t tileId) {
    for (const auto& request : m_tileRequestQueue) {
        if (request.segmentId == segmentId && request.tileId == tileId) {
            return true;
        }
    }
    return false;
}


bool TiledDashClientTileRequester::ParseMpdXmlContent(const std::string& mpdContent) {
    NS_LOG_INFO("Parsing MPD XML content");    
    // Parse tiling configuration - look for pattern in the content
    //m_horTiles = 6; // Default values
    //m_verTiles = 4;
    //m_horTiles = 5; // Default values
    //m_verTiles = 5;    
    m_horTiles=m_client->GetHorTiles();
    m_verTiles=m_client->GetVerTiles();
    // Look for explicit tiling pattern (e.g., "6x4")
    size_t patternPos = mpdContent.find("x");
    while (patternPos != std::string::npos && patternPos > 0 && patternPos < mpdContent.length() - 1) {
        if (std::isdigit(mpdContent[patternPos - 1]) && std::isdigit(mpdContent[patternPos + 1])) {
            size_t start = patternPos - 1;
            while (start > 0 && std::isdigit(mpdContent[start - 1])) start--;
            
            size_t end = patternPos + 1;
            while (end < mpdContent.length() && std::isdigit(mpdContent[end])) end++;
            
            std::string pattern = mpdContent.substr(start, end - start);
            size_t xPos = pattern.find("x");
            
            try {
                uint32_t first = std::stoul(pattern.substr(0, xPos));
                uint32_t second = std::stoul(pattern.substr(xPos + 1));
                
                if (first >= 1 && first <= 12 && second >= 1 && second <= 12) {
                    m_horTiles = first;
                    m_verTiles = second;
                    NS_LOG_INFO("Found tiling pattern: " << m_horTiles << "x" << m_verTiles);
                    break;
                }
            } catch (...) {
                // Pattern not valid numbers, continue searching
            }
        }
        patternPos = mpdContent.find("x", patternPos + 1);
    }
    
    // Parse segment duration
    m_segmentDuration = Seconds(1.0);
    size_t durationPos = mpdContent.find("duration=\"");
    if (durationPos != std::string::npos) {
        durationPos += 10;
        size_t endPos = mpdContent.find("\"", durationPos);
        if (endPos != std::string::npos) {
            std::string durationStr = mpdContent.substr(durationPos, endPos - durationPos);
            
            uint32_t timescale = 1;
            size_t timescalePos = mpdContent.find("timescale=\"");
            if (timescalePos != std::string::npos) {
                timescalePos += 11;
                size_t timescaleEnd = mpdContent.find("\"", timescalePos);
                if (timescaleEnd != std::string::npos) {
                    try {
                        timescale = std::stoul(mpdContent.substr(timescalePos, timescaleEnd - timescalePos));
                    } catch (...) {
                        timescale = 1;
                    }
                }
            }
            
            try {
                double duration = std::stod(durationStr) / timescale;
                m_segmentDuration = Seconds(duration);
                NS_LOG_INFO("Segment duration: " << m_segmentDuration.GetSeconds() << "s");
            } catch (...) {
                NS_LOG_WARN("Invalid duration format, using default 1s");
            }
        }
    }
    
    // Parse segment range
    m_startSegmentNumber = 1;
    size_t startNumPos = mpdContent.find("startNumber=\"");
    if (startNumPos != std::string::npos) {
        startNumPos += 13;
        size_t endPos = mpdContent.find("\"", startNumPos);
        if (endPos != std::string::npos) {
            try {
                m_startSegmentNumber = std::stoul(mpdContent.substr(startNumPos, endPos - startNumPos));
            } catch (...) {
                m_startSegmentNumber = 1;
            }
        }
    }
    
    // Calculate end segment
    m_endSegmentNumber = m_client->GetVideoSegments()-1; // Default
    size_t mediaDurationPos = mpdContent.find("mediaPresentationDuration=\"");
    if (mediaDurationPos != std::string::npos) {
        mediaDurationPos += 27;
        size_t endPos = mpdContent.find("\"", mediaDurationPos);
        if (endPos != std::string::npos) {
            std::string durationStr = mpdContent.substr(mediaDurationPos, endPos - mediaDurationPos);
            double totalSeconds = ParseIsoDuration(durationStr);
            if (totalSeconds > 0 && m_segmentDuration.GetSeconds() > 0) {
                m_endSegmentNumber = m_startSegmentNumber + 
                    static_cast<uint32_t>(std::ceil(totalSeconds / m_segmentDuration.GetSeconds())) - 1;
            }
        }
    }
    
    // Parse quality levels
    m_qualityLevels.clear();
    m_bandwidths.clear();
    
    std::set<uint32_t> uniqueQualities;
    std::map<uint32_t, uint32_t> qualityBandwidthMap;
    
    size_t repPos = 0;
    while ((repPos = mpdContent.find("<Representation", repPos)) != std::string::npos) {
        size_t repEnd = mpdContent.find(">", repPos);
        if (repEnd == std::string::npos) break;
        
        std::string repLine = mpdContent.substr(repPos, repEnd - repPos);
        
        // Extract ID
        size_t idPos = repLine.find("id=\"");
        if (idPos != std::string::npos) {
            idPos += 4;
            size_t idEnd = repLine.find("\"", idPos);
            if (idEnd != std::string::npos) {
                std::string idStr = repLine.substr(idPos, idEnd - idPos);
                
                // Extract quality level (first number before underscore)
                size_t underscorePos = idStr.find("_");
                std::string qualityStr = (underscorePos != std::string::npos) ? 
                    idStr.substr(0, underscorePos) : idStr;
                
                try {
                    uint32_t quality = std::stoul(qualityStr);
                    
                    // Extract bandwidth
                    size_t bwPos = repLine.find("bandwidth=\"");
                    if (bwPos != std::string::npos) {
                        bwPos += 11;
                        size_t bwEnd = repLine.find("\"", bwPos);
                        if (bwEnd != std::string::npos) {
                            uint32_t bandwidth = std::stoul(repLine.substr(bwPos, bwEnd - bwPos));
                            uniqueQualities.insert(quality);
                            qualityBandwidthMap[quality] = bandwidth;
                        }
                    }
                } catch (...) {
                    // Skip invalid quality values
                }
            }
        }
        repPos = repEnd + 1;
    }
    
    // Convert to sorted vectors
    for (uint32_t quality : uniqueQualities) {
        m_qualityLevels.push_back(quality);
        if (qualityBandwidthMap.find(quality) != qualityBandwidthMap.end()) {
            m_bandwidths.push_back(qualityBandwidthMap[quality]);
        }
    }
    
    // Sort quality levels
    std::sort(m_qualityLevels.begin(), m_qualityLevels.end());
    
    NS_LOG_INFO("MPD parsing completed:");
    NS_LOG_INFO("  Video: " << m_videoName);
    NS_LOG_INFO("  Tiling: " << m_horTiles << "x" << m_verTiles);
    NS_LOG_INFO("  Segment duration: " << m_segmentDuration.GetSeconds() << "s");
    NS_LOG_INFO("  Segment range: " << m_startSegmentNumber << "-" << m_endSegmentNumber);
    NS_LOG_INFO("  Quality levels: " << m_qualityLevels.size());
    
    for (size_t i = 0; i < m_qualityLevels.size(); i++) {
        if (i < m_bandwidths.size()) {
            NS_LOG_INFO("    Q" << m_qualityLevels[i] << ": " << m_bandwidths[i] << " bps");
        }
    }
    
    return true;
}
double TiledDashClientTileRequester::ParseIsoDuration(const std::string& isoDuration) {
    // Parse ISO 8601 duration format: PT1H2M3.4S
    double totalSeconds = 0;
    
    size_t pos = isoDuration.find("PT");
    if (pos == std::string::npos) return 0;
    pos += 2;
    
    // Parse hours
    size_t hPos = isoDuration.find("H", pos);
    if (hPos != std::string::npos) {
        std::string hours = isoDuration.substr(pos, hPos - pos);
        totalSeconds += std::stod(hours) * 3600;
        pos = hPos + 1;
    }
    
    // Parse minutes
    size_t mPos = isoDuration.find("M", pos);
    if (mPos != std::string::npos && mPos < isoDuration.find("S", pos)) {
        std::string minutes = isoDuration.substr(pos, mPos - pos);
        totalSeconds += std::stod(minutes) * 60;
        pos = mPos + 1;
    }
    
    // Parse seconds
    size_t sPos = isoDuration.find("S", pos);
    if (sPos != std::string::npos) {
        std::string seconds = isoDuration.substr(pos, sPos - pos);
        totalSeconds += std::stod(seconds);
    }
    
    return totalSeconds;
}


} // namespace ns3
