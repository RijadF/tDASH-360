#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/tiled-dash-server.h"
#include "ns3/tiled-dash-client.h"
#include "ns3/tiled-dash-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/energy-module.h"
#include "ns3/device-energy-model-container.h"
#include <fstream>
#include "ns3/single-model-spectrum-channel.h"
#include <iomanip>
#include "ns3/drop-tail-queue.h"
#include <iostream>
#include <string>
#include <cmath>
#include <map>
#include "ns3/log.h"
#include <set>
#include "ns3/traffic-control-module.h"
#include "ns3/bridge-helper.h"
#include "ns3/csma-module.h"
#include "ns3/bridge-module.h"
#include "ns3/rr-multi-user-scheduler.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/arp-cache.h"
#include "ns3/node-list.h"
#include "ns3/ipv4-interface.h"
#include "ns3/object-vector.h"
#include "ns3/pointer.h"
//
#include "ns3/buildings-module.h"
#include "ns3/three-gpp-propagation-loss-model.h"
#include "ns3/three-gpp-spectrum-propagation-loss-model.h"
//
#include <cstdlib> 
//
 #include "ns3/tag.h"
//dodano
#include <regex>
#include <iostream>
#include <sstream>
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-phy-rx-trace-helper.h"
using namespace ns3;
//Energy model
//
#include "ns3/basic-energy-source-helper.h"
#include "ns3/basic-energy-source.h"
#include "ns3/wifi-radio-energy-model-helper.h"
#include "ns3/wifi-radio-energy-model.h"
using namespace ns3;
//energy
#include "ns3/basic-energy-source-helper.h"
#include "ns3/basic-energy-source.h"
#include "ns3/wifi-radio-energy-model-helper.h"
#include "ns3/wifi-radio-energy-model.h"

// Add proper namespace usage
using namespace ns3::energy;

// Global variables
std::ofstream energyDataFile;
std::map<uint32_t, Ptr<WifiRadioEnergyModel>> g_deviceEnergyModels;
std::map<uint32_t, Ptr<EnergySource>> g_energySources;

struct EnergyMetrics {
    double totalEnergyConsumed = 0.0;
    double currentPower = 0.0;
    double remainingEnergy = 0.0;
    WifiPhyState currentState = WifiPhyState::IDLE;
    Time lastUpdateTime = Seconds(0);
    double voltage = 0.0;
    double current = 0.0;
    
    // Per-state tracking
    std::map<WifiPhyState, double> stateEnergy;  // Energy consumed in each state
    std::map<WifiPhyState, Time> stateDuration;  // Time spent in each state
    std::map<WifiPhyState, double> statePower;   // Average power in each state
    WifiPhyState lastState = WifiPhyState::IDLE;
    Time lastStateChange = Seconds(0);
};

std::map<uint32_t, EnergyMetrics> g_energyMetrics;
uint32_t g_numAPs = 0;

// Callbacks
void TotalEnergyConsumptionCallback(uint32_t nodeId, double oldValue, double totalEnergy)
{
    if (g_energyMetrics.find(nodeId) != g_energyMetrics.end()) {
        g_energyMetrics[nodeId].totalEnergyConsumed = totalEnergy;
    }
}

void RemainingEnergyCallback(uint32_t nodeId, double oldValue, double remainingEnergy)
{
    if (g_energyMetrics.find(nodeId) != g_energyMetrics.end()) {
        g_energyMetrics[nodeId].remainingEnergy = remainingEnergy;
    }
}
std::string WifiStateToString(WifiPhyState state)
{
    switch (state) {
        case WifiPhyState::IDLE: return "IDLE";
        case WifiPhyState::CCA_BUSY: return "CCA_BUSY";
        case WifiPhyState::TX: return "TX";
        case WifiPhyState::RX: return "RX";
        case WifiPhyState::SLEEP: return "SLEEP";
        default: return "UNKNOWN";
    }
}
// Add this after your existing callback functions
void PrintPeriodicState()
{
    Time now = Simulator::Now();
    //std::cout << "=== WiFi STATE DEBUG at " << now.GetSeconds() << "s ===" << std::endl;
    
    for (auto& pair : g_energyMetrics) {
        uint32_t nodeId = pair.first;
        EnergyMetrics& metrics = pair.second;
        std::string nodeType = (nodeId < g_numAPs) ? "AP" : "STA";
        
        //std::cout << nodeType << " " << (nodeType == "AP" ? nodeId : nodeId - g_numAPs) 
        //          << " (Node " << nodeId << "): " 
        //          << WifiStateToString(metrics.currentState) 
        //          << " (Last change: " << metrics.lastStateChange.GetSeconds() << "s)"
        //          << std::endl;
    }
    
    // Schedule next debug output (every 5 seconds)
    if (now.GetSeconds() < 60.0) { // Only for first minute
        Simulator::Schedule(Seconds(5.0), &PrintPeriodicState);
    }
}
double CalculateCurrentPower(Ptr<WifiRadioEnergyModel> model, Ptr<EnergySource> source, WifiPhyState state)
{
    if (!model || !source) return 0.0;
    
    double voltage = source->GetSupplyVoltage();
    double current = 0.0;
    
    switch (state) {
        case WifiPhyState::IDLE:
            current = model->GetIdleCurrentA();
            break;
        case WifiPhyState::CCA_BUSY:
            current = model->GetCcaBusyCurrentA();
            break;
        case WifiPhyState::TX:
            current = model->GetTxCurrentA();
            break;
        case WifiPhyState::RX:
            current = model->GetRxCurrentA();
            break;
        case WifiPhyState::SWITCHING:
            current = model->GetSwitchingCurrentA();
            break;
        case WifiPhyState::SLEEP:
            current = model->GetSleepCurrentA();
            break;
        case WifiPhyState::OFF:
            current = 0.0;
            break;
        default:
            current = 0.0;
            break;
    }
    
    return current * voltage;
}
// Add this function to manually check PHY states
void CheckPhyStates()
{
    Time now = Simulator::Now();
    //std::cout << "\n=== MANUAL PHY STATE CHECK at " << now.GetSeconds() << "s ===" << std::endl;
    
    // Check all nodes in g_energyMetrics
    for (auto& pair : g_energyMetrics) {
        uint32_t nodeId = pair.first;
        EnergyMetrics& metrics = pair.second;
        
        // Try to get current state directly from PHY
        Ptr<Node> node = NodeList::GetNode(nodeId);
        if (node) {
            for (uint32_t devIndex = 0; devIndex < node->GetNDevices(); ++devIndex) {
                Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(devIndex));
                if (wifiDev) {
                    Ptr<WifiPhy> phy = wifiDev->GetPhy();
                    if (phy) {
                        WifiPhyState currentState = phy->GetState()->GetState();
                        std::string nodeType = (nodeId < g_numAPs) ? "AP" : "STA";
                        uint32_t nodeIndex = (nodeId < g_numAPs) ? nodeId : nodeId - g_numAPs;
                        
                        //std::cout << nodeType << " " << nodeIndex << " (Node " << nodeId << "): "
                        //          << "Tracked State=" << WifiStateToString(metrics.currentState)
                        //          << ", Actual PHY State=" << WifiStateToString(currentState)
                        //          << std::endl;
                        
                        // Update if different
                        if (metrics.currentState != currentState) {
                            //std::cout << "  *** STATE MISMATCH - Updating tracked state ***" << std::endl;
                            metrics.currentState = currentState;
                            metrics.lastState = currentState;
                            metrics.lastStateChange = now;
                        }
                    }
                }
            }
        }
    }
    
    // Schedule next check (every 2 seconds for first 30 seconds)
    if (now.GetSeconds() < 30.0) {
        Simulator::Schedule(Seconds(2.0), &CheckPhyStates);
    }
}
void WifiStateChangeCallback(uint32_t nodeId, Time start, Time duration, WifiPhyState state)
{
    Time now = Simulator::Now();
    
    // Always print for debugging
    std::string nodeType = (nodeId < g_numAPs) ? "AP" : "STA";
    uint32_t nodeIndex = (nodeId < g_numAPs) ? nodeId : nodeId - g_numAPs;
    
    //std::cout << "=== STATE TRACE FIRED: " << nodeType << " " << nodeIndex 
    //          << " (Node " << nodeId << ") " 
    //          << WifiStateToString(g_energyMetrics[nodeId].currentState) 
    //          << " -> " << WifiStateToString(state)
    //          << " at " << now.GetSeconds() << "s" 
    //          << " (duration: " << duration.GetSeconds() << "s)" << std::endl;
    
    if (g_energyMetrics.find(nodeId) != g_energyMetrics.end()) {
        EnergyMetrics& metrics = g_energyMetrics[nodeId];
        
        // Calculate energy for previous state
        if (metrics.lastStateChange > Seconds(0)) {
            Time stateDuration = now - metrics.lastStateChange;
            metrics.stateDuration[metrics.lastState] += stateDuration;
            
            // Calculate energy consumed in previous state
            double statePower = CalculateCurrentPower(
                g_deviceEnergyModels[nodeId], 
                g_energySources[nodeId], 
                metrics.lastState
            );
            double stateEnergy = statePower * stateDuration.GetSeconds();
            metrics.stateEnergy[metrics.lastState] += stateEnergy;
            
            //std::cout << "  Energy in " << WifiStateToString(metrics.lastState) 
            //          << ": " << stateEnergy << " J over " 
            //          << stateDuration.GetSeconds() << "s" << std::endl;
        }
        
        // Update to new state
        WifiPhyState oldState = metrics.currentState;
        metrics.currentState = state;
        metrics.lastState = state;
        metrics.lastStateChange = now;
        
        // Update current power immediately
        metrics.currentPower = CalculateCurrentPower(
            g_deviceEnergyModels[nodeId], 
            g_energySources[nodeId], 
            state
        );
        
        //std::cout << "  Power: " << metrics.currentPower << " W" << std::endl;
    } else {
        //std::cout << "  WARNING: Node " << nodeId << " not found in g_energyMetrics!" << std::endl;
    }
}





// Enhanced periodic energy logging with per-state power tracking
void LogEnergyData()
{
    Time now = Simulator::Now();
    
    if (energyDataFile.is_open()) {
        for (auto& pair : g_energyMetrics) {
            uint32_t nodeId = pair.first;
            EnergyMetrics& metrics = pair.second;
            
            if (g_deviceEnergyModels.find(nodeId) != g_deviceEnergyModels.end() &&
                g_energySources.find(nodeId) != g_energySources.end()) {
                
                Ptr<WifiRadioEnergyModel> model = g_deviceEnergyModels[nodeId];
                Ptr<EnergySource> source = g_energySources[nodeId];
                
                if (model && source) {
                    // Get voltage and current based on current state
                    metrics.voltage = source->GetSupplyVoltage();
                    
                    // Calculate current based on state
                    switch (metrics.currentState) {
                        case WifiPhyState::IDLE:
                            metrics.current = model->GetIdleCurrentA();
                            break;
                        case WifiPhyState::CCA_BUSY:
                            metrics.current = model->GetCcaBusyCurrentA();
                            break;
                        case WifiPhyState::TX:
                            metrics.current = model->GetTxCurrentA();
                            break;
                        case WifiPhyState::RX:
                            metrics.current = model->GetRxCurrentA();
                            break;
                        case WifiPhyState::SWITCHING:
                            metrics.current = model->GetSwitchingCurrentA();
                            break;
                        case WifiPhyState::SLEEP:
                            metrics.current = model->GetSleepCurrentA();
                            break;
                        default:
                            metrics.current = 0.0;
                            break;
                    }
                    
                    // Update current power
                    metrics.currentPower = metrics.current * metrics.voltage;
                    
                    // Write to CSV
                    energyDataFile << std::fixed << std::setprecision(6);
                    energyDataFile << now.GetSeconds() << ","
                                  << nodeId << ","
                                  << (nodeId < g_numAPs ? "AP" : "STA") << ","
                                  << metrics.totalEnergyConsumed << ","
                                  << metrics.remainingEnergy << ","
                                  << metrics.currentPower << ","
                                  << metrics.voltage << ","
                                  << metrics.current << ","
                                  << WifiStateToString(metrics.currentState)
                                  << std::endl;
                }
            }
        }
    }
    
    // Schedule next logging
    Simulator::Schedule(MilliSeconds(10), &LogEnergyData);
}
// New callback that properly detects state changes
void EnergyStateChangeCallback(uint32_t nodeId, double oldValue, double totalEnergy)
{
    Time now = Simulator::Now();
    
    if (g_energyMetrics.find(nodeId) == g_energyMetrics.end()) {
        return;
    }
    
    // Get current state from the energy model
    if (g_deviceEnergyModels.find(nodeId) == g_deviceEnergyModels.end()) {
        return;
    }
    
    Ptr<WifiRadioEnergyModel> model = g_deviceEnergyModels[nodeId];
    if (!model) return;
    
    WifiPhyState currentState = model->GetCurrentState();
    EnergyMetrics& metrics = g_energyMetrics[nodeId];
    
    // Only process if state actually changed
    if (currentState != metrics.currentState) {
        std::string nodeType = (nodeId < g_numAPs) ? "AP" : "STA";
        uint32_t nodeIndex = (nodeId < g_numAPs) ? nodeId : nodeId - g_numAPs;
        
        //std::cout << "=== STATE CHANGE DETECTED: " << nodeType << " " << nodeIndex 
        //          << " (Node " << nodeId << ") " 
        //          << WifiStateToString(metrics.currentState) 
        //          << " -> " << WifiStateToString(currentState)
        //          << " at " << now.GetSeconds() << "s" << std::endl;
        
        // Calculate energy for previous state
        if (metrics.lastStateChange > Seconds(0)) {
            Time stateDuration = now - metrics.lastStateChange;
            metrics.stateDuration[metrics.lastState] += stateDuration;
            
            // Calculate energy consumed in previous state
            double statePower = CalculateCurrentPower(
                model, 
                g_energySources[nodeId], 
                metrics.lastState
            );
            double stateEnergy = statePower * stateDuration.GetSeconds();
            metrics.stateEnergy[metrics.lastState] += stateEnergy;
            
            //std::cout << "  Energy in " << WifiStateToString(metrics.lastState) 
             //         << ": " << stateEnergy << " J over " 
             //         << stateDuration.GetSeconds() << "s" << std::endl;
        }
        
        // Update to new state
        metrics.lastState = metrics.currentState;
        metrics.currentState = currentState;
        metrics.lastStateChange = now;
        
        // Update current power
        metrics.currentPower = CalculateCurrentPower(
            model, 
            g_energySources[nodeId], 
            currentState
        );
        
        //std::cout << "  New Power: " << metrics.currentPower << " W" << std::endl;
    }
    
    // Always update total energy
    metrics.totalEnergyConsumed = totalEnergy;
}

void ConnectWifiStateTraces(NodeContainer& apNodes, NodeContainer& staNodes)
{
    //std::cout << "\n=== CONNECTING WIFI STATE TRACES ===" << std::endl;
    
    int connectedTraces = 0;
    
    // Connect for APs
    for (uint32_t i = 0; i < apNodes.GetN(); ++i) {
        Ptr<Node> node = apNodes.Get(i);
        uint32_t nodeId = node->GetId();
        
        for (uint32_t devIndex = 0; devIndex < node->GetNDevices(); ++devIndex) {
            Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(devIndex));
            if (wifiDev) {
                Ptr<WifiPhy> phy = wifiDev->GetPhy();
                if (phy) {
                    // Connect to state trace
                    bool connected = phy->TraceConnectWithoutContext("State", 
                        MakeBoundCallback(&WifiStateChangeCallback, nodeId));
                    
                    if (connected) {
                        // Get initial state
                        WifiPhyState initialState = phy->GetState()->GetState();
                        g_energyMetrics[nodeId].currentState = initialState;
                        g_energyMetrics[nodeId].lastState = initialState;
                        g_energyMetrics[nodeId].lastStateChange = Simulator::Now();
                        
                        //std::cout << "Connected state trace for AP Node " << nodeId 
                        //          << " - Initial state: " << WifiStateToString(initialState) << std::endl;
                        connectedTraces++;
                    } else {
                        //std::cout << "FAILED to connect state trace for AP Node " << nodeId << std::endl;
                    }
                }
            }
        }
    }
    
    // Connect for STAs
    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        Ptr<Node> node = staNodes.Get(i);
        uint32_t nodeId = node->GetId();
        
        for (uint32_t devIndex = 0; devIndex < node->GetNDevices(); ++devIndex) {
            Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(devIndex));
            if (wifiDev) {
                Ptr<WifiPhy> phy = wifiDev->GetPhy();
                if (phy) {
                    // Connect to state trace
                    bool connected = phy->TraceConnectWithoutContext("State", 
                        MakeBoundCallback(&WifiStateChangeCallback, nodeId));
                    
                    if (connected) {
                        // Get initial state
                        WifiPhyState initialState = phy->GetState()->GetState();
                        g_energyMetrics[nodeId].currentState = initialState;
                        g_energyMetrics[nodeId].lastState = initialState;
                        g_energyMetrics[nodeId].lastStateChange = Simulator::Now();
                        
                        //std::cout << "Connected state trace for STA Node " << nodeId 
                        //          << "- Initial state: " << WifiStateToString(initialState) << std::endl;
                        connectedTraces++;
                    } else {
                        //std::cout << "FAILED to connect state trace for STA Node " << nodeId << std::endl;
                    }
                }
            }
        }
    }
    
    //std::cout << "=== WIFI STATE TRACES: " << connectedTraces << " connected successfully ===" << std::endl;
}
void ConnectEnergyModelTraces(NodeContainer& apNodes, NodeContainer& staNodes,
                              DeviceEnergyModelContainer& apEnergyModels,
                              DeviceEnergyModelContainer& staEnergyModels)
{
    //std::cout << "\n=== CONNECTING ENERGY MODEL STATE TRACES ===" << std::endl;
    
    int connectedTraces = 0;
    
    // Connect for APs
    for (uint32_t i = 0; i < apNodes.GetN(); ++i) {
        uint32_t nodeId = apNodes.Get(i)->GetId();
        Ptr<WifiRadioEnergyModel> model = DynamicCast<WifiRadioEnergyModel>(apEnergyModels.Get(i));
        
        if (model) {
            // Connect to the DeviceEnergyModel state change callback
            bool connected = model->TraceConnectWithoutContext(
                "TotalEnergyConsumption",
                MakeBoundCallback(&EnergyStateChangeCallback, nodeId)
            );
            
            if (connected) {
                // Get initial state from energy model (not PHY)
                WifiPhyState initialState = model->GetCurrentState();
                g_energyMetrics[nodeId].currentState = initialState;
                g_energyMetrics[nodeId].lastState = initialState;
                g_energyMetrics[nodeId].lastStateChange = Simulator::Now();
                
                //std::cout << " Connected energy trace for AP Node " << nodeId 
                //          << " - Initial state: " << WifiStateToString(initialState) << std::endl;
                connectedTraces++;
            }
        }
    }
    
    // Connect for STAs
    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        uint32_t nodeId = staNodes.Get(i)->GetId();
        Ptr<WifiRadioEnergyModel> model = DynamicCast<WifiRadioEnergyModel>(staEnergyModels.Get(i));
        
        if (model) {
            bool connected = model->TraceConnectWithoutContext(
                "TotalEnergyConsumption",
                MakeBoundCallback(&EnergyStateChangeCallback, nodeId)
            );
            
            if (connected) {
                WifiPhyState initialState = model->GetCurrentState();
                g_energyMetrics[nodeId].currentState = initialState;
                g_energyMetrics[nodeId].lastState = initialState;
                g_energyMetrics[nodeId].lastStateChange = Simulator::Now();
                
                //std::cout << " Connected energy trace for STA Node " << nodeId 
                //          << " - Initial state: " << WifiStateToString(initialState) << std::endl;
                connectedTraces++;
            }
        }
    }
    
    //std::cout << "=== ENERGY MODEL TRACES: " << connectedTraces << " connected successfully ===" << std::endl;
}


// Alternative: Poll-based state detection (more reliable)
void PollEnergyStates()
{
    Time now = Simulator::Now();
    
    for (auto& pair : g_energyMetrics) {
        uint32_t nodeId = pair.first;
        EnergyMetrics& metrics = pair.second;
        
        if (g_deviceEnergyModels.find(nodeId) == g_deviceEnergyModels.end()) {
            continue;
        }
        
        Ptr<WifiRadioEnergyModel> model = g_deviceEnergyModels[nodeId];
        if (!model) continue;
        
        WifiPhyState currentState = model->GetCurrentState();
        
        // Check if state changed
        if (currentState != metrics.currentState) {
            std::string nodeType = (nodeId < g_numAPs) ? "AP" : "STA";
            uint32_t nodeIndex = (nodeId < g_numAPs) ? nodeId : nodeId - g_numAPs;
            
            //std::cout << "[POLL] State change: " << nodeType << " " << nodeIndex 
            //          << " " << WifiStateToString(metrics.currentState)
            //          << " -> " << WifiStateToString(currentState)
            //          << " at " << now.GetSeconds() << "s" << std::endl;
            
            // Calculate energy for previous state
            if (metrics.lastStateChange > Seconds(0)) {
                Time stateDuration = now - metrics.lastStateChange;
                metrics.stateDuration[metrics.lastState] += stateDuration;
                
                double statePower = CalculateCurrentPower(
                    model, 
                    g_energySources[nodeId], 
                    metrics.lastState
                );
                double stateEnergy = statePower * stateDuration.GetSeconds();
                metrics.stateEnergy[metrics.lastState] += stateEnergy;
            }
            
            // Update state
            metrics.lastState = metrics.currentState;
            metrics.currentState = currentState;
            metrics.lastStateChange = now;
            
            // Update power
            metrics.currentPower = CalculateCurrentPower(
                model, 
                g_energySources[nodeId], 
                currentState
            );
        }
    }
    
    // Schedule next poll (every 1ms for accurate state tracking)
    Simulator::Schedule(MilliSeconds(1), &PollEnergyStates);
}
void InitializeEnergyDataFile()
{
    energyDataFile.open("wifi6_energy_consumption.csv");
    if (energyDataFile.is_open()) {
        energyDataFile << "Time_s,NodeID,NodeType,TotalEnergyConsumed_J,RemainingEnergy_J,"
                       << "CurrentPower_W,Voltage_V,Current_A,WifiState" << std::endl;
        std::cout << "Energy data file opened: wifi6_energy_consumption.csv" << std::endl;
    }
}

//end of energy

//end
// Structure to store supported Wi-Fi 6 metrics
struct UserMetrics {
    double rssi = 0.0;                    // RSSI (dBm)
    double snr = 0.0;                     // SNR (dB)
    double noiseFloor = 0.0;              // Noise floor (dBm)
    uint32_t dataRate = 0;                // Data rate (bps)
    uint32_t dataRatePhy=0;
    uint32_t txPackets = 0;               // Transmitted packets
    uint32_t rxPackets = 0;               // Received packets
    uint64_t txBytes = 0;                 // Transmitted bytes
    uint64_t rxBytes = 0;                 // Received bytes
    double throughput = 0.0;              // Throughput (Mbps)
    double avgLatency = 0.0;              // Average latency (ms)
    uint32_t crcErrors = 0;               // CRC errors
    Time lastUpdateTime = Seconds(0);     // Last update time
    std::vector<double> latencyHistory;   // Latency history
    std::vector<double> throughputHistory; // Throughput history
    uint32_t connectedAP = 0;             // Which AP the user is connected to
    
    // Additional tracking for accurate calculations
    uint64_t lastRxBytes = 0;
    uint64_t lastTxBytes = 0;
    Time lastThroughputUpdate = Seconds(0);
};

// Custom client position structure
struct ClientPosition {
    uint32_t row;
    uint32_t seat;
    ClientPosition(uint32_t r = 1, uint32_t s = 1) : row(r), seat(s) {}
};

// Global variables
std::map<uint32_t, UserMetrics> userMetrics;
uint64_t totalApTxBytes = 0;
uint64_t totalApRxBytes = 0;

std::map<uint32_t, Time> packetTxTimes;
std::vector<Ptr<TiledDashClient>> g_clientApplications;
// Custom client positions - modify this to set specific positions-> Classroom//Showroom Layout
std::map<uint32_t, ClientPosition> customClientPositions = {
    /*
    {1, ClientPosition(1, 2)},
    {2, ClientPosition(1, 3)},
    {3, ClientPosition(1, 4)},
    {4, ClientPosition(1, 5)},
    {5, ClientPosition(1, 6)},
    {6, ClientPosition(2, 3)},
    {7, ClientPosition(2, 4)},
    {8, ClientPosition(2, 5)},
    {9, ClientPosition(2, 6)},
    {10, ClientPosition(3, 5)},
    {11, ClientPosition(3, 6)},
    {12, ClientPosition(4, 6)}
    */

    //{1, ClientPosition(3,1)},
    //{2, ClientPosition(3,2)},
    //{3, ClientPosition(3,3)},
    //{4,ClientPosition(3,4)},
    //{5,ClientPosition(4,1)},
    //{6,ClientPosition(4,2)},
    //{7,ClientPosition(4,3)},
    //{8,ClientPosition(4,4)}

    //{3,ClientPosition(4,5)}
    //{2,ClientPosition(4,6)}

    //{1, ClientPosition(1,6)}
    //{2,ClientPosition(1,6)}
    //{3,ClientPosition(1,3)}
    //{4,ClientPosition(1,6)}
    
    //{2, ClientPosition(4, 6)}
    //
    //{2, ClientPosition(1, 2)},
    //{3, ClientPosition(2, 4)}
    //{1, ClientPosition(1, 2)},
    //{2, ClientPosition(1, 3)}
    //{1, ClientPosition(3, 2)},  // Client 1: Row 3, Seat 1
    //{2, ClientPosition(1, 2)},   // Client 2: Row 1, Seat 2
    //{3, ClientPosition(1, 3)}    // Client 3: Row 1, Seat 3
    //{1, ClientPosition(3, 5)}
};

// Add to your global variables section
struct RuMetrics {
    uint32_t totalRusAllocated = 0;
    uint32_t rusPerUser = 0;
    uint32_t ruType = 0; // 0: Not allocated, 1: 26-tone, 2: 52-tone, 3: 106-tone, 4: 242-tone, 5: 484-tone, 6: 996-tone
    double ruUtilization = 0.0;
};

std::map<uint32_t, RuMetrics> ruMetrics;
std::ofstream ruDataFile;
std::ofstream phyDataFile;

struct ApplicationMetrics {
    uint64_t lastTotalBytes = 0;
    double appDataRate = 0.0;  // Mbps
    Time lastUpdateTime = Seconds(0);
};

std::map<uint32_t, ApplicationMetrics> g_appMetrics;
// ===== EDGE AP POSITIONING FUNCTIONS (FROM TestDashV16_4) =====
// Function to calculate cinema seating positions with custom positioning
///*
Vector CalculateCinemaPosition(uint32_t clientIndex, uint32_t maxClientsPerRow = 6){
    double seatSpacing = 0.8;      // 80cm between seats
    double rowSpacing = 1.2;       // 120cm between rows
    double screenDistance = 0;     // 0m from screen to first row

    uint32_t row, seat;
    
    // Check if custom position is defined for this client
    if (customClientPositions.find(clientIndex) != customClientPositions.end()) {
        std::cout<<"Tu sam"<<std::endl;
        row = customClientPositions[clientIndex].row;
        seat = customClientPositions[clientIndex].seat;
        std::cout<<"row "<<row<<" seat "<<seat<<std::endl;
    } else {
        // Default sequential positioning
        row = ((clientIndex - 1) / maxClientsPerRow) + 1;
        seat = ((clientIndex - 1) % maxClientsPerRow) + 1;
    }
    
    // Validate positions
    if (row < 1 || row > 5) {
        std::cout << "WARNING: Invalid row " << row << " for client " << clientIndex 
                  << ". Using row 1." << std::endl;
        row = 1;
    }
    if (seat < 1 || seat > maxClientsPerRow) {
        std::cout << "WARNING: Invalid seat " << seat << " for client " << clientIndex 
                  << ". Using seat 1." << std::endl;
        seat = 1;
    }
    
    // Calculate x position (centered around x=0)
    double x = (seat - 1 - (maxClientsPerRow - 1) / 2.0) * seatSpacing;
    
    // Calculate y position (distance from screen)
    double y = screenDistance + ((row - 1) * rowSpacing);

    //double z = 0;
    double z = 0.91;//sitting heigh from ground level- stolica pod, 51cm, bilo 0.91
    return Vector(x, y, z);
}

// Function to calculate edge AP positions (4 APs at corners) - FROM TestDashV16_4
std::vector<Vector> CalculateEdgeAPPositions(uint32_t maxClientsPerRow = 6)
{
    std::vector<Vector> apPositions;
    
    double seatSpacing = 0.8;      // 80cm between seats
    double rowSpacing = 1.2;       // 120cm between rows
    double screenDistance = 0;     // 0m from screen to first row
    
    // Calculate leftmost and rightmost x positions
    double leftmostX = -((maxClientsPerRow - 1) / 2.0) * seatSpacing;   // -3.6m for 10 seats
    double rightmostX = ((maxClientsPerRow - 1) / 2.0) * seatSpacing;   // +3.6m for 10 seats
    
    // Row 1 and Row 5 y positions
    double row1Y = screenDistance;                    // 0m
    double row5Y = screenDistance + (3 * rowSpacing); // 4.8m, bilo 4 ovjde za 5 redova
    
    // AP height
    double apZ = 2.51; // 2.51m height, bilo 3
    
    // AP 0: Row 1, Left (far left, first row)
    //apPositions.push_back(Vector(leftmostX-1, row1Y, apZ));
    //------------
    //ap1 and ap2 active in our case
    // AP 1: Row 1, Right (far right, first row)
    //apPositions.push_back(Vector(rightmostX+1, row1Y, apZ)); //bilo x+1
    
    // *AP 2: Row 5, Left (far left, fifth row)
    apPositions.push_back(Vector(leftmostX-1, row5Y, apZ));//bulo x-1


    //apPositions.push_back(Vector(0, 1.5 * rowSpacing, apZ));
    // AP 3: Row 5, Right (far right, fifth row)  
    //apPositions.push_back(Vector(rightmostX+1, row5Y, apZ));
    
    return apPositions;
}

// Helper function to calculate Euclidean distance between two points
double CalculateDistanceAP(const Vector& pos1, const Vector& pos2) {
    double dx = pos1.x - pos2.x;
    double dy = pos1.y - pos2.y;
    double dz = pos1.z - pos2.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// Function to determine which AP a user should connect to based on distance
uint32_t DetermineClosestAP(uint32_t clientIndex, const std::vector<Vector>& apPositions, uint32_t maxClientsPerRow = 10)
{
    Vector clientPos = CalculateCinemaPosition(clientIndex, maxClientsPerRow);
    double minDistance = std::numeric_limits<double>::max();
    uint32_t closestAP = 0;
    
    for (uint32_t apIndex = 0; apIndex < apPositions.size(); ++apIndex) {
        double distance = CalculateDistanceAP(clientPos, apPositions[apIndex]);
        if (distance < minDistance) {
            minDistance = distance;
            closestAP = apIndex;
        }
    }
    
    return closestAP;
}
//*/

//Logging
// Helper functions
double g_signalDbmAvg; //!< Average signal power [dBm]
double g_noiseDbmAvg;  //!< Average noise power [dBm]
uint32_t g_samples;    //!< Number of samples
std::string HeRuTypeToString(HeRu::RuType ruType)
{
    switch (ruType) {
        case HeRu::RU_26_TONE:   return "RU26";
        case HeRu::RU_52_TONE:   return "RU52";
        case HeRu::RU_106_TONE:  return "RU106";
        case HeRu::RU_242_TONE:  return "RU242";
        case HeRu::RU_484_TONE:  return "RU484";
        case HeRu::RU_996_TONE:  return "RU996";
        case HeRu::RU_2x996_TONE: return "RU2x996";
        default: return "UNKNOWN_RU";
    }
}

std::string CodeRateToString(WifiCodeRate codeRate)
{
    switch (codeRate) {
        case WIFI_CODE_RATE_1_2:  return "1/2";
        case WIFI_CODE_RATE_2_3:  return "2/3";
        case WIFI_CODE_RATE_3_4:  return "3/4";
        case WIFI_CODE_RATE_5_6:  return "5/6";
        default: return "Unknown";
    }
}

// More accurate RU utilization calculation
double CalculateRuUtilization(HeRu::RuType ruType, uint16_t channelWidth)
{
    // Data subcarriers for each RU type (excluding pilots and guard tones)
    uint16_t ruDataSubcarriers = 0;
    
    switch (ruType) {
        case HeRu::RU_26_TONE:   ruDataSubcarriers = 24; break;   // 26-2 pilots
        case HeRu::RU_52_TONE:   ruDataSubcarriers = 48; break;   // 52-4 pilots  
        case HeRu::RU_106_TONE:  ruDataSubcarriers = 102; break;  // 106-4 pilots
        case HeRu::RU_242_TONE:  ruDataSubcarriers = 234; break;  // 242-8 pilots
        case HeRu::RU_484_TONE:  ruDataSubcarriers = 468; break;  // 484-16 pilots
        case HeRu::RU_996_TONE:  ruDataSubcarriers = 980; break;  // 996-16 pilots
        case HeRu::RU_2x996_TONE: ruDataSubcarriers = 1960; break; // 2*980
        default: return 0.0;
    }
    
    // Total DATA subcarriers for different channel widths (corrected values)
    uint16_t totalDataSubcarriers = 0;
    switch (channelWidth) {
        case 20:  totalDataSubcarriers = 234; break;  // HE20: 234 data subcarriers
        case 40:  totalDataSubcarriers = 468; break;  // HE40: 468 data subcarriers  
        case 80:  totalDataSubcarriers = 980; break;  // HE80: 980 data subcarriers
        case 160: totalDataSubcarriers = 1960; break; // HE160: 1960 data subcarriers
        default: return 0.0;
    }
    
    return (static_cast<double>(ruDataSubcarriers) / totalDataSubcarriers) * 100.0;
}

// SNR-based MCS estimation fallback (from previous solution)

std::string ModulationClassToString(WifiModulationClass modClass)
{
    switch (modClass) {
        case WIFI_MOD_CLASS_DSSS:     return "DSSS";
        case WIFI_MOD_CLASS_HR_DSSS:  return "HR-DSSS";
        case WIFI_MOD_CLASS_OFDM:     return "OFDM";
        case WIFI_MOD_CLASS_HT:       return "HT";
        case WIFI_MOD_CLASS_VHT:      return "VHT";
        case WIFI_MOD_CLASS_HE:       return "HE";
        default: return "UNKNOWN";
    }
}
std::string ModulationClassToString2(WifiModulationClass modClass, uint8_t mcs)
{
    std::stringstream ss;
    
    switch (modClass) {
        case WIFI_MOD_CLASS_HE:
            ss << "HE MCS " << static_cast<int>(mcs) << " (802.11ax) - ";
            switch (mcs) {
                case 0: ss << "BPSK, 1/2"; break;
                case 1: ss << "QPSK, 1/2"; break;
                case 2: ss << "QPSK, 3/4"; break;
                case 3: ss << "16-QAM, 1/2"; break;
                case 4: ss << "16-QAM, 3/4"; break;
                case 5: ss << "64-QAM, 2/3"; break;
                case 6: ss << "64-QAM, 3/4"; break;
                case 7: ss << "64-QAM, 5/6"; break;
                case 8: ss << "256-QAM, 5/6"; break;  // 
                case 9: ss << "256-QAM, 5/6"; break;  // 
                case 10: ss << "1024-QAM, 3/4"; break;
                case 11: ss << "1024-QAM, 5/6"; break;
                default: ss << "Unknown HE MCS"; break;
            }
            break;
            
        case WIFI_MOD_CLASS_VHT:
            ss << "VHT MCS " << static_cast<int>(mcs) << " (802.11ac) - ";
            if (mcs <= 7) {
                // VHT MCS 0-7 are similar to HE but different code rates
                const std::string vhtMcsDesc[] = {
                    "BPSK, 1/2", "QPSK, 1/2", "QPSK, 3/4", "16-QAM, 1/2",
                    "16-QAM, 3/4", "64-QAM, 2/3", "64-QAM, 3/4", "64-QAM, 5/6"
                };
                ss << vhtMcsDesc[mcs];
            } else if (mcs == 8) {
                ss << "256-QAM, 3/4";
            } else if (mcs == 9) {
                ss << "256-QAM, 5/6";
            } else {
                ss << "Unknown VHT MCS";
            }
            break;
            
        case WIFI_MOD_CLASS_HT:
            ss << "HT MCS " << static_cast<int>(mcs) << " (802.11n)";
            break;
            
        default:
            ss << "Legacy mode - MCS " << static_cast<int>(mcs);
            break;
    }
    
    return ss.str();
}


std::map<Mac48Address, uint32_t> macToNodeIdMap;
//za mapiranje mac addrese
///static std::map<Mac48Address, uint32_t> macToUserId;
//static uint32_t nextUserId = 1;
std::map<Mac48Address, uint32_t> macToUserId;
uint32_t nextUserId = 1;
Mac48Address ConvertReceivedMacToOriginalFormat(const Mac48Address& receivedMac) {
    uint8_t bytes[6];
    receivedMac.CopyTo(bytes);
    
    // Create the converted MAC bytes
    uint8_t convertedBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, bytes[3]};
    
    // Convert to string format
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 6; i++) {
        if (i > 0) oss << ":";
        oss << std::setw(2) << static_cast<int>(convertedBytes[i]);
    }
    
    return Mac48Address(oss.str().c_str());
}

struct LatestPacketData {
    // Latest values (not accumulated)
    double rssi = 0;              // Changed from rssiSum
    double snr = 0;               // Changed from snrSum
    uint32_t sampleCount = 0;     // Count of packets in this interval
    
    // Other fields (unchanged)
    uint8_t mcsValue = 0;
    std::string ruTypeDesc = "N/A";
    double ruUtilizationPercent = 0.0;
    double dataRateMbps = 0.0;
    uint64_t dataRatePhy = 0;     // Note: uint64_t, not double
    uint8_t nss = 1;
    uint16_t channelFreqMhz = 0;
    uint16_t channelWidth = 0;
    WifiModulationClass modClass = WIFI_MOD_CLASS_HE;
    WifiCodeRate codeRate = WIFI_CODE_RATE_UNDEFINED;
    uint32_t userId = 0;
    Mac48Address srcMac;
    uint16_t ruIndex = UINT16_MAX;
    bool ruPrimary80MHz = false;
    bool isAggregation = false;
    uint32_t psduLength = 0;
    uint32_t packetSize = 0;
    uint8_t mpduType = 0;
    uint32_t mpduRefNumber = 0;
    bool hasData = false;
};
LatestPacketData g_latestData;
std::map<uint32_t, LatestPacketData> g_latestUserData;
bool g_periodicLoggingStarted = false;

void WritePeriodicData()
{
    if (phyDataFile.is_open()) {
        for (auto& pair : g_latestUserData) {
            LatestPacketData& userData = pair.second;
            if (userData.hasData && userData.sampleCount > 0) {
                
                // Get application data rate for this user
                double appDataRate = 0.0;
                if (g_appMetrics.find(userData.userId) != g_appMetrics.end()) {
                    appDataRate = g_appMetrics[userData.userId].appDataRate;
                }
                
                phyDataFile << std::fixed << std::setprecision(6);
                phyDataFile << Simulator::Now().GetSeconds() << ","
                            << userData.userId << ","
                            << userData.srcMac << ","
                            << userData.rssi << ","
                            << userData.snr << ","
                            << userData.dataRateMbps << ","
                            << userData.dataRatePhy << ","
                            << static_cast<int>(userData.mcsValue) << ","
                            << "PERIODIC_LOG" << ","
                            << static_cast<int>(userData.nss) << ","
                            << "0.0" << ","
                            << "0" << ","
                            << "0" << ","
                            << "0" << ","
                            << "0" << ","
                            << "0" << ","
                            << "0.0" << ","
                            << "\"" << userData.ruTypeDesc << "\","
                            << userData.ruUtilizationPercent << ","
                            << "0" << ","
                            << userData.channelFreqMhz << ","
                            << userData.channelWidth << ","
                            << "\"" << ModulationClassToString(userData.modClass) << "\","
                            << "\"" << ModulationClassToString2(userData.modClass, userData.mcsValue) << "\","
                            << "\"" << CodeRateToString(userData.codeRate) << "\","
                            << (userData.ruIndex != UINT16_MAX ? static_cast<int>(userData.ruIndex) : -1) << ","
                            << (userData.ruPrimary80MHz ? 1 : 0) << ","
                            << (userData.isAggregation ? 1 : 0) << ","
                            << userData.psduLength << ","
                            << userData.packetSize << ","
                            << static_cast<int>(userData.mpduType) << ","
                            << userData.mpduRefNumber << ","
                            << userData.sampleCount << ","
                            << appDataRate  // Application data rate in Mbps
                            << std::endl;
                
                userData.sampleCount = 0;
                userData.hasData = false;
            }
        }
    }
    
    Simulator::Schedule(MilliSeconds(10), &WritePeriodicData);
}
///*
// Map to store latest SNR per node
std::map<uint32_t, double> g_latestNodeSnr;
void MonitorSniffRx(Ptr<const Packet> packet,
                    uint16_t channelFreqMhz,
                    WifiTxVector txVector,
                    MpduInfo aMpdu,
                    SignalNoiseDbm signalNoise,
                    uint16_t staId)
{
    // Start periodic logging on first packet (only once)
    
    if (!g_periodicLoggingStarted) {
        g_periodicLoggingStarted = true;
        Simulator::Schedule(MilliSeconds(10), &WritePeriodicData); //bilo 100
    }

    // Update global signal metrics
    g_samples++;
    g_signalDbmAvg += ((signalNoise.signal - g_signalDbmAvg) / g_samples);
    g_noiseDbmAvg += ((signalNoise.noise - g_noiseDbmAvg) / g_samples);
    if(staId>2048 || staId==0){
        //std::cout << "Skipping invalid STA-ID: " << staId << std::endl;
        return;
    }
    // === NEW LOGIC TO FIND THE CORRECT NODE ID ===
    uint32_t userId = 0; // default for unknown
    
    if (staId <= 2048) {
        // Initialize variables
        //--
        WifiMacHeader hdr;
        packet->PeekHeader(hdr);
        Mac48Address srcMac = hdr.GetAddr2(); // Source address
        Mac48Address convertedMac = ConvertReceivedMacToOriginalFormat(srcMac);
        for (auto const& pair : macToUserId) {
            if(pair.first == convertedMac){
                userId = pair.second;
            }
        }
        
        //userId=staId;
        //std::cout<<"userId u MonitorSniffRx: "<<userId<<std::endl;
        //--
        uint8_t mcsValue = 0;
        std::string ruTypeDesc = "N/A";
        double ruUtilizationPercent = 0.0;
        WifiCodeRate codeRate = WIFI_CODE_RATE_UNDEFINED;
        double dataRateMbps = 0.0;
        uint64_t dataRateBps = 0;
        uint64_t phyRateBps = 0;
        double dataRatePhy = 0.0;
        WifiModulationClass modClass = txVector.GetModulationClass();

        if (modClass != WIFI_MOD_CLASS_HE) {
            return;  // Skip OFDM, HT, VHT frames
        }
        uint8_t nss = txVector.GetNssMax();
        // Calculate SNR and RSSI
        double noiseFloor = signalNoise.noise;
        double snr = signalNoise.signal - noiseFloor;
        double rssi = signalNoise.signal;
        g_latestNodeSnr[userId] = snr;
        // Use HePhy static methods for rate calculation (works for SU and MU)
        if (modClass == WIFI_MOD_CLASS_HE) {
            // Get the Data Rate (throughput/MAC-level rate) in bps
            dataRateBps = HePhy::GetDataRateFromTxVector(txVector, staId);
            dataRateMbps = static_cast<double>(dataRateBps) / 1e6;

            // Get the PHY Rate (raw physical-layer rate) in bps
            phyRateBps = HePhy::GetPhyRateFromTxVector(txVector, staId);
            dataRatePhy = static_cast<double>(phyRateBps) / 1e6;
        }

        // Try to extract HE MU specific information
        try {
            if (modClass == WIFI_MOD_CLASS_HE) {
                
                // Get HE MU user info for this station
                HeMuUserInfo heMuInfo = txVector.GetHeMuUserInfo(staId);
                // Extract MCS from HE MU user info
                mcsValue = heMuInfo.mcs;
                
                // Get NSS from HE MU user info
                nss = heMuInfo.nss;
                
                // Extract coding rate
                codeRate = HePhy::GetCodeRate(mcsValue);
                
                // Get RU specification
                HeRu::RuSpec ruSpec = txVector.GetRu(staId);
                
                // Extract RU information
                HeRu::RuType ruType = ruSpec.GetRuType();
                
                // Convert RU type to descriptive string
                ruTypeDesc = HeRuTypeToString(ruType);
                
                // Calculate RU utilization (simplified calculation)
                ruUtilizationPercent = CalculateRuUtilization(ruType, txVector.GetChannelWidth());
            }
        } catch (const std::exception& e) {
            // Handle case where STA is not in MU transmission
            mcsValue = txVector.GetMode().GetMcsValue();
            // If it's HE but not MU-user (i.e., SU), ruTypeDesc should reflect that
            if (modClass == WIFI_MOD_CLASS_HE) {
                ruTypeDesc = "SU";
            }
        }
        
        // *** STORE LATEST PACKET DATA ***
        if (userId > 0) {
            LatestPacketData& userData = g_latestUserData[userId];
            
            // Store latest values (not accumulated)
            userData.rssi = rssi;
            userData.snr = snr;
            userData.sampleCount++;  // Increment packet counter
            
            userData.mcsValue = mcsValue;
            userData.ruTypeDesc = ruTypeDesc;
            userData.ruUtilizationPercent = ruUtilizationPercent;
            userData.dataRateMbps = dataRateMbps;
            userData.dataRatePhy = static_cast<uint64_t>(dataRatePhy * 1e6);
            userData.nss = nss;
            userData.channelFreqMhz = channelFreqMhz;
            userData.channelWidth = txVector.GetChannelWidth();
            userData.modClass = modClass;
            userData.codeRate = codeRate;
            userData.userId = userId;
            userData.srcMac = srcMac;
            userData.isAggregation = txVector.IsAggregation();
            userData.psduLength = txVector.GetLength();
            userData.packetSize = packet->GetSize();
            userData.mpduType = aMpdu.type;
            userData.mpduRefNumber = (aMpdu.mpduRefNumber != UINT32_MAX ? aMpdu.mpduRefNumber : 0);
            userData.hasData = true;
            
            // Get RU-specific info if available
            if (modClass == WIFI_MOD_CLASS_HE && ruTypeDesc != "SU" && ruTypeDesc != "N/A") {
                try {
                    HeRu::RuSpec ruSpec = txVector.GetRu(staId);
                    userData.ruIndex = ruSpec.GetIndex();
                    userData.ruPrimary80MHz = ruSpec.GetPrimary80MHz();
                } catch (const std::exception& e) {
                    userData.ruIndex = UINT16_MAX;
                    userData.ruPrimary80MHz = false;
                }
            } else {
                userData.ruIndex = UINT16_MAX;
                userData.ruPrimary80MHz = false;
            }
        }
    }
 
}
//*/
void MonitorApplicationDataRate()
{
    Time now = Simulator::Now();
    
    for (uint32_t i = 0; i < g_clientApplications.size(); ++i) {
        Ptr<TiledDashClient> clientApp = g_clientApplications[i];
        if (!clientApp) continue;
        
        uint32_t clientId = i + 1;
        ApplicationMetrics& appMetric = g_appMetrics[clientId];
        
        // Get network tracker to access throughput data
        Ptr<TiledDashClientNetworkTracker> networkTracker = clientApp->GetNetworkTracker();
        if (networkTracker) {
            // Get the estimated throughput from the tracker
            double currentThroughputBps = networkTracker->EstimateOverallThroughput();
            
            // Calculate data rate based on throughput estimate
            if (appMetric.lastUpdateTime > Seconds(0)) {
                double timeDiff = (now - appMetric.lastUpdateTime).GetSeconds();
                if (timeDiff >= 0.1) {
                    // Use the throughput estimate directly as data rate
                    appMetric.appDataRate = currentThroughputBps / 1e6; // Convert to Mbps
                    appMetric.lastUpdateTime = now;
                    
                    // Use std::cout instead of NS_LOG_INFO
                    //std::cout << "Client " << clientId << " app data rate: " 
                    //           << appMetric.appDataRate << " Mbps at " 
                    //           << now.GetSeconds() << "s" << std::endl;
                }
            } else {
                appMetric.lastUpdateTime = now;
            }
        }
    }
    
    // Schedule next monitoring (every 100ms)
    Simulator::Schedule(MilliSeconds(10), &MonitorApplicationDataRate);
}
//gurobi ispod
struct OptimizationResult {
    uint32_t clientId;
    uint32_t assignedAP;
    Vector position;
    double expectedThroughput;
    double signalStrength;
};

//Original optimization variables (if missing from your current code)
std::vector<OptimizationResult> g_optimizationResults;
bool g_optimizationComplete = false;

// New real-time optimization globals
bool g_measurementPhaseComplete = false;
double g_measurementPhaseDuration = 25.0; // seconds to collect measurements,bilo 25
double g_optimizationCallTime = 5.0;
std::string g_measurementFile = "wifi6_supported_metrics.csv";
std::string g_optimizationResultFile = "optimization_results.csv";
bool g_useRealTimeOptimization = false; //bio true
void InitializeDataFiles() {
    phyDataFile.open(g_measurementFile);
    ///*
    if (phyDataFile.is_open()) {
        phyDataFile << "Time,NodeID,SrcMacId,RSSI,SNR,DataRate_Mbps,DataRate_Phy,MCS_Index,Phase,NSS,"
                    << "Throughput_Mbps,RxPackets,RxBytes,TxPackets,TxBytes,CRC_Errors,AvgLatency_ms,"
                    << "RU_Type,RU_Utilization_Percent,Connected_AP,"
                    << "ChannelFreq_MHz,ChannelWidth_MHz,ModulationClass,ModulationClass2,CodeRate,"
                    << "RU_Index,Primary80MHz,Aggregation,PSDU_Length,"
                    << "Packet_Size,MPDU_Type,MPDU_Ref_Number, Testno,AppDataRate_Mbps"
                    << std::endl;
        std::cout << "Enhanced data file opened: " << g_measurementFile << std::endl;
    }
    //*/

}
// ===== ADDITIONAL FUNCTIONS TO ADD =====
// Make sure your ParseOptimizationResults function looks like this:
bool ParseOptimizationResults(const std::string& outputFile) {
    std::ifstream inFile(outputFile);
    if (!inFile.is_open()) {
        std::cerr << "Error: Could not open optimization results file: " << outputFile << std::endl;
        return false;
    }
    
    g_optimizationResults.clear();
    std::string line;
    
    // Skip header if present
    if (std::getline(inFile, line)) {
        if (line.find("CLIENT_ID") != std::string::npos) {
            // Header found, continue to next line
        } else {
            // No header, reset file position
            inFile.clear();
            inFile.seekg(0);
        }
    }
    
    while (std::getline(inFile, line)) {
        std::istringstream iss(line);
        std::string token;
        OptimizationResult result;
        
        // Parse CSV: CLIENT_ID,ASSIGNED_AP,POS_X,POS_Y,POS_Z,EXPECTED_THROUGHPUT,SIGNAL_STRENGTH
        if (std::getline(iss, token, ',')) result.clientId = std::stoul(token);
        if (std::getline(iss, token, ',')) result.assignedAP = std::stoul(token);
        if (std::getline(iss, token, ',')) result.position.x = std::stod(token);
        if (std::getline(iss, token, ',')) result.position.y = std::stod(token);
        if (std::getline(iss, token, ',')) result.position.z = std::stod(token);
        if (std::getline(iss, token, ',')) result.expectedThroughput = std::stod(token);
        if (std::getline(iss, token, ',')) result.signalStrength = std::stod(token);
        
        g_optimizationResults.push_back(result);
        
        std::cout << "Parsed result - Client " << result.clientId 
                  << " -> AP " << result.assignedAP 
                  << " at (" << result.position.x << ", " << result.position.y 
                  << ", " << result.position.z << ")" << std::endl;
    }
    
    inFile.close();
    g_optimizationComplete = true;
    return true;
}
// Enhanced function to call real-time Gurobi optimization
// Enhanced function to call real-time Gurobi optimization with separate data sources
bool CallGurobiOptimizationRealTime(const std::string& csvFile, const std::string& outputFile, double measurementWindow = 10.0) {
    std::cout << "Calling real-time Gurobi optimization with separate data sources..." << std::endl;
    std::cout << "Input WiFi CSV: " << csvFile << std::endl;
    std::cout << "Tile info directory: current directory (.)" << std::endl;
    std::cout << "Output file: " << outputFile << std::endl;
    std::cout << "Measurement window: " << measurementWindow << " seconds" << std::endl;
    
    // Updated Python command to use the enhanced script with separate data sources
    // Parameters: wifi_csv_file tile_info_directory output_file measurement_window
    std::string pythonCommand = "/PathToGurobiSkript" + 
                            csvFile + " " + outputFile + " " + std::to_string(measurementWindow) + " .";
    
    std::cout << "Executing command: " << pythonCommand << std::endl;
    int result = std::system(pythonCommand.c_str());
    
    if (result != 0) {
        std::cerr << "Error: Real-time Gurobi optimization failed with exit code: " << result << std::endl;
        return false;
    }
    
    std::cout << "Real-time Gurobi optimization completed successfully." << std::endl;
    return true;
}
// Function to apply optimized assignments during runtime
void ApplyOptimizedAssignments() {
    if (!g_optimizationComplete) {
        std::cout << "Warning: Optimization results not available for runtime assignment." << std::endl;
        return;
    }
    
    std::cout << "\n=== APPLYING OPTIMIZED ASSIGNMENTS AT RUNTIME ===" << std::endl;
    std::cout << "Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
    
    // Display the optimized assignments
    for (const auto& result : g_optimizationResults) {
        std::cout << "Client " << result.clientId << " should be assigned to AP " << result.assignedAP
                  << " (Expected throughput: " << result.expectedThroughput << " Mbps)" << std::endl;
    }
    
    // Note: In a real implementation, you might want to trigger reassociation here
    std::cout << "Note: Client reassociation logic would be implemented here." << std::endl;
}
// Function to trigger optimization after measurement phase
void TriggerOptimization() {
    std::cout << "\n=== MEASUREMENT PHASE COMPLETE ====" << std::endl;
    std::cout << "Triggering optimization at time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
    
    g_measurementPhaseComplete = true;
    
    // Check if measurement file exists and has data
    std::ifstream testFile(g_measurementFile);
    if (!testFile.good()) {
        std::cerr << "Warning: Measurement file not found or empty. Using default optimization." << std::endl;
        g_useRealTimeOptimization = false;
        return;
    }
    testFile.close();
    
    // Call the enhanced Gurobi optimization with real measurements
    if (CallGurobiOptimizationRealTime(g_measurementFile, g_optimizationResultFile, g_measurementPhaseDuration)) {
        // Parse the new optimization results
        if (ParseOptimizationResults(g_optimizationResultFile)) {
            std::cout << "Real-time optimization results parsed successfully." << std::endl;
            
            // Schedule client reassignments based on new optimization
            Simulator::Schedule(MilliSeconds(100), &ApplyOptimizedAssignments);
        } else {
            std::cerr << "Failed to parse real-time optimization results." << std::endl;
            g_useRealTimeOptimization = false;
        }
    } else {
        std::cerr << "Real-time optimization failed. Continuing with initial assignments." << std::endl;
        g_useRealTimeOptimization = false;
    }
}

// ===== UPDATED VERSIONS OF EXISTING FUNCTIONS =====
// Make sure your GetOptimizedClientPosition function looks like this:
Vector GetOptimizedClientPosition(uint32_t clientIndex) {
    if (!g_optimizationComplete && !g_measurementPhaseComplete) {
        // During measurement phase, use default positions
        return CalculateCinemaPosition(clientIndex, 6);
    }
    
    if (!g_optimizationComplete) {
        std::cerr << "Warning: Optimization not complete, using default position for client " 
                  << clientIndex << std::endl;
        return CalculateCinemaPosition(clientIndex, 6);
    }
    
    // Find the result for this client
    for (const auto& result : g_optimizationResults) {
        if (result.clientId == clientIndex) {
            return result.position;
        }
    }
    
    std::cerr << "Warning: No optimization result found for client " 
              << clientIndex << ", using default position" << std::endl;
    return CalculateCinemaPosition(clientIndex, 6);
}

// Make sure your GetOptimizedAPAssignment function looks like this:
uint32_t GetOptimizedAPAssignment(uint32_t clientIndex) {
    if (!g_optimizationComplete && !g_measurementPhaseComplete) {
        // During measurement phase, use closest AP assignment for initial measurements
        std::vector<Vector> apPositions = CalculateEdgeAPPositions(6);
        return DetermineClosestAP(clientIndex, apPositions, 6);
    }
    
    if (!g_optimizationComplete) {
        std::cerr << "Warning: Optimization not complete, using default AP for client " 
                  << clientIndex << std::endl;
        return 0; // Default to AP 0
    }
    
    // Find the result for this client
    for (const auto& result : g_optimizationResults) {
        if (result.clientId == clientIndex) {
            return result.assignedAP;
        }
    }
    
    std::cerr << "Warning: No optimization result found for client " 
              << clientIndex << ", using default AP" << std::endl;
    return 0; // Default to AP 0
}
void SchedulePeriodicOptimization() {
    // 1. Log the call time for clarity
    std::cout << "\n[SCHEDULER] Time: " << Simulator::Now().GetSeconds() 
              << "s. Initiating periodic optimization cycle." << std::endl;
    
    // 2. Call the main optimization logic
    TriggerOptimization();
    
    // 3. Reschedule this function for the next interval (25 seconds)
    // The next call will be g_measurementPhaseDuration (25.0s) from now.
    Simulator::Schedule(Seconds(g_measurementPhaseDuration), &SchedulePeriodicOptimization);
    
    std::cout << "[SCHEDULER] Next optimization scheduled for: " 
              << Simulator::Now().GetSeconds() + g_measurementPhaseDuration << "s" << std::endl;
}

uint32_t MapPositionToGridIndex(uint32_t clientIndex, uint32_t maxClientsPerRow = 6) {
    uint32_t row, seat;
    
    // Check if custom position is defined for this client
    if (customClientPositions.find(clientIndex) != customClientPositions.end()) {
        row = customClientPositions[clientIndex].row;
        seat = customClientPositions[clientIndex].seat;
    } else {
        // Default sequential positioning
        row = ((clientIndex - 1) / maxClientsPerRow) + 1;
        seat = ((clientIndex - 1) % maxClientsPerRow) + 1;
    }
    
    // Validate row and seat
    if (row < 1 || row > 4) {
        std::cout << "WARNING: Row " << row << " out of range for client " 
                  << clientIndex << ". Clamping to valid range." << std::endl;
        row = std::max(1u, std::min(4u, row));
    }
    if (seat < 1 || seat > maxClientsPerRow) {
        std::cout << "WARNING: Seat " << seat << " out of range for client " 
                  << clientIndex << ". Clamping to valid range." << std::endl;
        seat = std::max(1u, std::min(maxClientsPerRow, seat));
    }
    
    // Calculate grid index: (row - 1) * maxClientsPerRow + seat
    uint32_t gridIndex = (row - 1) * maxClientsPerRow + seat;
    
    std::cout << "Client " << clientIndex << " at Row " << row 
              << ", Seat " << seat << " -> Grid Index " << gridIndex << std::endl;
    
    return gridIndex;
}
//
//
//txPower=8dBm without nakagami and shadowing to match the table
// ===SNR CONTROL PARAMETERS ===
double txPower_dBm = 10.0;          // Realistic AP transmit power (15-20 dBm typical)
double noiseFigure_dB =7.0;        // Receiver noise figure (7-12 dB typical for consumer devices)
double pathLossExponent = 4;      // Indoor path loss exponent (2.5-4.0 typical)
double referenceLoss_dB = 46.67;    // Free space loss at 1m for 5GHz (46.67 dB)
double shadowingStdDev = 5;       // Shadowing standard deviation (6-12 dB typical)
//dodavanje
struct PhyStateLogEntry {
    Time timestamp;
    uint32_t nodeId;
    std::string state;  // IDLE, CCA_BUSY, TX, RX, SWITCHING, SLEEP, OFF
    double snr;
    Mac48Address macAddress;
};

struct MacStateLogEntry {
    Time timestamp;
    uint32_t nodeId;
    std::string state;  // Appropriate MAC state
    double snr;
    Mac48Address macAddress;
};

std::vector<PhyStateLogEntry> g_phyStateLog;
std::vector<MacStateLogEntry> g_macStateLog;
std::ofstream phyStateFile;
std::ofstream macStateFile;



// Callback for PHY state changes
void PhyStateTrace(std::string context, Time start, Time duration, WifiPhyState state)
{
    // Extract node ID from context
    std::string delimiter = "/NodeList/";
    size_t pos = context.find(delimiter);
    if (pos == std::string::npos) return;
    
    std::string nodeIdStr = context.substr(pos + delimiter.length());
    size_t endPos = nodeIdStr.find("/");
    if (endPos == std::string::npos) return;
    
    nodeIdStr = nodeIdStr.substr(0, endPos);
    uint32_t nodeId = std::stoul(nodeIdStr);
    
    // Get the node and its WiFi device to extract MAC address
    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(0));
    Mac48Address macAddr = Mac48Address("00:00:00:00:00:00");
    if (wifiDev) {
        macAddr = wifiDev->GetMac()->GetAddress();
    }
    
    // Get latest SNR for this node (from MonitorSniffRx)
    double currentSnr = 0.0;
    if (g_latestNodeSnr.find(nodeId) != g_latestNodeSnr.end()) {
        currentSnr = g_latestNodeSnr[nodeId];
    }
    
    PhyStateLogEntry entry;
    entry.timestamp = Simulator::Now();
    entry.nodeId = nodeId;
    entry.snr = currentSnr;
    entry.macAddress = macAddr;
    
    // Convert state enum to string
    switch(state) {
        case WifiPhyState::IDLE:
            entry.state = "IDLE";
            break;
        case WifiPhyState::CCA_BUSY:
            entry.state = "CCA_BUSY";
            break;
        case WifiPhyState::TX:
            entry.state = "TX";
            break;
        case WifiPhyState::RX:
            entry.state = "RX";
            break;
        case WifiPhyState::SWITCHING:
            entry.state = "SWITCHING";
            break;
        case WifiPhyState::SLEEP:
            entry.state = "SLEEP";
            break;
        case WifiPhyState::OFF:
            entry.state = "OFF";
            break;
        default:
            entry.state = "UNKNOWN";
    }
    
    g_phyStateLog.push_back(entry);
}

// Callback for MAC state (using MacTx/MacRx as proxies for state)
void MacTxTrace(std::string context, Ptr<const Packet> packet)
{
    std::string delimiter = "/NodeList/";
    size_t pos = context.find(delimiter);
    if (pos == std::string::npos) return;
    
    std::string nodeIdStr = context.substr(pos + delimiter.length());
    size_t endPos = nodeIdStr.find("/");
    if (endPos == std::string::npos) return;
    
    nodeIdStr = nodeIdStr.substr(0, endPos);
    uint32_t nodeId = std::stoul(nodeIdStr);
    
    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(0));
    Mac48Address macAddr = Mac48Address("00:00:00:00:00:00");
    if (wifiDev) {
        macAddr = wifiDev->GetMac()->GetAddress();
    }
    
    double currentSnr = 0.0;
    if (g_latestNodeSnr.find(nodeId) != g_latestNodeSnr.end()) {
        currentSnr = g_latestNodeSnr[nodeId];
    }
    
    MacStateLogEntry entry;
    entry.timestamp = Simulator::Now();
    entry.nodeId = nodeId;
    entry.state = "TX";
    entry.snr = currentSnr;
    entry.macAddress = macAddr;
    
    g_macStateLog.push_back(entry);
}

void MacRxTrace(std::string context, Ptr<const Packet> packet)
{
    std::string delimiter = "/NodeList/";
    size_t pos = context.find(delimiter);
    if (pos == std::string::npos) return;
    
    std::string nodeIdStr = context.substr(pos + delimiter.length());
    size_t endPos = nodeIdStr.find("/");
    if (endPos == std::string::npos) return;
    
    nodeIdStr = nodeIdStr.substr(0, endPos);
    uint32_t nodeId = std::stoul(nodeIdStr);
    
    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(node->GetDevice(0));
    Mac48Address macAddr = Mac48Address("00:00:00:00:00:00");
    if (wifiDev) {
        macAddr = wifiDev->GetMac()->GetAddress();
    }
    
    double currentSnr = 0.0;
    if (g_latestNodeSnr.find(nodeId) != g_latestNodeSnr.end()) {
        currentSnr = g_latestNodeSnr[nodeId];
    }
    
    MacStateLogEntry entry;
    entry.timestamp = Simulator::Now();
    entry.nodeId = nodeId;
    entry.state = "RX";
    entry.snr = currentSnr;
    entry.macAddress = macAddr;
    
    g_macStateLog.push_back(entry);
}

// Function to flush PHY state log to CSV
void FlushPhyStateLog()
{
    phyStateFile.open("phy_state_log.csv");
    
    if (!phyStateFile.is_open()) {
        std::cerr << "Failed to open PHY state log file" << std::endl;
        return;
    }
    
    // Write CSV header
    phyStateFile << "Timestamp_s,NodeID,MACAddress,PHY_State,SNR_dB\n";
    
    // Write all entries
    for (const auto& entry : g_phyStateLog) {
        phyStateFile << std::fixed << std::setprecision(9)
                     << entry.timestamp.GetSeconds() << ","
                     << entry.nodeId << ","
                     << entry.macAddress << ","
                     << entry.state << ","
                     << entry.snr << "\n";
    }
    
    phyStateFile.close();
    std::cout << "Flushed " << g_phyStateLog.size() << " PHY state entries to phy_state_log.csv" << std::endl;
}

// Function to flush MAC state log to CSV
void FlushMacStateLog()
{
    macStateFile.open("mac_state_log.csv");
    
    if (!macStateFile.is_open()) {
        std::cerr << "Failed to open MAC state log file" << std::endl;
        return;
    }
    
    // Write CSV header
    macStateFile << "Timestamp_s,NodeID,MACAddress,MAC_State,SNR_dB\n";
    
    // Write all entries
    for (const auto& entry : g_macStateLog) {
        macStateFile << std::fixed << std::setprecision(9)
                     << entry.timestamp.GetSeconds() << ","
                     << entry.nodeId << ","
                     << entry.macAddress << ","
                     << entry.state << ","
                     << entry.snr << "\n";
    }
    
    macStateFile.close();
    std::cout << "Flushed " << g_macStateLog.size() << " MAC state entries to mac_state_log.csv" << std::endl;
}
void InitializePhyMacStateFiles() {
    // Reserve space to avoid frequent reallocations
    g_phyStateLog.reserve(100000);
    g_macStateLog.reserve(100000);
    
    std::cout << "PHY and MAC state logging initialized" << std::endl;
}
void PeriodicWritePhyMacStateLogs()
{
    // Write PHY state log
    if (!phyStateFile.is_open()) {
        phyStateFile.open("phy_state_log.csv", std::ios::app);
        if (phyStateFile.tellp() == 0) {
            // Write header if file is new
            phyStateFile << "Timestamp_s,NodeID,MACAddress,PHY_State,SNR_dB\n";
        }
    }
    
    if (phyStateFile.is_open()) {
        for (const auto& entry : g_phyStateLog) {
            phyStateFile << std::fixed << std::setprecision(9)
                         << entry.timestamp.GetSeconds() << ","
                         << entry.nodeId << ","
                         << entry.macAddress << ","
                         << entry.state << ","
                         << entry.snr << "\n";
        }
        phyStateFile.flush();
    }
    
    // Clear PHY log after writing
    g_phyStateLog.clear();
    
    // Write MAC state log
    if (!macStateFile.is_open()) {
        macStateFile.open("mac_state_log.csv", std::ios::app);
        if (macStateFile.tellp() == 0) {
            // Write header if file is new
            macStateFile << "Timestamp_s,NodeID,MACAddress,MAC_State,SNR_dB\n";
        }
    }
    
    if (macStateFile.is_open()) {
        for (const auto& entry : g_macStateLog) {
            macStateFile << std::fixed << std::setprecision(9)
                         << entry.timestamp.GetSeconds() << ","
                         << entry.nodeId << ","
                         << entry.macAddress << ","
                         << entry.state << ","
                         << entry.snr << "\n";
        }
        macStateFile.flush();
    }
    
    // Clear MAC log after writing
    g_macStateLog.clear();
    
    // Schedule next periodic write
    Simulator::Schedule(MilliSeconds(500), &PeriodicWritePhyMacStateLogs);
}

// Function to close files at end of simulation
void ClosePhyMacStateFiles()
{
    // Write any remaining data
    if (phyStateFile.is_open()) {
        for (const auto& entry : g_phyStateLog) {
            phyStateFile << std::fixed << std::setprecision(9)
                         << entry.timestamp.GetSeconds() << ","
                         << entry.nodeId << ","
                         << entry.macAddress << ","
                         << entry.state << ","
                         << entry.snr << "\n";
        }
        phyStateFile.close();
        std::cout << "PHY state log closed. Total entries written." << std::endl;
    }
    
    if (macStateFile.is_open()) {
        for (const auto& entry : g_macStateLog) {
            macStateFile << std::fixed << std::setprecision(9)
                         << entry.timestamp.GetSeconds() << ","
                         << entry.nodeId << ","
                         << entry.macAddress << ","
                         << entry.state << ","
                         << entry.snr << "\n";
        }
        macStateFile.close();
        std::cout << "MAC state log closed. Total entries written." << std::endl;
    }
}
// Add these global variables
std::ofstream phyStateTxtFile;
std::ofstream macStateTxtFile;
bool phyStateTxtInitialized = false;
bool macStateTxtInitialized = false;

// Function to initialize text files
void InitializePhyMacStateTxtFiles()
{
    phyStateTxtFile.open("phy_state_log.txt", std::ios::out);
    macStateTxtFile.open("mac_state_log.txt", std::ios::out);
    
    if (phyStateTxtFile.is_open()) {
        phyStateTxtFile << "=== PHY STATE LOG ===\n";
        phyStateTxtFile << "Timestamp(s) | NodeID | MAC Address | PHY State | SNR(dB)\n";
        phyStateTxtFile << "---------------------------------------------------------\n";
        phyStateTxtInitialized = true;
    }
    
    if (macStateTxtFile.is_open()) {
        macStateTxtFile << "=== MAC STATE LOG ===\n";
        macStateTxtFile << "Timestamp(s) | NodeID | MAC Address | MAC State | SNR(dB)\n";
        macStateTxtFile << "---------------------------------------------------------\n";
        macStateTxtInitialized = true;
    }
    
    std::cout << "PHY and MAC state text files initialized" << std::endl;
}

// Function to write PHY and MAC state logs every 3 seconds
void PeriodicWritePhyMacStateLogsTxt()
{
    Time now = Simulator::Now();
    
    // Write PHY state log if there are entries
    if (!g_phyStateLog.empty() && phyStateTxtFile.is_open()) {
        phyStateTxtFile << "=== Time: " << now.GetSeconds() << "s ===\n";
        for (const auto& entry : g_phyStateLog) {
            phyStateTxtFile << std::fixed << std::setprecision(3) 
                           << std::setw(10) << entry.timestamp.GetSeconds() << " | "
                           << std::setw(6) << entry.nodeId << " | "
                           << std::setw(17) << entry.macAddress << " | "
                           << std::setw(10) << entry.state << " | "
                           << std::setw(8) << std::setprecision(2) << entry.snr << "\n";
        }
        phyStateTxtFile << "--- End of PHY states ---\n";
        phyStateTxtFile.flush();
    }
    
    // Write MAC state log if there are entries
    if (!g_macStateLog.empty() && macStateTxtFile.is_open()) {
        macStateTxtFile << "=== Time: " << now.GetSeconds() << "s ===\n";
        for (const auto& entry : g_macStateLog) {
            macStateTxtFile << std::fixed << std::setprecision(3) 
                           << std::setw(10) << entry.timestamp.GetSeconds() << " | "
                           << std::setw(6) << entry.nodeId << " | "
                           << std::setw(17) << entry.macAddress << " | "
                           << std::setw(10) << entry.state << " | "
                           << std::setw(8) << std::setprecision(2) << entry.snr << "\n";
        }
        macStateTxtFile << "--- End of MAC states ---\n";
        macStateTxtFile.flush();
    }
    
    // Clear the logs after writing
    g_phyStateLog.clear();
    g_macStateLog.clear();
    
    std::cout << "PHY/MAC state logs written at " << now.GetSeconds() << "s" << std::endl;
    
    // Schedule next write in 3 seconds
    Simulator::Schedule(Seconds(3.0), &PeriodicWritePhyMacStateLogsTxt);
}

// Function to close text files
void ClosePhyMacStateTxtFiles()
{
    // Write any remaining data before closing
    if (!g_phyStateLog.empty() && phyStateTxtFile.is_open()) {
        phyStateTxtFile << "=== FINAL DUMP - Time: " << Simulator::Now().GetSeconds() << "s ===\n";
        for (const auto& entry : g_phyStateLog) {
            phyStateTxtFile << std::fixed << std::setprecision(3) 
                           << std::setw(10) << entry.timestamp.GetSeconds() << " | "
                           << std::setw(6) << entry.nodeId << " | "
                           << std::setw(17) << entry.macAddress << " | "
                           << std::setw(10) << entry.state << " | "
                           << std::setw(8) << std::setprecision(2) << entry.snr << "\n";
        }
    }
    
    if (!g_macStateLog.empty() && macStateTxtFile.is_open()) {
        macStateTxtFile << "=== FINAL DUMP - Time: " << Simulator::Now().GetSeconds() << "s ===\n";
        for (const auto& entry : g_macStateLog) {
            macStateTxtFile << std::fixed << std::setprecision(3) 
                           << std::setw(10) << entry.timestamp.GetSeconds() << " | "
                           << std::setw(6) << entry.nodeId << " | "
                           << std::setw(17) << entry.macAddress << " | "
                           << std::setw(10) << entry.state << " | "
                           << std::setw(8) << std::setprecision(2) << entry.snr << "\n";
        }
    }
    
    if (phyStateTxtFile.is_open()) {
        phyStateTxtFile << "=== END OF PHY LOG ===\n";
        phyStateTxtFile.close();
        std::cout << "PHY state text log closed" << std::endl;
    }
    
    if (macStateTxtFile.is_open()) {
        macStateTxtFile << "=== END OF MAC LOG ===\n";
        macStateTxtFile.close();
        std::cout << "MAC state text log closed" << std::endl;
    }
}

void MacTxDropTrace(std::string context, Ptr<const Packet> packet)
{
    std::cout << "MacTxDrop at " << context << " Time: " << Simulator::Now().GetSeconds() 
              << "s, Packet UID: " << packet->GetUid() 
              << ", Size: " << packet->GetSize() << " bytes" << std::endl;
}

void MacRxDropTrace(std::string context, Ptr<const Packet> packet)
{
    std::cout << "MacRxDrop at " << context << " Time: " << Simulator::Now().GetSeconds() 
              << "s, Packet UID: " << packet->GetUid() 
              << ", Size: " << packet->GetSize() << " bytes" << std::endl;
}
//ASSSOCI
// ================= ASSOCIATION LOGGING GLOBALS =================

std::ofstream g_assocFile;
std::set<uint32_t> g_associated; // Track which STAs are currently associated

// Parse Node ID from context string
uint32_t ContextToNodeId(std::string context)
{
    std::string sub = context.substr(10);
    uint32_t pos = sub.find("/Device");
    return std::stoi(sub.substr(0, pos));
}

// Parse MAC Address from context string
Mac48Address ContextToMac(std::string context)
{
    std::string sub = context.substr(10);
    uint32_t pos = sub.find("/Device");
    uint32_t nodeId = std::stoi(sub.substr(0, pos));
    Ptr<Node> n = NodeList::GetNode(nodeId);
    Ptr<WifiNetDevice> d;
    // Find the WifiNetDevice on this node
    for (uint32_t i = 0; i < n->GetNDevices(); i++)
    {
        d = n->GetDevice(i)->GetObject<WifiNetDevice>();
        if (d)
        {
            break;
        }
    }
    if (!d) return Mac48Address("00:00:00:00:00:00");
    return Mac48Address::ConvertFrom(d->GetAddress());
}

// Callback for Association
void AssociationLog(std::string context, Mac48Address bssid)
{
    uint32_t nodeId = ContextToNodeId(context);
    Mac48Address staMac = ContextToMac(context);
    
    // Log to console
    std::cout << "Association: Time=" << Simulator::Now().GetSeconds() 
              << "s | Node=" << nodeId 
              << " | STA_MAC=" << staMac
              << " | AP_MAC=" << bssid << std::endl;
    
    // Track association state
    g_associated.insert(nodeId);
    
    // Write to CSV
    if (g_assocFile.is_open())
    {
        g_assocFile << std::fixed << std::setprecision(6)
                   << Simulator::Now().GetSeconds() << ","
                   << nodeId << ","
                   << staMac << ","
                   << bssid << ","
                   << "ASSOCIATED" << std::endl;
    }
}

// Callback for Disassociation
// MODIFIED FROM BIANCHI: Logs event instead of crashing
void DisassociationLog(std::string context, Mac48Address bssid)
{
    uint32_t nodeId = ContextToNodeId(context);
    Mac48Address staMac = ContextToMac(context);
    
    // Log to console
    std::cout << "Disassociation: Time=" << Simulator::Now().GetSeconds() 
              << "s | Node=" << nodeId 
              << " | STA_MAC=" << staMac
              << " | AP_MAC=" << bssid << std::endl;
    
    // Remove from tracking set
    g_associated.erase(nodeId);
    
    // Write to CSV (Crucial step for your requirement)
    if (g_assocFile.is_open())
    {
        g_assocFile << std::fixed << std::setprecision(6)
                   << Simulator::Now().GetSeconds() << ","
                   << nodeId << ","
                   << staMac << ","
                   << bssid << ","
                   << "DISASSOCIATED" << std::endl;
    }
}

void InitializeAssociationTracking()
{
    g_assocFile.open("association_log.csv");
    if (g_assocFile.is_open())
    {
        g_assocFile << "Timestamp_s,NodeID,STA_MAC,AP_MAC,Event_Type" << std::endl;
        std::cout << "Association tracking initialized." << std::endl;
    }
}

void CloseAssociationTracking()
{
    if (g_assocFile.is_open())
    {
        g_assocFile.close();
    }
}

void ConnectAssociationTraces()
{
    // Connect to the specific StaWifiMac trace sources
    // This path works for generic STA installations
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/Assoc",
                    MakeCallback(&AssociationLog));
    
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc",
                    MakeCallback(&DisassociationLog));
}
void VerifyNetworkConnectivity(uint32_t numAPs, uint32_t numClients,
                                Ipv4InterfaceContainer apInterfaces,
                                Ipv4InterfaceContainer staInterfaces) {
    std::cout << "\n=== NETWORK CONNECTIVITY VERIFICATION ===" << std::endl;
    
    // Check AP addresses
    for (uint32_t i = 0; i < numAPs; ++i) {
        std::cout << "AP " << i << " IP: " << apInterfaces.GetAddress(i) << std::endl;
    }
    
    // Check STA addresses
    for (uint32_t i = 0; i < numClients; ++i) {
        std::cout << "STA " << (i+1) << " IP: " << staInterfaces.GetAddress(i) << std::endl;
    }
    
    // Verify routing table entries
    Ptr<OutputStreamWrapper> routingStream = 
        Create<OutputStreamWrapper>("routing-tables.txt", std::ios::out);
    Ipv4GlobalRoutingHelper globalRouting;
    globalRouting.PrintRoutingTableAllAt(Seconds(2.0), routingStream);
    
    std::cout << "Routing tables printed to routing-tables.txt" << std::endl;
}
// In your main() after socket creation, add trace:
void MonitorTcpWindow(uint32_t oldValue, uint32_t newValue) {
    if (newValue == 0) {
        std::cout << "WARNING: TCP Zero Window detected at " 
                  << Simulator::Now().GetSeconds() << "s" << std::endl;
    }
}


int main(int argc, char *argv[])
{
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (1460)); 
    //dodantno novo
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(419430004)); // 4MB 419430004
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(419430004));//bilo je 4194304
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(300));//30 1, 300 2
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpVeno")); //TcpHybla

    std::cout << "\n=== Enabling WifiPhyRxTraceHelper ===" << std::endl;
    uint32_t runNumber =1;  // Different run numbers give different random sequences,1,2,3
    uint32_t seed = 2;      // Base seed for random number generation
    //const uint32_t numClients = 4;
    //const uint32_t numAPs = 1;
    //const uint32_t maxClientsPerRow = 6;//bilo 6
    //const uint32_t maxRows = 4;
    uint32_t numClients = 12;
    uint32_t numAPs = 1;
    uint32_t maxClientsPerRow = 6;
    uint32_t maxRows = 4;
    //./ns3 run "TestDashV16_42 --numClients=8 --runNumber=5 --seed=10",
    //
    CommandLine cmd;
    cmd.AddValue("runNumber", "Different run numbers for random sequences", runNumber);
    cmd.AddValue("seed", "Base seed for random number generation", seed);
    cmd.AddValue("numClients", "Number of client nodes", numClients);
    cmd.AddValue("numAPs", "Number of Access Points", numAPs);
    cmd.AddValue("maxClientsPerRow", "Max clients in a single row", maxClientsPerRow);
    cmd.AddValue("maxRows", "Max rows in the grid", maxRows);
    cmd.Parse(argc, argv);
        // Set unique random seed for this run
    RngSeedManager::SetSeed(seed);  // Fixed seed for reproducibility across run sets
    RngSeedManager::SetRun(runNumber);
    // Add this after your WifiHelper configuration
    //dodano ispod
    std::cout << "=== STARTING NS-3 SIMULATION WITH GUROBI OPTIMIZATION ===" << std::endl;
    
    // Calculate AP positions (these are fixed)
    std::vector<Vector> apPositions = CalculateEdgeAPPositions(maxClientsPerRow);
    
    // === STEP 2: RUN GUROBI OPTIMIZATION ===
    std::string outputFile = "optimization_results.csv";
    
    std::cout << "\n=== REAL-TIME MEASUREMENT-BASED OPTIMIZATION ===" << std::endl;

    if (g_useRealTimeOptimization) {
        std::cout << "Measurement phase duration: " << g_measurementPhaseDuration << " seconds" << std::endl;
        
        // Schedule optimization trigger after measurement phase
        g_optimizationCallTime = g_measurementPhaseDuration + 1.0;
        Simulator::Schedule(Seconds(g_optimizationCallTime), &TriggerOptimization);
        
        std::cout << "Optimization will be triggered at: " << g_optimizationCallTime << "s" << std::endl;
    }
    /*
    //scheduling-> If optimization is not used COMMENT IT OUT
    if (g_useRealTimeOptimization) {
        std::cout << "Periodic optimization interval set to: " << g_measurementPhaseDuration << " seconds" << std::endl;
        
        // The first optimization will be triggered after the first measurement duration.
        // We can start the first measurement immediately and call the first optimization 
        // at g_measurementPhaseDuration seconds.
        g_optimizationCallTime = g_measurementPhaseDuration; // First call at 25.0s
        
        // Start the periodic optimization loop
        Simulator::Schedule(Seconds(g_optimizationCallTime), &SchedulePeriodicOptimization);
        
        std::cout << "First optimization scheduled at: " << g_optimizationCallTime << "s" << std::endl;
    }
    */
    std::cout << "\n=== STARTING NS-3 NETWORK SIMULATION ===" << std::endl;
    InitializeDataFiles();

    // Create nodes: 4 APs + 1 client STAs + 4 server nodes
    NodeContainer nodes;
    nodes.Create(numAPs + numClients);

    // Create separate nodes for the servers
    NodeContainer serverNodes;
    serverNodes.Create(numAPs);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    LogComponentEnable("TiledDashServer", LOG_LEVEL_INFO);
    LogComponentEnable("TiledDashClient", LOG_LEVEL_INFO);


Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
//SpectrumWifiPhyHelper phy(1);  // 3 links per device (MLD) MLO operation
// Add delay model FIRST (this is critical for HE PHY)
Ptr<ConstantSpeedPropagationDelayModel> delayModel = 
    CreateObject<ConstantSpeedPropagationDelayModel>();
spectrumChannel->SetPropagationDelayModel(delayModel);
///*
// Main path loss
Ptr<LogDistancePropagationLossModel> pathLoss = CreateObject<LogDistancePropagationLossModel>();
pathLoss->SetAttribute("Exponent", DoubleValue(3.0)); 
pathLoss->SetAttribute("ReferenceLoss", DoubleValue(46.67)); //5.22GHz
pathLoss->SetAttribute("ReferenceDistance", DoubleValue(1.0));

// Shadowing using RandomPropagationLossModel with Normal distribution
Ptr<RandomPropagationLossModel> shadowingLoss = CreateObject<RandomPropagationLossModel>();
Ptr<NormalRandomVariable> shadowingRV = CreateObject<NormalRandomVariable>();
shadowingRV->SetAttribute("Mean", DoubleValue(0.0));
shadowingRV->SetAttribute("Variance", DoubleValue(1.0));  // (testing with 0.5 as well)
shadowingRV->SetAttribute("Bound", DoubleValue(2.0));  // n*variance, 0.5*2= 1 around 1dB all values are defind
shadowingLoss->SetAttribute("Variable", PointerValue(shadowingRV));
// 

// Nakagami fading
Ptr<NakagamiPropagationLossModel> nakagamiFading = CreateObject<NakagamiPropagationLossModel>();
nakagamiFading->SetAttribute("m0", DoubleValue(10.0)); //10
nakagamiFading->SetAttribute("m1", DoubleValue(2.0));//2
nakagamiFading->SetAttribute("m2", DoubleValue(1.0));//1
nakagamiFading->SetAttribute("Distance1", DoubleValue(10.0));
nakagamiFading->SetAttribute("Distance2", DoubleValue(20.0));

// Chain: Path Loss -> Shadowing -> Fading

//pathLoss->SetNext(nakagamiFading);

pathLoss->SetNext(shadowingLoss);
shadowingLoss->SetNext(nakagamiFading);
spectrumChannel->AddPropagationLossModel(pathLoss);
    /*
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager",
                                "UpdateStatistics", TimeValue(MilliSeconds(50)), //bilo 10, bilo 50 working case
                                //"UpdateStatistics",TimeValue(MilliSeconds(100)),//default ja ubrzao da radi , to je default 0.1 seconds
                                "LookAroundRate", UintegerValue(0), //bilo 10 
                                "EWMA",UintegerValue(80),//bilo 75
                                "SampleColumn",UintegerValue(10),
                                "PacketLength",UintegerValue(1460));
                                //"NonUnicastMode", StringValue (phyRate));
                                //"RtsCtsThreshold",UintegerValue(999999)); //bilo od komm, bilo 2200, "RtsCtsThreshold",UintegerValue(2200)     
    */
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager",
                                "UpdateStatistics", TimeValue(MilliSeconds(100)), //bilo 10, bilo 50 working case
                                //"UpdateStatistics",TimeValue(MilliSeconds(100)),//default ja ubrzao da radi , to je default 0.1 seconds
                                "LookAroundRate", UintegerValue(10), //bilo 10 
                                "EWMA",UintegerValue(75),//bilo 75
                                "SampleColumn",UintegerValue(10),
                                "PacketLength",UintegerValue(1460));
                                //"NonUnicastMode", StringValue (phyRate));
                                //"RtsCtsThreshold",UintegerValue(999999)); //bilo od komm, bilo 2200, "RtsCtsThreshold",UintegerValue(2200)  
    //wifi.SetRemoteStationManager("ns3::ThompsonSamplingManager");  
    WifiMacHelper mac;
    Ssid ssid = Ssid("ns3-80211ax");

    NetDeviceContainer apDevices;
    NodeContainer apNodes;
    std::vector<std::string> channelSettings = {
        //"{50, 160, BAND_5GHZ, 0}", 
        //"{114, 160, BAND_5GHZ, 0}" 
        "{42,80,BAND_5GHZ,0}",
        //"{122,80,BAND_5GHZ,0}"
    };
    //
    std::cout << "\n=== OPTIMIZED CLIENT-AP ASSIGNMENTS ===" << std::endl;
    std::map<uint32_t, std::vector<uint32_t>> optimizedAPClientMap;
    
    // Show optimization results
    for (uint32_t i = 1; i <= numClients; ++i) {
        uint32_t assignedAP = GetOptimizedAPAssignment(i);
        Vector clientPos = GetOptimizedClientPosition(i);
        optimizedAPClientMap[assignedAP].push_back(i);
        
        std::cout << "Client " << i << ": Optimally assigned to AP " << assignedAP 
                  << " at position (" << std::fixed << std::setprecision(2)
                  << clientPos.x << ", " << clientPos.y << ", " << clientPos.z << ")" << std::endl;
    }
    //

    std::cout << "\n=== EDGE AP POSITIONING SETUP (4 APs) WITH BSS COLORING ===" << std::endl;
     for (uint32_t apIndex = 0; apIndex < numAPs; ++apIndex) {
        apNodes.Add(nodes.Get(apIndex));
        SpectrumWifiPhyHelper phy;
        phy.Set("ChannelSettings",StringValue(channelSettings[apIndex]));
        phy.SetChannel(spectrumChannel);
        //Does not work with these values below at all. Low tx power?
        phy.Set("TxPowerStart", DoubleValue(txPower_dBm)); //16
        phy.Set("TxPowerEnd", DoubleValue(txPower_dBm)); //16-reference ones
        phy.Set("RxNoiseFigure", DoubleValue(noiseFigure_dB));
        mac.SetType("ns3::ApWifiMac",
                    "Ssid", SsidValue(ssid),
                    "QosSupported", BooleanValue(false),
                    "EnableBeaconJitter", BooleanValue(true), //bilo true
                    "BeaconGeneration", BooleanValue(true)); 

        // OPTIMIZED MU SCHEDULER FOR 16 USERS
        mac.SetMultiUserScheduler("ns3::RrMultiUserScheduler",
                                "NStations", UintegerValue(numClients), // define num client+1 additional,bilo numClients
                                "EnableTxopSharing", BooleanValue(true),// true
                                "EnableUlOfdma", BooleanValue(true), //true
                                "ForceDlOfdma", BooleanValue(false), // Force DL OFDMA for better efficiency
                                "EnableBsrp", BooleanValue(true),   // Enable buffer status reports, bilo false--poveca brzinu prenosa,TRUE BILO
                                "UseCentral26TonesRus", BooleanValue(false), // Save central tones
                                "MaxCredits", TimeValue(Seconds(1.0)),
                                "UlPsduSize", UintegerValue(500));  // Smaller UL PSDU for more users
        //Config::SetDefault("ns3::WifiDefaultAckManager::DlMuAckSequenceType",EnumValue(WifiAcknowledgment::DL_MU_AGGREGATE_TF));
        NetDeviceContainer singleApDevice = wifi.Install(phy, mac, nodes.Get(apIndex));
        apDevices.Add(singleApDevice);
        
        //apPhy.EnablePcap("ap-debug", apDevices);
        Vector apPos = apPositions[apIndex];
        std::string position;
        if (apIndex == 0) position = "Row 1, Right";
        else if (apIndex == 1) position = "Row 5, Left"; 
        else if (apIndex == 2) position = "Row 1, Left";
        else position = "Row 5, Right";
        
        std::cout << "AP " << apIndex << " (" << position << ") on " 
                  << channelSettings[apIndex] << " at (" 
                  << apPos.x << ", " << apPos.y << ", " << apPos.z << ")" << std::endl;
    }
    
    NodeContainer staNodes;
    for (uint32_t i = 0; i < numClients; ++i) {
        staNodes.Add(nodes.Get(numAPs + i));
    }

    // Create STA devices with adaptive channel selection
    NetDeviceContainer staDevices;
    
    for (uint32_t i = 0; i < numClients; ++i) {
        // Determine which AP this client will connect to
        uint32_t targetAP = DetermineClosestAP(i + 1, apPositions, maxClientsPerRow);
        SpectrumWifiPhyHelper phy;
        phy.Set("ChannelSettings",StringValue(channelSettings[targetAP]));
        phy.SetChannel(spectrumChannel);
        //Does not work with these values below at all. Low tx power?
        phy.Set("TxPowerStart", DoubleValue(txPower_dBm)); //16
        phy.Set("TxPowerEnd", DoubleValue(txPower_dBm)); //16-reference ones
        phy.Set("RxNoiseFigure", DoubleValue(noiseFigure_dB));
        // Configure STA MAC
        mac.SetType("ns3::StaWifiMac",
                    "Ssid", SsidValue(ssid),
                    "QosSupported", BooleanValue(false),
                    "MaxMissedBeacons",UintegerValue(20), //povecao broj MaxMixedBeacons na 20 i proslo, 10 defualt
                    //"MpduBufferSize", UintegerValue(256),
                    "ActiveProbing", BooleanValue(false)); // More tolerant to missed beacons, bilo false
        NetDeviceContainer singleStaDevice = wifi.Install(phy, mac, nodes.Get(numAPs + i));

        staDevices.Add(singleStaDevice);
        
        std::cout << "STA " << (i+1) << " configured for AP " << targetAP 
                  << " on " << channelSettings[targetAP] << std::endl;
                     
        //staPhy.EnablePcap("sta-debug", staDevices);
    }
    InitializeAssociationTracking();
    ConnectAssociationTraces();
    // === MAC ADDRESS TO USER ID MAPPING ===
    for (uint32_t i = 0; i < staDevices.GetN(); ++i) {
        Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        Mac48Address mac = dev->GetMac()->GetAddress();
        macToUserId[mac] = nextUserId;
        std::cout << "User " << nextUserId << " MAC: " << mac << std::endl;
        nextUserId++;
    }
//-----ENERGYYY
std::cout << "\n=== IMMEDIATE STATE TRACE CONNECTION ===" << std::endl;

// Force immediate connection to state traces
for (uint32_t i = 0; i < apDevices.GetN(); ++i) {
    Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(apDevices.Get(i));
    if (apDevice) {
        Ptr<WifiPhy> phy = apDevice->GetPhy();
        if (phy) {
            uint32_t nodeId = apNodes.Get(i)->GetId();
            bool connected = phy->TraceConnectWithoutContext("State", 
                MakeBoundCallback(&WifiStateChangeCallback, nodeId));
            
            WifiPhyState initialState = phy->GetState()->GetState();
            std::cout << "AP " << i << " (Node " << nodeId << "): " 
                      << (connected ? "CONNECTED" : "FAILED") 
                      << " - Initial state: " << WifiStateToString(initialState) << std::endl;
            
            // Initialize metrics
            g_energyMetrics[nodeId].currentState = initialState;
            g_energyMetrics[nodeId].lastState = initialState;
            g_energyMetrics[nodeId].lastStateChange = Simulator::Now();
            
            // Also connect to packet traces for debugging (using a proper function)
            //phy->TraceConnectWithoutContext("PhyTxBegin", 
            //    MakeCallback(&PhyTxBeginCallback));
        }
    }
}

for (uint32_t i = 0; i < staDevices.GetN(); ++i) {
    Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(staDevices.Get(i));
    if (staDevice) {
        Ptr<WifiPhy> phy = staDevice->GetPhy();
        if (phy) {
            uint32_t nodeId = staNodes.Get(i)->GetId();
            bool connected = phy->TraceConnectWithoutContext("State", 
                MakeBoundCallback(&WifiStateChangeCallback, nodeId));
            
            WifiPhyState initialState = phy->GetState()->GetState();
            std::cout << "STA " << i << " (Node " << nodeId << "): " 
                      << (connected ? "CONNECTED" : "FAILED") 
                      << " - Initial state: " << WifiStateToString(initialState) << std::endl;
            
            // Initialize metrics
            g_energyMetrics[nodeId].currentState = initialState;
            g_energyMetrics[nodeId].lastState = initialState;
            g_energyMetrics[nodeId].lastStateChange = Simulator::Now();
        }
    }
}
//------ENERGY
    InternetStackHelper stack;
    stack.Install(nodes);
    stack.Install(serverNodes);

    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FqCoDelQueueDisc", "MaxSize", StringValue("2000000000000p")); 
    // === MOBILITY SETUP ===
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    
    // Add AP positions
    for (uint32_t apIndex = 0; apIndex < numAPs; ++apIndex) {
        positionAlloc->Add(apPositions[apIndex]);
    }
    
    // Add client positions and show AP assignments
    std::cout << "\n=== CLIENT POSITIONING AND AP ASSIGNMENTS ===" << std::endl;
    std::map<uint32_t, std::vector<uint32_t>> apClientMap;
    
    for (uint32_t i = 1; i <= numClients; ++i) {
        Vector clientPos = CalculateCinemaPosition(i, maxClientsPerRow);
        //Vector clientPos = GetOptimizedClientPosition(i);
        positionAlloc->Add(clientPos);
        uint32_t closestAP = DetermineClosestAP(i, apPositions, maxClientsPerRow);
        //uint32_t closestAP = GetOptimizedAPAssignment(i);
        uint32_t userNodeId = numAPs + i - 1;
        userMetrics[userNodeId].connectedAP = closestAP;
        apClientMap[closestAP].push_back(i);
        
        double distance = CalculateDistanceAP(apPositions[closestAP], clientPos);
        
        std::cout << "Client " << i << ": (" << std::fixed << std::setprecision(2) 
                  << clientPos.x << ", " << clientPos.y << ", " << clientPos.z 
                  << ") -> AP " << closestAP << " (Channel " 
                  << channelSettings[closestAP] << "), Distance: " 
                  << distance << "m" << std::endl;
    }

    // Show load balancing across APs
    std::cout << "\n=== LOAD BALANCING ACROSS APs ===" << std::endl;
    for (uint32_t apIndex = 0; apIndex < numAPs; ++apIndex) {
        std::cout << "AP " << apIndex << " serves " << apClientMap[apIndex].size() 
                  << " clients (Load: " << std::fixed << std::setprecision(1)
                  << (100.0 * apClientMap[apIndex].size() / numClients) << "%)" << std::endl;
    }

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    MobilityHelper serverMobility;
    serverMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    serverMobility.Install(serverNodes);
 
    //POINT TO POINT LINK 
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("200Mbps"));
    p2p.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", QueueSizeValue(QueueSize("20000000000p")));

    NetDeviceContainer serverDevices;
    std::cout << "\n=== WIRED SERVER BACKHAUL CONFIGURATION (P2P) ===" << std::endl;
    
    Ipv4AddressHelper serverAddress;
    for (uint32_t i = 0; i < numAPs; ++i) {
        // Create a P2P link for each AP-Server pair
        NodeContainer p2pNodes;
        p2pNodes.Add(apNodes.Get(i));
        p2pNodes.Add(serverNodes.Get(i));

        // Install P2P devices
        NetDeviceContainer p2pDevices = p2p.Install(p2pNodes);
        
        // Install Traffic Control (FqCoDel) on the P2P devices
        tch.Install(p2pDevices);
        
        // Add the server's device (index 1) to the backhaul container
        serverDevices.Add(p2pDevices.Get(1)); 
        
        // Assign a unique IP subnet for this dedicated P2P link
        std::string subnet = "10.1." + std::to_string(i) + ".0";
        serverAddress.SetBase(subnet.c_str(), "255.255.255.0");
        Ipv4InterfaceContainer p2pInterfaces = serverAddress.Assign(p2pDevices);
        
        std::cout << "AP " << (i+1) << " connected to Server " << (i+1) << " via P2P link." << std::endl;
        std::cout << "  AP IP: " << p2pInterfaces.GetAddress(0) << std::endl;
        std::cout << "  Server IP: " << p2pInterfaces.GetAddress(1) << std::endl;
    }

    // Rest of your IP assignment and routing
    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterfaces = address.Assign(apDevices);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);
    
    NeighborCacheHelper nbCache;
    nbCache.PopulateNeighborCache();
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    double simTime=1050;//bilo 120 za 9 seg, za 60 320, 550 malo, bilo 1050, 1350
struct ClientConfig {
    uint32_t nodeIndex;
    double startTime;
    uint32_t clientId;
    Ipv4Address serverAddress;
    uint16_t serverPort;
    uint32_t closestAP;
};

// In your main function, replace the client setup:

// Server setup (unchanged)
ApplicationContainer serverApps;
uint16_t basePort=80;
for (uint32_t apIndex = 0; apIndex < numAPs; ++apIndex) {
    TiledDashServerHelper serverHelper;
    serverHelper.SetAttribute("LocalAddress", AddressValue(Ipv4Address::GetAny()));
    serverHelper.SetAttribute("LocalPort", UintegerValue(basePort + apIndex));
    serverHelper.SetAttribute("ServerFolder", StringValue("/Desktop/ns-allinone-3.44/ns-3.44/videoFiles3")); //File Path to your videoFiles folder, example provided
    ApplicationContainer singleServerApp = serverHelper.Install(serverNodes.Get(apIndex));
    singleServerApp.Start(Seconds(2.0));
    singleServerApp.Stop(Seconds(simTime));
    serverApps.Add(singleServerApp);
}

// CLIENT SETUP with deferred start
uint32_t numSockets = 1;
ApplicationContainer allClientApps;
std::vector<ClientConfig> clientConfigs;

// STEP 1: Install all clients immediately but with deferred start flag
for (uint32_t i = 1; i <= numClients; ++i) { 
    uint32_t userNodeId = numAPs + i - 1;
    uint32_t closestAP = userMetrics[userNodeId].connectedAP;
    
    // Get server node and IP
    Ptr<Node> serverNode = serverNodes.Get(closestAP);
    Ptr<Ipv4> serverIpv4 = serverNode->GetObject<Ipv4>();
    Ipv4Address serverAddressVal = serverIpv4->GetAddress(1, 0).GetLocal();
    uint16_t serverPort = basePort + closestAP;
    //uint16_t clientPort = basePort + i-1;
    std::cout << "Setting up Client " << i << " to connect to " 
              << serverAddressVal << ":" << serverPort << std::endl;

    TiledDashClientHelper clientHelper(InetSocketAddress(serverAddressVal, serverPort));
    clientHelper.SetAttribute("ServerPort", UintegerValue(serverPort));
    clientHelper.SetAttribute("VideoName", StringValue("360_Google_DoodlesSpotlight_Stories_Back_to_the_Moon_6x4_1000_dash"));
    clientHelper.SetAttribute("NumberOfSockets", UintegerValue(numSockets));
    clientHelper.SetAttribute("SegmentDuration", TimeValue(Seconds(1)));
    clientHelper.SetAttribute("VideoDuration", TimeValue(Seconds(60))); //bilo 60
    clientHelper.SetAttribute("VideoSegments", UintegerValue(61)); //bilo 61
    clientHelper.SetAttribute("HorTiles", UintegerValue(6));
    clientHelper.SetAttribute("VerTiles", UintegerValue(4));
    clientHelper.SetAttribute("QualityRates", StringValue("5688888,8533333,12799999,19200000"));
    clientHelper.SetAttribute("PovFile", StringValue("Desktop/ns-allinone-3.44/ns-3.44/videoFiles3/pov3.txt")); //File Path to pov test File
    clientHelper.SetAttribute("MaxBufferDuration", TimeValue(Seconds(6)));
    clientHelper.SetAttribute("HorFovAngle", DoubleValue(110.0));
    clientHelper.SetAttribute("VerFovAngle", DoubleValue(96.0));
    clientHelper.SetAttribute("ProjectionType", EnumValue(EQUIRECTANGULAR));
    clientHelper.SetAttribute("FovTrackingAlgorithm", EnumValue(IDEAL_FOV));
    clientHelper.SetAttribute("FoVPredictionHorizon", TimeValue(Seconds(0.5)));
    clientHelper.SetAttribute("FoVTrackingHistory", TimeValue(Seconds(2)));
    clientHelper.SetAttribute("SensorRate", DoubleValue(120.0));
    clientHelper.SetAttribute("ThroughputEstimationMethod", EnumValue(EXPONENTIAL_AVERAGE));//AVERAGE,EXPONENTIAL_AVERAGE,IDEAL_TP
    clientHelper.SetAttribute("ThroughputSamplingMethod", EnumValue(TILE_BASED));
    //clientHelper.SetAttribute("SamplingInterval", TimeValue(Seconds(1)));
    clientHelper.SetAttribute("SamplingWindow", UintegerValue(10));
    clientHelper.SetAttribute("ExponentialWeight", DoubleValue(0.6));
    clientHelper.SetAttribute("RefreshRate", DoubleValue(30.0));
    clientHelper.SetAttribute("VideoMode", EnumValue(MODE_360));

    // Install the application NOW but don't start streaming yet
    ApplicationContainer clientApps = clientHelper.Install(staNodes.Get(i-1));
    Ptr<TiledDashClient> clientApp = DynamicCast<TiledDashClient>(clientApps.Get(0));
    
    // CRITICAL: Set deferred start flag BEFORE the app starts
    clientApp->SetDeferredStart(true);
    
    g_clientApplications.push_back(clientApp);
    allClientApps.Add(clientApps);
    
    // Store configuration for scheduled startup
    ClientConfig config;
    config.nodeIndex = i - 1;
    //0.8 working case, 0.1 initaly to he working case
    //ovdje za 1 clienta bilo 2.0 + (i-1)*0.8
    config.startTime = 2.0 + (i - 1) *0.8;  // 2.0 + (i - 1) *0.1//0.5; 
    config.clientId = i;
    config.serverAddress = serverAddressVal;
    config.serverPort = serverPort;
    config.closestAP = closestAP;
    clientConfigs.push_back(config);
    
    std::cout << "Client " << i << " installed but will start at " << config.startTime << "s" << std::endl;
}

// STEP 2: Schedule configuration for each client
for (size_t i = 0; i < clientConfigs.size(); ++i) {
    double configTime = clientConfigs[i].startTime;
    
    Simulator::Schedule(Seconds(configTime), [i, numSockets, seed]() {
        if (i < g_clientApplications.size()) {
            Ptr<TiledDashClient> clientApp = g_clientApplications[i];
            if (clientApp) {
                std::cout << "\n=== Configuring Client " << (i+1) << " at " 
                          << Simulator::Now().GetSeconds() << "s ===" << std::endl;
                
                Ptr<TiledDashClientNetworkManager> networkManager = clientApp->GetNetworkManager();
                Ptr<TiledDashClientNetworkTracker> networkTracker = clientApp->GetNetworkTracker();
                Ptr<TiledDashClientMediaBuffer> mediaBuffer = clientApp->GetMediaBuffer();
                Ptr<TiledDashClientMediaDecoder> mediaDecoder = clientApp->GetMediaDecoder();
                Ptr<TiledDashClientFovTracker> fovTracker = clientApp->GetFovTracker();
                Ptr<TiledDashClientTileRequester> tileRequester = clientApp->GetTileRequester();
                Ptr<TiledDashClientAbrEngine> abrEngine = clientApp->GetAbrEngine();
                
                if (networkManager && networkTracker) {
                    //Commented out only used for Time-Based
                    //networkManager->ActivateTimedThroughput(Seconds(1.0)); 
                    networkManager->SetNode(clientApp->GetNode());
                    networkManager->SetNumberOfSockets(numSockets);
                    //networkTracker->SetSamplingInterval(Seconds(1.0));
                    networkTracker->SetSamplingWindow(10);
                    networkTracker->SetExponentialWeight(0.8);
                    networkTracker->SetSamplingMethod(TILE_BASED);
                    networkTracker->SetEstimationMethod(EXPONENTIAL_AVERAGE);
                    networkTracker->SetClientId(MapPositionToGridIndex(i+1, 6));
                    std::cout << "   Network components configured" << std::endl;
                }
                
                if (mediaBuffer) {
                    mediaBuffer->SetAttribute("NTiles", UintegerValue(24)); 
                    mediaBuffer->SetAttribute("MinTiles", UintegerValue(24));
                    mediaBuffer->SetAttribute("MaxBufferedSegments", UintegerValue(6)); 
                    mediaBuffer->SetClientAndRunId(MapPositionToGridIndex(i+1, 6), seed);
                    std::cout << "   Media buffer configured" << std::endl;
                }
                
                if (mediaDecoder) {
                    mediaDecoder->SetAttribute("InitialBufferingDuration", TimeValue(Seconds(2)));
                    mediaDecoder->SetAttribute("RebufferingDuration", TimeValue(Seconds(2)));
                    mediaDecoder->SetClient(clientApp);
                    mediaDecoder->SetSegmentDuration(Seconds(1));
                    mediaDecoder->SetNTiles(24); 
                    std::vector<int> initialFovScores(24, 3);
                    mediaDecoder->SetFovScores(initialFovScores);
                    std::cout << "   Media decoder configured" << std::endl;
                }
                
                if (fovTracker) {
                    fovTracker->SetClient(clientApp);
                    fovTracker->SetPredictionFile("Desktop/ns-allinone-3.44/ns-3.44/videoFiles3/prediction2_1.txt"); // Path for FoV pov prediction file
                    std::cout << "   FOV tracker configured" << std::endl;
                }
                
                if (tileRequester) {
                    tileRequester->SetVideoName("360_Google_DoodlesSpotlight_Stories_Back_to_the_Moon_6x4_1000_dash"); //Name of the file
                    tileRequester->SetSegmentDuration(Seconds(1));
                    tileRequester->SetHorTiles(6);
                    tileRequester->SetVerTiles(4);
                    std::cout << "   Tile requester configured" << std::endl;
                }
                
                if (abrEngine) {
                    abrEngine->SetClient(clientApp);
                    abrEngine->SetAbrAlgorithm(AbrStrategy::Simple);//Simple bio
                    std::cout << "   ABR engine configured" << std::endl;
                }
                
                std::cout << "Configured client " << (i+1) << " - ready to start" << std::endl;
            }
        }
    });
}

// STEP 3: Schedule actual startup for each client
for (size_t i = 0; i < clientConfigs.size(); ++i) {
    double startTime = clientConfigs[i].startTime;
    
    Simulator::Schedule(Seconds(startTime), [i, &clientConfigs, &staInterfaces]() {
        if (i < g_clientApplications.size()) {
            Ptr<TiledDashClient> clientApp = g_clientApplications[i];
            if (clientApp) {
                std::cout << "\n>>> Starting Client " << (i+1) << " at " 
                          << Simulator::Now().GetSeconds() << "s <<<" << std::endl;
                
                // Actually start the streaming now
                clientApp->DeferredStartup();
                
                std::cout << "Client " << (i+1) << " (" << staInterfaces.GetAddress(i) 
                          << ") started streaming to Server " 
                          << clientConfigs[i].serverAddress << ":" 
                          << clientConfigs[i].serverPort << std::endl;
            }
        }
    });
}

// Set stop times
for (uint32_t i = 0; i < allClientApps.GetN(); ++i) {
    allClientApps.Get(i)->SetStopTime(Seconds(simTime));
}

// Print summary
std::cout << "\n=== Client Start Schedule ===" << std::endl;
for (size_t i = 0; i < clientConfigs.size(); ++i) {
    std::cout << "Client " << (i+1) << ": starts at " 
              << clientConfigs[i].startTime << "s" << std::endl;
}
std::cout << "============================\n" << std::endl;
//ENERGY
    //setup energy model
    // === ENERGY MODEL SETUP ===
    g_numAPs = numAPs;
    InitializeEnergyDataFile();

    std::cout << "\n=== ENERGY MODEL INITIALIZATION ===" << std::endl;

    BasicEnergySourceHelper energySourceHelper;
    energySourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(10000.0));
    energySourceHelper.Set("PeriodicEnergyUpdateInterval", TimeValue(MilliSeconds(500)));

    // Install energy sources
    EnergySourceContainer apEnergySources = energySourceHelper.Install(apNodes);
    EnergySourceContainer staEnergySources = energySourceHelper.Install(staNodes);

    // Create WiFi Radio Energy Models
   // Line 2786-2792
    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Set("IdleCurrentA", DoubleValue(0.273));      
    radioEnergyHelper.Set("CcaBusyCurrentA", DoubleValue(0.273));   
    //radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.438));        // updated based on the paper
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.394)); //testing for 10dBm 
    radioEnergyHelper.Set("RxCurrentA", DoubleValue(0.313));        
    radioEnergyHelper.Set("SwitchingCurrentA", DoubleValue(0.273)); 
    radioEnergyHelper.Set("SleepCurrentA", DoubleValue(0.033));     
    // Install energy models
    DeviceEnergyModelContainer apEnergyModels = radioEnergyHelper.Install(apDevices, apEnergySources);
    DeviceEnergyModelContainer staEnergyModels = radioEnergyHelper.Install(staDevices, staEnergySources);

    // Setup for APs
    for (uint32_t i = 0; i < numAPs; ++i) {
        uint32_t nodeId = apNodes.Get(i)->GetId();
        g_energyMetrics[nodeId] = EnergyMetrics();
        
        Ptr<WifiRadioEnergyModel> model = DynamicCast<WifiRadioEnergyModel>(apEnergyModels.Get(i));
        g_deviceEnergyModels[nodeId] = model;
        
        Ptr<EnergySource> source = apEnergySources.Get(i);
        g_energySources[nodeId] = source;
        
        if (model && source) {
            model->TraceConnectWithoutContext("TotalEnergyConsumption", 
                            MakeBoundCallback(&TotalEnergyConsumptionCallback, nodeId));
            
            source->TraceConnectWithoutContext("RemainingEnergy",
                            MakeBoundCallback(&RemainingEnergyCallback, nodeId));
            
            // Get initial state from energy model
            WifiPhyState initialState = model->GetCurrentState();
            g_energyMetrics[nodeId].currentState = initialState;
            g_energyMetrics[nodeId].lastState = initialState;
            g_energyMetrics[nodeId].lastStateChange = Simulator::Now();
            
            std::cout << "AP " << i << " (Node " << nodeId << ") energy model installed - Initial state: "
                    << WifiStateToString(initialState) << std::endl;
        }
    }

    // Setup for STAs
    for (uint32_t i = 0; i < numClients; ++i) {
        uint32_t nodeId = staNodes.Get(i)->GetId();
        g_energyMetrics[nodeId] = EnergyMetrics();
        
        Ptr<WifiRadioEnergyModel> model = DynamicCast<WifiRadioEnergyModel>(staEnergyModels.Get(i));
        g_deviceEnergyModels[nodeId] = model;
        
        Ptr<EnergySource> source = staEnergySources.Get(i);
        g_energySources[nodeId] = source;
        
        if (model && source) {
            model->TraceConnectWithoutContext("TotalEnergyConsumption",
                            MakeBoundCallback(&TotalEnergyConsumptionCallback, nodeId));
            
            source->TraceConnectWithoutContext("RemainingEnergy",
                            MakeBoundCallback(&RemainingEnergyCallback, nodeId));
            
            // Get initial state from energy model
            WifiPhyState initialState = model->GetCurrentState();
            g_energyMetrics[nodeId].currentState = initialState;
            g_energyMetrics[nodeId].lastState = initialState;
            g_energyMetrics[nodeId].lastStateChange = Simulator::Now();
            
            std::cout << "STA " << (i+1) << " (Node " << nodeId << ") energy model installed - Initial state: "
                    << WifiStateToString(initialState) << std::endl;
        }
    }
    
    // Start periodic energy logging
    // Start POLLING-BASED state detection (most reliable method)
    Simulator::Schedule(MilliSeconds(1), &PollEnergyStates);
    std::cout << "Polling-based energy state tracking started - checking every 1ms" << std::endl;

    // Start periodic energy logging
    Simulator::Schedule(MilliSeconds(10), &LogEnergyData);
    std::cout << "Energy monitoring started - logging every 10ms" << std::endl;
    //stop energy

    //ENERGY
    //tracing data
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/Phy/MonitorSnifferRx",
                                              MakeCallback(&MonitorSniffRx));
                                
    Simulator::Schedule(Seconds(3.0), &MonitorApplicationDataRate);


    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowmon = flowmonHelper.InstallAll();

    // Print summary near the end
    Simulator::Schedule(Seconds(simTime - 1.0), [flowmon, &flowmonHelper]() {
        flowmon->CheckForLostPackets();
        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
        std::map<FlowId, FlowMonitor::FlowStats> stats = flowmon->GetFlowStats();
        std::cout << "\n=== FlowMonitor summary at " << Simulator::Now().GetSeconds() << "s ===\n";
        for (auto &kv : stats) {
            FlowId fid = kv.first;
            FlowMonitor::FlowStats st = kv.second;
            std::cout << "Flow " << fid << ": txBytes=" << st.txBytes
                      << " rxBytes=" << st.rxBytes << " txPkts=" << st.txPackets
                      << " rxPkts=" << st.rxPackets << " drops=" << st.lostPackets<< std::endl;
                     // << " rtx=" << st.retransmissions << std::endl;
        }
    });

       
Config::Connect("/NodeList/0/DeviceList/0/$ns3::WifiNetDevice/Mac/MacTxDrop",
                MakeCallback(&MacTxDropTrace));

Config::Connect("/NodeList/0/DeviceList/0/$ns3::WifiNetDevice/Mac/MacRxDrop",
                MakeCallback(&MacRxDropTrace));
        // Connect trace sources (after all devices are installed)
VerifyNetworkConnectivity(numAPs, numClients, apInterfaces, staInterfaces);
    Simulator::Stop(Seconds(simTime+2));
    Simulator::Run();

    if (energyDataFile.is_open()) {
        energyDataFile.close();
        std::cout << "\nEnergy data file closed." << std::endl;
    }
    // === CSV EXPORT ===
    std::ofstream finalCsv;
    finalCsv.open("final_energy_statistics.csv");
    // Write Header
    finalCsv << "NodeType,DeviceID,NodeID,TotalEnergyConsumed_J,RemainingEnergy_J,FinalState" << std::endl;

    // Print final energy statistics
    std::cout << "\n=== FINAL ENERGY STATISTICS ===" << std::endl;
    for (auto& pair : g_energyMetrics) {
        uint32_t nodeId = pair.first;
        EnergyMetrics& metrics = pair.second;
        std::string nodeType = (nodeId < numAPs) ? "AP" : "STA";
        uint32_t deviceId = (nodeType == "AP" ? nodeId : nodeId - numAPs + 1);
        
        // Original console output
        std::cout << nodeType << " " << deviceId 
                  << " (Node " << nodeId << "):" << std::endl;
        std::cout << "  Total Energy Consumed: " << std::fixed << std::setprecision(3)
                  << metrics.totalEnergyConsumed << " J" << std::endl;
        std::cout << "  Remaining Energy: " << metrics.remainingEnergy << " J" << std::endl;
        std::cout << "  Final State: " << WifiStateToString(metrics.currentState) << std::endl;

        // Write to the CSV file
        finalCsv << nodeType << "," 
                 << deviceId << "," 
                 << nodeId << "," 
                 << std::fixed << std::setprecision(6) << metrics.totalEnergyConsumed << "," 
                 << metrics.remainingEnergy << "," 
                 << WifiStateToString(metrics.currentState) << std::endl;
    }
    finalCsv.close();
    std::cout << "\n✓ Final statistics saved to final_energy_statistics.csv" << std::endl;

    CloseAssociationTracking();
    Simulator::Destroy();
    return 0;
}