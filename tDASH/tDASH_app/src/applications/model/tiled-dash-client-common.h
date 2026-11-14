// tiled-dash-client-common.h
#ifndef TILED_DASH_CLIENT_COMMON_H
#define TILED_DASH_CLIENT_COMMON_H
#include "ns3/nstime.h"
namespace ns3 {

    struct TileInfo
{
  uint32_t tileId;
  int quality;
  uint32_t size;  
  Time fetchDuration;
  Time bufferLevel;
  int fovScore;  
  bool isViewed;
  Time stallDuration;
  Time requestTime; 
  Time receiveTime;
  double throughputBps; 
  double netRate; 
  
  //TileInfo(): size(0),fovScore(-1), isViewed(0), stallDuration(0) {}
  TileInfo(): 
    tileId(0), 
    quality(0), 
    size(0), 
    fetchDuration(0), 
    bufferLevel(0), 
    fovScore(-1), 
    isViewed(false), 
    stallDuration(0),
    requestTime(0),
    receiveTime(0), 
    throughputBps(0.0), 
    netRate(0.0) 
  {}
};



}
#endif // TILED_DASH_CLIENT_COMMON_H
