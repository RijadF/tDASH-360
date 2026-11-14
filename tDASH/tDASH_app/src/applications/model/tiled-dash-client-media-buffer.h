// tiled-dash-client-media-buffer.h
#ifndef TILED_DASH_CLIENT_MEDIA_BUFFER_H
#define TILED_DASH_CLIENT_MEDIA_BUFFER_H

#include "ns3/object.h"
#include "ns3/uinteger.h"
#include "ns3/nstime.h"
#include "tiled-dash-client-common.h"

#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <queue>

//#include <H5Cpp.h>


namespace ns3 {
class TiledDashClient;

class TiledDashClientMediaBuffer : public Object
{
public:
  enum TileConsumptionState {
    TILE_AVAILABLE = 1,
    TILE_UNAVAILABLE = -1,
    TILE_UNNEEDED = -2
  };
  
  enum PlayoutState {
    BUFFERING,
    REBUFFERING,
    PLAYOUT,
    FOVCHANGE
  };
  
  static TypeId GetTypeId (void);
  TiledDashClientMediaBuffer ();
  virtual ~TiledDashClientMediaBuffer ();

  // Core buffer operations
  void StoreTile (uint32_t segmentId, TileInfo tileData);
  bool IsTileBuffered (uint32_t segmentId, uint32_t tileId);
  uint32_t GetNextSegmentId ();
  std::vector<TileConsumptionState> ConsumeTiles (uint32_t segmentId, const std::vector<int>& fovScores);
  uint32_t GetUnconsumedSegments ();
  
  // Configuration setters
  void SetMinTiles(uint32_t minTiles);
  void SetnTiles(uint32_t nTiles); 
  void SetMaxBufferedSegments(uint32_t maxBufferedSegments) { m_maxBufferedSegments = maxBufferedSegments; }
  void SetClient(Ptr<TiledDashClient> client) { m_client = client; } 
  
  // Stall duration tracking
  void UpdateTileStallDuration(uint32_t segmentId, uint32_t tileId, Time stallDuration); 
  
  // Logging
  void FlushTileInfoLog();
  
  // Enhanced buffer duration methods
  void UpdateBufferDuration();
  Time GetCurrentBufferDuration();
  
  // Playout state management
  void UpdatePlayoutState(PlayoutState newState);
  PlayoutState GetCurrentPlayoutState() const;
  Time GetTotalPlayoutTime() const;
  std::string GetStateString(PlayoutState state) const;
  uint32_t GetMinTiles()const{return m_minTiles;};
  // Stall-aware buffer dynamics
  Time CalculateSegmentStallImpact(uint32_t segmentId);
  void RecalculateTileBufferLevel(uint32_t segmentId, uint32_t tileId);
  Time GetTotalStallDuration() const;
  Time GetLastStallUpdateTime() const;
  
  // New timing methods
  Time GetCumulativeCompletionTime() const;
  Time GetActualPlayoutTime() const;
  void UpdateAllMetrics();
  uint32_t GetTilesInSegment(uint32_t segmentId);
  void CalculateBufferDuration();
  uint32_t GetCompletedUnconsumedSegments() const;
  Time GetBufferLevelForLogging(uint32_t currentSegmentId);
  void IncrementBufferLevel();
  void DecrementBufferLevel(); 
  bool IsSegmentFullyViewed(uint32_t segmentId);
  bool HasMinViewedTiles(uint32_t segmentId); //06.08
  void SetClientAndRunId(uint32_t clientId, uint32_t runId ){m_clientId=clientId;m_runId=runId;}
  // 10.08
  void CheckBufferLevel();
  // 11.08
  bool IsSegmentFullyTiled(uint32_t segmentId);
  // 25.08
  void PeriodicUpdate();
  // 09.09
  bool IsFull() {return m_isFull;}
  // 22.9
  void StopApplication(); 
  bool endPlayout(uint32_t segmentId);


private:
  // Core buffer components
  Ptr<TiledDashClient> m_client;
  std::map<uint32_t, std::vector<TileInfo>> m_buffer;
  uint32_t m_minTiles;
  uint32_t m_nTiles; 
  uint32_t m_consumedSegment;
  uint32_t m_maxBufferedSegments;
  uint32_t m_lastFullyViewedSegment;
  bool m_isFull;
  
  // Enhanced buffer duration tracking
  Time m_currentBufferDuration;     // Current buffer duration in time
  Time m_totalStallDuration;        // Total accumulated stall duration
  Time m_lastStallUpdateTime;       // Last time stall was updated
  
  // Playout state tracking
  PlayoutState m_currentPlayoutState; // Current playout state
  Time m_lastStateUpdateTime;       // Last time state was updated
  Time m_totalPlayoutTime;          // Total playout time since start
  Time m_playoutStartTime;          // When playout first started
  
  // New timing tracking members
  Time m_cumulativeCompletionTime;  // Always increments with fetch duration + stall
  Time m_actualPlayoutTime;         // Only increments during PLAYOUT state
  bool m_playoutPaused;             // Flag to track if playout time is paused

  Time m_lastPlayoutTimeUpdate;
  Time m_lastCumulativeTimeUpdate;
  //Time m_bufferDuration; 

  PlayoutState m_lastPlayoutState;
  //New time variables
  Time m_bufferDurationLevel;
  std::set<uint32_t> m_deductedSegments;
  //new variables
  uint32_t m_clientId;
  uint32_t m_runId;
  //std::map<uint32_t, Time> m_segmentFetchTime;
  Time m_lastTileFetchCompletionTime;
  Time m_lastTileFetchDuration;
  //
  int32_t m_lastAccountedSegment;
  //
  // In class declaration--25.08
  std::set<uint32_t> m_segmentsContributingToBuffer;
  //
  Time m_lastLogTime; 
  //
  bool m_loggingStopped;
  //logging
  uint32_t viewedRequired;
  uint32_t m_viewedRequiredTiles; // Track cumulative viewed required tiles
};

} // namespace ns3

#endif // TILED_DASH_CLIENT_MEDIA_BUFFER_H


