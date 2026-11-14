#include "tiled-dash-client.h"
#include "tiled-dash-client-media-decoder.h"
#include "tiled-dash-client-tile-requester.h"
#include "tiled-dash-client-fov-tracker.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/integer.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("TiledDashClientFovTracker");

NS_OBJECT_ENSURE_REGISTERED(TiledDashClientFovTracker);

// Static variables for the conversion functions
static const double PI = 3.14159265358979323846;
static const double RAD2DEG = 180.0 / PI;
static const double DEG2RAD = PI / 180.0;

TypeId
TiledDashClientFovTracker::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::TiledDashClientFovTracker")
            .SetParent<Object>()
            .SetGroupName("Applications")
            .AddConstructor<TiledDashClientFovTracker>()
            .AddAttribute("PovFile",
                          "Path to the file containing Point of View (PoV) data.",
                          StringValue("pov.txt"), //bilo je pov.dat 
                          MakeStringAccessor(&TiledDashClientFovTracker::m_povFile),
                          MakeStringChecker())
            .AddAttribute("PredictionFile",
                          "Path to the file containing pre-processed PoV data for prediction.",
                          StringValue("prediction.txt"),
                          MakeStringAccessor(&TiledDashClientFovTracker::m_predictionFile),
                          MakeStringChecker())
            .AddAttribute("FovUpdateRate",
                          "Rate at which to update the Field of View (FoV) in Hz.",
                          DoubleValue(60.0), // Default to 60 Hz
                          MakeDoubleAccessor(&TiledDashClientFovTracker::m_fovUpdateRate),
                          MakeDoubleChecker<double>())
            .AddAttribute("OrientationHistory",
                          "Number of past orientation samples to use for FoV prediction.",
                          IntegerValue(10), // Default to 10 samples
                          MakeIntegerAccessor(&TiledDashClientFovTracker::m_orientationHistory),
                          MakeIntegerChecker<int>())
            .AddAttribute("OrientationFuture",
                          "Number of future orientation samples to predict.",
                          IntegerValue(5), // Default to 5 samples
                          MakeIntegerAccessor(&TiledDashClientFovTracker::m_orientationFuture),
                          MakeIntegerChecker<int>())
            .AddAttribute("HorizontalVideoTiles",
                          "Number of horizontal tiles in the video.",
                          IntegerValue(32), // Default to 32 tiles
                          MakeIntegerAccessor(&TiledDashClientFovTracker::m_horTiles),
                          MakeIntegerChecker<int>())
            .AddAttribute("VerticalVideoTiles",
                          "Number of vertical tiles in the video.",
                          IntegerValue(16), // Default to 16 tiles
                          MakeIntegerAccessor(&TiledDashClientFovTracker::m_verTiles),
                          MakeIntegerChecker<int>())
            .AddAttribute("HorizontalFovAngle",
                          "Horizontal field of view angle in degrees.",
                          DoubleValue(90.0), // Default to 90 degrees
                          MakeDoubleAccessor(&TiledDashClientFovTracker::m_horFovAngle),
                          MakeDoubleChecker<double>())
            .AddAttribute("VerticalFovAngle",
                          "Vertical field of view angle in degrees.",
                          DoubleValue(90.0), // Default to 90 degrees
                          MakeDoubleAccessor(&TiledDashClientFovTracker::m_verFovAngle),
                          MakeDoubleChecker<double>())
			.AddAttribute("PovDataType",
						  "Type of POV data: 0 for Quaternion, 1 for Euler Angles",
						  EnumValue(EULER_ANGLES), // Default is Euler Angles
						  MakeEnumAccessor<PovDataType>(&TiledDashClientFovTracker::m_povDataType),
						  MakeEnumChecker(PovDataType::QUATERNION, "QUATERNION",
										  PovDataType::EULER_ANGLES, "EULER_ANGLES"))
            .AddAttribute("ProjectionType",
                          "Video projection type.",
                          EnumValue(EQUIRECTANGULAR), // Default value from your enum
                          MakeEnumAccessor<ProjectionType>(&TiledDashClientFovTracker::m_projectionType),
                          MakeEnumChecker(EQUIRECTANGULAR, "Equirectangular",
                                          CUBEMAP, "Cubemap")) // List all valid enum values
            .AddAttribute("FovPredictionAlgorithm",
                          "FoV prediction algorithm to use.",
                          EnumValue(IDEAL_FOV), // Default value from your enum
                          MakeEnumAccessor<FovTrackingAlgorithm>(&TiledDashClientFovTracker::m_fovPredictionAlgorithm),
                          MakeEnumChecker(IDEAL_FOV, "Ideal",
                                          LINEAR_REGRESSION, "LinearRegression"))
            ;
    return tid;
}

void
TiledDashClientFovTracker::DoInitialize() {
    NS_LOG_FUNCTION(this);

    NS_LOG_DEBUG("Initializing FoV Tracker with:");
    NS_LOG_DEBUG("  HorTiles: " << m_horTiles);
    NS_LOG_DEBUG("  VerTiles: " << m_verTiles);
    NS_LOG_DEBUG("  HorFovAngle: " << m_horFovAngle);
    NS_LOG_DEBUG("  VerFovAngle: " << m_verFovAngle);

    // Check for valid tile counts to avoid division by zero
    if (m_horTiles <= 0 || m_verTiles <= 0) {
        NS_LOG_ERROR("Invalid tile counts: HorTiles=" << m_horTiles << ", VerTiles=" << m_verTiles);
        // Handle error appropriately, maybe set defaults or mark object as invalid
        m_horFovTiles = 1; // Or some default/error value
        m_verFovTiles = 1;
        Object::DoInitialize(); // Call parent's DoInitialize
        return;
    }

    // Assuming tiles are equally spaced in the full 360x180 degree panorama
    double horTileAngle = 360.0 / m_horTiles; // Degrees per horizontal tile
    double verTileAngle = 180.0 / m_verTiles; // Degrees per vertical tile

    // Calculate how many tiles the FoV angle covers.
    // Use std::ceil to ensure we cover *at least* the specified angle.
    // Handle potential division by zero if tile angles are zero (shouldn't happen with valid m_*Tiles > 0)
    if (horTileAngle > 0) {
        m_horFovTiles = static_cast<int>(std::ceil(m_horFovAngle / horTileAngle));
    } else {
        m_horFovTiles = m_horTiles; // Fallback if calculation fails
        NS_LOG_WARN("Calculated horTileAngle is zero or negative.");
    }

    if (verTileAngle > 0) {
        m_verFovTiles = static_cast<int>(std::ceil(m_verFovAngle / verTileAngle));
    } else {
        m_verFovTiles = m_verTiles; // Fallback if calculation fails
        NS_LOG_WARN("Calculated verTileAngle is zero or negative.");
    }

    // Ensure at least 1 tile is considered in the FoV area.
    m_horFovTiles = std::max(1.0, m_horFovTiles);
    m_verFovTiles = std::max(1.0, m_verFovTiles);

    NS_LOG_DEBUG("Calculated FoV tile coverage:");
    NS_LOG_DEBUG("  m_horFovTiles: " << m_horFovTiles);
    NS_LOG_DEBUG("  m_verFovTiles: " << m_verFovTiles);

    Object::DoInitialize(); // Important: Call the parent's DoInitialize
}

TiledDashClientFovTracker::TiledDashClientFovTracker()
        : m_horFovTiles(1), // Will be calculated in DoInitialize
          m_verFovTiles(1),
          m_fovUpdateRate(60.0),  //initialized here and also through attribute.
          m_fovUpdateTime(Seconds(0.0)),
          m_povDataType(PovDataType::EULER_ANGLES), // Default value
          m_fovPredictionAlgorithm(FovTrackingAlgorithm::IDEAL_FOV),
          m_fovTrackingEvent(), //needs further testing
          m_projectionType(ProjectionType::EQUIRECTANGULAR) // C++ default

{
    NS_LOG_FUNCTION(this);
}

TiledDashClientFovTracker::~TiledDashClientFovTracker() {
    NS_LOG_FUNCTION(this);
    NS_LOG_DEBUG("Ending tracking");
    StopTracking();
    /*
    if (m_povFileHandle.is_open()) {
        m_povFileHandle.close();
    }
     if (m_predictionFileHandle.is_open()) {
        m_predictionFileHandle.close();
    }
    */
}


void
TiledDashClientFovTracker::SetPovFile(const std::string& filename) {
    m_povFile = filename;
}

void
TiledDashClientFovTracker::SetPredictionFile(const std::string& filename) {
    m_predictionFile = filename;
}

void
TiledDashClientFovTracker::SetFovUpdateRate(double rate) {
    m_fovUpdateRate = rate;
}

void
TiledDashClientFovTracker::SetOrientationHistory(int history) {
    m_orientationHistory = history;
}

void
TiledDashClientFovTracker::SetOrientationFuture(int future) {
    m_orientationFuture = future;
}

void
TiledDashClientFovTracker::SetHorTiles(int tiles) {
    m_horTiles = tiles;
}

void
TiledDashClientFovTracker::SetVerTiles(int tiles) {
    m_verTiles = tiles;
}

void
TiledDashClientFovTracker::SetHorFovAngle(double angle) {
    m_horFovAngle = angle;
}

void
TiledDashClientFovTracker::SetVerFovAngle(double angle) {
    m_verFovAngle = angle;
}


void
TiledDashClientFovTracker::SetPovDataType(PovDataType type) {
	m_povDataType = type;
}

void
TiledDashClientFovTracker::SetProjectionType(ProjectionType projectionType) {
    m_projectionType = projectionType;
}

void
TiledDashClientFovTracker::SetPredictionAlgorithm(FovTrackingAlgorithm algorithm) {
    m_fovPredictionAlgorithm = algorithm;
}




void
TiledDashClientFovTracker::StartTracking() {
 
    NS_LOG_FUNCTION(this);

    // --- NEW: Handle 1x1 tile grid ---
    if (m_horTiles == 1 && m_verTiles == 1) {
        NS_LOG_INFO("1x1 tile grid: FoV always covers the single tile.");
        std::vector<int> allZeros(1, 0);
        m_currentFovTiles = allZeros;
        m_pastFovTiles = allZeros;
        // Immediately notify the decoder so it uses score 0
        m_client->GetMediaDecoder()->HandleFovChange(allZeros);
        // Do not read POV file, do not schedule any tracking updates
        return;
    }
    NS_LOG_DEBUG("StartTracking called. Object address: " << this);
    NS_LOG_DEBUG("StartTracking called. Object address: " << this << ", m_horFovTiles: " << m_horFovTiles << ", m_verFovTiles: " << m_verFovTiles);
    NS_ASSERT_MSG(m_client->GetMediaDecoder() != nullptr, "Decoder must be set before starting tracking.");
    //NS_ASSERT_MSG(m_fovTrackingEvent.IsExpired(), "Tracking event should not be scheduled yet."); //check
    if (m_isTracking) {
        NS_LOG_WARN("Tracking is already started. Ignoring redundant call.");
        return;
    } 
    if (!m_fovTrackingEvent.IsExpired()) {
        NS_LOG_WARN("Cancelling existing FoV tracking event.");
        Simulator::Cancel(m_fovTrackingEvent);
    }

    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(!m_povFile.empty(), "PoV file must be set.");
    m_povFileHandle.open(m_povFile.c_str());
    NS_ASSERT_MSG(m_povFileHandle.is_open(), "Failed to open PoV file.");
    NS_ASSERT_MSG(!m_predictionFile.empty(), "Prediction file must be set.");
    m_predictionFileHandle.open(m_predictionFile.c_str());
    NS_ASSERT_MSG(m_predictionFileHandle.is_open(), "Failed to open Prediction file.");
    // Schedule the first FoV update
    TrackFoV();
    m_isTracking = true;
}

void TiledDashClientFovTracker::StopTracking() {
    NS_LOG_FUNCTION(this);
    m_isTracking = false;
    // Cancel the tracking event if it's running
    if (!m_fovTrackingEvent.IsExpired()) {
        Simulator::Cancel(m_fovTrackingEvent);
    }
    
    // Close file handles
    if (m_povFileHandle.is_open()) {
        m_povFileHandle.close();
    }
    if (m_predictionFileHandle.is_open()) {
        m_predictionFileHandle.close();
    }
}

void
TiledDashClientFovTracker::HaltFovTracking() {
    NS_LOG_FUNCTION(this);
    m_isTracking = false; 
    if (m_fovTrackingEvent.IsExpired())
    {
      return;
    }
    m_fovTrackingEvent.Cancel();
}

void
TiledDashClientFovTracker::ResumeTrackFov(Time lastStallDur) {
    NS_LOG_FUNCTION(this << lastStallDur);
    m_isTracking = true; 
    // Reschedule the FoV update, accounting for the stall duration
    m_fovUpdateTime += lastStallDur;
    m_fovTrackingEvent = Simulator::Schedule(m_fovUpdateTime, &TiledDashClientFovTracker::TrackFoV, this);
}

void
TiledDashClientFovTracker::TrackFoV() {
    NS_LOG_FUNCTION(this);

    // Read a line from the PoV file
    std::string line;
    if (!std::getline(m_povFileHandle, line)) {
        NS_LOG_ERROR("Error reading PoV data from file.  End of file reached.");
        return; // Stop tracking if we reach the end of the file.
    }

    PoV currentPov;
    std::istringstream iss(line);
    double timestampValue;

    // Parse the line based on the POV data type
    if (m_povDataType == PovDataType::QUATERNION) {
        double x, y, z, w;
        
        if (!(iss >> x >> y >> z >> w >> timestampValue)) {
            NS_LOG_ERROR("Error parsing quaternion data from line: " << line);
            return;
        }
        Time timestamp = Seconds(timestampValue);
        currentPov = QuaternionToEuler(x, y, z, w, timestamp);
    } 
    else { // m_povDataType == PovDataType::EULER_ANGLES
        if (!(iss >> currentPov.yaw >> currentPov.pitch >> currentPov.roll >> timestampValue)) {
            NS_LOG_ERROR("Error parsing Euler angle data from line: " << line);
            return;
        }
        NS_LOG_WARN("yaw" <<currentPov.yaw <<" pitch "<< currentPov.pitch << " roll "<<currentPov.roll << " timestamp "<< timestampValue);
        currentPov.timestamp = Seconds(timestampValue);
    }

    // Update FoV tiles and predict future FoV
    UpdateFovTiles(currentPov);
    PredictFovTiles(currentPov);


    // Schedule the next FoV update
    m_fovUpdateTime = Seconds(1.0 / m_fovUpdateRate);
    //m_fovUpdateTime = Simulator::Now() + Seconds(1.0 / m_fovUpdateRate);
    m_fovTrackingEvent = Simulator::Schedule(m_fovUpdateTime, &TiledDashClientFovTracker::TrackFoV, this);
}


void
TiledDashClientFovTracker::UpdateFovTiles(const PoV& currentPov) {
    NS_LOG_FUNCTION(this);
    std::vector<int> fovScores = PovToTiles(currentPov); 
    m_currentFovTiles=fovScores;   
    if (m_pastFovTiles.empty()) {
        m_pastFovTiles = fovScores;
    } else if (!CompareFovTiles(fovScores, m_pastFovTiles)) {
        m_client->GetMediaDecoder()->HandleFovChange(fovScores); // Notify decoder of FoV change
        m_pastFovTiles = fovScores;
    }

}

void
TiledDashClientFovTracker::PredictFovTiles(const PoV& currentPov) {
    NS_LOG_FUNCTION(this);

     // Add current PoV to the history
    m_povHistory.push(currentPov);

    if (m_povHistory.size() < (size_t)m_orientationHistory) {
        return; // Wait until we have enough history.
    }

    // Call the prediction algorithm
    std::vector<PoV> predictedPovs;
    switch (m_fovPredictionAlgorithm) {
    case IDEAL_FOV:
        predictedPovs = PredictIdealFov();
        break;
    case LINEAR_REGRESSION:
        // predictedPovs = PredictLinearRegressionFov(); // You'd need to implement this
        NS_LOG_WARN("LinearRegressionFov prediction not yet implemented, using Ideal.");
        predictedPovs = PredictIdealFov(); // Fallback for now
        break;
    // Add other cases as you implement more algorithms
    default:
        NS_LOG_ERROR("Unknown FoV prediction algorithm: " << static_cast<int>(m_fovPredictionAlgorithm));
        // Fallback to a default or return empty
        predictedPovs = PredictIdealFov();
        break;
    }
    
    std::vector<int> predictedFovScores = PovsToTiles(predictedPovs);
    if (m_predictedFovTiles.empty() || !CompareFovTiles(predictedFovScores, m_predictedFovTiles)) {
        // Notify the tile requester (through the decoder) of the change in predicted FoV tiles.
        //NS_LOG_WARN("Testing");        
        std::vector<uint32_t> neededSegmentsIds = m_client->
        GetMediaDecoder()->findHorizonSegments(m_client->GetFovTrackingHorizon());
        //NS_LOG_WARN("Size of the predicted needed segments"<<neededSegmentsIds.size());
        m_predictedFovTiles = predictedFovScores;
        //for(auto p:predictedFovScores){
        //    NS_LOG_WARN("trenutni fov p "<<p);
        //}
        m_client->GetTileRequester()->UpdateTileRequestQueue(predictedFovScores, neededSegmentsIds); //same as the current fov.
    }
    m_povHistory.pop(); // Remove the oldest PoV from the history
    return;
}

std::vector<int>
TiledDashClientFovTracker::PovToTiles(const PoV& pov) {
    NS_LOG_FUNCTION(this);

    if (m_horTiles <= 0 || m_verTiles <= 0) {
        NS_LOG_ERROR("Invalid tile counts: HorTiles=" << m_horTiles << ", VerTiles=" << m_verTiles);
        return std::vector<int>(m_horTiles * m_verTiles, 3); // fallback, same as before
    }

    int totalTiles = m_horTiles * m_verTiles;
    std::vector<int> fovScores(totalTiles, 3); // Initialize all scores to 3 (default)
    //int minTiles = totalTiles; // was hardcoded 24; now tracks actual grid size
    int minTiles=m_client->GetMediaBuffer()->GetMinTiles();
    // === CALCULATE FOV TILE COVERAGE ===
    double horTileAngle = 360.0 / m_horTiles;
    double verTileAngle = 180.0 / m_verTiles;

    int horFovTiles = 1;
    int verFovTiles = 1;

    if (horTileAngle > 0) {
        horFovTiles = static_cast<int>(std::ceil((double)m_horFovAngle / horTileAngle));
    }
    if (verTileAngle > 0) {
        verFovTiles = static_cast<int>(std::ceil((double)m_verFovAngle / verTileAngle));
    }

    horFovTiles = std::max(1, horFovTiles);
    verFovTiles = std::max(1, verFovTiles);

    NS_LOG_DEBUG("Calculated FoV tile coverage - Horizontal: " << horFovTiles
                 << ", Vertical: " << verFovTiles);
    // === END OF FOV TILE COVERAGE CALCULATION ===

    // 1. Convert POV to normalized coordinates [0,1]
    double yaw = pov.yaw;
    double pitch = pov.pitch;

    // Normalize yaw to [0, 360)
    while (yaw < 0.0) yaw += 360.0;
    while (yaw >= 360.0) yaw -= 360.0;

    // Normalize pitch to [-90, 90]
    if (pitch > 180.0) {
        pitch = pitch - 360.0;
    }
    pitch = std::max(-90.0, std::min(90.0, pitch));

    NS_LOG_DEBUG("Original angles - yaw: " << pov.yaw << ", pitch: " << pov.pitch);
    NS_LOG_DEBUG("Normalized angles - yaw: " << yaw << ", pitch: " << pitch);

    double x = yaw / 360.0;
    double y = (pitch + 90.0) / 180.0;

    NS_LOG_DEBUG("Normalized coordinates - x: " << x << ", y: " << y);

    double tileX = x * m_horTiles;
    double tileY = y * m_verTiles;

    if (tileX >= m_horTiles) tileX = m_horTiles - 1;
    if (tileY >= m_verTiles) tileY = m_verTiles - 1;

    int centerTileX = static_cast<int>(tileX);
    int centerTileY = static_cast<int>(tileY);

    NS_LOG_DEBUG("Tile coordinates - tileX: " << tileX << ", tileY: " << tileY);
    NS_LOG_DEBUG("Center tile indices - X: " << centerTileX << ", Y: " << centerTileY);

    double halfHorFov = horFovTiles / 2.0;
    double halfVerFov = verFovTiles / 2.0;

    // --- ANGLE DEFINITION ---
    // Horizontal (yaw) is circular: do NOT clamp minX/maxX to [0, m_horTiles-1].
    // Keep the raw (possibly out-of-range / negative) bounds and wrap with modulo
    // when indexing into fovScores. Vertical (pitch) is NOT circular, so it still clamps.
    int minXraw = static_cast<int>(std::floor(tileX - halfHorFov));
    int maxXraw = static_cast<int>(std::floor(tileX + halfHorFov));
    int minY = std::max(0, static_cast<int>(std::floor(tileY - halfVerFov)));
    int maxY = std::min(m_verTiles - 1, static_cast<int>(std::floor(tileY + halfVerFov)));

    // Guard against a FoV window wider than the grid itself (e.g. horFovTiles > m_horTiles)
    int horSpan = maxXraw - minXraw + 1;
    if (horSpan >= m_horTiles) {
        minXraw = 0;
        maxXraw = m_horTiles - 1; // covers everything, no need to wrap
    }

    NS_LOG_DEBUG("FoV bounding box (raw, pre-wrap): minX=" << minXraw << ", maxX=" << maxXraw
                 << ", minY=" << minY << ", maxY=" << maxY);
    NS_LOG_DEBUG("Half FoV values - hor: " << halfHorFov << ", ver: " << halfVerFov);

    // 2. Set the scores for the tiles within the FoV to 0 (with horizontal wraparound)
    for (int y = minY; y <= maxY; ++y) {
        for (int xr = minXraw; xr <= maxXraw; ++xr) {
            int x = ((xr % m_horTiles) + m_horTiles) % m_horTiles;
            int tileIndex = y * m_horTiles + x;
            if (tileIndex >= 0 && tileIndex < (int)fovScores.size()) {
                fovScores[tileIndex] = 0;
            }
        }
    }

    // 3. Direct neighbors -> score 1
    for (int y = minY - 1; y <= maxY + 1; ++y) {
        for (int xr = minXraw - 1; xr <= maxXraw + 1; ++xr) {
            if (y >= 0 && y < m_verTiles) {
                int x = ((xr % m_horTiles) + m_horTiles) % m_horTiles;
                int tileIndex = y * m_horTiles + x;
                if (tileIndex >= 0 && tileIndex < (int)fovScores.size()) {
                    if (fovScores[tileIndex] == 3) fovScores[tileIndex] = 1;
                }
            }
        }
    }

    // 4. Second-level neighbors -> score 2
    for (int y = minY - 2; y <= maxY + 2; ++y) {
        for (int xr = minXraw - 2; xr <= maxXraw + 2; ++xr) {
            if (y >= 0 && y < m_verTiles) {
                int x = ((xr % m_horTiles) + m_horTiles) % m_horTiles;
                int tileIndex = y * m_horTiles + x;
                if (tileIndex >= 0 && tileIndex < (int)fovScores.size()) {
                    if (fovScores[tileIndex] == 3) fovScores[tileIndex] = 2;
                }
            }
        }
    }

    // === APPLY MIN TILES LOGIC === (driven by real grid size)
    if (minTiles < (int)fovScores.size()) {
        std::vector<int> selectedTiles;

        for (int i = 0; i < (int)fovScores.size(); i++) {
            if (fovScores[i] == 0) {
                selectedTiles.push_back(i);
            }
            if (selectedTiles.size() >= (size_t)minTiles) break;
        }

        if (selectedTiles.size() < (size_t)minTiles) {
            for (int i = 0; i < (int)fovScores.size(); i++) {
                if (fovScores[i] == 1) {
                    selectedTiles.push_back(i);
                    if (selectedTiles.size() >= (size_t)minTiles) break;
                }
            }
        }

        if (selectedTiles.size() < (size_t)minTiles) {
            for (int i = 0; i < (int)fovScores.size(); i++) {
                if (fovScores[i] == 2) {
                    selectedTiles.push_back(i);
                    if (selectedTiles.size() >= (size_t)minTiles) break;
                }
            }
        }

        if (selectedTiles.size() < (size_t)minTiles) {
            for (int i = 0; i < (int)fovScores.size(); i++) {
                if (fovScores[i] == 3) {
                    selectedTiles.push_back(i);
                    if (selectedTiles.size() >= (size_t)minTiles) break;
                }
            }
        }

        std::vector<int> finalFovScores(fovScores.size(), 4);
        for (int tileIdx : selectedTiles) {
            finalFovScores[tileIdx] = fovScores[tileIdx];
        }

        fovScores = finalFovScores;

        NS_LOG_DEBUG("MinTiles filter applied: " << selectedTiles.size()
                     << " tiles selected out of " << minTiles << " required");
    }

    // Debug: Count tiles by score
    int count0 = 0, count1 = 0, count2 = 0, count3 = 0;
    for (int s : fovScores) {
        switch (s) {
            case 0: count0++; break;
            case 1: count1++; break;
            case 2: count2++; break;
            case 3: count3++; break;
        }
    }
    NS_LOG_DEBUG("Score distribution: 0s=" << count0 << ", 1s=" << count1
                 << ", 2s=" << count2 << ", 3s=" << count3);

    return fovScores;
}


std::vector<int>
TiledDashClientFovTracker::PovsToTiles(const std::vector<PoV>& window) {
    NS_LOG_FUNCTION(this);
    std::vector<int> fovScores(m_horTiles * m_verTiles, 0); // Initialize all scores to 0, m_horTiles * m_verTiles, 24
    //std::vector<int> fovScores(25, 0); 
    // Calculate fovScores for each POV in the window and update the combined fovScores
    for (const auto& pov : window) {
        std::vector<int> currentFovScores = PovToTiles(pov);
        for (size_t i = 0; i < fovScores.size(); ++i) {
            if (currentFovScores[i] >= 0 ) { // Consider only positive scores from individual POVs, dodao ovo manje od 4 && currentFovScores[i] < 4
                if (fovScores[i] == 0)
                {
                    fovScores[i] = currentFovScores[i];
                }
                else
                {
                  fovScores[i] = std::min(fovScores[i], currentFovScores[i]);
                }
            }
        }
    }
    return fovScores;
}

bool
TiledDashClientFovTracker::CompareFovTiles(const std::vector<int>& fovScore1, const std::vector<int>& fovScore2) {
    NS_LOG_FUNCTION(this);
    if (fovScore1.size() != fovScore2.size()) {
        return false; // Different sizes, not equal
    }
    for (size_t i = 0; i < fovScore1.size(); ++i) {
        if (fovScore1[i] != fovScore2[i]) {
            return false; // Found a difference
        }
    }
    return true; // All elements are equal
}
//do 27.01
std::vector<PoV> 
TiledDashClientFovTracker::PredictIdealFov() { 
    NS_LOG_FUNCTION(this);
    //static std::deque<PoV> predictedPovs; // Use a static deque to store the predicted POVs
    std::deque<PoV> predictedPovs; 
    if (predictedPovs.empty())
    {
        for (int i = 0; i < m_orientationFuture; ++i) {
            std::string line;
            if (!std::getline(m_predictionFileHandle, line)) {
                NS_LOG_ERROR("Error reading predicted PoV data from file.  End of file reached.");
                break;
            }
            std::istringstream iss(line);
            PoV predictedPov;
            double timestampValue;
            if (m_povDataType == PovDataType::QUATERNION) {
                double x, y, z, w;
                
                if (!(iss >> x >> y >> z >> w >> timestampValue)) {
                    NS_LOG_ERROR("Error parsing quaternion data from line: " << line);
                    break;
                }
                Time timestamp = Seconds(timestampValue);
                predictedPov = QuaternionToEuler(x,y,z,w, timestamp);
            }
            else
            {
                if (!(iss >> predictedPov.yaw >> predictedPov.pitch >> predictedPov.roll >> timestampValue)) {
                    NS_LOG_ERROR("Error parsing predicted Euler angle data from line: " << line);
                    break;
                }
                predictedPov.timestamp = Seconds(timestampValue);
            }
            predictedPovs.push_back(predictedPov);
        }
    }
    else
    {
         std::string line;
        if (!std::getline(m_predictionFileHandle, line)) {
            NS_LOG_ERROR("Error reading predicted PoV data from file.  End of file reached.");
            return std::vector<PoV>(predictedPovs.begin(), predictedPovs.end()); // Return the current queue
        }
        std::istringstream iss(line);
        PoV newPredictedPov;
        double timestampValue;
        if (m_povDataType == PovDataType::QUATERNION) {
            double x, y, z, w;
            if (!(iss >> x >> y >> z >> w >> timestampValue)) {
                NS_LOG_ERROR("Error parsing quaternion data from line: " << line);
                return std::vector<PoV>(predictedPovs.begin(), predictedPovs.end());
            }
            Time timestamp = Seconds(timestampValue);
            newPredictedPov = QuaternionToEuler(x,y,z,w, timestamp);
        }
        else
        {
            if (!(iss >> newPredictedPov.yaw >> newPredictedPov.pitch >> newPredictedPov.roll >> timestampValue)) {
                NS_LOG_ERROR("Error parsing predicted Euler angle data from line: " << line);
                return std::vector<PoV>(predictedPovs.begin(), predictedPovs.end());
            }
            newPredictedPov.timestamp = Seconds(timestampValue);
        }
        predictedPovs.pop_front();
        predictedPovs.push_back(newPredictedPov);
    }
    //Logging component
    //for (const auto& pov : predictedPovs) {
    //    NS_LOG_WARN("Vrijeme: " << pov.timestamp.GetSeconds() << "s | " <<
    //              "Yaw: " << pov.yaw << ", Pitch: " << pov.pitch << ", Roll: " << pov.roll);
    //}
    return std::vector<PoV>(predictedPovs.begin(), predictedPovs.end());
}

PoV
TiledDashClientFovTracker::QuaternionToEuler(double x, double y, double z, double w, Time timestamp) {
    NS_LOG_FUNCTION(this);
    PoV euler;
    euler.timestamp = timestamp;

    double t0 = +2.0 * (w * x + y * z);
    double t1 = +1.0 - 2.0 * (x * x + y * y);
    euler.yaw = std::atan2(t0, t1) * RAD2DEG;

    double t2 = +2.0 * (w * y - z * x);
    t2 = (t2 > 1.0) ? 1.0 : t2;
    t2 = (t2 < -1.0) ? -1.0 : t2;
    euler.pitch = std::asin(t2) * RAD2DEG;

    double t3 = +2.0 * (w * z + x * y);
    double t4 = +1.0 - 2.0 * (y * y + z * z);
    euler.roll = std::atan2(t3, t4) * RAD2DEG;

    return euler;
}

PoV
TiledDashClientFovTracker::EulerToQuaternion(double yaw, double pitch, double roll, Time timestamp) {
    NS_LOG_FUNCTION(this);
    PoV quaternion;
    quaternion.timestamp = timestamp;

    double yawRad = yaw * DEG2RAD;
    double pitchRad = pitch * DEG2RAD;
    double rollRad = roll * DEG2RAD;

    double cy = std::cos(yawRad * 0.5);
    double sy = std::sin(yawRad * 0.5);
    double cp = std::cos(pitchRad * 0.5);
    double sp = std::sin(pitchRad * 0.5);
    double cr = std::cos(rollRad * 0.5);
    double sr = std::sin(rollRad * 0.5);

    double w = cr * cp * cy + sr * sp * sy;
    double x = sr * cp * cy - cr * sp * sy;
    double y = cr * sp * cy + sr * cp * sy;
    double z = cr * cp * sy - sr * sp * cy;

    quaternion.yaw = x;
    quaternion.pitch = y;
    quaternion.roll = z;
    return quaternion;
}


// Function to generate prediction file from original PoV file
void
TiledDashClientFovTracker::GeneratePredictionFile(const std::string& inputPovFile, 
                                                  const std::string& outputPredictionFile,
                                                  FovTrackingAlgorithm algorithm) {
    NS_LOG_FUNCTION(this << inputPovFile << outputPredictionFile << static_cast<int>(algorithm));
    
    std::ifstream inputFile(inputPovFile.c_str());
    if (!inputFile.is_open()) {
        NS_LOG_ERROR("Failed to open input PoV file: " << inputPovFile);
        return;
    }
    
    std::vector<PoV> originalPovs;
    std::string line;
    
    // Read all PoV data from input file
    while (std::getline(inputFile, line)) {
        std::istringstream iss(line);
        PoV pov;
        double timestampValue;
        
        if (m_povDataType == PovDataType::QUATERNION) {
            double x, y, z, w;
            if (!(iss >> x >> y >> z >> w >> timestampValue)) {
                NS_LOG_WARN("Error parsing quaternion data from line: " << line);
                continue;
            }
            pov = QuaternionToEuler(x, y, z, w, Seconds(timestampValue));
        } else {
            if (!(iss >> pov.yaw >> pov.pitch >> pov.roll >> timestampValue)) {
                NS_LOG_WARN("Error parsing Euler angle data from line: " << line);
                continue;
            }
            pov.timestamp = Seconds(timestampValue);
        }
        originalPovs.push_back(pov);
    }
    inputFile.close();
    
    if (originalPovs.empty()) {
        NS_LOG_ERROR("No valid PoV data found in input file");
        return;
    }
    
    // Generate predictions based on algorithm
    std::vector<PoV> predictedPovs;
    
    switch (algorithm) {
        case IDEAL_FOV:
            predictedPovs = GenerateIdealFovPrediction(originalPovs);
            break;
        case LINEAR_REGRESSION:
            predictedPovs = GenerateLinearRegressionPrediction(originalPovs);
            break;
        default:
            NS_LOG_ERROR("Unknown prediction algorithm");
            return;
    }
    
    // Write predictions to output file
    WritePredictionFile(outputPredictionFile, predictedPovs);
    
    NS_LOG_INFO("Generated prediction file: " << outputPredictionFile 
                << " with " << predictedPovs.size() << " entries");
}

std::vector<PoV>
TiledDashClientFovTracker::GenerateIdealFovPrediction(const std::vector<PoV>& originalPovs) {
    NS_LOG_FUNCTION(this);
    
    std::vector<PoV> predictedPovs;
    
    // For ideal prediction, we simply shift the original data by the history window
    // This simulates perfect future knowledge
    int startIndex = m_orientationHistory;
    
    if (originalPovs.size() <= (size_t)startIndex) {
        NS_LOG_WARN("Not enough PoV data for history window. Need at least " 
                    << m_orientationHistory << " samples");
        return predictedPovs;
    }
    
    // Take data starting from history window offset
    for (size_t i = startIndex; i < originalPovs.size(); ++i) {
        PoV predictedPov = originalPovs[i];
        // Adjust timestamp to match the prediction timing
        // The prediction should be aligned with the current tracking time
        predictedPov.timestamp = Seconds((i - startIndex) * (1.0 / m_fovUpdateRate));
        predictedPovs.push_back(predictedPov);
    }
    
    return predictedPovs;
}

std::vector<PoV>
TiledDashClientFovTracker::GenerateLinearRegressionPrediction(const std::vector<PoV>& originalPovs) {
    NS_LOG_FUNCTION(this);
    
    std::vector<PoV> predictedPovs;
    
    if (originalPovs.size() < (size_t)(m_orientationHistory + m_orientationFuture)) {
        NS_LOG_WARN("Not enough data for linear regression prediction");
        return predictedPovs;
    }
    
    // For each prediction point, use linear regression on the history window
    for (size_t currentIndex = m_orientationHistory; 
         currentIndex < originalPovs.size() - m_orientationFuture; ++currentIndex) {
        
        // Use history window for prediction
        std::vector<PoV> historyWindow;
        for (int h = 0; h < m_orientationHistory; ++h) {
            historyWindow.push_back(originalPovs[currentIndex - m_orientationHistory + h]);
        }
        
        // Predict future orientations using linear regression
        for (int f = 1; f <= m_orientationFuture; ++f) {
            PoV predictedPov = PredictLinearRegression(historyWindow, f);
            predictedPov.timestamp = Seconds((currentIndex - m_orientationHistory + f) * (1.0 / m_fovUpdateRate));
            predictedPovs.push_back(predictedPov);
        }
    }
    
    return predictedPovs;
}

PoV
TiledDashClientFovTracker::PredictLinearRegression(const std::vector<PoV>& historyWindow, int futureStep) {
    NS_LOG_FUNCTION(this << futureStep);
    
    if (historyWindow.size() < 2) {
        // Not enough data for regression, return last known value
        return historyWindow.back();
    }
    
    // Simple linear regression for yaw, pitch, roll
    // Using time-based regression
    
    double sumX = 0, sumY_yaw = 0, sumY_pitch = 0, sumY_roll = 0;
    double sumXX = 0, sumXY_yaw = 0, sumXY_pitch = 0, sumXY_roll = 0;
    int n = historyWindow.size();
    
    for (int i = 0; i < n; ++i) {
        double x = i; // time index
        double yaw = historyWindow[i].yaw;
        double pitch = historyWindow[i].pitch;
        double roll = historyWindow[i].roll;
        
        sumX += x;
        sumY_yaw += yaw;
        sumY_pitch += pitch;
        sumY_roll += roll;
        sumXX += x * x;
        sumXY_yaw += x * yaw;
        sumXY_pitch += x * pitch;
        sumXY_roll += x * roll;
    }
    
    // Calculate slopes and intercepts
    double denominator = n * sumXX - sumX * sumX;
    if (std::abs(denominator) < 1e-9) {
        // Avoid division by zero, return last known value
        return historyWindow.back();
    }
    
    double slope_yaw = (n * sumXY_yaw - sumX * sumY_yaw) / denominator;
    double intercept_yaw = (sumY_yaw - slope_yaw * sumX) / n;
    
    double slope_pitch = (n * sumXY_pitch - sumX * sumY_pitch) / denominator;
    double intercept_pitch = (sumY_pitch - slope_pitch * sumX) / n;
    
    double slope_roll = (n * sumXY_roll - sumX * sumY_roll) / denominator;
    double intercept_roll = (sumY_roll - slope_roll * sumX) / n;
    
    // Predict future value
    double futureX = n + futureStep - 1;
    
    PoV predictedPov;
    predictedPov.yaw = slope_yaw * futureX + intercept_yaw;
    predictedPov.pitch = slope_pitch * futureX + intercept_pitch;
    predictedPov.roll = slope_roll * futureX + intercept_roll;
    
    // Handle angle wrapping for yaw (0-360 degrees)
    while (predictedPov.yaw >= 360.0) predictedPov.yaw -= 360.0;
    while (predictedPov.yaw < 0.0) predictedPov.yaw += 360.0;
    
    // Clamp pitch to valid range (-90 to 90 degrees)
    predictedPov.pitch = std::max(-90.0, std::min(90.0, predictedPov.pitch));
    
    // Handle roll wrapping (-180 to 180 degrees)
    while (predictedPov.roll > 180.0) predictedPov.roll -= 360.0;
    while (predictedPov.roll <= -180.0) predictedPov.roll += 360.0;
    
    return predictedPov;
}

void
TiledDashClientFovTracker::WritePredictionFile(const std::string& filename, 
                                               const std::vector<PoV>& predictedPovs) {
    NS_LOG_FUNCTION(this << filename);
    
    std::ofstream outFile(filename.c_str());
    if (!outFile.is_open()) {
        NS_LOG_ERROR("Failed to open output file: " << filename);
        return;
    }
    
    // Set precision for output
    outFile << std::fixed << std::setprecision(6);
    
    for (const auto& pov : predictedPovs) {
        if (m_povDataType == PovDataType::QUATERNION) {
            // Convert Euler back to quaternion if needed
            PoV quaternion = EulerToQuaternion(pov.yaw, pov.pitch, pov.roll, pov.timestamp);
            outFile << quaternion.yaw << " " << quaternion.pitch << " " 
                    << quaternion.roll << " " << pov.timestamp.GetSeconds() << std::endl;
        } else {
            outFile << pov.yaw << " " << pov.pitch << " " << pov.roll 
                    << " " << pov.timestamp.GetSeconds() << std::endl;
        }
    }
    
    outFile.close();
}

// Utility function to preprocess PoV files before simulation starts
void
TiledDashClientFovTracker::PreprocessPovFiles(const std::string& inputPovFile,
                                               const std::string& outputPredictionFile,
                                               FovTrackingAlgorithm algorithm,
                                               int historyWindow,
                                               int futureWindow,
                                               double updateRate,
                                               PovDataType dataType) {
    
    // Create a temporary tracker instance for preprocessing
    Ptr<TiledDashClientFovTracker> tempTracker = CreateObject<TiledDashClientFovTracker>();
    tempTracker->SetOrientationHistory(historyWindow);
    tempTracker->SetOrientationFuture(futureWindow);
    tempTracker->SetFovUpdateRate(updateRate);
    tempTracker->SetPovDataType(dataType);
    
    // Generate the prediction file
    tempTracker->GeneratePredictionFile(inputPovFile, outputPredictionFile, algorithm);
    
    std::cout << "Preprocessing complete. Generated: " << outputPredictionFile << std::endl;
}

} // namespace ns3
