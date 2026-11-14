// tiled-dash-client-media-decoder.h
#ifndef TILED_DASH_CLIENT_MEDIA_DECODER_H
#define TILED_DASH_CLIENT_MEDIA_DECODER_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include <vector>
#include <map>
#include <set>
#include <string>

namespace ns3 {

class TiledDashClient;

class TiledDashClientMediaDecoder : public Object {
public:
    enum PlayoutState {
        BUFFERING,
        REBUFFERING,
        PLAYOUT,
        FOVCHANGE
    };

    static TypeId GetTypeId(void);
    
    TiledDashClientMediaDecoder();
    virtual ~TiledDashClientMediaDecoder();

    // Configuration methods
    void SetInitialBufferingDuration(Time initialBufferingDuration);
    void SetBufferingDuration(Time bufferingDuration);
    void SetNTiles(uint32_t nTiles);
    void SetFovScores(const std::vector<int>& fovScores);
    void SetSegmentDuration(Time segDur){ m_segDur = segDur; }
    // Client binding
    void SetClient(Ptr<TiledDashClient> client) { m_client = client; }
    
    // Core functionality
    void newTileReceived(uint32_t segmentId, uint32_t tileId);
    void ConsumeTiles();
    void StartPlayout();
    void StopPlayout();
    void HandleFovChange(const std::vector<int>& newFovScores);
    void ForceStateUpdate();
    
    // Getters
    uint32_t GetCurrentSegmentId();
    Time GetSegmentPlayoutTime(uint32_t segmentId);
    std::vector<uint32_t> findHorizonSegments(Time fovHorizon);
    PlayoutState GetPlayoutState() const { return m_playoutState; }
    //void SetPlayoutState(PlayoutState s) {m_playoutState=s;}
    Time GetTotalStallTime() const { return m_totalStallTime; }
    bool IsBuffering() const { return m_isBuffering; }
    
    void ChangeFovScores();
    void ScheduleFovChange();
    
    void NotifyBufferStateChange(PlayoutState newState);
   
    void CheckAndHandleRebuffering();
    
    void HandleBufferDepletion();
    Time m_receiveTime;
    Time GetReceiveTileTime(uint32_t segmentId, uint32_t tileId){return m_receiveTime;}
    uint32_t GetCurrentSegmentId() const{return m_currentSegmentId;}
    uint32_t GetInitialBufferingDuration() const{return static_cast<uint32_t>(m_initialBufferingDuration.GetSeconds());}
private:
    // Core state management
    void UpdatePlayoutState();
    void ScheduleConsumeTiles();
    void RequestMissingTiles();
    std::string GetStateString(PlayoutState state);
    
    bool m_isRequestingTiles = false; 

    // Client reference
    Ptr<TiledDashClient> m_client;
    
    // Playout state
    uint32_t m_currentSegmentId;
    PlayoutState m_playoutState;
    Time m_playoutStartTime;
    Time m_stallStartTime;
    Time m_totalStallTime;
    bool m_isBuffering;
    
    // Configuration
    Time m_initialBufferingDuration;
    Time m_bufferingDuration;
    uint32_t m_nTiles;
    Time m_segDur;
    std::vector<int> m_fovScores;
    
    // Missing tiles tracking
    std::map<uint32_t, std::set<uint32_t>> m_missingTiles;
    
    // Event scheduling
    EventId m_nextSegmentEvent;
    
    // Logging support
    uint32_t m_logSegId;
    uint32_t m_logTileId;
    
    EventId m_fovChangeEvent;  // Event for scheduling FoV changes
    uint32_t m_fovChangeCount; // Counter for cycling through patterns
    bool m_autoFovChangeEnabled; // Flag to enable/disable auto FoV changes
};

} // namespace ns3

#endif // TILED_DASH_CLIENT_MEDIA_DECODER_H
