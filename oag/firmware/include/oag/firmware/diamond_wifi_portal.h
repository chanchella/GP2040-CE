#pragma once

#include <cstdint>

struct tcp_pcb;
struct udp_pcb;
struct pbuf;
struct ip_addr;

namespace oag::firmware {

class DiamondWifiPortal {
public:
    bool start();
    bool started() const { return started_; }

private:
    static void dhcpReceiveThunk(
        void* arg,
        udp_pcb* pcb,
        pbuf* packet,
        const ip_addr* address,
        std::uint16_t port);

    static err_t acceptThunk(void* arg, tcp_pcb* client, err_t error);
    static err_t receiveThunk(
        void* arg,
        tcp_pcb* client,
        pbuf* packet,
        err_t error);

    void handleDhcp(udp_pcb* pcb, pbuf* packet);
    void handleHttp(tcp_pcb* client, pbuf* packet);
    bool startDhcp();
    bool startHttp();

    bool started_ = false;
    udp_pcb* dhcpPcb_ = nullptr;
    tcp_pcb* httpListener_ = nullptr;
};

} // namespace oag::firmware
