#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WSN_Simulation");

// 📌 Application pour simuler l'envoi des mesures de pH
class PhSensorApp : public Application {
private:
    Ptr<Socket> m_socket;
    Address m_peer;
    EventId m_sendEvent;
    bool m_running;
    uint32_t m_packetSize;
    double m_interval;

public:
    PhSensorApp() : m_socket(0), m_running(false), m_packetSize(32), m_interval(2.0) {}

    void Setup(Ptr<Socket> socket, Address peer) {
        m_socket = socket;
        m_peer = peer;
    }

    void StartApplication() override {
        m_running = true;
        SendPacket();
    }

    void StopApplication() override {
        m_running = false;
        if (m_sendEvent.IsRunning()) {
            Simulator::Cancel(m_sendEvent);
        }
        if (m_socket) {
            m_socket->Close();
        }
    }

    void SendPacket() {
        if (!m_running) return;

        // Générer une valeur de pH aléatoire entre 6.0 et 8.0
        double phValue = 6.0 + (static_cast<double>(rand()) / RAND_MAX) * 2.0;
        std::ostringstream msg;
        msg << "pH: " << phValue;

        Ptr<Packet> packet = Create<Packet>((uint8_t*)msg.str().c_str(), msg.str().length());
        m_socket->SendTo(packet, 0, m_peer);

        // Planifier le prochain envoi
        m_sendEvent = Simulator::Schedule(Seconds(m_interval), &PhSensorApp::SendPacket, this);
    }
};

// 📌 Fonction pour afficher les paquets reçus à la gateway
void ReceivePacket(Ptr<Socket> socket) {
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from))) {
        std::ostringstream msg;
        packet->CopyData(reinterpret_cast<uint8_t*>(&msg), packet->GetSize());
        NS_LOG_INFO("Gateway received: " << msg.str());
    }
}

int main(int argc, char* argv[]) {
    LogComponentEnable("WSN_Simulation", LOG_LEVEL_INFO);

    // 1️⃣ Créer les nœuds (capteurs + gateway)
    NodeContainer sensorNodes;
    sensorNodes.Create(5);
    NodeContainer gatewayNode;
    gatewayNode.Create(1);

    // 2️⃣ Configurer le Wi-Fi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel");

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    Ssid ssid = Ssid("WSN-Network");
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));

    NetDeviceContainer sensorDevices = wifi.Install(phy, mac, sensorNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer gatewayDevice = wifi.Install(phy, mac, gatewayNode);

    // 3️⃣ Attribuer les IPs
    InternetStackHelper stack;
    stack.Install(sensorNodes);
    stack.Install(gatewayNode);

    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer sensorInterfaces = address.Assign(sensorDevices);
    Ipv4InterfaceContainer gatewayInterface = address.Assign(gatewayDevice);

    // 4️⃣ Configuration des sockets pour la transmission UDP
    uint16_t port = 50000;
    Address gatewayAddress(InetSocketAddress(gatewayInterface.GetAddress(0), port));

    // Configurer le serveur UDP sur la gateway
    Ptr<Socket> gatewaySocket = Socket::CreateSocket(gatewayNode.Get(0), UdpSocketFactory::GetTypeId());
    gatewaySocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
    gatewaySocket->SetRecvCallback(MakeCallback(&ReceivePacket));

    // Configurer les capteurs pour envoyer les valeurs de pH
    for (uint32_t i = 0; i < sensorNodes.GetN(); i++) {
        Ptr<Socket> sensorSocket = Socket::CreateSocket(sensorNodes.Get(i), UdpSocketFactory::GetTypeId());
        Ptr<PhSensorApp> app = CreateObject<PhSensorApp>();
        app->Setup(sensorSocket, gatewayAddress);
        sensorNodes.Get(i)->AddApplication(app);
        app->SetStartTime(Seconds(2.0));
        app->SetStopTime(Seconds(10.0));
    }

    // 5️⃣ Configurer la mobilité
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(0.0),
                                  "MinY", DoubleValue(0.0),
                                  "DeltaX", DoubleValue(10.0),
                                  "DeltaY", DoubleValue(10.0),
                                  "GridWidth", UintegerValue(3),
                                  "LayoutType", StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(sensorNodes);
    mobility.Install(gatewayNode);

    // 6️⃣ Ajouter NetAnim pour la visualisation
    AnimationInterface anim("wsn_animation.xml");
    anim.SetMaxPktsPerTraceFile(50000);
    
    // 7️⃣ Démarrer la simulation
    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
