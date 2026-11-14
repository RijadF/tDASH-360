// tiled-dash-client-network-tracker.h
#ifndef TILED_DASH_CLIENT_NETWORK_TRACKER_H
#define TILED_DASH_CLIENT_NETWORK_TRACKER_H

#include "ns3/object.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "ns3/ptr.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "tiled-dash-client-enums.h" // Assuming global enums are here

#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <deque> // Still useful for m_throughputHistory per socket

namespace ns3 {

// Forward declaration
class TiledDashClient;

class TiledDashClientNetworkTracker : public Object {
public:
    static TypeId GetTypeId(void);
    TiledDashClientNetworkTracker();
    virtual ~TiledDashClientNetworkTracker();

    // Configuration methods
    void SetSamplingInterval(Time interval);
    void SetSamplingWindow(uint32_t samples); // Renamed for clarity
    void SetExponentialWeight(double weight);
    void SetThroughputTraceFile(const std::string& traceFile); // Renamed for clarity
    
    // Using global enums now
    void SetSamplingMethod(ThroughputSamplingMethod method);
    void SetEstimationMethod(ThroughputEstimationMethod method);
    
    void SetClient(Ptr<TiledDashClient> client);

    // Called by NetworkManager (with socket=nullptr for time-based) or TileRequester (with actual socket for tile-based)
    void SubmitThroughput(Ptr<Socket> socket, double throughput); 

    // Estimates throughput for a specific socket (mainly for tile-based or debugging)
    double EstimateSocketThroughput(Ptr<Socket> socket);
    // Estimates the overall network throughput for ABR decisions
    double EstimateOverallThroughput();
    //For testing--if you want to load your own type of file
    void LoadAndPrintThroughputTrace(const std::string& filename);
    //Throughput Logging
    void SetClientId(uint32_t clientId);
    uint32_t GetClientId() const;
    void InitializeThroughputLogging();
    void LogThroughput(double throughput, Time timestamp);
    void CloseThroughputLogging();
    //Periodic logging
    void StartPeriodicLogging();
    void PeriodicLog();
    //nobo
    void InitializeNetRateLogging();
    void LogNetRate(double netRate, double throughput, Time timestamp);
    void StartPeriodicNetRateLogging();
    void PeriodicNetRateLog();
    void CloseNetRateLogging();
    //void RemoveSocketHistory (Ptr<Socket> socket); 

protected:
    virtual void DoInitialize() override;

private:
    // Throughput estimation methods
    double CalculateAverageThroughput(const std::deque<double>& history) const;
    double CalculateExponentialAverageThroughput(const std::deque<double>& history, double weight) const;
    //dodano harmonic mean 12.01
    double CalculateHarmonicMeanThroughput(const std::deque<double>& history) const;
    void UpdateIdealThroughput(); // Reads single value from trace

    // Member variables
    Time m_samplingInterval;                // Time interval for time-based sampling AND ideal throughput reading
    uint32_t m_samplingWindow;     // Number of throughput samples to keep per socket history (and for overall history)
    double m_exponentialWeight;             // Weight for exponential moving average
    std::string m_throughputTraceFile;      // Path to the throughput trace file
    
    // History: Ptr<Socket> can be nullptr for overall time-based samples
    std::map<Ptr<Socket>, std::deque<double>> m_throughputHistory; 
    
    ThroughputSamplingMethod m_samplingMethod;   
    ThroughputEstimationMethod m_estimationMethod; 
    
    Ptr<TiledDashClient> m_client; // Mediator
    
    std::ifstream m_traceFileStream; // For reading the ideal throughput trace
    double m_currentIdealThroughput; // Stores the single last read ideal throughput value
    EventId m_idealThroughputEvent;
    //dodano 16.08
    uint32_t m_clientId;  // Add client ID member
    std::ofstream m_throughputLogFile;  // File stream for throughput logging
    std::string m_throughputLogFileName;  // File name for throughput logging
    //dodano 22.09
    EventId m_loggingEvent;  // To track the scheduled event
    bool m_isLoggingActive;  // To control logging state
    double m_currentThroughput;  // Store current throughput value
    //01.11
    std::ofstream m_netRateLogFile;
    std::string m_netRateLogFileName;
    ns3::EventId m_netRateLoggingEvent;
    ns3::Time m_netRateLoggingInterval;
    std::map<Ptr<Socket>, Time> m_lastSampleTime;
};

} // namespace ns3

#endif // TILED_DASH_CLIENT_NETWORK_TRACKER_H