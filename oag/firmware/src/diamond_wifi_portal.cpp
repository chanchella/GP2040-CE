#include "oag/firmware/diamond_wifi_portal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "dhcpserver.h"
#include "dnsserver.h"
}

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"

#include "oag/firmware/diamond_config_store.h"

namespace {

using oag::firmware::DiamondWifiPortal;

dhcp_server_t gDhcpServer {};
dns_server_t gDnsServer {};
tcp_pcb* gHttpListener = nullptr;
DiamondWifiPortal* gPortal = nullptr;

constexpr std::size_t kHttpBufferBytes = 3072;
constexpr std::size_t kHttpClientSlots = 2;

struct HttpClientState {
    tcp_pcb* client = nullptr;
    std::size_t used = 0;
    bool inUse = false;
    std::array<char, kHttpBufferBytes> data {};
};

std::array<HttpClientState, kHttpClientSlots> gClients {};

constexpr char kDashboardHtml[] = R"HTML(<!doctype html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'><title>OAG ABO GEMI</title><style>
*{box-sizing:border-box}body{margin:0;background:#080b12;color:#eef2ff;font-family:Arial,sans-serif}
header{padding:28px 20px;background:#111827;border-bottom:1px solid #283247}h1{margin:0;font-size:30px}
small{color:#94a3b8}.wrap{max-width:980px;margin:auto;padding:20px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px}
.card{background:#111827;border:1px solid #263247;border-radius:16px;padding:18px}
.tag{display:inline-block;padding:5px 9px;border-radius:99px;background:#1f2937;margin:3px 3px 3px 0}
h2{font-size:18px;margin:0 0 12px}.ok{color:#86efac}.muted{color:#94a3b8}
</style></head><body><header><h1>OAG ABO GEMI</h1><small>Direct Configuration Portal</small></header>
<main class='wrap'><div class='grid'>
<section class='card'><h2>Controllers</h2><div class='tag' data-oag-controller='Controller'>Controller</div><p class='muted'>Live stick monitor, center calibration and anti-drift.</p></section>
<section class='card'><h2>Games</h2><p class='muted'>Add and manage game profiles.</p></section>
<section class='card'><h2>Weapons</h2><div class='tag' data-oag-number='1' data-oag-type='WEAPON'>OAG ABO GEMI WEAPON 1</div><p class='muted'>Weapon names remain original; numbered weapon slots use OAG ABO GEMI WEAPON.</p></section>
<section class='card'><h2>Recoil</h2><p class='muted'>Vertical, horizontal and timing configuration.</p></section>
<section class='card'><h2>Combos</h2><div class='tag' data-oag-number='1' data-oag-type='COMBO'>OAG ABO GEMI COMBO 1</div><p class='muted'>Universal actions for keyboard, mouse and controller triggers.</p></section>
<section class='card'><h2>Input Bindings</h2><p class='muted'>Native K/M, controller and touch bindings.</p></section>
<section class='card'><h2>Profiles</h2><div class='tag' data-oag-number='1'></div><div class='tag' data-oag-number='2'></div><div class='tag' data-oag-number='3'></div><p class='muted'>Game and weapon names stay natural; displayed slot numbers use OAG branding.</p></section>
<section class='card'><h2>System</h2><p class='ok'>Direct configuration active</p><p class='muted'>No username, password, login or provisioning. Persistent A/B flash storage remains available for configuration data.</p></section>
</div></main><script>
const OAG_BRAND='OAG ABO GEMI';
function oagController(n){return OAG_BRAND+' '+String(n||'Controller');}
function oagNumberedItem(n,t){return OAG_BRAND+(t?' '+String(t).toUpperCase():'')+' '+String(n);}
document.querySelectorAll('[data-oag-controller]').forEach(e=>e.textContent=oagController(e.dataset.oagController));
document.querySelectorAll('[data-oag-number]').forEach(e=>e.textContent=oagNumberedItem(e.dataset.oagNumber,e.dataset.oagType||''));
</script></body></html>)HTML";

void sendResponse(
    tcp_pcb* client,
    const char* status,
    const char* contentType,
    const char* body
) {
    if (client == nullptr || body == nullptr) {
        return;
    }

    char header[640] {};
    const std::size_t bodyLength = std::strlen(body);
    const int headerLength = std::snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Content-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; frame-ancestors 'none'\r\n"
        "Connection: close\r\n"
        "Content-Length: %u\r\n\r\n",
        status,
        contentType,
        static_cast<unsigned>(bodyLength)
    );

    if (
        headerLength > 0 &&
        static_cast<std::size_t>(headerLength) < sizeof(header)
    ) {
        tcp_write(
            client,
            header,
            static_cast<u16_t>(headerLength),
            TCP_WRITE_FLAG_COPY
        );
        tcp_write(
            client,
            body,
            static_cast<u16_t>(bodyLength),
            TCP_WRITE_FLAG_COPY
        );
        tcp_output(client);
    }

    tcp_close(client);
}

std::size_t contentLength(const char* request) {
    const char* header = std::strstr(request, "Content-Length:");
    if (header == nullptr) {
        return 0;
    }
    header += std::strlen("Content-Length:");
    while (*header == ' ') {
        ++header;
    }
    return static_cast<std::size_t>(std::strtoul(header, nullptr, 10));
}

void releaseClient(HttpClientState* state) {
    if (state == nullptr) {
        return;
    }
    state->client = nullptr;
    state->used = 0;
    state->inUse = false;
    state->data.fill('\0');
}

err_t httpReceive(
    void* rawState,
    tcp_pcb* client,
    pbuf* packet,
    err_t error
) {
    auto* state = static_cast<HttpClientState*>(rawState);

    if (error != ERR_OK || state == nullptr) {
        if (packet != nullptr) {
            pbuf_free(packet);
        }
        if (client != nullptr) {
            tcp_close(client);
        }
        releaseClient(state);
        return ERR_OK;
    }

    if (packet == nullptr) {
        tcp_close(client);
        releaseClient(state);
        return ERR_OK;
    }

    const std::size_t available =
        state->data.size() - state->used - 1u;
    if (packet->tot_len > available) {
        tcp_recved(client, packet->tot_len);
        pbuf_free(packet);
        sendResponse(
            client,
            "413 Payload Too Large",
            "text/plain",
            "Request too large"
        );
        releaseClient(state);
        return ERR_OK;
    }

    pbuf_copy_partial(
        packet,
        state->data.data() + state->used,
        packet->tot_len,
        0
    );
    state->used += packet->tot_len;
    state->data[state->used] = '\0';
    tcp_recved(client, packet->tot_len);
    pbuf_free(packet);

    const char* headerEnd =
        std::strstr(state->data.data(), "\r\n\r\n");
    if (headerEnd == nullptr) {
        return ERR_OK;
    }

    const std::size_t headerBytes =
        static_cast<std::size_t>(
            headerEnd - state->data.data()
        ) + 4u;
    const std::size_t required =
        headerBytes + contentLength(state->data.data());
    if (state->used < required) {
        return ERR_OK;
    }

    tcp_arg(client, nullptr);
    if (gPortal != nullptr) {
        gPortal->handleHttpRequest(
            client,
            state->data.data(),
            state->used
        );
    } else {
        sendResponse(
            client,
            "503 Service Unavailable",
            "text/plain",
            "Portal unavailable"
        );
    }
    releaseClient(state);
    return ERR_OK;
}

err_t httpAccept(void*, tcp_pcb* client, err_t error) {
    if (error != ERR_OK || client == nullptr) {
        return error;
    }

    for (auto& state : gClients) {
        if (state.inUse) {
            continue;
        }
        state.inUse = true;
        state.client = client;
        state.used = 0;
        state.data.fill('\0');
        tcp_arg(client, &state);
        tcp_recv(client, httpReceive);
        return ERR_OK;
    }

    sendResponse(
        client,
        "503 Service Unavailable",
        "text/plain",
        "Portal busy"
    );
    return ERR_OK;
}

} // namespace

namespace oag::firmware {

bool DiamondWifiPortal::start(DiamondConfigStore& store) {
    if (started_) {
        return true;
    }

    store_ = &store;
    if (!store_->load()) {
        return false;
    }

    if (cyw43_arch_init() != 0) {
        return false;
    }

    cyw43_arch_enable_ap_mode(
        "OAG ABO GEMI",
        nullptr,
        CYW43_AUTH_OPEN
    );

    ip_addr_t gateway {};
    ip_addr_t mask {};
#if LWIP_IPV6
    gateway.u_addr.ip4.addr =
        PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    mask.u_addr.ip4.addr =
        PP_HTONL(CYW43_DEFAULT_IP_MASK);
#else
    gateway.addr =
        PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    mask.addr =
        PP_HTONL(CYW43_DEFAULT_IP_MASK);
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

    if (
        tcp_bind(
            gHttpListener,
            IP_ANY_TYPE,
            80
        ) != ERR_OK
    ) {
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

    gPortal = this;
    tcp_accept(gHttpListener, httpAccept);
    cyw43_arch_lwip_end();

    started_ = true;
    return true;
}

void DiamondWifiPortal::handleHttpRequest(
    void* rawClient,
    const char* request,
    std::size_t requestLength
) {
    (void)requestLength;
    auto* client = static_cast<tcp_pcb*>(rawClient);
    if (
        client == nullptr ||
        request == nullptr ||
        store_ == nullptr
    ) {
        return;
    }

    char method[8] {};
    char path[64] {};
    if (
        std::sscanf(
            request,
            "%7s %63s",
            method,
            path
        ) != 2
    ) {
        sendResponse(
            client,
            "400 Bad Request",
            "text/plain",
            "Bad request"
        );
        return;
    }

    if (
        std::strcmp(method, "GET") == 0 &&
        std::strcmp(path, "/api/status") == 0
    ) {
        char json[256] {};
        std::snprintf(
            json,
            sizeof(json),
            "{\"brand\":\"OAG ABO GEMI\",\"directConfig\":true,\"generation\":%lu,\"persistent\":%s}",
            static_cast<unsigned long>(
                store_->generation()
            ),
            store_->loadedFromFlash()
                ? "true"
                : "false"
        );
        sendResponse(
            client,
            "200 OK",
            "application/json; charset=utf-8",
            json
        );
        return;
    }

    if (
        std::strcmp(method, "GET") == 0 &&
        std::strcmp(path, "/") == 0
    ) {
        sendResponse(
            client,
            "200 OK",
            "text/html; charset=utf-8",
            kDashboardHtml
        );
        return;
    }

    sendResponse(
        client,
        "404 Not Found",
        "text/plain",
        "Not found"
    );
}

} // namespace oag::firmware
