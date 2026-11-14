// tiled-dash-client-media-decoder.cc (Fixed Version)
#include "tiled-dash-client-media-decoder.h"
#include "tiled-dash-client.h"
#include "tiled-dash-client-fov-tracker.h"
#include "tiled-dash-client-tile-requester.h"
#include "tiled-dash-client-media-buffer.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h" 
#include <algorithm> 
#include <cmath> 
#include <vector>    
#include <set> 

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("TiledDashClientMediaDecoder");

NS_OBJECT_ENSURE_REGISTERED(TiledDashClientMediaDecoder);

TypeId
TiledDashClientMediaDecoder::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::TiledDashClientMediaDecoder")
            .SetParent<Object>()
            .SetGroupName("Applications") 
            .AddConstructor<TiledDashClientMediaDecoder>()
            .AddAttribute("InitialBufferingDuration",
                          "Minimum duration of media to buffer before starting playout.",
                          TimeValue(Seconds(2)), // Reduced for testing
                          MakeTimeAccessor(&TiledDashClientMediaDecoder::m_initialBufferingDuration),
                          MakeTimeChecker())
            .AddAttribute("RebufferingDuration",
                          "Minimum duration of media to buffer to exit a rebuffering state.",
                          TimeValue(Seconds(2)), // Reduced for testing
                          MakeTimeAccessor(&TiledDashClientMediaDecoder::m_bufferingDuration),
                          MakeTimeChecker())
            ;
    return tid;
}

TiledDashClientMediaDecoder::TiledDashClientMediaDecoder()
        : m_currentSegmentId(1),
          m_playoutState(PlayoutState::BUFFERING),
          m_playoutStartTime(Seconds(0.0)),
          m_totalStallTime(Seconds(0.0)),
          m_isBuffering(true),
          m_nTiles(24), //bilo 24
          //m_nTiles(25),
          m_receiveTime(0),
          m_logSegId(1),     
          m_logTileId(0),       
          m_segDur(Seconds(1.0)) {
    NS_LOG_FUNCTION(this);
}

TiledDashClientMediaDecoder::~TiledDashClientMediaDecoder() {
    NS_LOG_FUNCTION(this);
}

void TiledDashClientMediaDecoder::SetInitialBufferingDuration(Time initialBufferingDuration) {
    m_initialBufferingDuration = initialBufferingDuration;
}

void TiledDashClientMediaDecoder::SetBufferingDuration(Time bufferingDuration) {
    m_bufferingDuration = bufferingDuration;
}

void TiledDashClientMediaDecoder::SetNTiles(uint32_t nTiles) {
    m_nTiles = nTiles;
}

void TiledDashClientMediaDecoder::SetFovScores(const std::vector<int>& fovScores) {
    NS_LOG_WARN("Setting FoV scores inital");
    m_fovScores = fovScores;
}

Time TiledDashClientMediaDecoder::GetSegmentPlayoutTime(uint32_t segmentId) {
    return m_playoutStartTime + (segmentId * m_segDur) + m_totalStallTime;
}

std::vector<uint32_t> TiledDashClientMediaDecoder::findHorizonSegments(Time fovHorizon) {
    NS_LOG_FUNCTION(this << fovHorizon);
    std::vector<uint32_t> overlappingSegments;
    if (m_playoutState == PlayoutState::BUFFERING) {
        return overlappingSegments;
    }

    Time currentPlayoutTime = Simulator::Now() - m_playoutStartTime - m_totalStallTime;
    NS_LOG_WARN("Current time "<<Simulator::Now()<<" playout "<<m_playoutStartTime); 
    Time horizonEndTime = currentPlayoutTime + fovHorizon;
    uint32_t latestSegmentId = static_cast<uint32_t>(horizonEndTime.GetSeconds() / m_segDur.GetSeconds());
    NS_LOG_WARN("TEKST TEST currentPlayoutTime "<<currentPlayoutTime<<" horizonEndTime "<< horizonEndTime<<" Lates segment "<<latestSegmentId<<" Trenutni segment"<<m_currentSegmentId);
    for (uint32_t segmentId = m_currentSegmentId; segmentId <= latestSegmentId; ++segmentId) {
        Time segmentPlayoutTime = GetSegmentPlayoutTime(segmentId);
        if (segmentPlayoutTime <= horizonEndTime) {
            overlappingSegments.push_back(segmentId);
            NS_LOG_WARN("USAO JE "<<segmentId<<" njegov time je "<<GetSegmentPlayoutTime(segmentId));
        }
    }

    return overlappingSegments;
}

void TiledDashClientMediaDecoder::newTileReceived(uint32_t segmentId, uint32_t tileId) {
    NS_LOG_FUNCTION(this << segmentId << tileId);
    m_receiveTime=Simulator::Now();
    NS_LOG_INFO("newTileReceived: Segment " << segmentId << ", Tile " << tileId);

    // Remove tile from missing tiles
    if (m_missingTiles.find(segmentId) != m_missingTiles.end()) {
        m_missingTiles[segmentId].erase(tileId);
        if (m_missingTiles[segmentId].empty()) {
            m_logSegId = segmentId;
            m_logTileId = tileId; 
            m_missingTiles.erase(segmentId);
            NS_LOG_INFO("newTileReceived: All missing tiles received for segment " << segmentId);
        }
    }
    
    // Trigger immediate state update
    UpdatePlayoutState();
}

uint32_t TiledDashClientMediaDecoder::GetCurrentSegmentId() {
    return m_currentSegmentId;
}

void TiledDashClientMediaDecoder::ConsumeTiles() {
    NS_LOG_FUNCTION(this << " for segment " << m_currentSegmentId);
    NS_LOG_INFO("ConsumeTiles: Attempting to consume segment " << m_currentSegmentId);

    if (!m_client || !m_client->GetMediaBuffer()) { 
         NS_LOG_ERROR("Client or media buffer not set. Cannot consume.");
         return;
    }

    uint32_t totalNumSegments = m_client->GetVideoSegments();
    NS_LOG_DEBUG("Totalno "<<totalNumSegments<<" trenutno: "<<m_currentSegmentId);  
    if (m_currentSegmentId+1>= totalNumSegments && m_client->GetMediaBuffer()->endPlayout(m_client->GetVideoSegments()-1)) { //dodao +1 i ovo jos, bilo 60 u endPlayout
        NS_LOG_INFO("Video ended. Stopping playout.");
        StopPlayout(); 
        return;
    }
    //
    std::vector<TiledDashClientMediaBuffer::TileConsumptionState> consumptionStates =
            m_client->GetMediaBuffer()->ConsumeTiles(m_currentSegmentId, m_fovScores);

    bool missingTiles = false;
    uint32_t missingCount = 0;
    
    for (uint32_t tileId = 0; tileId < consumptionStates.size(); ++tileId) {
        if (consumptionStates[tileId] == TiledDashClientMediaBuffer::TILE_UNAVAILABLE) {
            m_missingTiles[m_currentSegmentId].insert(tileId);
            missingTiles = true;
            missingCount++;
        }
    }
    
    if (missingTiles) {
        NS_LOG_INFO("ConsumeTiles: Found " << missingCount << " missing tiles for segment " << m_currentSegmentId);
        RequestMissingTiles();
    }

    UpdatePlayoutState(); 

    if (m_playoutState == PlayoutState::PLAYOUT) {
        if (m_isBuffering) {
            NS_LOG_INFO("ConsumeTiles: Exiting buffering or rebuffering state");
            m_isBuffering = false;
        } else {
            NS_LOG_INFO("ConsumeTiles: Moving to next segment " << (m_currentSegmentId + 1));
            
            if(m_client->GetMediaBuffer()->IsSegmentFullyTiled(m_currentSegmentId)){ //if segment is Fully viewed
                m_currentSegmentId++;
            }  
        }
        ScheduleConsumeTiles();
    }
    if (m_client && m_client->GetTileRequester())
    {
        NS_LOG_INFO("ConsumeTiles: Notifying TileRequester to check queue after consumption of segment " << m_currentSegmentId);
        m_client->GetTileRequester()->ProcessTileRequestQueue();
    }         
}



void TiledDashClientMediaDecoder::StopPlayout() {
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("StopPlayout: Stopping at segment " << m_currentSegmentId);

    if (m_nextSegmentEvent.IsPending()) {
        Simulator::Cancel(m_nextSegmentEvent);
    }

    if (m_client && m_client->GetMediaBuffer()) {
        m_client->GetMediaBuffer()->FlushTileInfoLog();
    }
    
    if (m_client) {
        m_client->NotifyPlayoutComplete(); 
    }
}

void TiledDashClientMediaDecoder::ScheduleConsumeTiles() {
    NS_LOG_FUNCTION(this);

    Time nextSegmentTime = GetSegmentPlayoutTime(m_currentSegmentId) - Simulator::Now();
    if (nextSegmentTime <= Seconds(0)) {
        nextSegmentTime =MicroSeconds(100); // Small delay to avoid immediate scheduling
        //nextSegmentTime=MicroSeconds(1);
    }
    NS_LOG_INFO("ScheduleConsumeTiles: Next segment in " << nextSegmentTime.GetSeconds() << " seconds");
    m_nextSegmentEvent = Simulator::Schedule(nextSegmentTime, &TiledDashClientMediaDecoder::ConsumeTiles, this);
}

void TiledDashClientMediaDecoder::NotifyBufferStateChange(PlayoutState newState) {
    NS_LOG_FUNCTION(this << "Notifying buffer of state change to " << GetStateString(newState));
    
    if (m_client && m_client->GetMediaBuffer()) {
        // Convert media decoder state to buffer state enum
        TiledDashClientMediaBuffer::PlayoutState bufferState;
        
        switch (newState) {
            case BUFFERING:
                bufferState = TiledDashClientMediaBuffer::BUFFERING;
                break;
            case REBUFFERING:
                bufferState = TiledDashClientMediaBuffer::REBUFFERING;
                break;
            case PLAYOUT:
                bufferState = TiledDashClientMediaBuffer::PLAYOUT;
                break;
            case FOVCHANGE:
                bufferState = TiledDashClientMediaBuffer::FOVCHANGE;
                break;
            default:
                NS_LOG_ERROR("Unknown playout state");
                return;
        }
        
        m_client->GetMediaBuffer()->UpdatePlayoutState(bufferState);
        NS_LOG_INFO("NotifyBufferStateChange: Buffer notified of state " << GetStateString(newState));
    } else {
        NS_LOG_ERROR("NotifyBufferStateChange: Client or media buffer not available");
    }
}

void TiledDashClientMediaDecoder::UpdatePlayoutState() 
{
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("UpdatePlayoutState: Current state = " << GetStateString(m_playoutState) << ", current segment = " << m_currentSegmentId);
    PlayoutState previousState = m_playoutState;

    if (m_playoutState == PlayoutState::BUFFERING) {
        uint32_t unconsumedSegments = m_client->GetMediaBuffer()->GetUnconsumedSegments();
        NS_LOG_INFO("UpdatePlayoutState: Buffering check - unconsumed segments: " << unconsumedSegments);
        Time bufferedDuration = unconsumedSegments * m_segDur;
        
        NS_LOG_INFO("UpdatePlayoutState: Buffered duration = " << bufferedDuration.GetSeconds() 
                    << "s, Required = " << m_initialBufferingDuration.GetSeconds() << "s");
        
        if (bufferedDuration >= m_initialBufferingDuration) {
            // Start FOV tracking if available
            if (m_client->GetFovTracker()) {
                NS_LOG_INFO("UpdatePlayoutState: Starting FOV tracking...");
                m_client->GetFovTracker()->StartTracking();
            }
            
            // Transition to playout
            m_playoutState = PlayoutState::PLAYOUT;
            m_currentSegmentId = 1;//bilo 1
            m_playoutStartTime = Simulator::Now();
            
            if (m_fovScores.empty() || std::all_of(m_fovScores.begin(), m_fovScores.end(), 
                                       [](int score) { return score == -1; })) {
                NS_LOG_WARN("UpdatePlayoutState: Invalid FOV scores (empty or all -1), using defaults");
                m_fovScores.assign(m_nTiles, 3);
            }            
            NS_LOG_INFO("UpdatePlayoutState: Transitioned from BUFFERING to PLAYOUT");
            NotifyBufferStateChange(m_playoutState);
            
            // Start consuming immediately
            ScheduleConsumeTiles();
        }
    } 
    else if (m_playoutState == PlayoutState::REBUFFERING) {
        bool missingTilesCleared = m_missingTiles.empty();
        Time currentBufferLevel = m_client->GetMediaBuffer()->GetCurrentBufferDuration();
        bool bufferSufficient = currentBufferLevel >= m_bufferingDuration;
        uint32_t unconsumedSegments = m_client->GetMediaBuffer()->GetUnconsumedSegments();
        
        NS_LOG_INFO("UpdatePlayoutState: REBUFFERING check - Missing tiles cleared: " << (missingTilesCleared ? "Yes" : "No") 
                    << ", Buffer level: " << currentBufferLevel.GetSeconds() << "s, Required: " << m_bufferingDuration.GetSeconds() << "s"
                    << ", Unconsumed segments: " << unconsumedSegments);
        

        // Exit rebuffering if current segment is available OR buffer conditions are met (currentSegmentAvailable && unconsumedSegments > 0) || 
        if ((bufferSufficient)) {
            m_playoutState = PlayoutState::PLAYOUT;
            NotifyBufferStateChange(m_playoutState);
            Time stallDuration = Simulator::Now() - m_stallStartTime;
            m_totalStallTime = m_totalStallTime + stallDuration;
            NS_LOG_WARN("Total Stall Time"<<m_totalStallTime);
            if (m_client->GetFovTracker()) {
                m_client->GetFovTracker()->ResumeTrackFov(stallDuration);
            }
            m_client->GetMediaBuffer()->UpdateTileStallDuration(m_currentSegmentId, m_logTileId, stallDuration);
            // Schedule immediate consumption to continue playback
            ScheduleConsumeTiles();
        }
        else {
            // Still need more content - request additional segments
            NS_LOG_INFO("UpdatePlayoutState: Still in rebuffering, requesting more content");

            std::vector<int> fovScores;
            if (m_client->GetFovTracker()) {
                fovScores = m_client->GetFovTracker()->GetCurrentFovScores();
            } else {
                fovScores = m_fovScores;
            }
            
            // Request current segment and next few segments
            std::vector<uint32_t> segIds;
            uint32_t maxSegments = m_client->GetVideoSegments();
            for (uint32_t i = 0; i < 3 && (m_currentSegmentId + i) < maxSegments; i++) { 
                segIds.push_back(m_currentSegmentId + i);
            }
            
            if (!segIds.empty() && m_client->GetTileRequester()) {
                m_client->GetTileRequester()->UpdateTileRequestQueue(fovScores, segIds);
                NS_LOG_INFO("UpdatePlayoutState: Requesting tiles for " << segIds.size() << " segments during rebuffering");
            }
            
        }
    }
    
    else if (m_playoutState == PlayoutState::PLAYOUT || m_playoutState == PlayoutState::FOVCHANGE) {
        // Check for rebuffering conditions during normal playback
        
        bool hasMissingTiles = !m_missingTiles.empty();
        Time currentBufferLevel = m_client->GetMediaBuffer()->GetCurrentBufferDuration();
        uint32_t unconsumedSegments = m_client->GetMediaBuffer()->GetUnconsumedSegments();
        bool bufferDepleted = (currentBufferLevel <= Time(0) && unconsumedSegments == 0);
        // Enter rebuffering if current segment is not playable OR buffer is critically low, -----hasMissingTiles || 
        if (bufferDepleted) { //||hasMissingTiles
            m_stallStartTime = Simulator::Now();
            
            if (hasMissingTiles && bufferDepleted) {
                NS_LOG_INFO("UpdatePlayoutState: Transitioning to REBUFFERING due to both segment unavailability AND buffer depletion.");
            } else if (hasMissingTiles) {
                NS_LOG_INFO("UpdatePlayoutState: Transitioning to REBUFFERING due to current segment being unplayable.");
            } else {
                NS_LOG_INFO("UpdatePlayoutState: Transitioning to REBUFFERING due to buffer depletion.");
            }
            
            m_playoutState = PlayoutState::REBUFFERING;
            //m_isBuffering = true;
            
            NotifyBufferStateChange(m_playoutState);
            
            if (m_client->GetFovTracker()) {
                m_client->GetFovTracker()->HaltFovTracking();
            }
            if (m_nextSegmentEvent.IsPending()) {
                Simulator::Cancel(m_nextSegmentEvent);
            }
            
        }
        else if (m_playoutState == PlayoutState::FOVCHANGE && !hasMissingTiles) {
            m_playoutState = PlayoutState::PLAYOUT;
            NS_LOG_INFO("UpdatePlayoutState: Transitioning from FOVCHANGE to PLAYOUT as no missing tiles.");
            NotifyBufferStateChange(m_playoutState);
        }
    }

    if (previousState != m_playoutState) {
        NS_LOG_INFO("UpdatePlayoutState: State changed from " << GetStateString(previousState) 
                    << " to " << GetStateString(m_playoutState));
        NotifyBufferStateChange(m_playoutState);
    }
        
}

void TiledDashClientMediaDecoder::CheckAndHandleRebuffering() {
    NS_LOG_FUNCTION(this);
    
    // Only proceed if we are currently playing
    if (m_playoutState == PLAYOUT || m_playoutState == PlayoutState::FOVCHANGE) {
        // Add null pointer checks
        if (!m_client || !m_client->GetMediaBuffer()) {
            NS_LOG_ERROR("CheckAndHandleRebuffering: Client or MediaBuffer is null");
            return;
        }
        
        Time currentBufferLevel = m_client->GetMediaBuffer()->GetCurrentBufferDuration();
        bool hasMissingTiles = !m_missingTiles.empty();
        bool bufferDepleted = currentBufferLevel <= Time(0);
        
        // *** NEW: Trigger rebuffering for either condition ***
        if (hasMissingTiles || (bufferDepleted && m_client->GetMediaBuffer()->GetUnconsumedSegments()==0)) { 
            if (hasMissingTiles && bufferDepleted) {
                NS_LOG_INFO("CheckAndHandleRebuffering: Rebuffering triggered due to both missing tiles AND buffer depletion (BDL: " << currentBufferLevel.GetSeconds() << "s) for segment " << m_currentSegmentId);
            } else if (hasMissingTiles) {
                NS_LOG_INFO("CheckAndHandleRebuffering: Rebuffering triggered due to missing tiles for segment " << m_currentSegmentId);
            } else {
                NS_LOG_INFO("CheckAndHandleRebuffering: Rebuffering triggered due to buffer depletion (BDL: " << currentBufferLevel.GetSeconds() << "s) for segment " << m_currentSegmentId);
            }
            
            // Update the decoder's state
            m_playoutState = REBUFFERING;
            
            // Immediately notify the buffer of the state change
            m_client->GetMediaBuffer()->UpdatePlayoutState(TiledDashClientMediaBuffer::REBUFFERING);
            
            // Set the stall start time for later calculation
            m_stallStartTime = Simulator::Now();
            
            // Halt FOV tracking and cancel pending events
            if (m_client->GetFovTracker()) {
                m_client->GetFovTracker()->HaltFovTracking();
            }
            if (m_nextSegmentEvent.IsPending()) {
                Simulator::Cancel(m_nextSegmentEvent);
            }

        } else {
            NS_LOG_INFO("CheckAndHandleRebuffering: No rebuffering needed. Buffer level: " << currentBufferLevel.GetSeconds() << "s, Missing tiles: " << (hasMissingTiles ? "Yes" : "No"));
        }
    }
}

//not used- used by buffer to let media decoder know that the buffer is depleated
void TiledDashClientMediaDecoder::HandleBufferDepletion() {
    NS_LOG_FUNCTION(this);
    NS_LOG_WARN("HandleBufferDepletion: Buffer has been depleted, forcing transition to REBUFFERING");
    
    // Only handle if we're currently in PLAYOUT
    if (m_playoutState == PlayoutState::PLAYOUT) {
        m_stallStartTime = Simulator::Now();
        m_playoutState = PlayoutState::REBUFFERING;
        
        NS_LOG_INFO("HandleBufferDepletion: Transitioned from PLAYOUT to REBUFFERING due to buffer depletion at " << m_stallStartTime.GetSeconds() << "s");
        
        // Notify buffer of the state change
        NotifyBufferStateChange(m_playoutState);
        
        // Halt FOV tracking and cancel pending events
        if (m_client->GetFovTracker()) {
            m_client->GetFovTracker()->HaltFovTracking();
        }
        if (m_nextSegmentEvent.IsPending()) {
            Simulator::Cancel(m_nextSegmentEvent);
        }

        // Request additional segments to refill buffer
        if (m_client->GetTileRequester()) {
            uint32_t additionalSegmentsNeeded = static_cast<uint32_t>(std::ceil(m_bufferingDuration.GetSeconds() / m_segDur.GetSeconds()));
            uint32_t nextIncompleteSegment = m_client->GetMediaBuffer()->GetNextSegmentId();
            
            std::vector<int> fovScores;
            if (m_client->GetFovTracker()) {
                fovScores = m_client->GetFovTracker()->GetCurrentFovScores();
            } else {
                fovScores = m_fovScores;
            }
            std::vector<uint32_t> segIds;
            
            uint32_t maxSegments = m_client->GetVideoSegments();
            uint32_t segmentsToRequest = std::min(additionalSegmentsNeeded, static_cast<uint32_t>(3));
            
            for (uint32_t i = 0; i < segmentsToRequest && (nextIncompleteSegment + i) < maxSegments; i++) {
                uint32_t segmentId = nextIncompleteSegment + i;
                segIds.push_back(segmentId);
                NS_LOG_INFO("HandleBufferDepletion: Queuing segment " << segmentId << " for buffer replenishment");
            }
            
            if (!segIds.empty()) {
                m_client->GetTileRequester()->UpdateTileRequestQueue(fovScores, segIds);
                NS_LOG_INFO("HandleBufferDepletion: Requesting tiles for " << segIds.size() 
                           << " segments to replenish depleted buffer.");
            }
        }
    } else {
        NS_LOG_WARN("HandleBufferDepletion: Called but current state is " << GetStateString(m_playoutState) << ", not PLAYOUT");
    }
}


std::string TiledDashClientMediaDecoder::GetStateString(PlayoutState state) {
    switch (state) {
        case BUFFERING: return "BUFFERING";
        case REBUFFERING: return "REBUFFERING";
        case PLAYOUT: return "PLAYOUT";
        case FOVCHANGE: return "FOVCHANGE";
        default: return "UNKNOWN";
    }
}

void TiledDashClientMediaDecoder::RequestMissingTiles() {
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("RequestMissingTiles: Requesting missing tiles");
    
    std::vector<int> fovScores(m_nTiles, -1);
    std::vector<uint32_t> segIds;

    for (const auto& segmentPair : m_missingTiles) {
        uint32_t segmentId = segmentPair.first;
        segIds.push_back(segmentId);

        for (uint32_t tileId : segmentPair.second) {
            if (tileId < fovScores.size()) {
                fovScores[tileId] = m_fovScores[tileId];
            }
        }
    }
    
    if (!segIds.empty() && m_client->GetTileRequester()) {
        m_client->GetTileRequester()->UpdateTileRequestQueue(fovScores, segIds);
        NS_LOG_INFO("RequestMissingTiles: Requested tiles for " << segIds.size() << " segments");
    }
}

void TiledDashClientMediaDecoder::HandleFovChange(const std::vector<int>& newFovScores) {
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("HandleFovChange: FoV changed, transitioning to FOVCHANGE state");

    m_fovScores = newFovScores;
    
    // Only transition to FOVCHANGE if we're not already in it
    if (m_playoutState != PlayoutState::FOVCHANGE) {
        m_playoutState = PlayoutState::FOVCHANGE;
        NotifyBufferStateChange(m_playoutState);
        
        // Only schedule consumption if we're not currently processing
        if (!m_nextSegmentEvent.IsPending()) {
            ConsumeTiles();
        }
    } else {
        // If already in FOVCHANGE, just update the scores and let the current process continue
        NS_LOG_INFO("HandleFovChange: Already in FOVCHANGE state, just updating FoV scores");
    }
}
// Add the missing method declaration
void TiledDashClientMediaDecoder::ForceStateUpdate() {
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("ForceStateUpdate: Forcing state update check");
    UpdatePlayoutState();
}

void TiledDashClientMediaDecoder::StartPlayout() {
    NS_LOG_FUNCTION(this);

    if (m_isBuffering) {
       NotifyBufferStateChange(m_playoutState);
       //izbacio ovo 28.08-scheduel
       ScheduleConsumeTiles(); // Start scheduling ConsumeTiles()
       //UpdatePlayoutState();

    }
   // Playout will start after the initial buffering duration
}

} // namespace ns3
