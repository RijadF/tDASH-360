#include "tiled-dash-client-network-manager.h"
#include "tiled-dash-client.h" 
#include "tiled-dash-client-network-tracker.h"

#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/tcp-socket.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/event-id.h"
#include "ns3/simulator.h"
#include "ns3/node.h"
#include "ns3/ipv4.h" // For InetSocketAddress
#include "ns3/inet-socket-address.h" // For InetSocketAddress
#include "ns3/address.h"
#include "ns3/nstime.h"
#include "ns3/config.h"
#include <algorithm> // For std::remove


namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TiledDashClientNetworkManager");

NS_OBJECT_ENSURE_REGISTERED (TiledDashClientNetworkManager);

TypeId
TiledDashClientNetworkManager::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TiledDashClientNetworkManager")
    .SetParent<Object> ()
    .SetGroupName("Applications") // Or "Network" if more appropriate for components
    .AddConstructor<TiledDashClientNetworkManager> ()
     .AddAttribute("SamplingInterval",
                          "The interval at which throughput is sampled for time-based estimation.",
                          TimeValue(Seconds(1.0)),
                          MakeTimeAccessor(&TiledDashClientNetworkManager::m_samplingInterval),
                          MakeTimeChecker())
    ;
  return tid;
}


TiledDashClientNetworkManager::TiledDashClientNetworkManager ()
     : m_samplingInterval(Seconds(1.0)), m_node(nullptr), m_client(nullptr)
{
  NS_LOG_FUNCTION (this);
}


TiledDashClientNetworkManager::~TiledDashClientNetworkManager ()
{
  NS_LOG_FUNCTION (this);
  // StopApplication should have cleaned up sockets and events
}


void
TiledDashClientNetworkManager::SetNumberOfSockets (uint32_t nSockets)
{
  m_nSockets = nSockets;
}

void
TiledDashClientNetworkManager::SetNode (Ptr<Node> node)
{
    m_node = node;
}

void
TiledDashClientNetworkManager::SetClient(Ptr<TiledDashClient> client)
{
    m_client = client;
}



void
TiledDashClientNetworkManager::StartApplication ()
{
  
  NS_LOG_INFO("Entered in StartApplication function");
  NS_LOG_FUNCTION (this);
  if (!m_node) {
      NS_LOG_ERROR("Node not set for NetworkManager. Cannot create sockets.");
      return;
  }
  m_socketPool.clear();
  m_socketActivity.clear();
  m_socketReceivedData.clear();

  for (uint32_t i = 0; i < m_nSockets; ++i)
    {
      CreateSocket ();
    }
}

void
TiledDashClientNetworkManager::StopApplication ()
{
  NS_LOG_INFO("Entered in StopApplication function");
  NS_LOG_FUNCTION (this);
  Simulator::Cancel (m_samplingEvent); // Cancel any pending sampling event
  m_samplingEvent.Cancel(); // Ensure it's marked as cancelled

  for (auto socket : m_socketPool)
    {
      if (socket) {
        socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        socket->SetConnectCallback(MakeNullCallback<void, Ptr<Socket>>(), 
                                   MakeNullCallback<void, Ptr<Socket>>());
        socket->SetCloseCallbacks(MakeNullCallback<void, Ptr<Socket>>(), 
                                  MakeNullCallback<void, Ptr<Socket>>());
        socket->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t>());
        //if (m_client && m_client->GetNetworkTracker()) {
        //  m_client->GetNetworkTracker()->RemoveSocketHistory(socket);
        //}
        socket->Close ();
      }
    }
  m_socketPool.clear ();
  m_socketActivity.clear();
  m_socketReceivedData.clear();
}

Ptr<TcpSocket>
TiledDashClientNetworkManager::GetIdleSocket ()
{
  NS_LOG_DEBUG("Entered in GetIdleSocket function");
  NS_LOG_FUNCTION (this);
  for (Ptr<TcpSocket> tcpSock : m_socketPool) // Iterate Ptr<TcpSocket>
    {
      Ptr<Socket> sock = tcpSock; // Cast for map key
      // Check if socket is connected and not busy
      NS_LOG_DEBUG("Soket trenutni u Poolu je "<<m_socketActivity.count(sock)<<" i "<<m_socketActivity[sock]<<" i to je socket"<<sock);
      //Cant return it because it is not considered idle anymore
      if (m_socketActivity.count(sock) && !m_socketActivity[sock])
        {
          m_socketActivity[sock] = true;
          NS_LOG_DEBUG("Returning idle socket: " << sock);
          return tcpSock;
        }
      NS_LOG_DEBUG("Socket: " << sock << ", in map: " << m_socketActivity.count(sock)
                  << ", activity: " << m_socketActivity[sock]);
    }
  NS_LOG_DEBUG("No idle and established sockets available. Pool size: " << m_socketPool.size());
  return nullptr;
}

void
TiledDashClientNetworkManager::ReleaseSocket (Ptr<TcpSocket> socket)
{
  NS_LOG_INFO("Entered in ReleaseSocket function");
  NS_LOG_INFO("Release after Time passed "<<Simulator::Now().GetSeconds());
  NS_LOG_FUNCTION (this << socket);
  if (socket && m_socketActivity.count(socket)) // Check if socket is in map
    {
      m_socketActivity[socket] = false;
      NS_LOG_DEBUG("Released socket: " << socket);
      NS_LOG_INFO("Released socket");
    } else {
      NS_LOG_WARN("Attempted to release a socket not in activity map or null: " << socket);
    }
}

void
TiledDashClientNetworkManager::SendData (Ptr<TcpSocket> socket, Ptr<Packet> packet)
{
  NS_LOG_INFO("Entered in SendData function"); //bilo je info
  NS_LOG_FUNCTION (this << socket << packet);
  if (socket && packet)
    {
      if (m_socketActivity.count(socket) && m_socketActivity[socket]) {
        socket->Send (packet);
        NS_LOG_INFO("Sent packet of size " << packet->GetSize() << " on socket " << socket);
      } else {
        NS_LOG_ERROR("Attempted to send data on a non-established socket. State: " << socket);
        // Potentially re-queue the packet or handle error
      }
    }
  else
    {
      NS_LOG_ERROR ("Attempted to send data on a null socket or with a null packet.");
    }
}

void
TiledDashClientNetworkManager::DataReceivedOnSocket (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Address from;
  Ptr<Packet> packet;

  //  MUST loop until no more data is available
  while ((packet = socket->RecvFrom(from)))
  {
    if (packet->GetSize() == 0)
    {
      // Peer closed connection
      NS_LOG_INFO("Socket " << socket << " received FIN (connection closed by peer)");
      break;
    }

    NS_LOG_INFO("Socket " << socket << " received packet of size " 
                << packet->GetSize() << " from " 
                << InetSocketAddress::ConvertFrom(from).GetIpv4());

    // Accumulate for throughput tracking
    if (m_socketReceivedData.count(socket)) {
        m_socketReceivedData[socket] += packet->GetSize();
    } else {
        m_socketReceivedData[socket] = packet->GetSize();
    }

    // Pass to upper layer (TiledDashClient)
    m_dataReceivedCallback(socket, packet, from);
  }

  // If packet is null, it means no more data (normal)
  if (!packet)
  {
    NS_LOG_DEBUG("Socket " << socket << " has no more data to read");
  }
}

bool
TiledDashClientNetworkManager::IsSocketActive (Ptr<TcpSocket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_LOG_INFO("Entered in IsSocketActive function");
  if (socket && m_socketActivity.find(socket) != m_socketActivity.end())
    {
        return m_socketActivity[socket];
    }
  return false;
}

void
TiledDashClientNetworkManager::SetDataReceivedCallback (DataReceivedCallback callback)
{
    NS_LOG_INFO("Entered in SetDataReceivedCallback function");
    m_dataReceivedCallback = callback;
}

void
TiledDashClientNetworkManager::CreateSocket ()
{
  NS_LOG_FUNCTION (this);
  if (!m_node) {
      NS_LOG_ERROR("Cannot create socket, m_node is null.");
      return;
  }
  Ptr<TcpSocket> tcpSocket = DynamicCast<TcpSocket> (Socket::CreateSocket (m_node, TcpSocketFactory::GetTypeId()));

  if (tcpSocket)
    {
      tcpSocket->Bind (); // Bind to an ephemeral port
      // Setup callbacks before connecting
      tcpSocket->SetConnectCallback (MakeCallback (&TiledDashClientNetworkManager::ConnectionSucceeded, this),
                                    MakeCallback (&TiledDashClientNetworkManager::ConnectionFailed, this));
      tcpSocket->SetRecvCallback (MakeCallback (&TiledDashClientNetworkManager::DataReceivedOnSocket, this));
      tcpSocket->SetCloseCallbacks (MakeCallback (&TiledDashClientNetworkManager::NormalClose, this),
                                 MakeCallback (&TiledDashClientNetworkManager::ErrorClose, this));
      // Optional: tcpSocket->SetSendCallback (MakeCallback (&TiledDashClientNetworkManager::SocketSentData, this));

      NS_LOG_INFO("Attempting to connect socket " << tcpSocket << " to " << m_client->GetServerAddress() << ":" << m_client->GetServerPort());
      tcpSocket->Connect(InetSocketAddress (InetSocketAddress::ConvertFrom(m_client->GetServerAddress()).GetIpv4(), m_client->GetServerPort()));
      
      // Add to pool immediately; state will be managed by callbacks
      m_socketPool.push_back (tcpSocket);
      Ptr<Socket> baseSocket = tcpSocket; // For map keys
      m_socketActivity[baseSocket] = false; // Initially not active for sending data, and not necessarily connected
      m_socketReceivedData[baseSocket] = 0;
    }
  else
    {
      NS_LOG_ERROR ("Failed to create TCP socket.");
    }
} 

void TiledDashClientNetworkManager::ConnectionSucceeded (Ptr<Socket> socket)
{
    NS_LOG_INFO ("Socket " << socket );
    NS_LOG_INFO("Entered in ConnectionSucceeded function");
    // Socket is now in ESTABLISHED state.
    // It's ready to be picked by GetIdleSocket if m_socketActivity[socket] is false.
    if (m_socketActivity.find(socket) == m_socketActivity.end()) {
        // This case should ideally not happen if added to map in CreateSocket
        m_socketActivity[socket] = false; 
    }
    // No need to set m_socketActivity[socket] = false here, as GetIdleSocket checks this.
}

void TiledDashClientNetworkManager::ConnectionFailed (Ptr<Socket> socket)
{
    //if (!m_socketErrorCallback.IsNull()) {
    //    m_socketErrorCallback(socket);
    //}
    NS_LOG_INFO("Entered in ConnectionFailed function");
    NS_LOG_ERROR ("Socket " << socket << " failed to connect to " << m_client->GetServerAddress() << ":" << m_client->GetServerPort());
    //if (m_client && m_client->GetNetworkTracker()) {
    //  m_client->GetNetworkTracker()->RemoveSocketHistory(socket);
    //}
    // Remove the failed socket from the pool and maps
    m_socketPool.erase(std::remove(m_socketPool.begin(), m_socketPool.end(), DynamicCast<TcpSocket>(socket)), m_socketPool.end());
    m_socketActivity.erase(socket);
    m_socketReceivedData.erase(socket);
    // Optionally, try to create a new socket to maintain pool size
    CreateSocket(); 
}
//*/
void TiledDashClientNetworkManager::NormalClose (Ptr<Socket> socket)
{
    //if (!m_socketErrorCallback.IsNull()) {
    //    m_socketErrorCallback(socket);
    //}
    NS_LOG_INFO("Entered in ClosedNormally function");
    NS_LOG_INFO ("Socket " << socket << " closed normally by remote peer.");
    //if (m_client && m_client->GetNetworkTracker()) {
    //  m_client->GetNetworkTracker()->RemoveSocketHistory(socket);
    //}
    m_socketActivity.erase(socket);
    m_socketReceivedData.erase(socket);
    m_socketPool.erase(std::remove(m_socketPool.begin(), m_socketPool.end(), DynamicCast<TcpSocket>(socket)), m_socketPool.end());
    // Optionally, try to re-establish or create a new socket
    CreateSocket();
}

void TiledDashClientNetworkManager::ErrorClose (Ptr<Socket> socket)
{
    //if (!m_socketErrorCallback.IsNull()) {
    //  m_socketErrorCallback(socket);
    //}
    NS_LOG_INFO("Entered in ErrorClose function");
    NS_LOG_ERROR ("Socket " << socket << " closed due to an error.");
    //if (m_client && m_client->GetNetworkTracker()) {
    //  m_client->GetNetworkTracker()->RemoveSocketHistory(socket);
    //}
    m_socketActivity.erase(socket);
    m_socketReceivedData.erase(socket);
    m_socketPool.erase(std::remove(m_socketPool.begin(), m_socketPool.end(), DynamicCast<TcpSocket>(socket)), m_socketPool.end());
    // Optionally, try to re-establish or create a new socket
    CreateSocket();
}

void TiledDashClientNetworkManager::SocketSentData (Ptr<Socket> socket, uint32_t bytesSent)
{
    NS_LOG_INFO("Entered in SocketSentData function");
    NS_LOG_INFO ("Socket " << socket << " sent " << bytesSent << " bytes. Available buffer: " << socket->GetTxAvailable());
    // This callback is useful if you need to manage sending large data amounts
    // and need to know when buffer space is available again.
}


void
TiledDashClientNetworkManager::SetSamplingInterval (Time interval)
{
    m_samplingInterval = interval;
}

Time
TiledDashClientNetworkManager::GetSamplingInterval () const
{
  return m_samplingInterval;
}

void
TiledDashClientNetworkManager::ActivateTimedThroughput (Time interval)
{
  NS_LOG_FUNCTION (this << interval);
  if (interval > Time(0)) {
    m_samplingInterval = interval;
    // Schedule the first sample collection if not already running
    if (!m_samplingEvent.IsPending()) {
        m_samplingEvent = Simulator::Schedule (m_samplingInterval, &TiledDashClientNetworkManager::CollectThroughputSamples, this);
        NS_LOG_INFO("Activated timed throughput sampling with interval: " << m_samplingInterval.GetSeconds() << "s");
    } else {
        NS_LOG_INFO("Timed throughput sampling already active.");
    }
  } else {
      NS_LOG_WARN("Cannot activate timed throughput with zero or negative interval.");
  }
}

void
TiledDashClientNetworkManager::CollectThroughputSamples ()
{
  NS_LOG_FUNCTION (this);

  if (!m_client) {
      NS_LOG_WARN("Client Ptr is null in CollectThroughputSamples. Cannot access Network Tracker.");
      // Reschedule if desired, or stop sampling
      if (m_samplingInterval > Time(0)) {
          m_samplingEvent = Simulator::Schedule (m_samplingInterval, &TiledDashClientNetworkManager::CollectThroughputSamples, this);
      }
      return;
  }

  Ptr<TiledDashClientNetworkTracker> networkTracker = m_client->GetNetworkTracker();
  if (!networkTracker) {
      NS_LOG_WARN("Network Tracker Ptr is null in CollectThroughputSamples. Cannot submit throughput samples.");
      // Reschedule if desired, or stop sampling
      if (m_samplingInterval > Time(0)) {
          m_samplingEvent = Simulator::Schedule (m_samplingInterval, &TiledDashClientNetworkManager::CollectThroughputSamples, this);
      }
      return;
  }

  if (m_samplingInterval.IsZero()) {
      NS_LOG_WARN("Sampling interval is zero. Skipping throughput collection.");
      return; // Do not reschedule if interval is zero
  }

  NS_LOG_INFO("Collecting throughput samples for " << m_socketReceivedData.size() << " sockets.");
  for (auto const& pair : m_socketReceivedData)
    {
      Ptr<Socket> socket = pair.first;
      uint32_t receivedDataSinceLastSample = pair.second;
      // Throughput = (Bits received in interval) / (Interval duration in seconds)
      double throughputBps = (static_cast<double> (receivedDataSinceLastSample) * 8.0) / m_samplingInterval.GetSeconds ();
      
      // Assuming SubmitTileThroughput is the correct method on NetworkTracker
      // It might need adjustment based on NetworkTracker's actual interface
      // For instance, it might expect total bytes and timestamp, or just the rate.
      // For now, sending the calculated rate.
      networkTracker->SubmitThroughput(socket, throughputBps); // Example: Submit overall sample
      // Or if it needs per-socket: networkTracker->SubmitSocketSample(socket, throughputBps);

      NS_LOG_INFO ("Socket " << socket << " (Interval Data: " << receivedDataSinceLastSample << " bytes) -> Throughput: " << throughputBps << " bps");
    }

  // Reset the received data counts for the next interval for all tracked sockets
  for (auto& pair : m_socketReceivedData) {
      pair.second = 0; // Reset bytes for next interval
  }
  // Note: m_socketReceivedData.clear() would remove sockets that had no traffic.
  // Resetting to 0 keeps all previously active sockets in the map for the next sample.

  // Reschedule the next sample collection
  if (m_samplingInterval > Time(0)) {
    m_samplingEvent = Simulator::Schedule (m_samplingInterval, &TiledDashClientNetworkManager::CollectThroughputSamples, this);
  }
}

//Ptr<TcpSocket> TiledDashClientNetworkManager::GetSocket(uint32_t i) const { if (i < m_socketPool.size()) { return m_socketPool[i]; } return nullptr; }

} // namespace ns3