/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

// Based on tcp-star-server.cc from the ns3 distribution, with copious cut and paste from elsewhere

// uses boost::format
// Thus the ns3 build step needs to be something like this:
// CXXFLAGS="-I/usr/include -O3" ./waf -d debug --enable-examples --enable-tests configure
// My ubuntu system puts the boost headers in /usr/include/boost
// I had to install the package "libboost-dev" to get them

// Default Network topology, dumbbell network with N nodes each side
// N (default 15) servers
// N-M (default 5) clients have R2 (default 40 Mbps) bandwidth
//    but also have between 100 and 102 ms delay
// M (default 10) clients have R1 (default 10 Mbps) bandwidth
//    but delays in the 1 to 2 ms range
// Bottleneck link has R (default 100 Mbps) bandwidht, 1 ms delay
// Server links are identical to the bottleneck
// Bottleneck link queue can be RED or DropTail, many parameters adjustable

// Usage examples for things you might want to tweak:
// List possible arguments:
//       ./waf --run="tcp-qfp --PrintHelp"
// Run with default arguments:
//       ./waf --run="tcp-qfp"
// Set lots of parameters:
//       ./waf --run="tcp-qfp --appDataRate=15Mbps --R1=10Mbps --R2=30Mbps --nNodes=15 --mNodes=10 --R=90Mbps --queueType=RED"

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

#include <boost/format.hpp>

#include "ns3/callback.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/flow-monitor-helper.h"
#include <iomanip>

using namespace ns3;
using namespace boost;

NS_LOG_COMPONENT_DEFINE ("TcpServer");

// void bytesInQueueTrace(uint32_t v_old, uint32_t v_new)
// {
//   NS_LOG_INFO ((format("Queue went from %d to %d (%d)") % (v_old) % (v_new) % (((int) v_new)-((int) v_old))).str());
// }

// void countTrace(uint32_t v_old, uint32_t v_new)
// {
//   NS_LOG_INFO ((format("Count went from %d to %d (%d)") % (v_old) % (v_new) % (((int) v_new)-((int) v_old))).str());
// }

std::string pathOut = ".";

static void
CheckQueue (std::string filePlotQueue, std::string filePlotQueueAvg, std::string filePlotJurneyAvg,
                    double avgQueueSize, uint32_t checkTimes, Ptr<Queue> queue)
{
  uint32_t qSize = queue->GetNBytes ();

  avgQueueSize += qSize;
  checkTimes++;

  // check queue size every 1/100 of a second
  Simulator::Schedule (Seconds (0.01), &CheckQueue, filePlotQueue, filePlotQueueAvg, filePlotJurneyAvg, avgQueueSize, checkTimes, queue);

  std::ofstream fPlotQueue (filePlotQueue.c_str (), std::ios::out|std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();

  std::ofstream fPlotQueueAvg (filePlotQueueAvg.c_str (), std::ios::out|std::ios::app);
  fPlotQueueAvg << Simulator::Now ().GetSeconds () << " " << avgQueueSize / checkTimes << std::endl;
  fPlotQueueAvg.close ();

  std::ofstream fPlotJurneyAvg (filePlotJurneyAvg.c_str (), std::ios::out|std::ios::app);
  fPlotJurneyAvg << Simulator::Now ().GetSeconds () << " " << queue->GetPacketJurneyAvg().GetMilliSeconds() << "ms" << std::endl;
  fPlotJurneyAvg.close ();
}

static void
WriteForPlot(Ptr<NetDevice> ptrNd, std::string suffix = "")
{
  std::ostringstream oss;
  oss << "tcp-qfp-";

  std::string nodename;
  std::string devicename;

  Ptr<PointToPointNetDevice> nd = StaticCast<PointToPointNetDevice> (ptrNd);
  Ptr<Node> node = nd->GetNode ();

  nodename = Names::FindName (node);
  devicename = Names::FindName (nd);

  if (nodename.size ())
    {
      oss << nodename;
    }
  else
    {
      oss << node->GetId ();
    }

  oss << "-";

  if (devicename.size ())
    {
      oss << devicename;
    }
  else
    {
      oss << nd->GetIfIndex ();
    }

  std::stringstream filePlotQueue, filePlotQueueAvg, filePlotQueueJourneyAvg;

  filePlotQueue << pathOut << "/" << oss.str() << suffix << "-queue.plotme";
  filePlotQueueAvg << pathOut << "/" << oss.str() << suffix << "-queue-avg.plotme";
  filePlotQueueJourneyAvg << pathOut << "/" << oss.str() << suffix << "-queue-journey-avg.plotme";

  remove (filePlotQueue.str ().c_str ());
  remove (filePlotQueueAvg.str ().c_str ());
  remove (filePlotQueueJourneyAvg.str().c_str());
  Ptr<Queue> queue = nd->GetQueue ();
  Simulator::ScheduleNow (&CheckQueue, filePlotQueue.str(), filePlotQueueAvg.str(), filePlotQueueJourneyAvg.str(), 0, 0, queue);
}

int
main (int argc, char *argv[])
{
  // Users may find it convenient to turn on explicit debugging
  // for selected modules; the below lines suggest how to do this

  LogComponentEnable ("TcpServer", LOG_LEVEL_INFO);
  //LogComponentEnable ("PointToPointNetDevice", LOG_LEVEL_INFO);
  //LogComponentEnable ("TcpL4Protocol", LOG_LEVEL_ALL);
  //LogComponentEnable ("Config", LOG_LEVEL_ALL);
  //LogComponentEnable ("SfqQueue", LOG_LEVEL_INFO);
  //LogComponentEnable ("RedQueue", LOG_LEVEL_INFO);
  //LogComponentEnable ("Ipv4Interface", LOG_LEVEL_LOGIC);
  //LogComponentEnable ("CoDelQueue", LOG_LEVEL_ALL);
  //LogComponentEnable ("Fq_CoDelQueue", LOG_LEVEL_ALL);

  // turn on checksums
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  uint32_t N = 15; //number of nodes
  uint32_t M = 9; //number of low latency nodes
  std::string appDataRate = "10Mbps";
  std::string bottleneckRate = "90Mbps";
  std::string CoDelInterval = "100ms";
  std::string Target = "5ms";
  std::string R1 = "10Mbps";
  std::string R2 = "25Mbps";
  std::string Q1queueType = "RED";
  std::string Q2queueType = "RED";
  uint32_t    Q1maxPackets = 90;
  uint32_t    Q2maxPackets = 90;
  uint32_t    maxPackets = 90;
  uint32_t    pktSize = 1000;
  uint32_t    modeBytes  = 0;
  uint32_t    stack  = 0;
  uint32_t    modeGentle  = 0;
  bool       modeAdaptive = 0;
  uint32_t    sfqheadmode = 0;
  uint32_t maxBytes = 0;
  uint32_t quantum = 4507;
  int appsPerInterface = 1;
  std::string queueType = "SFQ";
  bool opd = false;
  bool drr = false;
  bool ecnTcp = false;
  int noQueue = 1;
  double wq1 = 3, wq2 = 3, wq3 = 3, wq4 = 3,
          wq5 = 3, wq6 = 3, wq7 = 3, wq8 = 3;

  double AppStartTime   = 0.1001;
  bool flowMonitor = false;
  bool writeForPlot = false;
  bool printSockStats = false;
  double runTime = 30;

  // cubic is the default congestion algorithm in Linux 2.6.26
  std::string tcpCong = "cubic";
  // the name of the NSC stack library that should be used
  std::string nscStack = "liblinux2.6.26.so";

  // Allow the user to override any of the defaults and the above
  // Config::SetDefault()s at run-time, via command-line arguments
  CommandLine cmd;
  // cmd.AddValue ("appDataRate", "Set OnOff App DataRate", appDataRate);
  cmd.AddValue ("RunTime", "Number of seconds to run", runTime);
  cmd.AddValue ("pathOut", "Path to save results from --writeForPlot/--writePcap/--writeFlowMonitor", pathOut);
  cmd.AddValue ("writeFlowMonitor", "<0/1> to enable Flow Monitor and write their results", flowMonitor);
  cmd.AddValue ("writeForPlot", "<0/1> to enable Queue Size and Average Queue Size monitoring", writeForPlot);
  cmd.AddValue ("printSockStats", "<0/1> to enable Socket stats monitoring", printSockStats);
  cmd.AddValue ("maxBytes",
                "Total number of bytes for application to send", maxBytes);
  cmd.AddValue ("Quantum", "SFQ and fq_codel quantum", quantum);
  cmd.AddValue ("queueType", "Set Queue type to CoDel, DropTail, RED, or SFQ", queueType);
  cmd.AddValue ("modeBytes", "Set RED Queue mode to Packets <0> or bytes <1>", modeBytes);
  cmd.AddValue ("stack", "Set TCP stack to NSC <0> or linux-2.6.26 <1> (warning, linux stack is really slow in the sim)", stack);
  cmd.AddValue ("modeGentle", "Set RED Queue mode to standard <0> or gentle <1>", modeBytes);
  cmd.AddValue ("maxPackets","Max Packets allowed in the queue", maxPackets);
  cmd.AddValue ("nNodes", "Number of client nodes", N);
  cmd.AddValue ("mNodes", "Number of low latency client nodes", M);
  cmd.AddValue ("R", "Bottleneck rate", bottleneckRate);
  cmd.AddValue ("R1", "Low latency node edge link bottleneck rate", R1);
  cmd.AddValue ("Q1Type", "Set Queue type to DropTail or RED", Q1queueType);
  cmd.AddValue ("Q1maxPackets","Max Packets allowed in the queue", Q1maxPackets);
  cmd.AddValue ("R2", "High latency node edge link bottleneck rate", R2);
  cmd.AddValue ("Q2Type", "Set Queue type to DropTail or RED", Q2queueType);
  cmd.AddValue ("Q2maxPackets","Max Packets allowed in the queue", Q2maxPackets);
  cmd.AddValue ("SFQHeadMode", "New SFQ flows go to the head", sfqheadmode);
  cmd.AddValue ("Interval", "CoDel algorithm interval", CoDelInterval);
  cmd.AddValue ("Target", "Target queue delay", Target);
  cmd.AddValue ("ECN", "Use ECN capable TCP", ecnTcp);
  cmd.AddValue ("OPD", "Red uses Oldest Packet Drop", opd);
  cmd.AddValue ("DRR", "Red uses Deficit Round Robin", drr);
  cmd.AddValue ("noQueues", "Number of queues for RED with DRR (8 is max for now)", noQueue);
  cmd.AddValue ("wq1", "DRR weight for RED queue 1", wq1);
  cmd.AddValue ("wq2", "DRR weight for RED queue 2", wq2);
  cmd.AddValue ("wq3", "DRR weight for RED queue 3", wq3);
  cmd.AddValue ("wq4", "DRR weight for RED queue 4", wq4);
  cmd.AddValue ("wq5", "DRR weight for RED queue 5", wq5);
  cmd.AddValue ("wq6", "DRR weight for RED queue 6", wq6);
  cmd.AddValue ("wq7", "DRR weight for RED queue 7", wq7);
  cmd.AddValue ("wq8", "DRR weight for RED queue 8", wq8);
  cmd.AddValue ("adaptive", "Use adaptive RED", modeAdaptive);
  cmd.AddValue ("appsPerIf", "Number of apps per interface", appsPerInterface);
  cmd.Parse (argc, argv);

  if ((queueType != "RED") && (queueType != "DropTail") && (queueType != "DropOldestQueue") && (queueType != "SFQ") && (queueType != "CoDel") && (queueType != "fq_codel"))
    {
      NS_ABORT_MSG ("Invalid queue type: Use --queueType=RED or --queueType=DropTail");
    }
  if ((Q1queueType != "RED") && (Q1queueType != "DropTail") && (Q1queueType != "DropOldestQueue") && (Q1queueType != "CoDel"))
    {
      NS_ABORT_MSG ("Invalid Q1 queue type: Use --queueType=RED or --queueType=DropTail or --queueType=CoDel");
    }
  if ((Q2queueType != "RED") && (Q2queueType != "DropTail") && (Q2queueType != "DropOldestQueue") && (Q2queueType != "CoDel"))
    {
      NS_ABORT_MSG ("Invalid Q2 queue type: Use --queueType=RED or --queueType=DropTail or --queueType=CoDel");
    }
  if (runTime < 20)
    {
      NS_ABORT_MSG ("Invalid RunTime value: RunTime must be larger then 20");
    }
  if (noQueue > 8 || noQueue < 1)
    {
      NS_ABORT_MSG ("Invalid noQueue value: noQueue must be lesser or equal to 8 and larger than 0");
    }
  else
    {
      Config::SetDefault ("ns3::RedQueue::NoQueues", UintegerValue (noQueue));
    }

  if (appsPerInterface < 1)
    {
      NS_ABORT_MSG ("Invalid appsPerIf value: appsPerIf must be larger than 0");
    }

  if (ecnTcp)
    {
      Config::SetDefault ("ns3::TcpSocket::ECN", BooleanValue (true));
    }

  if (drr)
    {
      Config::SetDefault ("ns3::RedQueue::DRR", BooleanValue (true));
    }

  if (opd)
    {
      Config::SetDefault ("ns3::RedQueue::HeadDrop", BooleanValue (true));
    }

  if (modeGentle)
    {
      Config::SetDefault ("ns3::RedQueue::Gentle", BooleanValue (true));
    }

  if (modeAdaptive)
    {
      Config::SetDefault ("ns3::RedQueue::Adaptive", BooleanValue (true));
    }

  if (sfqheadmode)
    {
      Config::SetDefault ("ns3::SfqQueue::headMode", BooleanValue (true));
    }

  Config::SetDefault ("ns3::SfqQueue::peturbInterval", UintegerValue(131));
  Config::SetDefault ("ns3::SfqQueue::Quantum", UintegerValue(quantum));
  Config::SetDefault ("ns3::Fq_CoDelQueue::Quantum", UintegerValue(quantum));

  Config::SetDefault ("ns3::RedQueue::LInterm", DoubleValue (50));
  if (!modeBytes)
    {
      Config::SetDefault ("ns3::RedQueue::Mode", EnumValue (RedQueue::QUEUE_MODE_PACKETS));
      //Config::SetDefault ("ns3::DropTailQueue::Mode", StringValue ("Packets"));
      Config::SetDefault ("ns3::DropTailQueue::MaxPackets", UintegerValue (maxPackets));
      //Config::SetDefault ("ns3::RedQueue::Mode", StringValue ("Packets"));
      Config::SetDefault ("ns3::RedQueue::QueueLimit", UintegerValue (maxPackets));
    }
  else
    {
      Config::SetDefault ("ns3::RedQueue::Mode", EnumValue (RedQueue::QUEUE_MODE_BYTES));
      //Config::SetDefault ("ns3::DropTailQueue::Mode", StringValue ("Bytes"));
      Config::SetDefault ("ns3::DropTailQueue::MaxBytes", UintegerValue (maxPackets * pktSize));
      //Config::SetDefault ("ns3::RedQueue::Mode", StringValue ("Bytes"));
      Config::SetDefault ("ns3::RedQueue::QueueLimit", UintegerValue (maxPackets * pktSize));

      Config::SetDefault ("ns3::RedQueue::WQ1", DoubleValue (wq1 * pktSize));
      Config::SetDefault ("ns3::RedQueue::WQ2", DoubleValue (wq2 * pktSize));
      Config::SetDefault ("ns3::RedQueue::WQ3", DoubleValue (wq3 * pktSize));
      Config::SetDefault ("ns3::RedQueue::WQ4", DoubleValue (wq4 * pktSize));
      Config::SetDefault ("ns3::RedQueue::WQ5", DoubleValue (wq5 * pktSize));
      Config::SetDefault ("ns3::RedQueue::WQ6", DoubleValue (wq6 * pktSize));
      Config::SetDefault ("ns3::RedQueue::WQ7", DoubleValue (wq7 * pktSize));
      Config::SetDefault ("ns3::RedQueue::WQ8", DoubleValue (wq8 * pktSize));

      Q1maxPackets *= pktSize;
      Q2maxPackets *= pktSize;
    }

  Config::SetDefault ("ns3::RedQueue::MinTh", DoubleValue (0));
  Config::SetDefault ("ns3::RedQueue::MaxTh", DoubleValue (0));
  Config::SetDefault ("ns3::RedQueue::LinkBandwidth", StringValue (bottleneckRate));
  Config::SetDefault ("ns3::RedQueue::LinkDelay", StringValue ("1ms"));
  Config::SetDefault ("ns3::RedQueue::MeanPktSize", UintegerValue(pktSize));
  Config::SetDefault ("ns3::RedQueue::QW", DoubleValue(-1.0));
  Config::SetDefault ("ns3::RedQueue::TargetDelay", StringValue(Target));

  Config::SetDefault ("ns3::CoDelQueue::Interval", StringValue(CoDelInterval));
  Config::SetDefault ("ns3::CoDelQueue::Target", StringValue(Target));

  Config::SetDefault ("ns3::BulkSendApplication::SendSize", UintegerValue (pktSize));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (pktSize));
  Config::SetDefault ("ns3::OnOffApplication::DataRate", StringValue (appDataRate));

  NS_LOG_INFO ("Create nodes.");
  NodeContainer serverNodes;
  NodeContainer leftNode;
  NodeContainer rightNode;
  NodeContainer clientNodes;
  serverNodes.Create (N);
  clientNodes.Create (N);
  leftNode.Create (1);
  rightNode.Create (1);
  NodeContainer allNodes = NodeContainer (serverNodes, leftNode, rightNode, clientNodes);

  for(uint32_t i=0; i<N; ++i)
    {
      Names::Add ((format("/Names/Servers/Server%d") % (i+1)).str(), serverNodes.Get (i));
    }
  for(uint32_t i=0; i<N; ++i)
    {
      Names::Add ((format("/Names/Clients/Client%d") % (i+1)).str(), clientNodes.Get (i));
    }
  Names::Add ("/Names/Left", leftNode.Get (0));
  Names::Add ("/Names/Right", rightNode.Get (0));


  // Install network stacks on the nodes
  InternetStackHelper internet;
  if (stack)
    {
      internet.SetTcp ("ns3::NscTcpL4Protocol","Library",StringValue (nscStack));
      // Could set something other than Cubic here, but setting Cubic explicitly exits the sim
      if (tcpCong != "cubic")
        Config::Set ("/NodeList/*/$ns3::Ns3NscStack<linux2.6.26>/net.ipv4.tcp_congestion_control", StringValue (tcpCong));
    }

  internet.Install (serverNodes);
  internet.Install (clientNodes);

  InternetStackHelper internet2;
  internet2.Install (leftNode);
  internet2.Install (rightNode);

  //Collect an adjacency list of nodes for the p2p topology
  std::vector<NodeContainer> nodeAdjacencyList (2*N+1);
  for(uint32_t i = N, j = 0; i<2*N; ++i, ++j)
    {
      nodeAdjacencyList[i] = NodeContainer (leftNode, clientNodes.Get (j));
    }
  for(uint32_t i=0; i<N; ++i)
    {
      nodeAdjacencyList[i] = NodeContainer (rightNode, serverNodes.Get (i));
    }
  nodeAdjacencyList[2*N] = NodeContainer (leftNode, rightNode);


  // We create the channels first without any IP addressing information
  NS_LOG_INFO ("Create channels.");
  PointToPointHelper p1channel;
  p1channel.SetDeviceAttribute ("DataRate", StringValue (R1));
  if (Q1queueType == "RED")
    {
      p1channel.SetQueue ("ns3::RedQueue",
                          "MinTh", DoubleValue (0),
                          "MaxTh", DoubleValue (0),
                          "LinkBandwidth", StringValue (R1),
                          "QueueLimit",  UintegerValue (Q1maxPackets));
    }
  else if (Q1queueType == "CoDel")
    {
      p1channel.SetQueue ("ns3::CoDelQueue",
                          "Interval", StringValue("100ms"),
                          "Target", StringValue("5ms"),
                          "MinBytes", UintegerValue(1500)
                          );
    }
  else if (Q1queueType == "DropOldestQueue")
    {
      p1channel.SetQueue ("ns3::DropOldestQueue");
    }

  PointToPointHelper p2channel;
  p2channel.SetDeviceAttribute ("DataRate", StringValue (R2));
  if (Q2queueType == "RED")
    {
      p2channel.SetQueue ("ns3::RedQueue",
                          "MinTh", DoubleValue (0),
                          "MaxTh", DoubleValue (0),
                          "LinkBandwidth", StringValue (R2),
                          "QueueLimit",  UintegerValue (Q2maxPackets));
    }
  else if (Q2queueType == "CoDel")
    {
      p2channel.SetQueue ("ns3::CoDelQueue",
                          "Interval", StringValue("100ms"),
                          "Target", StringValue("5ms"),
                          "MinBytes", UintegerValue(1500)
                          );
    }
  else if (Q2queueType == "DropOldestQueue")
    {
      p2channel.SetQueue ("ns3::DropOldestQueue");
    }

  PointToPointHelper serverchannel;
  // server links are fast
  serverchannel.SetDeviceAttribute ("DataRate", StringValue (bottleneckRate));
  serverchannel.SetChannelAttribute ("Delay", StringValue ("1ms"));
  if (queueType == "RED")
    {
      serverchannel.SetQueue ("ns3::RedQueue",
                                  "MinTh", DoubleValue (0),
                                  "MaxTh", DoubleValue (0),
                                  "LinkBandwidth", StringValue (bottleneckRate),
                                  "LinkDelay", StringValue ("1ms"));
    }
  else if (queueType == "SFQ")
    {
      serverchannel.SetQueue ("ns3::SfqQueue");
    }
  else if (queueType == "fq_codel")
    {
      serverchannel.SetQueue ("ns3::Fq_CoDelQueue");
    }
  else if (queueType == "CoDel")
    {
      serverchannel.SetQueue ("ns3::CoDelQueue",
                                  "MinBytes", UintegerValue(1500)
                                  );
    }
  else if (queueType == "DropOldestQueue")
    {
      serverchannel.SetQueue ("ns3::DropOldestQueue");
    }

  PointToPointHelper bottleneckchannel;
  // last one has different properties
  bottleneckchannel.SetDeviceAttribute ("DataRate", StringValue (bottleneckRate));
  bottleneckchannel.SetChannelAttribute ("Delay", StringValue ("1ms"));
  if (queueType == "RED")
    {
      bottleneckchannel.SetQueue ("ns3::RedQueue",
                                  "MinTh", DoubleValue (0),
                                  "MaxTh", DoubleValue (0),
                                  "LinkBandwidth", StringValue (bottleneckRate),
                                  "LinkDelay", StringValue ("1ms"));
    }
  else if (queueType == "SFQ")
    {
      bottleneckchannel.SetQueue ("ns3::SfqQueue");
    }
  else if (queueType == "fq_codel")
    {
      bottleneckchannel.SetQueue ("ns3::Fq_CoDelQueue");
    }
  else if (queueType == "CoDel")
    {
      bottleneckchannel.SetQueue ("ns3::CoDelQueue",
                                  "MinBytes", UintegerValue(1500)
                                  );
    }
  else if (queueType == "DropOldestQueue")
    {
      bottleneckchannel.SetQueue ("ns3::DropOldestQueue");
    }

  UniformVariable shortrtt (1,2);
  UniformVariable longrtt (160,166);

  // order devices are put in here ends up being numbering order
  std::vector<NetDeviceContainer> deviceAdjacencyList (2 * N + 1);
  for(uint32_t i = 0; i < deviceAdjacencyList.size () - 1; ++i)
    {
      // N-M clients have more bandwith but long RTT
      if (i > (N + M - 1))
        {
          double rn = longrtt.GetValue ();
          p2channel.SetChannelAttribute ("Delay", StringValue ((format("%gms") % rn).str()));
          deviceAdjacencyList[i] = p2channel.Install (nodeAdjacencyList[i]);
          NS_LOG_INFO ((format("device %d (%s-%s) at R2=%s, RTT %gms") % i
                        % Names::FindName(nodeAdjacencyList[i].Get (0))
                        % Names::FindName(nodeAdjacencyList[i].Get (1))
                        % R2 % rn).str());
        }
      else if ((i > (N - 1)) && (i < (N + M - 1)))
        {
          double rn = shortrtt.GetValue ();
          p1channel.SetChannelAttribute ("Delay", StringValue ((format("%gms") % rn).str()));
          deviceAdjacencyList[i] = p1channel.Install (nodeAdjacencyList[i]);
          NS_LOG_INFO ((format("device %d (%s-%s) at R1=%s, RTT %gms") % i
                        % Names::FindName(nodeAdjacencyList[i].Get (0))
                        % Names::FindName(nodeAdjacencyList[i].Get (1))
                        % R1 % rn).str());
        }
      else
        {
          deviceAdjacencyList[i] = serverchannel.Install (nodeAdjacencyList[i]);
        }
    }

  deviceAdjacencyList[2 * N] = bottleneckchannel.Install (nodeAdjacencyList[2 * N]);
  /* There is only sense to print queue stats for the bottleneck queue */
  if (writeForPlot)
    {
      WriteForPlot(deviceAdjacencyList[2 * N].Get (0), "-bottleneck");
    }

  // Later, we add IP addresses.
  NS_LOG_INFO ("Assign IP Addresses.");
  Ipv4AddressHelper ipv4;
  std::vector<Ipv4InterfaceContainer> interfaceAdjacencyList (2 * N + 1);
  for(uint32_t i = 0; i < interfaceAdjacencyList.size (); ++i)
    {
      std::ostringstream subnet;
      subnet<<"10.1."<<i+1<<".0";
      NS_LOG_INFO (subnet.str ().c_str ());
      ipv4.SetBase (subnet.str ().c_str (), "255.255.255.0");
      interfaceAdjacencyList[i] = ipv4.Assign (deviceAdjacencyList[i]);
    }

  //Turn on global static routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


  // Create the OnOff applications to send TCP to the server
  // OnOffHelper clientHelper ("ns3::TcpSocketFactory", Address ());
  // clientHelper.SetAttribute
  //   ("OnTime", RandomVariableValue (ConstantVariable (1)));
  // clientHelper.SetAttribute
  //   ("OffTime", RandomVariableValue (ConstantVariable (0)));

  BulkSendHelper clientHelper ("ns3::TcpSocketFactory", Address ());
  // Set the amount of data to send in bytes.  Zero is unlimited.
  clientHelper.SetAttribute ("MaxBytes", UintegerValue (maxBytes));


  //normally wouldn't need a loop here but the server IP address is different
  //on each p2p subnet
  ApplicationContainer clientApps;
  ApplicationContainer sinkApps;
  uint16_t portStart = 5000;
  std::vector<uint16_t> port;
  std::vector<PacketSinkHelper> sinkHelper;

  for (int l = 0; l < appsPerInterface; ++l)
    {
      port.push_back(portStart);
      Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), portStart));
      sinkHelper.push_back(PacketSinkHelper("ns3::TcpSocketFactory", sinkLocalAddress));
      portStart++;
    }

  UniformVariable x (0,1);
  double rn;
  for(uint32_t i=0; i<clientNodes.GetN (); ++i)
    {
      for (int l = 0; l < appsPerInterface; ++l)
        {
          rn = x.GetValue ();
          AddressValue remoteAddress(InetSocketAddress (interfaceAdjacencyList[i].GetAddress (1), port[l]));

          clientHelper.SetAttribute ("TOS", UintegerValue((i + l) % 6));
          clientHelper.SetAttribute ("Remote", remoteAddress);

          ApplicationContainer app = clientHelper.Install (clientNodes.Get (i));
          NS_LOG_INFO ((format("Application on device %d (%s) starting at %g") % (N+i+1)
                        % Names::FindName(clientNodes.Get (i))
                        % (AppStartTime+rn)).str());

          app.Start (Seconds (AppStartTime + rn));
          app.Stop (Seconds (AppStartTime + runTime - 13 + rn));

          clientApps.Add (app);
          sinkApps.Add (sinkHelper[l].Install (serverNodes.Get (i)));
        }
    }

  sinkApps.Start (Seconds (0.01));
  sinkApps.Stop (Seconds (runTime - 10));

  PointToPointHelper pointToPoint;

  //configure tracing
  AsciiTraceHelper ascii;
  std::stringstream asciiStr;
  asciiStr << pathOut << "/tcp-qfp.tr";
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream (asciiStr.str());
  pointToPoint.EnableAsciiAll (stream);
  std::stringstream pcapStr;
  pcapStr << pathOut << "/tcp-qfp";
  pointToPoint.EnablePcapAll (pcapStr.str());

  //if (printSockStats)
  //  {
  //    Config::ConnectWithoutContext ("/NodeList/*/ApplicationList/*/$ns3::BulkSendApplication/SocketCreateTrace", MakeCallback (&SocketCreateTraceNodeCB));
  //    Config::ConnectWithoutContext ("/NodeList/*/ApplicationList/*/$ns3::PacketSink/SocketCreateTrace", MakeCallback (&SocketCreateTraceSinkCB));
  //  }
  //Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/$ns3::CoDelQueue/count",
  //                                MakeCallback(&countTrace));
  //Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/$ns3::CoDelQueue/bytesInQueue",
  //                                MakeCallback(&bytesInQueueTrace));

  Ptr<FlowMonitor> flowmon;
  if (flowMonitor)
    {
      FlowMonitorHelper flowmonHelper;
      flowmon = flowmonHelper.InstallAll ();
    }

  NS_LOG_INFO ("Run Simulation.");
  Simulator::Stop (Seconds (runTime));
  Simulator::Run ();

  if (flowMonitor)
    {
      std::stringstream stmp;
      stmp << pathOut << "/tcp-qfp.flowmon";

      flowmon->SerializeToXmlFile (stmp.str ().c_str (), false, false);
    }

  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");

  return 0;
}
