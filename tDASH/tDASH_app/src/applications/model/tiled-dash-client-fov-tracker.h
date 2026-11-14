#ifndef TILED_DASH_CLIENT_FOV_TRACKER_H
#define TILED_DASH_CLIENT_FOV_TRACKER_H

#include "ns3/object.h"
#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "ns3/ptr.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include <fstream>
#include <vector>
#include <string>
#include <queue>
# include "tiled-dash-client-enums.h"

namespace ns3 {

class TiledDashClient;

// Structure to represent a Point of View (POV)
struct PoV {
    double yaw;   // Yaw angle in degrees
    double pitch; // Pitch angle in degrees
    double roll;  // Roll angle in degrees
    Time timestamp; // Timestamp of the POV data
};


class TiledDashClientFovTracker : public Object {
public:
    static TypeId GetTypeId(void);
    TiledDashClientFovTracker();
    virtual ~TiledDashClientFovTracker();

    // Configuration methods
    void SetPovFile(const std::string& filename);
    void SetPredictionFile(const std::string& filename);
    void SetFovUpdateRate(double rate);
    void SetOrientationHistory(int history);
    void SetOrientationFuture(int future);
    void SetHorTiles(int tiles);
    void SetVerTiles(int tiles);
    void SetHorFovAngle(double angle); 
    void SetVerFovAngle(double angle);
	void SetPovDataType(PovDataType type); // Set POV data type
    void SetClient(Ptr<TiledDashClient> client) { m_client = client; }
    void SetProjectionType(ProjectionType projectionType);
    void SetPredictionAlgorithm(FovTrackingAlgorithm algorithm);
    std::vector<int> GetCurrentFovScores() {return m_currentFovTiles;  }
    // Tracking methods
    void StartTracking();
    void HaltFovTracking();
    void ResumeTrackFov(Time lastStallDur);
    void StopTracking();
    std::vector<int> GetPredictedFovTiles() const {
        return m_predictedFovTiles;
    }
    
    /**
     * Generate prediction file from original PoV file
     * \param inputPovFile Path to input PoV file
     * \param outputPredictionFile Path to output prediction file
     * \param algorithm Prediction algorithm to use
     */
    void GeneratePredictionFile(const std::string& inputPovFile, 
                               const std::string& outputPredictionFile,
                               FovTrackingAlgorithm algorithm);

    /**
     * Static utility function to preprocess PoV files before simulation
     * \param inputPovFile Path to input PoV file
     * \param outputPredictionFile Path to output prediction file
     * \param algorithm Prediction algorithm to use
     * \param historyWindow Size of history window
     * \param futureWindow Size of future prediction window
     * \param updateRate FoV update rate in Hz
     * \param dataType Type of PoV data (quaternion or Euler)
     */
    static void PreprocessPovFiles(const std::string& inputPovFile,
                                  const std::string& outputPredictionFile,
                                  FovTrackingAlgorithm algorithm,
                                  int historyWindow = 10,
                                  int futureWindow = 5,
                                  double updateRate = 60.0,
                                  PovDataType dataType = PovDataType::EULER_ANGLES);
protected:
    void DoInitialize() override; 
private:
    // Core tracking methods
    void TrackFoV();
    void UpdateFovTiles(const PoV& currentPov);
    void PredictFovTiles(const PoV& currentPov);

    // POV to Tile conversion
    std::vector<int> PovToTiles(const PoV& pov);
    std::vector<int> PovsToTiles(const std::vector<PoV>& window);
    bool CompareFovTiles(const std::vector<int>& fovScore1, const std::vector<int>& fovScore2);

     // Prediction Algorithms
    std::vector<PoV> PredictIdealFov();

    // Conversion functions
    PoV QuaternionToEuler(double x, double y, double z, double w, Time timestamp);
    PoV EulerToQuaternion(double yaw, double pitch, double roll, Time timestamp);

    // Member variables
    std::string m_povFile;            // Path to the POV data file
    std::string m_predictionFile;     // Path to the pre-processed prediction file.
    std::ifstream m_povFileHandle;      // File handle for reading POV data
    std::ifstream m_predictionFileHandle; // File handle for reading preprocessed data
    PoV m_orientationData;          // Current orientation data (yaw, pitch, roll) - Not really used, data is read from file.
    int m_orientationHistory;       // Number of past orientation samples to use for prediction
    int m_orientationFuture;        // Number of future orientation samples to predict
    int m_horTiles;          // Number of horizontal tiles in the video
    int m_verTiles;          // Number of vertical tiles in the video
    double m_horFovAngle;         // Horizontal field of view angle in degrees
    double m_verFovAngle;         // Vertical field of view angle in degrees
    double m_horFovTiles;         // Calculated horizontal FoV size in tiles
    double m_verFovTiles;         // Calculated vertical FoV size in tiles
    std::vector<int> m_currentFovTiles;   // Current FoV tile scores
    std::vector<int> m_pastFovTiles;      // Previous FoV tile scores for comparison
    std::vector<int> m_predictedFovTiles; // Predicted FoV tile scores
    EventId m_fovTrackingEvent;     // Event ID for the scheduled FoV tracking
    double m_fovUpdateRate;       // Rate at which to update FoV (in Hz)
    Time m_fovUpdateTime;         // Time of the next FoV update
    std::queue<PoV> m_povHistory;     // Queue to store past PoV data for prediction
	PovDataType m_povDataType;	   // Type of POV data (Quaternion or Euler)
    Ptr<TiledDashClient> m_client; // Pointer to the TiledDashClient instance
    ProjectionType m_projectionType;    // Video projection type
    FovTrackingAlgorithm m_fovPredictionAlgorithm; // FoV prediction algorithm
    bool m_isTracking; 
    /**
     * Generate ideal FoV prediction (perfect future knowledge)
     * \param originalPovs Vector of original PoV data
     * \return Vector of predicted PoV data
     */
    std::vector<PoV> GenerateIdealFovPrediction(const std::vector<PoV>& originalPovs);

    /**
     * Generate linear regression based FoV prediction
     * \param originalPovs Vector of original PoV data
     * \return Vector of predicted PoV data
     */
    std::vector<PoV> GenerateLinearRegressionPrediction(const std::vector<PoV>& originalPovs);

    /**
     * Predict future PoV using linear regression
     * \param historyWindow History window of PoV data
     * \param futureStep Number of steps into the future to predict
     * \return Predicted PoV
     */
    PoV PredictLinearRegression(const std::vector<PoV>& historyWindow, int futureStep);

    /**
     * Write prediction data to file
     * \param filename Output filename
     * \param predictedPovs Vector of predicted PoV data
     */
    void WritePredictionFile(const std::string& filename, const std::vector<PoV>& predictedPovs);

};

} // namespace ns3

#endif // TILED_DASH_CLIENT_FOV_TRACKER_H