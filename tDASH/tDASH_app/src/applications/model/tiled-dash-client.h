// tiled-dash-client.h
#ifndef TILED_DASH_CLIENT_H
#define TILED_DASH_CLIENT_H

#include "ns3/application.h"
#include "ns3/address.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include "ns3/string.h"

#include "tiled-dash-client-enums.h"
#include "tiled-dash-client-network-manager.h"
#include "tiled-dash-client-abr-engine.h"
#include "tiled-dash-client-network-tracker.h"
#include "tiled-dash-client-media-buffer.h"
#include "tiled-dash-client-media-decoder.h"
#include "tiled-dash-client-fov-tracker.h"
#include "tiled-dash-client-tile-requester.h"

namespace ns3 {

class TiledDashClient : public Application
{
public:
  static TypeId GetTypeId (void);
  TiledDashClient ();
  virtual ~TiledDashClient ();
  //all of the classes divided and represented into a one class. Easier for application. All getters in one place.
  Ptr<TiledDashClientNetworkManager> GetNetworkManager() const { return m_networkManager; }
  Ptr<TiledDashClientNetworkTracker> GetNetworkTracker() const { return m_networkTracker; }
  Ptr<TiledDashClientFovTracker> GetFovTracker() const { return m_fovTracker; }
  Ptr<TiledDashClientMediaBuffer> GetMediaBuffer() const { return m_mediaBuffer; }
  Ptr<TiledDashClientAbrEngine> GetAbrEngine() const { return m_abrEngine; }
  Ptr<TiledDashClientTileRequester> GetTileRequester() const { return m_tileRequester; }
  Ptr<TiledDashClientMediaDecoder> GetMediaDecoder() const { return m_mediaDecoder; }
  Time GetSegmentDuration() const { return m_segmentDuration; }
  Time GetFovTrackingHorizon() const { return m_fovTrackingHorizon; }
  
  void SetDeferredStart(bool deferred);
  void DeferredStartup();
  void setServerAddress(Address serverAddress) { m_serverAddress = serverAddress;}
  Address GetServerAddress() const { return m_serverAddress;}
  void setServerPort(uint16_t serverPort) { m_serverPort = serverPort;}
  uint16_t GetServerPort() const { return m_serverPort;}
  void setNSockets(uint32_t nSockets) { m_nSockets = nSockets;}
  void setThroughputTraceFile(std::string throughputTraceFile) { m_throughputTraceFile = throughputTraceFile;}

  void setVideoName(std::string videoName) { m_videoName = videoName;} 
  void setQualityRateString(std::string qualityRatesString);
  void setsegDuration(Time segDuration) { m_segmentDuration = segDuration;}
  void setHorTiles(uint32_t horTiles) { m_horTiles = horTiles;}
  void setVerTiles(uint32_t verTiles) { m_verTiles = verTiles;}
  
  void setVideoDuration(Time videoDuration);
  
  void setPovFile(std::string povFile) { m_povFile = povFile;}
  void setHorFovAngle(double horFovAngle) { m_horFovAngle = horFovAngle;}
  void setVerFovAngle(double verFovAngle) { m_verFovAngle = verFovAngle;}
  void setProjectionType(enum ProjectionType projectionType) { m_projectionType = projectionType;}
  void setFovTrackingAlgorithm(enum FovTrackingAlgorithm fovTrackingAlgorithm) { m_fovTrackingAlgorithm = fovTrackingAlgorithm;}
  void setFovTrackingHorizon(Time fovTrackingHorizon) { m_fovTrackingHorizon = fovTrackingHorizon;}
  void setFovTrackingHistory(Time fovTrackingHistory) { m_fovTrackingHistory = fovTrackingHistory;}
  void setFovSensorRate(double fovSensorRate) { m_fovSensorRate = fovSensorRate;}
  void setVideoMode(enum VideoMode videoMode){m_videoMode=videoMode;}
  void setMaxBufferDuration(Time maxBufferDuration) { m_maxBufferDuration = maxBufferDuration;}
  void setBufferingDuration(Time bufferingDuration) { m_bufferingDuration = bufferingDuration;}
  void setRebufferingDuration(Time rebufferingDuration) { m_rebufferingDuration = rebufferingDuration;}
  uint32_t GetVideoSegments() const { return m_videoSegments; }
  void NotifyPlayoutComplete();  
  void InitiatePlayback();
  uint32_t GetHorTiles() const {return m_horTiles;}
  uint32_t GetVerTiles() const {return m_verTiles;}
  std::string GetVideoName() const {return m_videoName;}
  VideoMode GetVideoMode() const{return m_videoMode;}
protected:
  void DoDispose () override;

private:
  void StartApplication () override;
  void StopApplication () override;
  void SetupQualityRates();
    bool m_deferredStart;
    EventId m_startEvent;

  Address m_serverAddress;
  uint16_t m_serverPort;
  uint32_t m_nSockets;
  std::string m_videoName;
  Time m_segmentDuration;
  Time m_videoDuration;
  uint32_t m_horTiles;
  uint32_t m_verTiles;
  std::string m_qualityRatesString;
  std::vector<double> m_qualityRates;

  std::string m_povFile; 
  std::string m_throughputTraceFile;
  
  Time m_maxBufferDuration;
  Time m_bufferingDuration; 
  Time m_rebufferingDuration;
    
  enum ProjectionType m_projectionType;
  double m_horFovAngle;
  double m_verFovAngle;
  enum FovTrackingAlgorithm m_fovTrackingAlgorithm;
  Time m_fovTrackingHorizon;
  Time m_fovTrackingHistory;
  double m_fovSensorRate;

  enum ThroughputEstimationMethod m_throughputEstimationMethod;
  enum ThroughputSamplingMethod m_throughputSamplingMethod;
  Time m_samplingInterval;
  uint32_t m_samplingWindow;  
  double m_exponentialWeight;
  
  double m_refreshRate;
  VideoMode m_videoMode;
  uint32_t m_videoSegments; 

  Ptr<TiledDashClientNetworkManager> m_networkManager;
  Ptr<TiledDashClientTileRequester> m_tileRequester;
  Ptr<TiledDashClientAbrEngine> m_abrEngine;
  Ptr<TiledDashClientMediaBuffer> m_mediaBuffer;
  Ptr<TiledDashClientMediaDecoder> m_mediaDecoder;
  Ptr<TiledDashClientFovTracker> m_fovTracker;
  Ptr<TiledDashClientNetworkTracker> m_networkTracker;

};

} // namespace ns3

#endif // TILED_DASH_CLIENT_H
