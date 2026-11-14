// tiled-dash-client-network-manager.h
#ifndef TILED_DASH_CLIENT_NETWORK_MANAGER_H
#define TILED_DASH_CLIENT_NETWORK_MANAGER_H

#include "ns3/object.h"
#include "ns3/ipv4-address.h"
#include "ns3/tcp-socket.h" // Includes ns3/socket.h indirectly
#include "ns3/packet.h"
#include "ns3/nstime.h"
#include "ns3/node.h"      // For Ptr<Node>
#include "ns3/callback.h"  // For Callback
#include <vector>
#include <map>

namespace ns3 {

// Forward declaration for TiledDashClient to avoid circular dependency
class TiledDashClient;
// Forward declaration for TiledDashClientNetworkTracker if its methods are called directly
// (though typically done via TiledDashClient mediator)

class TiledDashClientNetworkManager : public Object
{
public:
  static TypeId GetTypeId (void);
  TiledDashClientNetworkManager ();
  virtual ~TiledDashClientNetworkManager ();
  void SetNumberOfSockets (uint32_t nSockets);
  void SetNode (Ptr<Node> node); // Added to provide node context
  void SetClient(Ptr<TiledDashClient> client); // To access other components like NetworkTracker

  void SetSamplingInterval (Time interval);
  Time GetSamplingInterval () const;

  void StartApplication ();
  void StopApplication ();

  Ptr<TcpSocket> GetIdleSocket ();
  void ReleaseSocket (Ptr<TcpSocket> socket);
  void SendData (Ptr<TcpSocket> socket, Ptr<Packet> packet);
  bool IsSocketActive (Ptr<TcpSocket> socket);
  void ActivateTimedThroughput (Time interval);
  //for the sake of testing added this getter, just to confirm that we have data that is being passed around
  Ptr<TcpSocket> GetSocket(uint32_t i) const{ if (i < m_socketPool.size()) { return m_socketPool[i]; } return nullptr; };

  // Callback for when data is received on a socket
  // The registered callback will be invoked with the socket, packet, and source address
  typedef Callback<void, Ptr<Socket>, Ptr<Packet>, const Address&> DataReceivedCallback;
  void SetDataReceivedCallback (DataReceivedCallback callback);
  //testing
  //typedef Callback<void, Ptr<Socket>> SocketErrorCallback;
  //void SetSocketErrorCallback (SocketErrorCallback cb);
private:
  void CreateSocket ();
  //added
  //SocketErrorCallback m_socketErrorCallback;
  // Socket Callbacks
  void DataReceivedOnSocket (Ptr<Socket> socket); // Renamed from ReceiveDataCallback to avoid confusion with typedef
  void ConnectionSucceeded (Ptr<Socket> socket);
  void ConnectionFailed (Ptr<Socket> socket);
  void NormalClose (Ptr<Socket> socket);
  void ErrorClose (Ptr<Socket> socket);
  void SocketSentData (Ptr<Socket> socket, uint32_t bytesSent); // Optional: for send buffer management

  void CollectThroughputSamples ();


  uint32_t m_nSockets;
  Time m_samplingInterval;
  Ptr<Node> m_node; // Node context for creating sockets
  Ptr<TiledDashClient> m_client; // Mediator client pointer

  std::vector<Ptr<TcpSocket>> m_socketPool;
  std::map<Ptr<Socket>, bool> m_socketActivity; // Use Ptr<Socket> as key for consistency with callbacks
  std::map<Ptr<Socket>, uint32_t> m_socketReceivedData;
  
  DataReceivedCallback m_dataReceivedCallback; // Stores the callback from upper layer
  EventId m_samplingEvent;
};

} // namespace ns3

#endif // TILED_DASH_CLIENT_NETWORK_MANAGER_H