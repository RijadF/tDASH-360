// tiled-dash-client.cc
#include "tiled-dash-client.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include <cmath> 
#include <sstream>
#include <fstream>
#include <iostream>




namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TiledDashClient");

NS_OBJECT_ENSURE_REGISTERED (TiledDashClient);

TypeId
TiledDashClient::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TiledDashClient")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<TiledDashClient> ()
    .AddAttribute ("ServerAddress", "The address of the DASH server.",
                   AddressValue (),
                   MakeAddressAccessor (&TiledDashClient::setServerAddress, &TiledDashClient::GetServerAddress),
                   MakeAddressChecker ())
    .AddAttribute ("ServerPort", "The port of the DASH server.",
                   UintegerValue (80),
                   MakeUintegerAccessor (&TiledDashClient::m_serverPort),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("NumberOfSockets", "The number of sockets to use.",
                   UintegerValue (1),
                   MakeUintegerAccessor (&TiledDashClient::m_nSockets),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("VideoName", "The name of the video to stream.",
                   StringValue ("video"),
                   MakeStringAccessor (&TiledDashClient::m_videoName),
                   MakeStringChecker ())
    .AddAttribute ("SegmentDuration", "The duration of each segment.",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&TiledDashClient::m_segmentDuration),
                   MakeTimeChecker ())
    .AddAttribute ("VideoDuration", "Total duration of the video to simulate",
                   TimeValue (Seconds (10)),
                   MakeTimeAccessor (&TiledDashClient::m_videoDuration),
                   MakeTimeChecker())
    .AddAttribute ("VideoSegments", "Number of streamed video segments.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&TiledDashClient::m_videoSegments),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("PovFile", "The path to the PoV file.",
                   StringValue ("pov.txt"),
                   MakeStringAccessor (&TiledDashClient::m_povFile),
                   MakeStringChecker ())
    .AddAttribute ("ThroughputTraceFile", "The path to the throughput trace file.",
                   StringValue (""),
                   MakeStringAccessor (&TiledDashClient::m_throughputTraceFile),
                   MakeStringChecker ())
    .AddAttribute ("MaxBufferDuration", "Maximum buffer duration in seconds",
                   TimeValue(Seconds(10)),
                   MakeTimeAccessor(&TiledDashClient::m_maxBufferDuration),
                   MakeTimeChecker ())
    .AddAttribute ("QualityRates", "List of quality rates in kilobits per second",
                   StringValue("100,200,400,800"),
                   MakeStringAccessor(&TiledDashClient::m_qualityRatesString),
                   MakeStringChecker())
    .AddAttribute ("HorTiles", "Number of horizontal video tiles.",//ovjde bi trebalo logicno biti obrnuto
                   UintegerValue (6), //bilo 6
                   MakeUintegerAccessor (&TiledDashClient::m_horTiles),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("VerTiles", "Number of vertical video tiles.",
                   UintegerValue (4), //bilo 4
                   MakeUintegerAccessor (&TiledDashClient::m_verTiles),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("HorFovAngle", "Horizontal FoV angle in degrees.",
                   DoubleValue (110.0),
                   MakeDoubleAccessor (&TiledDashClient::m_horFovAngle),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("VerFovAngle", "Vertical FoV angle in degrees.",
                   DoubleValue (90.0),
                   MakeDoubleAccessor (&TiledDashClient::m_verFovAngle),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("ProjectionType", "Projection type of the video.",
                   EnumValue (EQUIRECTANGULAR),
                   MakeEnumAccessor<ProjectionType>(&TiledDashClient::m_projectionType),
                   MakeEnumChecker (EQUIRECTANGULAR, "Equirectangular", CUBEMAP, "Cubemap"))
    .AddAttribute ("FovTrackingAlgorithm", "Algorithm for FoV tracking.",
                   EnumValue (IDEAL_FOV),
                   MakeEnumAccessor<FovTrackingAlgorithm> (&TiledDashClient::m_fovTrackingAlgorithm),
                   MakeEnumChecker (IDEAL_FOV, "Ideal", LINEAR_REGRESSION, "LinearRegression")) 
    .AddAttribute ("FoVPredictionHorizon", "The duration of fov prediction horizon", //could not build due to the spaces that were present
                   TimeValue (Seconds (0.5)),
                   MakeTimeAccessor (&TiledDashClient::m_fovTrackingHorizon),
                   MakeTimeChecker ())
    .AddAttribute ("FoVTrackingHistory", "The duration historic FoV window in seconds.",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&TiledDashClient::m_fovTrackingHistory),
                   MakeTimeChecker ())
    .AddAttribute ("SensorRate", "Sensor rate in Hz.",
                   DoubleValue (60.0),
                   MakeDoubleAccessor (&TiledDashClient::m_fovSensorRate),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("ThroughputEstimationMethod", "Algorithm for throughput estimation.",
                   EnumValue (EXPONENTIAL_AVERAGE),
                   MakeEnumAccessor<ThroughputEstimationMethod> (&TiledDashClient::m_throughputEstimationMethod),
                   MakeEnumChecker (ThroughputEstimationMethod::AVERAGE, "AVERAGE",
                                    ThroughputEstimationMethod::EXPONENTIAL_AVERAGE, "EXPONENTIAL_AVERAGE",
                                    ThroughputEstimationMethod::HARMONIC_MEAN, "HARMONIC_MEAN", //dodano 12.01
                                    ThroughputEstimationMethod::IDEAL_TP, "IDEAL"))
    .AddAttribute ("ThroughputSamplingMethod", "Mode of throughput estimation.",
                   EnumValue (TILE_BASED),
                   MakeEnumAccessor<ThroughputSamplingMethod> (&TiledDashClient::m_throughputSamplingMethod),
                   MakeEnumChecker (ThroughputSamplingMethod::TILE_BASED, "TILE_BASED", ThroughputSamplingMethod::TIME_BASED, "TIME_BASED"))
    .AddAttribute ("SamplingInterval", "Sampling interval in seconds (for time-based estimation).",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&TiledDashClient::m_samplingInterval),
                   MakeTimeChecker ())
    .AddAttribute ("SamplingWindow", "Number of samples used for throughput estimation.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&TiledDashClient::m_samplingWindow),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("ExponentialWeight", "Exponential weight used for exponential average estimation.",
                   DoubleValue (0.4),
                   MakeDoubleAccessor (&TiledDashClient::m_exponentialWeight),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("RefreshRate", "Video refresh rate in Hz.",
                   DoubleValue (30.0),
                   MakeDoubleAccessor (&TiledDashClient::m_refreshRate),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("VideoMode", "Video mode (2D or 360).",
                   EnumValue (MODE_360),
                   MakeEnumAccessor<VideoMode> (&TiledDashClient::m_videoMode),
                   MakeEnumChecker (MODE_2D, "MODE_2D", MODE_360, "MODE_360"))
    ;
  return tid;
}

TiledDashClient::TiledDashClient (): m_deferredStart(false) 
{
  NS_LOG_FUNCTION (this);
}

TiledDashClient::~TiledDashClient ()
{
  NS_LOG_FUNCTION (this);
}

void
TiledDashClient::SetupQualityRates ()
{
    std::stringstream ss(m_qualityRatesString);
    std::string token;
    while(std::getline(ss, token, ','))
    {
        m_qualityRates.push_back(std::stod(token));
    }
}

void
TiledDashClient::DeferredStartup()
{
  NS_LOG_FUNCTION(this << "at time " << Simulator::Now().GetSeconds());
  
  if (!m_networkManager || !m_tileRequester || !m_mediaDecoder) {
    NS_LOG_ERROR("Cannot start - components not initialized");
    return;
  }

  NS_LOG_INFO("Starting network manager, tile requester, and media decoder at " 
              << Simulator::Now().GetSeconds() << "s");
  
  m_networkManager->StartApplication();
  m_tileRequester->Start();
  m_networkTracker->EstimateOverallThroughput();
  m_mediaDecoder->StartPlayout();
  
  NS_LOG_INFO("Client fully started and streaming at " << Simulator::Now().GetSeconds() << "s");
}

void TiledDashClient::setVideoDuration(Time videoDuration) { 
  
  m_videoDuration = videoDuration;
  m_videoSegments = static_cast<uint32_t> ( m_videoDuration.GetSeconds() / m_segmentDuration.GetSeconds());
   NS_LOG_INFO ("Setting video duration to: " << videoDuration.GetSeconds() << " seconds and video segments to: " << m_videoSegments);
}

void TiledDashClient::NotifyPlayoutComplete() {
  NS_LOG_INFO("TiledDashClient notified that playout is complete.");
  StopApplication();
}
void
TiledDashClient::SetDeferredStart(bool deferred)
{
    m_deferredStart = deferred;
}

// Modify StartApplication to check the flag:
void
TiledDashClient::StartApplication ()
{
  NS_LOG_FUNCTION (this);

  SetupQualityRates();
  
  // Create all components
  m_networkManager = CreateObject<TiledDashClientNetworkManager>();
  NS_LOG_INFO("NetworkManager created: " << m_networkManager);
  m_networkTracker = CreateObject<TiledDashClientNetworkTracker>();
  NS_LOG_INFO("NetworkTracker created: " << m_networkTracker);
  m_fovTracker = CreateObject<TiledDashClientFovTracker>();
  NS_LOG_INFO("FovTracker created: " << m_fovTracker);
  m_mediaBuffer = CreateObject<TiledDashClientMediaBuffer>();
  NS_LOG_INFO("MediaBuffer created: " << m_mediaBuffer);
  m_abrEngine = CreateObject<TiledDashClientAbrEngine>();
  NS_LOG_INFO("ABR engine created: " << m_abrEngine);
  m_tileRequester = CreateObject<TiledDashClientTileRequester>();
  NS_LOG_INFO("TileRequester created: " << m_tileRequester);
  m_mediaDecoder = CreateObject<TiledDashClientMediaDecoder>();
  NS_LOG_INFO("MediaDecoder created: " << m_mediaDecoder);
  
  // Set client pointer in each component
  m_networkManager->SetClient(this);
  m_networkTracker->SetClient(this);
  m_fovTracker->SetClient(this);
  m_mediaBuffer->SetClient(this);
  m_abrEngine->SetClient(this);
  m_tileRequester->SetClient(this);
  m_mediaDecoder->SetClient(this);

  m_mediaBuffer->SetnTiles(m_horTiles*m_verTiles); 
  m_mediaBuffer->SetMinTiles(std::ceil(m_horFovAngle/360*m_horTiles) * 
                             std::ceil(m_verFovAngle/180*m_verTiles)); 
  if (m_segmentDuration > Time(0)) {
    uint32_t maxSegments = static_cast<uint32_t>(
        m_maxBufferDuration.GetSeconds() / m_segmentDuration.GetSeconds() );
    m_mediaBuffer->SetMaxBufferedSegments(maxSegments);
  } else {
    NS_LOG_WARN("Segment duration is zero, cannot calculate max buffered segments. Setting to a default (e.g., 10).");
    m_mediaBuffer->SetMaxBufferedSegments(10);
  }

  // Set fov tracker parameters 
  m_fovTracker->SetPovFile (m_povFile);
  m_fovTracker->SetHorTiles(m_horTiles);
  m_fovTracker->SetVerTiles(m_verTiles);
  if(m_videoMode==MODE_2D){
    m_horFovAngle=360.0;
    m_verFovAngle=180.0;
    m_fovTracker->SetHorFovAngle(m_horFovAngle);
    m_fovTracker->SetVerFovAngle(m_verFovAngle);
  }
  else{
    m_fovTracker->SetHorFovAngle(m_horFovAngle);
    m_fovTracker->SetVerFovAngle(m_verFovAngle);
  }
  m_fovTracker->SetProjectionType(m_projectionType);
  m_fovTracker->SetPredictionAlgorithm(m_fovTrackingAlgorithm);
  m_fovTracker->SetOrientationFuture( static_cast<int>((m_fovTrackingHorizon*m_fovSensorRate).GetSeconds()) ); 
  m_fovTracker->SetOrientationHistory(static_cast<int>((m_fovTrackingHistory*m_fovSensorRate).GetSeconds())); 
  
  m_fovTracker->SetPredictionFile("/Desktop/ns-allinone-3.44/ns-3.44/videoFiles5/prediction2_1.txt"); //Path to prediction file
  // Set abr engine parameters 
  m_abrEngine->SetQualityRates(m_qualityRates);

  // Set decoder parameters
  m_mediaDecoder->SetSegmentDuration (m_segmentDuration);
  
  // Set network manager parameters 
  m_networkManager->SetNumberOfSockets (m_nSockets);
  m_networkManager->SetNode (GetNode());

  // Set network tracker parameters
  //These two lines have to be commented out to be deifned for time-based
  //m_networkTracker->SetThroughputTraceFile(m_throughputTraceFile);
  //m_networkTracker->SetSamplingInterval(m_samplingInterval);
  m_networkTracker->SetSamplingWindow(m_samplingWindow);
  m_networkTracker->SetExponentialWeight(m_exponentialWeight);
  m_networkTracker->SetEstimationMethod(m_throughputEstimationMethod);
  //m_networkTracker->EstimateOverallThroughput();
  // Set tile requester parameters
  m_tileRequester->SetHorTiles(m_horTiles);
  m_tileRequester->SetVerTiles(m_verTiles);
  m_tileRequester->SetVideoName(m_videoName);
  m_tileRequester->SetSegmentDuration(m_segmentDuration);
  //m_tileRequester->SetVideoName("360_Google_DoodlesSpotlight_Stories_Back_to_the_Moon_6x4_1000_dash");
  m_tileRequester->SetVideoName(GetVideoName());
  // CRITICAL: Only start if not deferred
  if (!m_deferredStart) {
    NS_LOG_INFO("Starting application immediately");
    DeferredStartup();
  } else {
    NS_LOG_INFO("Application startup deferred - waiting for explicit start call");
  }
  
  NS_LOG_INFO("TiledDashClient::StartApplication finished configuration.");
}
void
TiledDashClient::StopApplication ()
{
  NS_LOG_FUNCTION (this); 

  m_networkManager->StopApplication ();

  //m_tileRequester->StopApplication ();
  m_fovTracker->StopTracking();
  //m_mediaDecoder->StopPlayout();
  //m_networkTracker->StopApplication();
  m_mediaBuffer->StopApplication();
}

void
TiledDashClient::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  m_networkManager = nullptr;
  m_tileRequester = nullptr;
  m_abrEngine = nullptr;
  m_mediaBuffer = nullptr;
  m_mediaDecoder = nullptr;
  m_fovTracker = nullptr;
  m_networkTracker = nullptr;

  Application::DoDispose ();
}
void TiledDashClient::setQualityRateString(std::string qualityRatesString) { 
  m_qualityRatesString = qualityRatesString; 
  SetupQualityRates(); 
}


} // namespace ns3
