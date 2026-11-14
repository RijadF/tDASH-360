#include "tiled-dash-client-media-buffer.h"
#include "tiled-dash-client.h"
#include "tiled-dash-client-media-decoder.h"
#include <algorithm>
#include <cmath> // Required for std::abs
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <iomanip>
#include <set>

//#include <H5Cpp.h>   // inside your .cc file

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TiledDashClientMediaBuffer");

NS_OBJECT_ENSURE_REGISTERED (TiledDashClientMediaBuffer);

TypeId
TiledDashClientMediaBuffer::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::TiledDashClientMediaBuffer")
        .SetParent<Object> ()
        .SetGroupName("Applications")
        .AddConstructor<TiledDashClientMediaBuffer> ()
        .AddAttribute ("NTiles",
                        "The total number of tiles per segment.",
                        UintegerValue (24),
                        MakeUintegerAccessor (&TiledDashClientMediaBuffer::m_nTiles),
                        MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("MinTiles",
                        "The minimum number of downloaded tiles required for a segment to be considered somewhat complete for certain logic (e.g., GetNextSegmentId).",
                        UintegerValue (4),
                        MakeUintegerAccessor (&TiledDashClientMediaBuffer::m_minTiles),
                        MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("MaxBufferedSegments",
                        "The maximum number of unconsumed segments the buffer should hold before being considered full.",
                        UintegerValue (10),
                        MakeUintegerAccessor (&TiledDashClientMediaBuffer::m_maxBufferedSegments),
                        MakeUintegerChecker<uint32_t> ());
    return tid;
}

TiledDashClientMediaBuffer::TiledDashClientMediaBuffer()
    : m_consumedSegment(0), 
      m_isFull(false),
      m_totalStallDuration(Time(0)),
      m_lastStallUpdateTime(Time(0)),
      m_currentPlayoutState(BUFFERING),
      m_lastStateUpdateTime(Simulator::Now()),
      m_totalPlayoutTime(Time(0)),
      m_playoutStartTime(Time(0)),
      m_cumulativeCompletionTime(Time(0)),
      m_actualPlayoutTime(Time(0)),
      m_playoutPaused(false),
      m_bufferDurationLevel(Time(0)), 
      m_nTiles(24), 
      m_minTiles(24), 
      //m_nTiles(25), 
      //m_minTiles(25),
      m_lastFullyViewedSegment(-1),
      m_clientId(0),
      m_runId(0),
      m_lastTileFetchCompletionTime(Time(0)),
      m_lastTileFetchDuration(Time(0)),
      m_lastLogTime(Time(0)),  // NEW: Initialize new member
      m_loggingStopped(false)
{
    NS_LOG_FUNCTION (this);
    
    Simulator::Schedule(MilliSeconds(10000), &TiledDashClientMediaBuffer::PeriodicUpdate, this);
    
}

TiledDashClientMediaBuffer::~TiledDashClientMediaBuffer ()
{
    NS_LOG_FUNCTION (this);
}

uint32_t TiledDashClientMediaBuffer::GetTilesInSegment(uint32_t segmentId) {
    NS_LOG_INFO("=== GetTilesInSegment Debug for segment " << segmentId << " ===");
    
    if (m_buffer.find(segmentId) == m_buffer.end()) {
        NS_LOG_INFO("Segment " << segmentId << " not found in buffer");
        return 0;
    }
    
    const std::vector<TileInfo>& tiles = m_buffer.at(segmentId);
    NS_LOG_INFO("Segment " << segmentId << " vector size: " << tiles.size());
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < tiles.size(); ++i) {
        const TileInfo& tileInfo = tiles[i];
        NS_LOG_INFO("  Tile " << i << ": size=" << tileInfo.size << ", tileId=" << tileInfo.tileId);
        
        if (tileInfo.size > 0) {
            count++;
        }
    }
    
    NS_LOG_INFO("Final tile count for segment " << segmentId << ": " << count);
    return count;
}

uint32_t TiledDashClientMediaBuffer::GetUnconsumedSegments() {
    NS_LOG_INFO("=== GetUnconsumedSegments Debug ===");
    NS_LOG_INFO("m_consumedSegment: " << m_consumedSegment);
    NS_LOG_INFO("m_minTiles: " << m_minTiles);
    NS_LOG_INFO("Buffer size: " << m_buffer.size());
    
    uint32_t storedSegments = 0;
    std::vector<uint32_t> segmentIds;
    
    // Debug: Print all segments in buffer
    for (const auto& segmentPair : m_buffer) {
        uint32_t segmentId = segmentPair.first;
        uint32_t tilesInSegment = GetTilesInSegment(segmentId);
        NS_LOG_INFO("Segment " << segmentId << " has " << tilesInSegment << " tiles (size: " << segmentPair.second.size() << ")");
        
        if (segmentPair.first > m_consumedSegment) {
            segmentIds.push_back(segmentPair.first);
            NS_LOG_INFO("  -> Added to segmentIds (> consumed segment " << m_consumedSegment << ")");
        } else {
            NS_LOG_INFO("  -> Skipped (not > consumed segment " << m_consumedSegment << ")");
        }
    }
    
    std::sort(segmentIds.begin(), segmentIds.end());
    NS_LOG_INFO("Sorted segmentIds size: " << segmentIds.size());

    for (uint32_t segmentId : segmentIds) {
        uint32_t tilesInSegment = GetTilesInSegment(segmentId);
        NS_LOG_INFO("Checking segment " << segmentId << ": has " << tilesInSegment << " tiles, needs " << m_minTiles);
        
        if (tilesInSegment >= m_minTiles) {
            storedSegments++;
            NS_LOG_INFO("  -> Counted as stored segment (total now: " << storedSegments << ")");
        } else {
            NS_LOG_INFO("  -> Breaking at incomplete segment " << segmentId);
            break;
        }
    }
    
    NS_LOG_INFO("Final unconsumed segments count: " << storedSegments);
    NS_LOG_INFO("=== End Debug ===");
    
    return storedSegments;
}

bool TiledDashClientMediaBuffer::IsSegmentFullyViewed(uint32_t segmentId) {
    auto segmentIt = m_buffer.find(segmentId);
    if (segmentIt == m_buffer.end()) {
        return false;
    }
    
    uint32_t viewedTiles = 0;
    uint32_t tilesWithFovScoreZero = 0;
    uint32_t downloadedTiles = 0;
    
    for (uint32_t tileId = 0; tileId < segmentIt->second.size(); ++tileId) {
        if (segmentIt->second[tileId].size > 0) {  // Tile exists
            downloadedTiles++;
            
            // Only count tiles that were in viewport center (fovScore == 0)
            if (segmentIt->second[tileId].fovScore == 0) {
                tilesWithFovScoreZero++;
                if (segmentIt->second[tileId].isViewed) {
                    viewedTiles++;
                }
            }
        }
    }
    
    NS_LOG_INFO("IsSegmentFullyViewed S" << segmentId << ": downloaded=" << downloadedTiles 
                << ", fovScore==0 tiles=" << tilesWithFovScoreZero 
                << ", viewed=" << viewedTiles 
                << ", minTiles=" << m_minTiles);
    
    // Segment is fully viewed if:
    // 1. We have minimum tiles downloaded
    // 2. All tiles with fovScore==0 have been viewed (OR there are no fovScore==0 tiles yet)
    return (downloadedTiles >= m_minTiles) && 
           (viewedTiles >= tilesWithFovScoreZero); //|| tilesWithFovScoreZero == 0);
}

void TiledDashClientMediaBuffer::StoreTile(uint32_t segmentId, TileInfo tileData)
{
    NS_LOG_INFO("StoreTile: Segment " << segmentId << ", Tile " << tileData.tileId << ", Size " << tileData.size);
    NS_LOG_ERROR("=== StoreTile CALLED === Segment: " << segmentId << ", Tile: " << tileData.tileId << ", Size: " << tileData.size);
    NS_LOG_FUNCTION(this << segmentId);
    NS_ASSERT_MSG(m_client, "m_client is null");
    UpdateAllMetrics();
    
    if (m_buffer.find(segmentId) == m_buffer.end())
    {
        m_buffer[segmentId].resize(m_nTiles);
    }
    //Added
    if (IsTileBuffered(segmentId, tileData.tileId)) {                // NEW
        NS_LOG_WARN("StoreTile: duplicate S" << segmentId << " T" << tileData.tileId << " ignored");
        return;
    }
    Time currentTime = Simulator::Now();
    
    m_cumulativeCompletionTime += tileData.fetchDuration;

    // Get tiles count BEFORE storing the new tile
    uint32_t tilesBefore = GetTilesInSegment(segmentId);
    
    // Store the tile
    m_buffer[segmentId][tileData.tileId] = tileData;
    
    // Get tiles count AFTER storing the new tile
    uint32_t tilesAfter = GetTilesInSegment(segmentId);

    // Only increment BufferDurationLevel when minimum required tiles are downloaded--- bilo u if ,tilesBefore < m_minTiles && 
    if (tilesAfter >= m_minTiles  && segmentId > m_consumedSegment) {
        NS_LOG_INFO("StoreTile: Segment " << segmentId << " now has minimum required tiles. Tiles before: " << tilesBefore << ", tiles after: " << tilesAfter << " (minimum required: " << m_minTiles << ")");
        Time segmentDuration = m_client->GetSegmentDuration();
        Time maxBufferDuration = m_maxBufferedSegments * segmentDuration;

        if(!m_segmentsContributingToBuffer.count(segmentId)){
            // MODIFIED: Proactive buffer management logic
            if (m_bufferDurationLevel >= maxBufferDuration) {
                NS_LOG_WARN("StoreTile: Buffer is already full (BDL: " << m_bufferDurationLevel.GetSeconds() 
                            << "s >= " << maxBufferDuration.GetSeconds() << "s). Not adding more duration for segment " << segmentId);
                m_isFull = true; // Ensure the full flag is set
            } else {
                m_bufferDurationLevel += segmentDuration;
                if (m_bufferDurationLevel >= maxBufferDuration) {
                    NS_LOG_WARN("StoreTile: Buffer is already full (BDL: " << m_bufferDurationLevel.GetSeconds() 
                                << "s >= " << maxBufferDuration.GetSeconds() << "s). Not adding more duration for segment " << segmentId);
                    m_isFull = true; // Ensure the full flag is set
                }
                //
                if (m_bufferDurationLevel > maxBufferDuration) {
                    NS_LOG_INFO("StoreTile: Buffer overflow detected. Capping BDL from " << m_bufferDurationLevel.GetSeconds() 
                                << "s to " << maxBufferDuration.GetSeconds() << "s.");
                    m_bufferDurationLevel = maxBufferDuration; // Cap the buffer at its max duration
                }
                NS_LOG_INFO("BufferDurationLevel: +" << segmentDuration.GetSeconds() << "s for segment " << segmentId 
                            << ". New BDL: " << m_bufferDurationLevel.GetSeconds() << "s");
            }
            
            m_segmentsContributingToBuffer.insert(segmentId);

        }
        else {
            NS_LOG_INFO("Segment " << segmentId << " already contributed to buffer, skipping buffer level addition");
        }
    }
    
    // Notify the MediaDecoder about the new tile.
    m_client->GetMediaDecoder()->newTileReceived(segmentId, tileData.tileId);

    // Ensure BufferDurationLevel does not go below zero
    m_bufferDurationLevel = std::max(Time(0), m_bufferDurationLevel);
    
    // Check buffer level after all operations
    //CheckBufferLevel();

    NS_LOG_INFO("StoreTile: Current Buffer Level (Calculated) is " << (m_client->GetSegmentDuration() * GetUnconsumedSegments()).GetSeconds() << "s with " << GetUnconsumedSegments() << " unconsumed segments.");
    NS_LOG_INFO("StoreTile: New BufferDurationLevel (Managed) is " << m_bufferDurationLevel.GetSeconds() << "s.");

    if (GetUnconsumedSegments() >= m_maxBufferedSegments)
        m_isFull = true;
    // Flush the log after all state changes have occurred
    FlushTileInfoLog();
}
// *** NEW METHOD: Check buffer level and trigger rebuffering *** 
void TiledDashClientMediaBuffer::CheckBufferLevel()
{
    NS_LOG_FUNCTION(this);
    
    // Only check during PLAYOUT state to avoid false triggers and prevent recursion, (bufferDepleted && m_client->GetMediaBuffer()->GetUnconsumedSegments()==0)// ja prosirio sa getUnconsumed segments
    if ((m_currentPlayoutState == PLAYOUT)  && (m_bufferDurationLevel <= Time(0) &&m_client->GetMediaBuffer()->GetUnconsumedSegments()==0)) {
        NS_LOG_WARN("CheckBufferLevel: BufferDurationLevel has reached zero during PLAYOUT! Triggering immediate REBUFFERING notification.");
        
        // Set a flag to prevent recursive calls
        static bool bufferCheckInProgress = false;
        if (bufferCheckInProgress) {
            NS_LOG_WARN("CheckBufferLevel: Already processing buffer check, avoiding recursion");
            return;
        }
        
        bufferCheckInProgress = true;
        
        // Go to REBUFFERING
        NS_LOG_INFO("CheckBufferLevel: Transitioning from PLAYOUT to REBUFFERING due to buffer depletion.");
        UpdatePlayoutState(REBUFFERING);
        
        // Notify the MediaDecoder about buffer depletion
        if (m_client && m_client->GetMediaDecoder()) {
            NS_LOG_INFO("CheckBufferLevel: Notifying MediaDecoder to handle buffer depletion.");
            m_client->GetMediaDecoder()->HandleBufferDepletion();
        }
        
        bufferCheckInProgress = false;
    }
}

bool TiledDashClientMediaBuffer::IsTileBuffered(uint32_t segmentId, uint32_t tileId) {
    NS_LOG_FUNCTION(this << segmentId << tileId);
    NS_LOG_INFO("Bufferd : S"<<segmentId<<" T: "<<tileId);
    auto segmentIt = m_buffer.find(segmentId);
    if (segmentIt == m_buffer.end()) {
        return false;
    }
    if (tileId >= segmentIt->second.size()) {
        NS_LOG_ERROR("Error : Tileid is too big.");
        return false;
    }
    return segmentIt->second[tileId].size > 0;
}

uint32_t TiledDashClientMediaBuffer::GetNextSegmentId() {
    NS_LOG_FUNCTION(this);
    uint32_t nextSegmentToRequest = m_consumedSegment + 1;

    for (uint32_t segmentId = nextSegmentToRequest; segmentId < m_consumedSegment + m_maxBufferedSegments; ++segmentId) {
        if (m_buffer.find(segmentId) == m_buffer.end()) {
            return segmentId;
        }

        uint32_t downloadedTiles = GetTilesInSegment(segmentId);

        if (downloadedTiles < m_minTiles) {
            return segmentId;
        }
    }

    return nextSegmentToRequest;
}

void TiledDashClientMediaBuffer::UpdateTileStallDuration(uint32_t segmentId, uint32_t tileId, Time stallDuration) {
    NS_LOG_FUNCTION(this << segmentId << tileId << stallDuration);
    NS_LOG_DEBUG("=== UPDATE TILE STALL DURATION CALLED ===");
    m_buffer[segmentId][tileId].stallDuration = stallDuration;
    
    // Check if segment exists
    if (m_buffer.find(segmentId) == m_buffer.end()) {
        NS_LOG_DEBUG("Segment " << segmentId << " not found in buffer!");
        NS_LOG_DEBUG("Available segments: ");
        for (const auto& pair : m_buffer) {
            NS_LOG_DEBUG("  - Segment " << pair.first);
        }
    } else {
        NS_LOG_DEBUG("Segment " << segmentId << " found in buffer");
        
        // Check if tileId is within bounds
        if (tileId >= m_buffer[segmentId].size()) {
            NS_LOG_DEBUG("Tile " << tileId << " out of bounds for segment " << segmentId 
                         << " (buffer size: " << m_buffer[segmentId].size() << ")");
        } else {
            NS_LOG_DEBUG("ENTERED - Tile " << tileId << " is within bounds");
            Time oldStallDuration = m_buffer[segmentId][tileId].stallDuration;
            m_buffer[segmentId][tileId].stallDuration = stallDuration;
            Time stallDifference = stallDuration - oldStallDuration;
            m_totalStallDuration += stallDifference;
            m_cumulativeCompletionTime += stallDifference;
            m_lastStallUpdateTime = Simulator::Now();
            
            NS_LOG_WARN("UpdateTileStallDuration: Updated tile stall from "
                        << oldStallDuration.GetSeconds() << "s to " << stallDuration.GetSeconds()
                        << "s, total stall now " << m_totalStallDuration.GetSeconds() << "s"
                        << ", cumulative time now " << m_cumulativeCompletionTime.GetSeconds() << "s");
            NS_LOG_WARN("Tile stall on its level " << m_buffer[segmentId][tileId].stallDuration.GetSeconds() << "s");
        }
    }
    FlushTileInfoLog();
}

std::vector<TiledDashClientMediaBuffer::TileConsumptionState>
TiledDashClientMediaBuffer::ConsumeTiles(uint32_t segmentId, const std::vector<int>& fovScores) {
    NS_LOG_FUNCTION(this << segmentId);
    NS_LOG_INFO("=== ConsumeTiles ENTERED === Segment: " << segmentId);
    NS_LOG_WARN("USAO U CONSUME TILE BUFFER "<<Simulator::Now()<<" with segment "<<segmentId);
    //Logging
    //for(auto f:fovScores){
    //    NS_LOG_WARN("CUrrent fovScores "<<f);
    //}
    //for (uint32_t tileId = 0; tileId < fovScores.size(); ++tileId) {
        // Check if segmentId exists
    //    if (IsTileBuffered(segmentId,tileId)) {
    //        NS_LOG_WARN("Segment: " << segmentId 
    //                      << " | Tile: " << tileId 
    //                      << " | FoV Score: " << m_buffer[segmentId][tileId].fovScore);
    //    } else {
    //        NS_LOG_WARN("Data missing in m_buffer for Segment " << segmentId << " Tile " << tileId);
    //    }
    //}
    UpdateAllMetrics();

    std::vector<TileConsumptionState> qualityScores;
    
    if (m_buffer.find(segmentId) == m_buffer.end()) {
        NS_LOG_INFO("ConsumeTiles: Segment " << segmentId << " not in buffer");
        for (uint32_t tileId = 0; tileId < fovScores.size(); ++tileId) {
            if (fovScores[tileId] >= 0) {
                qualityScores.push_back(TILE_UNAVAILABLE);
            } else {
                qualityScores.push_back(TILE_UNNEEDED);
            }
        }
        return qualityScores;
    }
    bool missingTiles = false;
    uint32_t availableTiles = 0;
    uint32_t tilesWithFovScoreZero = 0;
    
    //Mark tiles as viewed ONLY if fovScore == 0
    for (uint32_t tileId = 0; tileId < fovScores.size(); ++tileId) {
        if (fovScores[tileId] >= 0 && fovScores[tileId]<=3) {  // Tile is required
            if (IsTileBuffered(segmentId, tileId)) {
                //  Only mark as viewed if fovScore == 0 (in viewport center)
                if (fovScores[tileId] == 0) {
                    m_buffer[segmentId][tileId].isViewed = true;
                    tilesWithFovScoreZero++;
                    NS_LOG_INFO("ConsumeTiles: S" << segmentId << " T" << tileId 
                              << " marked as VIEWED (fovScore==0)");
                } else {
                    // Don't mark as viewed, but tile is still available
                    m_buffer[segmentId][tileId].isViewed = false;
                    NS_LOG_INFO("ConsumeTiles: S" << segmentId << " T" << tileId 
                              << " available but NOT viewed (fovScore==" << fovScores[tileId] << ")");
                }
                
                // Update fovScore
                m_buffer[segmentId][tileId].fovScore = std::max(
                    m_buffer[segmentId][tileId].fovScore, fovScores[tileId]);
            
                qualityScores.push_back(TILE_AVAILABLE);
                availableTiles++;
            } else {
                qualityScores.push_back(TILE_UNAVAILABLE);
                missingTiles = true;
            }
        } else {
            qualityScores.push_back(TILE_UNNEEDED);
            availableTiles++;
        }
    }
    
    NS_LOG_INFO("ConsumeTiles S" << segmentId << ": available=" << availableTiles 
                << ", fovScore==0=" << tilesWithFovScoreZero 
                << ", missing=" << missingTiles);

    // CRITICAL: Consumption based on AVAILABILITY, not viewing status
    // This is the key to maintaining synchronization!,
    if (availableTiles >= m_minTiles) {
        NS_LOG_INFO("ConsumeTiles: Segment " << segmentId << " has minimum required tiles available");
        
        // Synchronize with MediaDecoder state
        if (m_client && m_client->GetMediaDecoder()) {
            auto decoderState = m_client->GetMediaDecoder()->GetPlayoutState();
            PlayoutState expectedBufferState;
            switch (decoderState) {
                case TiledDashClientMediaDecoder::PLAYOUT: expectedBufferState = PLAYOUT; break;
                case TiledDashClientMediaDecoder::REBUFFERING: expectedBufferState = REBUFFERING; break;
                case TiledDashClientMediaDecoder::BUFFERING: expectedBufferState = BUFFERING; break;
                case TiledDashClientMediaDecoder::FOVCHANGE: expectedBufferState = FOVCHANGE; break;
                default: expectedBufferState = m_currentPlayoutState;
            }
            
            if (m_currentPlayoutState != expectedBufferState) {
                NS_LOG_INFO("ConsumeTiles: Syncing state from " << GetStateString(m_currentPlayoutState) 
                          << " to " << GetStateString(expectedBufferState));
                m_currentPlayoutState = expectedBufferState;
            }
        }
        
        // Check if segment was already consumed
        bool segmentAlreadyConsumed = (m_consumedSegment > segmentId) || 
                             (m_consumedSegment == segmentId && IsSegmentFullyViewed(segmentId));
        // Check if segment was already consumed. This prevents double-counting.
        // We only consume if the current segmentId is greater than the last consumed one.
        if (segmentId > m_consumedSegment) {
            m_consumedSegment = segmentId;

            // This is the synchronized consumption event
            Time segmentDuration = m_client->GetSegmentDuration();
            
            // 1. Decrease the managed buffer level
            m_bufferDurationLevel -= segmentDuration;
            
            // 2. Increase the actual playout time
            m_actualPlayoutTime += segmentDuration;
            
            // 3. Mark the buffer as not full since we just consumed a segment
            //m_isFull = false; //ne znamo jos dok ne provjerimo buffelevel

            NS_LOG_INFO("ConsumeTiles: *** SYNCHRONIZED CONSUMPTION *** S" << segmentId 
                      << ". Deducted " << segmentDuration.GetSeconds() << "s. "
                      << "New BDL: " << m_bufferDurationLevel.GetSeconds() << "s. "
                      << "New ActualPlayoutTime: " << m_actualPlayoutTime.GetSeconds() << "s. "
                      << "New Unconsumed Segments: " << GetUnconsumedSegments());

            // Update fully viewed tracker
            if (IsSegmentFullyViewed(segmentId)) {
                m_lastFullyViewedSegment = segmentId;
                NS_LOG_INFO("ConsumeTiles: S" << segmentId << " is FULLY VIEWED");
            } else {
                // Keep the previous value if the new one isn't fully viewed.
                // Or set to -1 if you want to track only the immediately last one.
                m_lastFullyViewedSegment = -1;
            }

            CheckBufferLevel();
        } else {
             NS_LOG_INFO("ConsumeTiles: S" << segmentId << " already consumed or is current segment (m_consumedSegment: " 
                      << m_consumedSegment << "). No action taken.");
        }                 
        // Ensure BufferDurationLevel never goes negative
        m_bufferDurationLevel = std::max(Time(0), m_bufferDurationLevel);
        
        if (GetUnconsumedSegments() < m_maxBufferedSegments) {
            m_isFull = false;
        }
    } else if (!missingTiles && availableTiles < m_minTiles) {
        NS_LOG_INFO("ConsumeTiles: S" << segmentId << " has only " << availableTiles 
                  << " tiles (need " << m_minTiles << "). Not ready.");
    } else {
        NS_LOG_INFO("ConsumeTiles: S" << segmentId << " has missing tiles. Cannot consume yet.");
    }

    NS_LOG_INFO("ConsumeTiles: Calculated Buffer: " 
                << (m_client->GetSegmentDuration() * GetUnconsumedSegments()).GetSeconds() << "s ("
                << GetUnconsumedSegments() << " segments)");
    NS_LOG_INFO("ConsumeTiles: BufferDurationLevel: " << m_bufferDurationLevel.GetSeconds() << "s");
    NS_LOG_INFO("ConsumeTiles: ActualPlayoutTime: " << m_actualPlayoutTime.GetSeconds() << "s");
    NS_LOG_INFO("=== ConsumeTiles EXITED ===\n");

    FlushTileInfoLog();
    return qualityScores;
}

void TiledDashClientMediaBuffer::SetMinTiles(uint32_t minTiles)
{
    m_minTiles = minTiles;
}

void TiledDashClientMediaBuffer::SetnTiles(uint32_t nTiles)
{
    m_nTiles = nTiles;
}

bool TiledDashClientMediaBuffer::endPlayout(uint32_t segmentId)
{
    // Compute the end time as (last segment index) * segmentDuration
    Time videoEnd = (m_client->GetVideoSegments() - 1) * m_client->GetSegmentDuration();
    
    uint32_t viewedRequiredTiles = 0;
    for (uint32_t tileId = 0; tileId < m_nTiles; ++tileId) {
        if (IsTileBuffered(segmentId, tileId)) {
            // (optional) only count viewed tiles if needed
            viewedRequiredTiles++;
        }
    }
    
    return (viewedRequiredTiles >= m_minTiles && m_actualPlayoutTime == videoEnd);
}
void TiledDashClientMediaBuffer::StopApplication()
{
    NS_LOG_FUNCTION(this);
    
    if (m_loggingStopped) {
        NS_LOG_INFO("MediaBuffer: Logging already stopped for client " << m_clientId);
        return;
    }
    
    NS_LOG_INFO("MediaBuffer: Stopping application and logging for client " << m_clientId);
    
    // Set the flag to stop all future logging
    if(endPlayout(m_client->GetVideoSegments()-1))//bilo 9, bilo 60
        m_loggingStopped = true;
        //m_client->GetNetworkTracker()->CloseNetRateLogging();
    // Perform one final flush of current data
    if (m_clientId > 0 && m_runId > 0) {
        //string basePATH to your file
        std::string basePath = "Desktop/ns-allinone-3.44/ns-3.44/";
        std::string txtFileName = basePath + "tile_info_log_" + std::to_string(m_clientId) + "_" + std::to_string(m_runId) + ".txt";
        std::string csvFileName = basePath + "tile_info_log_" + std::to_string(m_clientId) + "_" + std::to_string(m_runId) + ".csv";
        
        // Write final summary to TXT file
        std::ofstream txtFile(txtFileName, std::ios::app);
        if (txtFile.is_open()) {
            txtFile << "\n=== FINAL SUMMARY (Client: " << m_clientId << ", Run: " << m_runId << ") - " 
                    << Simulator::Now().GetSeconds() << "s ===\n";
            txtFile << "Logging stopped - Client completed\n";
            txtFile << "Final Buffer Duration: " << m_bufferDurationLevel.GetSeconds() << "s\n";
            txtFile << "Final Actual Playout Time: " << m_actualPlayoutTime.GetSeconds() << "s\n";
            txtFile << "Final Total Stall Duration: " << m_totalStallDuration.GetSeconds() << "s\n";
            txtFile << "Final Cumulative Completion Time: " << m_cumulativeCompletionTime.GetSeconds() << "s\n";
            txtFile << "=== END OF LOG ===\n";
            txtFile.close();
        }
        
        // Write final summary to CSV file
        std::ofstream csvFile(csvFileName, std::ios::app);
        if (csvFile.is_open()) {
            // Add a final summary row to CSV
            csvFile << m_clientId << ","
                    << m_runId << ","
                    << "FINAL" << ","  // SegmentId
                    << "SUMMARY" << ","  // TileId
                    << "0" << ","  // Quality
                    << "0" << ","  // Size
                    << "0" << ","  // FetchDuration
                    << m_bufferDurationLevel.GetSeconds() << ","  // BufferLevel
                    << "0" << ","  // FovScore
                    << "0" << ","  // IsViewed
                    << m_totalStallDuration.GetSeconds() << ","  // StallDuration
                    << "CLIENT_COMPLETED" << ","  // CurrentPlayoutState
                    << Simulator::Now().GetSeconds() << ","  // SimulationTime
                    << m_cumulativeCompletionTime.GetSeconds() << ","  // CumulativeCompletionTime
                    << m_actualPlayoutTime.GetSeconds() << ","  // ActualPlayoutTime
                    << m_bufferDurationLevel.GetSeconds() << ","  // BufferDurationLevel
                    << std::endl;
            csvFile.close();
        }
        
        std::cout << "MediaBuffer: Final log written to both TXT and CSV for client " << m_clientId 
                  << " at " << Simulator::Now().GetSeconds() << "s" << std::endl;
    }
    
    NS_LOG_INFO("MediaBuffer: Application stopped for client " << m_clientId);
}

void TiledDashClientMediaBuffer::FlushTileInfoLog()
{
    NS_LOG_FUNCTION(this);
    if(IsSegmentFullyTiled(m_client->GetVideoSegments()-1)){
        m_client->GetNetworkTracker()->CloseNetRateLogging();
    }
    
    Time currentTime = Simulator::Now();
    
    if (m_loggingStopped) {
        NS_LOG_DEBUG("FlushTileInfoLog: Logging stopped for client " << m_clientId << ". Skipping log flush.");
        return;
    }

    // CHECK: Only log if 100ms have passed since last log,10000, moze 800, 500, 400 min
    if (currentTime - m_lastLogTime < MilliSeconds(10000)) {
        NS_LOG_DEBUG("FlushTileInfoLog: Skipping log - only " << (currentTime - m_lastLogTime).GetMilliSeconds() << "ms since last log");
        return;
    }
    
    // Update the last log time
    m_lastLogTime = currentTime;
    if (m_clientId == 0 || m_runId == 0) {
        NS_LOG_INFO("FlushTileInfoLog: ClientId or RunId is not yet set. Skipping logging.");
        return;
    }
    UpdateAllMetrics();
    //string basePATH to your file
    std::string basePath = "Desktop/ns-allinone-3.44/ns-3.44/";
    std::string txtFileName = basePath + "tile_info_log_" + std::to_string(m_clientId) + "_" + std::to_string(m_runId) + ".txt";
    std::string csvFileName = basePath + "tile_info_log_" + std::to_string(m_clientId) + "_" + std::to_string(m_runId) + ".csv";
    
    std::ofstream txtFile(txtFileName, std::ios::app);
    std::ofstream csvFile(csvFileName, std::ios::app);
    
    if (!txtFile.is_open()) {
        NS_LOG_ERROR("Failed to open TXT file: " << txtFileName);
        return;
    }
    
    if (!csvFile.is_open()) {
        NS_LOG_ERROR("Failed to open CSV file: " << csvFileName);
        txtFile.close();
        return;
    }
    
    std::string currentMediaDecoderState = "UNKNOWN";
    Time currentSimulationTime = Simulator::Now();
    
    if (m_client && m_client->GetMediaDecoder()) {
        auto decoderState = m_client->GetMediaDecoder()->GetPlayoutState();
        switch (decoderState) {
            case TiledDashClientMediaDecoder::BUFFERING:
                currentMediaDecoderState = "BUFFERING";
                break;
            case TiledDashClientMediaDecoder::REBUFFERING:
                currentMediaDecoderState = "REBUFFERING";
                break;
            case TiledDashClientMediaDecoder::PLAYOUT:
                currentMediaDecoderState = "PLAYOUT";
                break;
            case TiledDashClientMediaDecoder::FOVCHANGE:
                currentMediaDecoderState = "FOVCHANGE";
                break;
            default:
                currentMediaDecoderState = "UNKNOWN";
        }
    }
    
    csvFile.seekp(0, std::ios::end);
    bool isCSVEmpty = (csvFile.tellp() == 0);
    
    if (isCSVEmpty) {
        //csvFile << "ClientId,RunId,SegmentId,TileId,Quality,Size,FetchDuration,BufferLevel,FovScore,IsViewed,StallDuration,CurrentPlayoutState,SimulationTime,CumulativeCompletionTime,ActualPlayoutTime,BufferDurationLevel,ThroughputBps,NetRate\n";
        //Without netrate and throughput
        //csvFile << "ClientId,RunId,SegmentId,TileId,Quality,Size,FetchDuration,RequestTime,ReceiveTime,BufferLevel,FovScore,IsViewed,StallDuration,CurrentPlayoutState,SimulationTime,CumulativeCompletionTime,ActualPlayoutTime,BufferDurationLevel\n";
        csvFile << "ClientId,RunId,SegmentId,TileId,Quality,Size,FetchDuration,RequestTime,ReceiveTime,BufferLevel,FovScore,IsViewed,StallDuration,CurrentPlayoutState,SimulationTime,CumulativeCompletionTime,ActualPlayoutTime,BufferDurationLevel,NetRate\n";
    }
    
    Time currentBufferDurationCalculated = m_client->GetSegmentDuration() * GetUnconsumedSegments(); 
    uint32_t unconsumedSegments = GetUnconsumedSegments();
    
    txtFile << "\n=== Tile Info Log (Client: " << m_clientId << ", Run: " << m_runId << ") - " << currentSimulationTime.GetSeconds() << "s ===\n";
    txtFile << "Current MediaDecoder State: " << currentMediaDecoderState << "\n";
    txtFile << "Current Buffer State: " << GetStateString(m_currentPlayoutState) << "\n";
    txtFile << "Current Buffer Duration (Calculated): " << currentBufferDurationCalculated.GetSeconds() << "s\n";
    txtFile << "BufferDurationLevel (New): " << m_bufferDurationLevel.GetSeconds() << "s\n"; // Log the new variable
    txtFile << "Total Playout Time: " << GetTotalPlayoutTime().GetSeconds() << "s\n";
    txtFile << "Actual Playout Time: " << m_actualPlayoutTime.GetSeconds() << "s\n";
    txtFile << "Total Stall Duration: " << m_totalStallDuration.GetSeconds() << "s\n";
    txtFile << "Cumulative Completion Time: " << m_cumulativeCompletionTime.GetSeconds() << "s\n";
    txtFile << "Unconsumed Segments: " << unconsumedSegments << "\n\n";
    
    std::vector<uint32_t> segmentIds;
    for (const auto& segmentPair : m_buffer) {
        segmentIds.push_back(segmentPair.first);
    }
    std::sort(segmentIds.begin(), segmentIds.end());
    
    for (uint32_t segmentId : segmentIds) {
        //const std::vector<TileInfo>& tiles = m_buffer[segmentId];
        std::vector<TileInfo>& tiles = m_buffer[segmentId];
        std::sort(tiles.begin(), tiles.end(), [](const TileInfo& a, const TileInfo& b) {
            return a.requestTime < b.requestTime; // Returns true if 'a' comes before 'b'
        });
        for (const auto& tileInfo : tiles) {
            if (tileInfo.size > 0) {
                // Update the TXT file output
                txtFile << "Segment: " << segmentId
                        << " | Tile: " << tileInfo.tileId
                        << " | Quality: " << tileInfo.quality
                        << " | Size: " << tileInfo.size << " bytes"
                        << " | Fetch Duration: " << tileInfo.fetchDuration.GetSeconds() << "s"
                        <<"  | Request Duration: "<<tileInfo.requestTime.GetSeconds()<<"s"
                        <<"  | Receive Duration: "<<tileInfo.receiveTime.GetSeconds()<<"s"
                        << " | Buffer Level (Calculated): " << currentBufferDurationCalculated.GetSeconds() << "s"
                        << " | FOV Score: " << tileInfo.fovScore
                        << " | Is Viewed: " << (tileInfo.isViewed ? "Yes" : "No")
                        << " | Stall Duration: " << tileInfo.stallDuration.GetSeconds() << "s"
                        << " | Current State: " << currentMediaDecoderState
                        << " | Simulation Time: " << currentSimulationTime.GetSeconds() << "s"
                        << " | Cumulative Time: " << m_cumulativeCompletionTime.GetSeconds() << "s"
                        << " | Actual Playout Time: " << m_actualPlayoutTime.GetSeconds() << "s"
                        << " | BufferDurationLevel (New): " << m_bufferDurationLevel.GetSeconds() << "s"
                        //<< " | ThroughputBps: " << tileInfo.throughputBps << "bps"    // Add this line
                        << " | NetRate: " << tileInfo.netRate                     // Add this line
                        << std::endl;

                // Update the CSV file output
                csvFile << m_clientId << ","
                        << m_runId << ","
                        << segmentId << ","
                        << tileInfo.tileId << ","
                        << tileInfo.quality << ","
                        << tileInfo.size << ","
                        << tileInfo.fetchDuration.GetSeconds() << ","
                        << tileInfo.requestTime.GetSeconds()<<","
                        << tileInfo.receiveTime.GetSeconds()<<","
                        << currentBufferDurationCalculated.GetSeconds() << ","
                        << tileInfo.fovScore << ","
                        << (tileInfo.isViewed ? 1 : 0) << ","
                        << tileInfo.stallDuration.GetSeconds() << ","
                        << currentMediaDecoderState << ","
                        << currentSimulationTime.GetSeconds() << ","
                        << m_cumulativeCompletionTime.GetSeconds() << ","
                        << m_actualPlayoutTime.GetSeconds() << ","
                        << m_bufferDurationLevel.GetSeconds() << ","
                        //<< tileInfo.throughputBps << "," // Add this line
                        << tileInfo.netRate              // Add this line
                        << std::endl;
                //NS_LOG_WARN("Fethc Time "<<tileInfo.requestTime.GetSeconds());
                //NS_LOG_WARN("Receive Time "<<tileInfo.receiveTime.GetSeconds());
                // Update the NS_LOG_INFO output
                NS_LOG_INFO("TileInfo (Client: " << m_clientId << ", Run: " << m_runId << "): Segment " << segmentId
                            << " Tile " << tileInfo.tileId
                            << " Quality " << tileInfo.quality
                            << " Size " << tileInfo.size
                            << " FetchDuration " << tileInfo.fetchDuration.GetSeconds() << "s"
                            << " BufferLevel (Calculated) " << currentBufferDurationCalculated.GetSeconds() << "s"
                            << " FovScore " << tileInfo.fovScore
                            << " IsViewed " << (tileInfo.isViewed ? "Yes" : "No")
                            << " StallDuration " << tileInfo.stallDuration.GetSeconds() << "s"
                            << " CurrentState " << currentMediaDecoderState
                            << " SimulationTime " << currentSimulationTime.GetSeconds() << "s"
                            << " CumulativeTime " << m_cumulativeCompletionTime.GetSeconds() << "s"
                            << " ActualPlayoutTime " << m_actualPlayoutTime.GetSeconds() << "s"
                            << " BufferDurationLevel (New) " << m_bufferDurationLevel.GetSeconds() << "s"//);
                            //<< " ThroughputBps " << tileInfo.throughputBps << "bps"    // Add this line
                            << " NetRate " << tileInfo.netRate << "bps");             // Add this line
            }
        }
    }
    
    txtFile.close();
    csvFile.close();
    
    NS_LOG_INFO("=== LOGGING SUMMARY ===");
    NS_LOG_INFO("Tile info logged to files: " << txtFileName << " and " << csvFileName);
    NS_LOG_INFO("Client ID: " << m_clientId);
    NS_LOG_INFO("Run ID: " << m_runId);
    NS_LOG_INFO("Current MediaDecoder state: " << currentMediaDecoderState);
    NS_LOG_INFO("Current Buffer state: " << GetStateString(m_currentPlayoutState));
    NS_LOG_INFO("Current Buffer Duration (Calculated): " << currentBufferDurationCalculated.GetSeconds() << "s");
    NS_LOG_INFO("BufferDurationLevel (New): " << m_bufferDurationLevel.GetSeconds() << "s");
    NS_LOG_INFO("Total playout: " << GetTotalPlayoutTime().GetSeconds() << "s");
    NS_LOG_INFO("Actual playout time: " << m_actualPlayoutTime.GetSeconds() << "s");
    NS_LOG_INFO("Total stall: " << m_totalStallDuration.GetSeconds() << "s");
    NS_LOG_INFO("Cumulative completion time: " << m_cumulativeCompletionTime.GetSeconds() << "s");
    NS_LOG_INFO("Unconsumed segments: " << unconsumedSegments);
    NS_LOG_INFO("Simulation time: " << currentSimulationTime.GetSeconds() << "s");
    
    if(endPlayout(m_client->GetVideoSegments()-1)){//bilo 9
        m_client->GetMediaDecoder()->StopPlayout();
    }
    //
}

void TiledDashClientMediaBuffer::UpdateAllMetrics()
{
    Time currentTime = Simulator::Now();
    Time timeSinceLastUpdate = currentTime - m_lastStateUpdateTime;
    
    if (timeSinceLastUpdate.IsZero()) {
        return;
    }

    // SIMPLIFIED: Only handle REBUFFERING state time tracking
    if (m_currentPlayoutState == REBUFFERING) {
        //m_totalStallDuration += timeSinceLastUpdate;
        m_cumulativeCompletionTime += timeSinceLastUpdate;

        
        NS_LOG_INFO("UpdateAllMetrics: During REBUFFERING, added " << timeSinceLastUpdate.GetSeconds() 
                  << "s to cumulative time and all tiles. Total: " << m_cumulativeCompletionTime.GetSeconds() << "s");
    }
    
    m_lastStateUpdateTime = currentTime;
    
    NS_LOG_INFO("UpdateAllMetrics: State=" << GetStateString(m_currentPlayoutState) 
              << ", BufferDurationLevel=" << m_bufferDurationLevel.GetSeconds() << "s"
              << ", ActualPlayoutTime=" << m_actualPlayoutTime.GetSeconds() << "s");
}

void TiledDashClientMediaBuffer::UpdatePlayoutState(PlayoutState newState) {
    NS_LOG_FUNCTION(this << "State change from " << GetStateString(m_currentPlayoutState) << " to " << GetStateString(newState));
    
    UpdateAllMetrics();
    
    PlayoutState previousState = m_currentPlayoutState;
    
    if (previousState != newState) {
        if (newState == PLAYOUT && previousState == BUFFERING) {
            Time calculatedBufferLevel = m_client->GetSegmentDuration() * GetUnconsumedSegments();
            m_playoutStartTime = Simulator::Now();
            m_totalPlayoutTime = Time(0);
            //m_bufferDurationLevel=calculatedBufferLevel+Time(Seconds(1)); 
            NS_LOG_INFO("UpdatePlayoutState: Starting playout from BUFFERING. BufferDurationLevel: " << m_bufferDurationLevel.GetSeconds() << "s");
        }
        
        // === NEW LOGIC: Handle REBUFFERING state transitions ===
        if (newState == REBUFFERING) {
            // NEW: If entering REBUFFERING with non-zero buffer, add to actual playout time and reset
            if (m_bufferDurationLevel > Time(0)) {
                NS_LOG_INFO("UpdatePlayoutState: Entering REBUFFERING with BufferDurationLevel " << m_bufferDurationLevel.GetSeconds() << "s. Adding to ActualPlayoutTime and resetting to zero.");
                //m_actualPlayoutTime += m_bufferDurationLevel;
                //m_bufferDurationLevel = Time(0);
            } else {
                NS_LOG_INFO("UpdatePlayoutState: Entering REBUFFERING with zero BufferDurationLevel.");
            }
        }
        
        if (previousState == REBUFFERING && newState == PLAYOUT) {
            // When exiting REBUFFERING to PLAYOUT, continue from the zero buffer level
            NS_LOG_INFO("UpdatePlayoutState: Exiting REBUFFERING to PLAYOUT. BufferDurationLevel: " << m_bufferDurationLevel.GetSeconds() << "s");
        }
        
        if (previousState == REBUFFERING && newState == BUFFERING) {
            // When going from REBUFFERING back to BUFFERING, maintain the buffer level
            NS_LOG_INFO("UpdatePlayoutState: Exiting REBUFFERING to BUFFERING. Maintaining BufferDurationLevel at " << m_bufferDurationLevel.GetSeconds() << "s");
        }
        
        if (previousState == REBUFFERING && newState == FOVCHANGE) {
            NS_LOG_INFO("UpdatePlayoutState: Exiting REBUFFERING to FOVCHANGE. Maintaining BufferDurationLevel at " << m_bufferDurationLevel.GetSeconds() << "s");
        }
        
        if (previousState == FOVCHANGE && newState == PLAYOUT) {
            NS_LOG_INFO("UpdatePlayoutState: Exiting FOVCHANGE to PLAYOUT. Maintaining BufferDurationLevel at " << m_bufferDurationLevel.GetSeconds() << "s");
        }
        
        m_currentPlayoutState = newState;
        
        NS_LOG_INFO("UpdatePlayoutState: State updated to " << GetStateString(m_currentPlayoutState) << " at " << Simulator::Now().GetSeconds() << "s with BufferDurationLevel: " << m_bufferDurationLevel.GetSeconds() << "s");
    }
}
//*/
std::string TiledDashClientMediaBuffer::GetStateString(PlayoutState state) const {
    switch (state) {
        case BUFFERING: return "BUFFERING";
        case REBUFFERING: return "REBUFFERING";
        case PLAYOUT: return "PLAYOUT";
        case FOVCHANGE: return "FOVCHANGE";
        default: return "UNKNOWN";
    }
}
void TiledDashClientMediaBuffer::PeriodicUpdate()
{
    NS_LOG_FUNCTION(this);
    if (m_loggingStopped) {
        NS_LOG_DEBUG("PeriodicUpdate: Logging stopped for client " << m_clientId << ". Stopping periodic updates.");
        return; // Don't schedule next update
    }
    // Trigger the logging check (it will only log if 100ms have passed)
    FlushTileInfoLog();
    // Schedule next update,100,10000
    Simulator::Schedule(MilliSeconds(10000), &TiledDashClientMediaBuffer::PeriodicUpdate, this);
}


Time TiledDashClientMediaBuffer::GetTotalPlayoutTime() const {
    if (m_playoutStartTime > Time(0)) {
        return Simulator::Now() - m_playoutStartTime;
    }
    return Time(0);
}

Time TiledDashClientMediaBuffer::GetCurrentBufferDuration() {
    return m_bufferDurationLevel;
}

Time TiledDashClientMediaBuffer::CalculateSegmentStallImpact(uint32_t segmentId) {
    NS_LOG_FUNCTION(this << segmentId);
    return Time(0);
}

bool TiledDashClientMediaBuffer::IsSegmentFullyTiled(uint32_t segmentId)
{
    // The segment is fully tiled if the number of downloaded tiles
    // is greater than or equal to the minimum required tiles.
    return GetTilesInSegment(segmentId) >= m_minTiles;
}


} // namespace ns3
