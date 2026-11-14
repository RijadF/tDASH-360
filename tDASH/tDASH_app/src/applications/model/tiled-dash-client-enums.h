// tiled-dash-client-enums.h
#ifndef TILED_DASH_CLIENT_ENUMS_H
#define TILED_DASH_CLIENT_ENUMS_H
#include "ns3/enum.h" 

namespace ns3 {

enum PovDataType {
    QUATERNION,
    EULER_ANGLES
};

enum FovTrackingAlgorithm {
    IDEAL_FOV,          // Default or first value
    LINEAR_REGRESSION // Example of another type
    // Add other prediction logic types as needed
};

enum ProjectionType {
  EQUIRECTANGULAR,
  CUBEMAP  // Add other projection types as needed
};

enum VideoMode {
  MODE_2D,
  MODE_360
}; 

// Enum for selecting throughput sampling method
enum ThroughputSamplingMethod {
  TILE_BASED,
  TIME_BASED
};

// Enum for selecting throughput estimation method
enum ThroughputEstimationMethod {
  AVERAGE,
  EXPONENTIAL_AVERAGE,
  HARMONIC_MEAN, 
  IDEAL_TP
  // Add other estimation methods here
};

// Enum for selecting ABR method
enum AbrStrategy {
  Simple,
  ViewportAware,
  UniformViewportQualityAdaptQuality,
  CenterTileFirstAdaptQuality, 
  ViewportOnlyAdaptQuality,
  ViewportPlusAdaptQuality,
  Bola360AdaptQuality,
  FlareAdaptQuality,
  ProbDashAdaptQuality,
  PanoAdaptQuality,
  SalientVRAdaptQuality,
  ProbViewportAwareAdaptQuality,
  NaiveAdaptQuality,
  MosaicAdaptQuality,
  //2D
  Bola2DAdaptQuality,
  Bba2_2DAdaptQuality,
  Elastic2DAdaptQuality,
  Logistic2DAdaptQuality,
  AnotherMethod
  // Add other algorithms here
};

} // namespace ns3

#endif // TILED_DASH_CLIENT_ENUMS_H
