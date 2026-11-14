#include "tiled-dash-client-network-tracker.h"
#include "tiled-dash-client.h" // Required for Ptr<TiledDashClient>
#include "tiled-dash-client-network-manager.h" // For m_client->GetNetworkManager()


#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <numeric> // for std::accumulate
#include <sstream> // for std::istringstream

#include <iomanip>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("TiledDashClientNetworkTracker");

NS_OBJECT_ENSURE_REGISTERED(TiledDashClientNetworkTracker);


TypeId
TiledDashClientNetworkTracker::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::TiledDashClientNetworkTracker")
            .SetParent<Object>()
            .SetGroupName("Applications")
            .AddConstructor<TiledDashClientNetworkTracker>()
            .AddAttribute("SamplingInterval",
                          "Time interval for time-based throughput sampling and ideal throughput updates.",
                          TimeValue(Seconds(1.0)),
                          MakeTimeAccessor(&TiledDashClientNetworkTracker::m_samplingInterval),
                          MakeTimeChecker())
            .AddAttribute("SamplingWindow",
                          "Number of throughput samples to keep per history (socket or overall).",
                          UintegerValue(10),
                          MakeUintegerAccessor(&TiledDashClientNetworkTracker::m_samplingWindow),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("ExponentialWeight",
                          "Weight (alpha) for exponential moving average calculation.",
                          DoubleValue(0.4),
                          MakeDoubleAccessor(&TiledDashClientNetworkTracker::m_exponentialWeight),
                          MakeDoubleChecker<double>(0.0, 1.0)) // Weight should be between 0 and 1
            .AddAttribute("ThroughputTraceFile",
                          "Path to the file containing ideal throughput trace data.",
                          StringValue(""),
                          MakeStringAccessor(&TiledDashClientNetworkTracker::m_throughputTraceFile),
                          MakeStringChecker())
            .AddAttribute("SamplingMethod",
                          "The throughput sampling method.",
                          EnumValue(ThroughputSamplingMethod::TILE_BASED),
                          MakeEnumAccessor<ThroughputSamplingMethod>(&TiledDashClientNetworkTracker::m_samplingMethod),
                          MakeEnumChecker(ThroughputSamplingMethod::TILE_BASED, "TILE_BASED",
                                          ThroughputSamplingMethod::TIME_BASED, "TIME_BASED"))
            .AddAttribute("EstimationMethod",
                          "The throughput estimation method.",
                          EnumValue(ThroughputEstimationMethod::AVERAGE),
                          MakeEnumAccessor<ThroughputEstimationMethod>(&TiledDashClientNetworkTracker::m_estimationMethod),
                          MakeEnumChecker(ThroughputEstimationMethod::AVERAGE, "AVERAGE",
                                          ThroughputEstimationMethod::EXPONENTIAL_AVERAGE, "EXPONENTIAL_AVERAGE",
                                          ThroughputEstimationMethod::HARMONIC_MEAN, "HARMONIC_MEAN", //dodano 12.01
                                          ThroughputEstimationMethod::IDEAL_TP, "IDEAL"))
            ;
    return tid;
}

TiledDashClientNetworkTracker::TiledDashClientNetworkTracker()
        : m_samplingInterval(Seconds(1.0)),
          m_samplingWindow(10),
          m_exponentialWeight(0.4),
          //Path to your throughput Trace file
          m_throughputTraceFile("Desktop/ns-allinone-3.44/ns-3.44/throughput-trace.txt"),
          m_samplingMethod(ThroughputSamplingMethod::TILE_BASED), // Default
          m_estimationMethod(ThroughputEstimationMethod::AVERAGE), // Default
          m_client(nullptr),
          m_currentIdealThroughput(0.0), //mijenjao sam parametar bio je 0
          //logging
          m_netRateLoggingInterval(MilliSeconds(100))//bilo 10
{
    NS_LOG_FUNCTION(this);
}

TiledDashClientNetworkTracker::~TiledDashClientNetworkTracker() {
    NS_LOG_FUNCTION(this);
    CloseThroughputLogging();
    //new
    //CloseNetRateLogging();
    //
    if (m_traceFileStream.is_open()) {
        m_traceFileStream.close();
    }
    if (m_idealThroughputEvent.IsPending()) {
      Simulator::Cancel(m_idealThroughputEvent);
    }
}
void
TiledDashClientNetworkTracker::LoadAndPrintThroughputTrace(const std::string& filename)
{
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        std::cout << "Failed to open throughput trace file: " << filename << std::endl;
        return;
    }

    std::vector<double> throughputs;
    double value;
    while (infile >> value) {
        throughputs.push_back(value);
    }
    infile.close();

    if (throughputs.empty()) {
        std::cout << "No throughput values found in file: " << filename << std::endl;
        return;
    }

    std::cout << "Throughput values from " << filename << ":" << std::endl;
    for (size_t i = 0; i < throughputs.size(); ++i) {
        std::cout << "  [" << i << "]: " << throughputs[i] << std::endl;
    }

    double sum = std::accumulate(throughputs.begin(), throughputs.end(), 0.0);
    double avg = sum / throughputs.size();
    std::cout << "Average throughput: " << avg << std::endl;
}

void
TiledDashClientNetworkTracker::DoInitialize() {
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("TiledDashClientNetworkTracker::DoInitialize called");
    Object::DoInitialize(); // Call parent class DoInitialize

    if (m_samplingMethod == ThroughputSamplingMethod::TIME_BASED) {
        NS_ASSERT_MSG(m_client && m_client->GetNetworkManager(),
                      "Client and its NetworkManager must be set when using time-based throughput sampling.");
        // NetworkManager will be configured by TiledDashClient to call ActivateTimedThroughput.
        // NetworkManager's CollectThroughputSamples will then call this->SubmitThroughput(nullptr, rate).
        NS_LOG_INFO("Time-based sampling enabled. NetworkManager should be active.");
    }

    if (m_estimationMethod == ThroughputEstimationMethod::IDEAL_TP) {
        NS_ASSERT_MSG(!m_throughputTraceFile.empty(), "Throughput trace file must be set for IDEAL estimation.");
        m_traceFileStream.open(m_throughputTraceFile.c_str());
        if (!m_traceFileStream.is_open()) {
            NS_LOG_ERROR("Failed to open throughput trace file: " << m_throughputTraceFile << ". IDEAL estimation will not work.");
        } else {
            NS_LOG_INFO("Opened throughput trace file: " << m_throughputTraceFile);
            UpdateIdealThroughput(); // Start reading the trace
        }
    }
}

void
TiledDashClientNetworkTracker::SetSamplingInterval(Time interval) {
    m_samplingInterval = interval;
    NS_LOG_DEBUG("Sampling interval set to " << interval.GetSeconds() << "s");
}

void
TiledDashClientNetworkTracker::SetSamplingWindow(uint32_t samples) {
    m_samplingWindow = std::max(1u, samples); // Ensure at least 1 sample is kept
    NS_LOG_DEBUG("Throughput samples to keep set to " << m_samplingWindow);
}

void
TiledDashClientNetworkTracker::SetExponentialWeight(double weight) {
    if (weight >= 0.0 && weight <= 1.0) {
        m_exponentialWeight = weight;
        NS_LOG_DEBUG("Exponential weight set to " << m_exponentialWeight);
    } else {
        NS_LOG_WARN("Invalid exponential weight: " << weight << ". Must be between 0.0 and 1.0. Not changed.");
    }
}

void
TiledDashClientNetworkTracker::SetThroughputTraceFile(const std::string& traceFile) {
    m_throughputTraceFile = traceFile;
    NS_LOG_DEBUG("Throughput trace file set to " << m_throughputTraceFile);
}

void
TiledDashClientNetworkTracker::SetSamplingMethod(ThroughputSamplingMethod method) {
    m_samplingMethod = method;
    NS_LOG_DEBUG("Sampling method set to " << static_cast<int>(method));
}

void
TiledDashClientNetworkTracker::SetEstimationMethod(ThroughputEstimationMethod method) {
    m_estimationMethod = method;
    NS_LOG_DEBUG("Estimation method set to " << static_cast<int>(method));
}
void
TiledDashClientNetworkTracker::SetClient(Ptr<TiledDashClient> client) {
    m_client = client;
}

void
TiledDashClientNetworkTracker::SetClientId(uint32_t clientId) {
    m_clientId = clientId;
    InitializeThroughputLogging();
    StartPeriodicLogging();
    //01.11
    InitializeNetRateLogging();
    StartPeriodicNetRateLogging();
}

uint32_t
TiledDashClientNetworkTracker::GetClientId() const {
    return m_clientId;
}
// =========================================================================
// NEW 10ms LOGGING IMPLEMENTATION
// =========================================================================

void
TiledDashClientNetworkTracker::InitializeNetRateLogging() {
    // Create CSV log file name connected to client ID
    std::stringstream ss;
    //Where the logging will be done
    ss << "Desktop/ns-allinone-3.44/ns-3.44/netrate_throughput_client_"
       << m_clientId << ".csv";
    m_netRateLogFileName = ss.str();

    // Open the file
    m_netRateLogFile.open(m_netRateLogFileName.c_str());
    if (m_netRateLogFile.is_open()) {
        // Write CSV header
        m_netRateLogFile << "Timestamp(s),NetRate_bps,ThroughputBps" << std::endl;
        m_netRateLogFile.flush();
        NS_LOG_INFO("10ms NetRate/Throughput log file opened: " << m_netRateLogFileName);
    } else {
        NS_LOG_ERROR("Failed to open 10ms NetRate/Throughput log file: " << m_netRateLogFileName);
    }
}

void
TiledDashClientNetworkTracker::LogNetRate(double netRate, double throughput, Time timestamp) {
    if (m_netRateLogFile.is_open()) {
        m_netRateLogFile << std::fixed << std::setprecision(6) 
                           << timestamp.GetSeconds() << "," 
                           << std::fixed << std::setprecision(2) << netRate << "," 
                           << std::fixed << std::setprecision(2) << throughput << std::endl;
    }
}

void
TiledDashClientNetworkTracker::StartPeriodicNetRateLogging() {
    if (m_netRateLogFile.is_open()) {
        // Schedule the first logging event after 10ms
        m_netRateLoggingEvent = Simulator::Schedule(m_netRateLoggingInterval, 
                                           &TiledDashClientNetworkTracker::PeriodicNetRateLog, this);
    }
}

void
TiledDashClientNetworkTracker::PeriodicNetRateLog() {
    if (m_netRateLogFile.is_open()) {
        // NetRate is the EstimateOverallThroughput() as requested
        double netRate = EstimateOverallThroughput();
        // Use the same value for the second ThroughputBps column, as it's the best available estimate.
        double throughputBps = netRate; 

        LogNetRate(netRate, throughputBps, Simulator::Now());
        
        // Schedule the next logging event
        m_netRateLoggingEvent = Simulator::Schedule(m_netRateLoggingInterval, 
                                           &TiledDashClientNetworkTracker::PeriodicNetRateLog, this);
    } else {
        // If the file closed unexpectedly, cancel the event
        if (m_netRateLoggingEvent.IsRunning()) {
            Simulator::Cancel(m_netRateLoggingEvent);
        }
    }
}

void
TiledDashClientNetworkTracker::CloseNetRateLogging() {
    if (m_netRateLoggingEvent.IsRunning()) {
        Simulator::Cancel(m_netRateLoggingEvent);
    }
    if (m_netRateLogFile.is_open()) {
        m_netRateLogFile.close();
        NS_LOG_INFO("10ms NetRate/Throughput log file closed for client " << m_clientId);
    }
}

void
TiledDashClientNetworkTracker::InitializeThroughputLogging() {
    // Create throughput log file name
    std::stringstream ss;
    //Location of logging files
    ss << "Desktop/ns-allinone-3.44/ns-3.44/throughput_traces_client_"<< m_clientId << ".txt";
    m_throughputLogFileName = ss.str();
    //nove dvije linije dodane
    m_isLoggingActive = false;
    m_currentThroughput = 0.0;

    // Open the file
    m_throughputLogFile.open(m_throughputLogFileName.c_str());
    if (m_throughputLogFile.is_open()) {
        // Write header
        m_throughputLogFile << "# Throughput traces for Client ID: " << m_clientId << std::endl;
        m_throughputLogFile << "# Format: Timestamp(s) Throughput(bps)" << std::endl;
        m_throughputLogFile.flush();
        NS_LOG_INFO("Throughput log file opened: " << m_throughputLogFileName);
        m_isLoggingActive = true;
    } else {
        NS_LOG_ERROR("Failed to open throughput log file: " << m_throughputLogFileName);
    }
}

void
TiledDashClientNetworkTracker::LogThroughput(double throughput, Time timestamp) {
    if (m_throughputLogFile.is_open()) {
        m_throughputLogFile << std::fixed << std::setprecision(6) 
                           << timestamp.GetSeconds() << " " 
                           << throughput << std::endl;
        m_throughputLogFile.flush();  // Ensure immediate write
    }
}

void
TiledDashClientNetworkTracker::CloseThroughputLogging() {
    if (m_loggingEvent.IsRunning()) {
        Simulator::Cancel(m_loggingEvent);
    }
    m_isLoggingActive = false;
    if (m_throughputLogFile.is_open()) {
        m_throughputLogFile.close();
        NS_LOG_INFO("Throughput log file closed for client " << m_clientId);
    }
}

// New method to start periodic logging
void
TiledDashClientNetworkTracker::StartPeriodicLogging() {
    if (m_isLoggingActive) {
        // Schedule the first logging event after 1000ms
        m_loggingEvent = Simulator::Schedule(MilliSeconds(10000), //1s radi
                                           &TiledDashClientNetworkTracker::PeriodicLog, this);
    }
}

// New method for periodic logging callback
void
TiledDashClientNetworkTracker::PeriodicLog() {
    if (m_isLoggingActive && m_throughputLogFile.is_open()) {
        // Log the current throughput value
        LogThroughput(m_currentThroughput, Simulator::Now());
        
        // Schedule the next logging event
        m_loggingEvent = Simulator::Schedule(MilliSeconds(10000), 
                                           &TiledDashClientNetworkTracker::PeriodicLog, this);
    }
}

void
TiledDashClientNetworkTracker::SubmitThroughput(Ptr<Socket> socket, double throughput) {
    NS_LOG_FUNCTION(this << (socket ? socket->GetNode()->GetId() : -1) << throughput); // Log node ID or -1 for nullptr
    
    if (throughput < 0) {
        NS_LOG_WARN("Attempted to submit negative throughput: " << throughput << ". Ignoring.");
        return;
    }

    std::deque<double>& history = m_throughputHistory[socket]; // Creates entry if not exists
    history.push_back(throughput);
    //Logging component
    //LogThroughput(throughput, Simulator::Now());
    //
    //m_lastSampleTime[socket] = Simulator::Now();
    while (history.size() > m_samplingWindow) {
        history.pop_front();
    }
    if (socket == nullptr) {
        NS_LOG_INFO("Submitted overall throughput sample: " << throughput << " bps. History size: " << history.size());
        NS_LOG_INFO("Time interval"<<Simulator::Now().GetSeconds());
    } else {
        NS_LOG_INFO("Submitted throughput for socket " << socket << ": " << throughput << " bps. History size: " << history.size());
    }
}

double
TiledDashClientNetworkTracker::EstimateSocketThroughput(Ptr<Socket> socket) {
    NS_LOG_FUNCTION(this << socket);
    if (!socket) {
        NS_LOG_WARN("EstimateSocketThroughput called with null socket. This method is for specific sockets.");
        return 0.0; // Or handle as an error
    }

    if (m_estimationMethod == ThroughputEstimationMethod::IDEAL_TP) {
        return m_currentIdealThroughput; // Ideal is system-wide, not per-socket specific from trace
    }

    const auto it = m_throughputHistory.find(socket);
    if (it == m_throughputHistory.end() || it->second.empty()) {
        NS_LOG_INFO("No throughput history for socket " << socket);
        return 0.0; // No data for this specific socket
    }

    const std::deque<double>& history = it->second;
    switch (m_estimationMethod) {
        case ThroughputEstimationMethod::AVERAGE:
            return CalculateAverageThroughput(history);
        case ThroughputEstimationMethod::EXPONENTIAL_AVERAGE:
            return CalculateExponentialAverageThroughput(history, m_exponentialWeight);
        case ThroughputEstimationMethod::HARMONIC_MEAN:
            return CalculateHarmonicMeanThroughput(history);
        default:
            NS_LOG_ERROR("Unknown or unsupported estimation method for socket throughput: " << static_cast<int>(m_estimationMethod));
            return 0.0;
    }
}

double
TiledDashClientNetworkTracker::EstimateOverallThroughput() {
    NS_LOG_FUNCTION(this);
    //NS_LOG_INFO("In estimated overall");
    if (m_estimationMethod == ThroughputEstimationMethod::IDEAL_TP) {
        NS_LOG_INFO("Estimating overall throughput (IDEAL): " << m_currentIdealThroughput << " bps");
        return m_currentIdealThroughput;
    }

    // For TIME_BASED sampling, prioritize the overall samples stored with a nullptr key
    if (m_samplingMethod == ThroughputSamplingMethod::TIME_BASED) {
        const auto it = m_throughputHistory.find(nullptr);
        if (it != m_throughputHistory.end() && !it->second.empty()) {
            const std::deque<double>& overallHistory = it->second;
            double estimate = 0.0;
            switch (m_estimationMethod) {
                case ThroughputEstimationMethod::AVERAGE:
                    estimate = CalculateAverageThroughput(overallHistory);
                    break;
                case ThroughputEstimationMethod::EXPONENTIAL_AVERAGE:
                    estimate = CalculateExponentialAverageThroughput(overallHistory, m_exponentialWeight);
                    break;
                case ThroughputEstimationMethod::HARMONIC_MEAN:
                    estimate=CalculateHarmonicMeanThroughput(overallHistory);
                    break;
                default: // Should not happen if IDEAL is handled above
                    NS_LOG_ERROR("Invalid estimation method for time-based overall: " << static_cast<int>(m_estimationMethod));
                    return 0.0;
            }
            NS_LOG_INFO("Estimating overall throughput (TIME_BASED, from overall samples): " << estimate << " bps");
            return estimate;
        } else {
            NS_LOG_WARN("Time-based sampling active, but no overall (nullptr socket) throughput history found. Returning 0.");
            return 0.0; // No overall samples to use
        }
    }
    // Prefer the whole-client samples (submitted under nullptr)
    const auto agg = m_throughputHistory.find(nullptr);
    if (agg != m_throughputHistory.end() && !agg->second.empty()) {
        switch (m_estimationMethod) {
            case ThroughputEstimationMethod::AVERAGE:
                return CalculateAverageThroughput(agg->second);
            case ThroughputEstimationMethod::EXPONENTIAL_AVERAGE:
                return CalculateExponentialAverageThroughput(agg->second, m_exponentialWeight);
            case ThroughputEstimationMethod::HARMONIC_MEAN:
                return CalculateHarmonicMeanThroughput(agg->second);
            default:
                break;
        }
    }
    // For TILE_BASED sampling (or if TIME_BASED had no overall samples, though less likely)
    // Aggregate from individual, non-nullptr socket histories.
    // Strategy: Average of individual socket estimates.
    if (m_throughputHistory.empty()) {
        NS_LOG_INFO("No throughput history available at all.");
        NS_LOG_DEBUG("No throughput history available at all.");
        return 0.0;
    }

    double totalAggregatedThroughput = 0.0;
    int activeSocketHistories = 0;
    for (const auto& pair : m_throughputHistory) {
        if (pair.first == nullptr || pair.second.empty()) { // Skip nullptr key (overall) or empty histories
            continue;
        }
        //Testing the socket histroy-work in progress
        //auto tsIt = m_lastSampleTime.find(pair.first);
        //if (tsIt != m_lastSampleTime.end()) {
        //    Time age = Simulator::Now() - tsIt->second;
        //    Time staleThreshold = (m_samplingInterval > Time(0)) ? m_samplingInterval * 2 : Seconds(2.0);
        //    if (age > staleThreshold) {
        //        continue;
        //    }
        //}
        const std::deque<double>& socketHist = pair.second;
        double socketEstimate = 0.0;
        switch (m_estimationMethod) { // Must be AVERAGE or EXP_AVG here
            case ThroughputEstimationMethod::AVERAGE:
                socketEstimate = CalculateAverageThroughput(socketHist);
                break;
            case ThroughputEstimationMethod::EXPONENTIAL_AVERAGE:
                socketEstimate = CalculateExponentialAverageThroughput(socketHist, m_exponentialWeight);
                break;
            case ThroughputEstimationMethod::HARMONIC_MEAN:
                socketEstimate=CalculateHarmonicMeanThroughput(socketHist);
                break;
            default: // Should not happen
                continue; 
        }
        totalAggregatedThroughput += socketEstimate;
        activeSocketHistories++;
    }

    if (activeSocketHistories == 0) {
        NS_LOG_INFO("Tile-based sampling: No active socket histories to estimate overall throughput.");
        return 0.0;
    }
    //double finalEstimate = totalAggregatedThroughput / (activeSocketHistories*16); //times num of users
    double finalEstimate=totalAggregatedThroughput/activeSocketHistories;
    //double finalEstimate=totalAggregatedThroughput;
    //double finalEstimate = totalAggregatedThroughput / 3;
    NS_LOG_INFO("Estimating overall throughput (TILE_BASED, average of " << activeSocketHistories << " sockets): " << finalEstimate << " bps");
    return finalEstimate;
}


double
TiledDashClientNetworkTracker::CalculateAverageThroughput(const std::deque<double>& history) const {
    if (history.empty()) {
        return 0.0;
    }
    double sum = std::accumulate(history.begin(), history.end(), 0.0);
    return sum / history.size();
}

double
TiledDashClientNetworkTracker::CalculateExponentialAverageThroughput(const std::deque<double>& history, double weight) const {
    if (history.empty()) {
        return 0.0;
    }
    double ema = history[0]; // S_0 = Y_0
    for (size_t i = 1; i < history.size(); ++i) {
        ema = weight * history[i] + (1.0 - weight) * ema; // Standard EWMA: S_t = alpha*Y_t + (1-alpha)*S_{t-1}
        NS_LOG_INFO("Current ema "<<i<<" is "<<ema);
    }
    return ema;
}
double
TiledDashClientNetworkTracker::CalculateHarmonicMeanThroughput(const std::deque<double>& history) const {
    if (history.empty()) {
        return 0.0;
    }
    
    double sumReciprocals = 0.0;
    uint32_t validSamples = 0;
    
    for (const auto& sample : history) {
        if (sample > 0.0) {  // Only include positive values to avoid division by zero
            sumReciprocals += 1.0 / sample;
            validSamples++;
        } else {
            NS_LOG_DEBUG("Skipping non-positive sample in harmonic mean calculation: " << sample);
        }
    }
    
    if (validSamples == 0) {
        NS_LOG_DEBUG("No valid positive samples for harmonic mean calculation");
        return 0.0;
    }
    
    double harmonicMean = validSamples / sumReciprocals;
    NS_LOG_DEBUG("Calculated harmonic mean: " << harmonicMean << " from " << validSamples << " samples");
    return harmonicMean;
}
void
TiledDashClientNetworkTracker::UpdateIdealThroughput() {
    NS_LOG_FUNCTION(this);
    if (!m_traceFileStream.is_open()) {
        NS_LOG_WARN("Ideal throughput trace file is not open. Cannot update.");
        // Optionally stop trying: m_idealThroughputEvent.Cancel();
        return;
    }

    std::string line;
    if (std::getline(m_traceFileStream, line)) {
        std::istringstream iss(line);
        double sampleValue;
        if (iss >> sampleValue) {
            if (sampleValue >= 0) {
                m_currentIdealThroughput = sampleValue;
                NS_LOG_INFO("Updated ideal throughput from trace to: " << m_currentIdealThroughput << " bps");
            } else {
                NS_LOG_WARN("Read negative ideal throughput from trace: " << sampleValue << ". Ignoring.");
            }
        } else {
            NS_LOG_WARN("Error parsing ideal throughput value from trace line: " << line);
            
        }
    } else {
        NS_LOG_INFO("End of ideal throughput trace file reached or error reading line.");
        if (m_traceFileStream.eof()) {
           NS_LOG_INFO("EOF reached for ideal throughput trace. Stopping updates.");
           return; // Do not reschedule
        }
    }

    // Reschedule the next update
    if (m_samplingInterval > Time(0)) {
         m_idealThroughputEvent = Simulator::Schedule(m_samplingInterval, &TiledDashClientNetworkTracker::UpdateIdealThroughput, this);
    } else {
        NS_LOG_WARN("Ideal throughput sampling interval is zero or negative. Stopping updates.");
    }
}

} // namespace ns3