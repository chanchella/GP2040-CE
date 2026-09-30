#include "oag/firmware/diamond_wifi_portal.h"

#include <cstddef>
#include <cstring>

#include "dhcpserver.h"
#include "dnsserver.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"

namespace {

dhcp_server_t gDhcpServer {};
dns_server_t gDnsServer {};
tcp_pcb* gHttpListener = nullptr;

constexpr char kPortalHtml[] =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>OAG ABO GEMI</title><style>"
"*{box-sizing:border-box}body{margin:0;background:#080b12;color:#eef2ff;font-family:Arial,sans-serif}"
"header{padding:28px 20px;background:#111827;border-bottom:1px solid #283247}"
"h1{margin:0;font-size:30px}small{color:#94a3b8}.wrap{max-width:980px;margin:auto;padding:20px}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px}"
".card{background:#111827;border:1px solid #263247;border-radius:16px;padding:18px}"
".tag{display:inline-block;padding:5px 9px;border-radius:99px;background:#1f2937;margin:3px 3px 3px 0}"
"h2{font-size:18px;margin:0 0 12px}.ok{color:#86efac}.muted{color:#94a3b8}"
"</style></head><body><header><h1>OAG ABO GEMI</h1>"
"<small>OAG ABO GEMI Configuration Portal</small></header><main class='wrap'>"
"<div class='grid'>"
"<section class='card'><h2>Controllers</h2><div class='tag'>OAG ABO GEMI Controller - OAG 1</div>"
"<p class='muted'>Live stick monitor, center calibration and anti-drift.</p></section>"
"<section class='card'><h2>Games</h2><p class='muted'>Add and manage game profiles.</p></section>"
"<section class='card'><h2>Weapons</h2><p class='muted'>Weapon profiles keep their real weapon names.</p></section>"
"<section class='card'><h2>Recoil</h2><p class='muted'>Vertical, horizontal and timing configuration.</p></section>"
"<section class='card'><h2>Combos</h2><p class='muted'>Universal actions for keyboard, mouse and controller triggers.</p></section>"
"<section class='card'><h2>Input Bindings</h2><p class='muted'>Native K/M, controller and touch bindings.</p></section>"
"<section class='card'><h2>Profiles</h2><div class='tag'>OAG 1</div><div class='tag'>OAG 2</div>"
"<div class='tag'>OAG 3</div><p class='muted'>Games and weapons keep their original names.</p></section>"
"<section class='card'><h2>System</h2><p class='ok'>OAG ABO GEMI portal online</p>"
"<p class='muted'>Backup, restore and Save & Play will live here.</p></section>"
"</div></main></body></html>";

err_t httpReceive(void*, tcp_pcb* client, pbuf* packet, err_t error) {
    if (error != ERR_OK || packet == nullptr) {
        if (packet != nullptr) {
            pbuf_free(packet);
        }
        tcp_close(client);
        return ERR_OK;
    }

    tcp_recved(client, packet->tot_len);
    pbuf_free(packet);

    char header[160] {};
    const int headerLength = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: %u\r\n\r\n",
        static_cast<unsigned>(sizeof(kPortalHtml) - 1u)
    );

    if (headerLength > 0) {
        tcp_write(client, header, static_cast<u16_t>(headerLength), TCP_WRITE_FLAG_COPY);
        tcp_write(
            client,
            kPortalHtml,
            static_cast<u16_t>(sizeof(kPortalHtml) - 1u),
            TCP_WRITE_FLAG_COPY
        );
        tcp_output(client);
    }

    tcp_close(client);
    return ERR_OK;
}

err_t httpAccept(void*, tcp_pcb* client, err_t error) {
    if (error != ERR_OK || client == nullptr) {
        return error;
    }

    tcp_recv(client, httpReceive);
    return ERR_OK;
}

} // namespace

namespace oag::firmware {

bool DiamondWifiPortal::start() {
    if (started_) {
        return true;
    }

    if (cyw43_arch_init() != 0) {
        return false;
    }

    cyw43_arch_enable_ap_mode(
        "OAG ABO GEMI",
        "OAGABOGEMI",
        CYW43_AUTH_WPA2_AES_PSK
    );

    ip_addr_t gateway {};
    ip_addr_t mask {};
#if LWIP_IPV6
    gateway.u_addr.ip4.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    mask.u_addr.ip4.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
#else
    gateway.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
#endif

    cyw43_arch_lwip_begin();

    dhcp_server_init(
        &gDhcpServer,
        &cyw43_state.netif[CYW43_ITF_AP],
        &gateway,
        &mask
    );
    dns_server_init(
        &gDnsServer,
        &cyw43_state.netif[CYW43_ITF_AP],
        &gateway
    );

    gHttpListener = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (gHttpListener == nullptr) {
        cyw43_arch_lwip_end();
        return false;
    }

    if (tcp_bind(gHttpListener, IP_ANY_TYPE, 80) != ERR_OK) {
        tcp_close(gHttpListener);
        gHttpListener = nullptr;
        cyw43_arch_lwip_end();
        return false;
    }

    gHttpListener = tcp_listen(gHttpListener);
    if (gHttpListener == nullptr) {
        cyw43_arch_lwip_end();
        return false;
    }

    tcp_accept(gHttpListener, httpAccept);
    cyw43_arch_lwip_end();

    started_ = true;
    return true;
}

} // namespace oag::firmware
