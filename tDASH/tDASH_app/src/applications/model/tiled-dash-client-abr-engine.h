// tiled-dash-client-abr-engine.h
#ifndef TILED_DASH_CLIENT_ABR_ENGINE_H
#define TILED_DASH_CLIENT_ABR_ENGINE_H

#include "ns3/object.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include <vector>
#include <map>
#include "tiled-dash-client-enums.h"

namespace ns3 {

class TiledDashClient; 

class TiledDashClientAbrEngine : public Object
{
public:
  static TypeId GetTypeId (void);
  TiledDashClientAbrEngine ();
  virtual ~TiledDashClientAbrEngine ();

  void SetQualityRates (std::vector<double> qualityRates);
  void SetAbrAlgorithm (AbrStrategy abrAlgorithm);
  void SetClient(Ptr<TiledDashClient> client) { m_client = client; }
  std::map<uint32_t, std::vector<int>> GetSegmentTileQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline);
  
private:
  std::map<uint32_t, std::vector<int>> SimpleAdaptQuality (double netRate, uint32_t segmentId, const std::vector<int>& fovScores, Time deadline);
  std::map<uint32_t, std::vector<int>> ViewportAwareAdaptQuality (double netRate,
                                                                 uint32_t segmentId,
                                                                 const std::vector<int>& fovScores,
                                                                 Time deadline);
  void ApplyBufferAwareAdjustments (std::vector<int>& tileQualities,
                                   const std::vector<uint32_t>& viewportTiles,
                                   const std::vector<uint32_t>& adjacentTiles,
                                   const std::vector<uint32_t>& tier2Tiles,
                                   const std::vector<uint32_t>& outsideTiles);
  std::map<uint32_t, std::vector<int>> UniformViewportQualityAdaptQuality (double netRate,
                                                                          uint32_t segmentId,
                                                                          const std::vector<int>& fovScores,
                                                                          Time deadline);
  std::map<uint32_t, std::vector<int>> CenterTileFirstAdaptQuality (double netRate,
                                                                   uint32_t segmentId,
                                                                   const std::vector<int>& fovScores,
                                                                   Time deadline);
  std::map<uint32_t, std::vector<int>> ViewportOnlyAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> ViewportPlusAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  //Update 05.10
  std::map<uint32_t, std::vector<int>> Bola360AdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> FlareAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> ProbDashAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> PanoAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> SalientVRAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> ProbViewportAwareAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> NaiveAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> MosaicAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  //2D 
  std::map<uint32_t, std::vector<int>> Bola2DAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> Bba2_2DAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> Elastic2DAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);
  std::map<uint32_t, std::vector<int>> Logistic2DAdaptQuality (double netRate,
                                                               uint32_t segmentId,
                                                               const std::vector<int>& fovScores,
                                                               Time deadline);

  std::vector<double> m_qualityRates;
  AbrStrategy m_abrAlgorithm;
  
  double m_mosaicLastQ = -1.0; 
  Ptr<TiledDashClient> m_client;
  //2D expansion
  //Update 2D: state kept between segments by the 2D algorithms
  std::vector<double> m_throughputHistory2D;   // SampleProcessor: one throughput sample (netRate) per downloaded segment
  int m_lastQuality2D = -1;                    // selectedIndex / lastRepLevel of the previous segment (-1 = nothing downloaded yet)
  double m_lastSegmentBits2D = 0.0;            // bits requested for the previous segment (-> lastSampleDurationMs)
  double m_elasticStaticAlgParameter2D = 0.0;  // Elastic: staticAlgParameter
  int m_bba2StaticAlgPar2D = 0;                // BBA2: m_staticAlgPar
  int m_bolaState2D = 1;                       // BOLA: state (1 = BOLA_STATE_STARTUP, 2 = BOLA_STATE_STEADY)
  double m_bolaPlaceholderBuffer2D = 0.0;      // BOLA: placeholderBuffer

};

} // namespace ns3

#endif // TILED_DASH_CLIENT_ABR_ENGINE_H
