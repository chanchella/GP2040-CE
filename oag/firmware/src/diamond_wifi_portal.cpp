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
#include "pico/time.h"

#include "oag/config/diamond_config.h"
#include "oag/firmware/diamond_config_store.h"
#include "oag/firmware/output_profile_selector.h"

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

constexpr char kDashboardHtml[] = R"HTML(<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>OAG ABO GEMI</title><style>
*{box-sizing:border-box}body{margin:0;background:#080b12;color:#eef2ff;font:15px Arial}header{padding:20px;background:#111827}h1{margin:0}.w{max-width:760px;margin:auto;padding:14px}.c{background:#111827;border:1px solid #263247;border-radius:14px;padding:15px;margin-bottom:12px}h2{margin:0 0 10px;font-size:18px}label{display:block;margin:9px 0 4px;color:#cbd5e1}input,select,button{width:100%;padding:11px;border-radius:9px;border:1px solid #334155;background:#0f172a;color:white}.r{display:grid;grid-template-columns:1fr 1fr;gap:9px}.ck{display:flex;gap:8px;align-items:center}.ck input{width:auto}button{margin-top:12px;background:#e5e7eb;color:#111827;font-weight:bold}.play{background:#86efac;color:#052e16}.m{color:#94a3b8;font-size:13px}.s{min-height:18px;margin-top:8px;color:#86efac}</style>
<header><h1>OAG ABO GEMI</h1><span class=m>Direct Live Configuration</span></header><main class=w>
<section class=c><h2>Controllers</h2><label>Controller</label><select id=ci></select><label class=ck><input id=ce type=checkbox>Enable anti-drift calibration</label><div class=r><div><label>Left Deadzone</label><input id=ld type=number min=0 max=32767></div><div><label>Right Deadzone</label><input id=rd type=number min=0 max=32767></div></div><button id=sc>Save Controller</button><div class=s id=cs></div></section>
<section class=c><h2>Profiles</h2><div class=r><div><label>Active Game</label><select id=ag></select></div><div><label>Active Weapon</label><select id=aw></select></div></div><label class=ck><input id=ge type=checkbox>Enable selected game</label><button id=sp>Save Profile</button><div class=s id=ps></div></section>
<section class=c><h2>Coming Next</h2><p class=m>Games · Weapons · Recoil · Combos · Input Bindings · Live Calibration</p></section>
<section class=c><h2>System</h2><p id=gn class=m>Loading...</p><button class=play id=go>SAVE & PLAY</button><div class=s id=os></div><p class=m>Save to A/B Flash and restart into PC/Gaming mode.</p></section></main><script>
const q=x=>document.getElementById(x);let c;
function fs(e,n,t){for(let i=1;i<=n;i++){let o=document.createElement('option');o.value=i;o.textContent='OAG ABO GEMI '+t+' '+i;e.appendChild(o)}}
fs(q('ci'),8,'CONTROLLER');fs(q('ag'),8,'GAME');fs(q('aw'),24,'WEAPON');
async function load(){let r=await fetch('/api/config',{cache:'no-store'});c=await r.json();q('gn').textContent='Flash generation: '+c.generation;q('ag').value=c.activeGame+1;q('aw').value=c.activeWeapon+1;dc();dg()}
function dc(){if(!c)return;let x=c.controllers[+q('ci').value-1];q('ce').checked=x.enabled;q('ld').value=x.leftDeadzone;q('rd').value=x.rightDeadzone}
function dg(){if(c)q('ge').checked=c.games[+q('ag').value-1].enabled}
async function post(p,d,s){q(s).textContent='Saving...';let r=await fetch(p,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(d)});let t=await r.text();q(s).textContent=r.ok?'Saved':t;if(r.ok)await load();return r.ok}
q('ci').onchange=dc;q('ag').onchange=dg;
q('sc').onclick=()=>post('/api/controller',{slot:q('ci').value,enabled:q('ce').checked?1:0,left_deadzone:q('ld').value,right_deadzone:q('rd').value},'cs');
q('sp').onclick=()=>post('/api/profile',{game:q('ag').value,weapon:q('aw').value,enabled:q('ge').checked?1:0},'ps');
q('go').onclick=async()=>{if(await post('/api/save-play',{},'os'))q('os').textContent='Saved. Restarting...'};
load().catch(()=>q('gn').textContent='Config load failed');
</script>)HTML";

static_assert(
    sizeof(kDashboardHtml) + 640u < TCP_SND_BUF,
    "Dashboard plus HTTP headers must fit in TCP_SND_BUF"
);

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
        tcp_write(client, header, static_cast<u16_t>(headerLength), TCP_WRITE_FLAG_COPY);
        tcp_write(client, body, static_cast<u16_t>(bodyLength), TCP_WRITE_FLAG_COPY);
        tcp_output(client);
    }
    tcp_close(client);
}

std::size_t contentLength(const char* request) {
    const char* header = std::strstr(request, "Content-Length:");
    if (header == nullptr) return 0;
    header += std::strlen("Content-Length:");
    while (*header == ' ') ++header;
    return static_cast<std::size_t>(std::strtoul(header, nullptr, 10));
}

bool formValue(
    const char* body,
    const char* key,
    char* output,
    std::size_t outputSize
) {
    if (!body || !key || !output || outputSize == 0) return false;
    output[0] = '\0';
    const std::size_t keyLength = std::strlen(key);
    const char* cursor = body;

    while (*cursor) {
        if (
            (cursor == body || cursor[-1] == '&') &&
            std::strncmp(cursor, key, keyLength) == 0 &&
            cursor[keyLength] == '='
        ) {
            cursor += keyLength + 1u;
            std::size_t written = 0;
            while (*cursor && *cursor != '&') {
                char c = *cursor++;
                if (c == '+') c = ' ';
                if (c == '%' && cursor[0] && cursor[1]) {
                    auto hex = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                        return -1;
                    };
                    const int hi = hex(cursor[0]);
                    const int lo = hex(cursor[1]);
                    if (hi < 0 || lo < 0) return false;
                    c = static_cast<char>((hi << 4) | lo);
                    cursor += 2;
                }
                if (
                    c == '\0' || c == '\r' || c == '\n' ||
                    written + 1u >= outputSize
                ) return false;
                output[written++] = c;
            }
            output[written] = '\0';
            return true;
        }
        const char* next = std::strchr(cursor, '&');
        if (!next) break;
        cursor = next + 1;
    }
    return false;
}

bool parseUnsigned(
    const char* body,
    const char* key,
    std::uint32_t minimum,
    std::uint32_t maximum,
    std::uint32_t& value
) {
    char raw[16] {};
    if (!formValue(body, key, raw, sizeof(raw)) || raw[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(raw, &end, 10);
    if (
        end == raw || *end != '\0' ||
        parsed < minimum || parsed > maximum
    ) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

void releaseClient(HttpClientState* state) {
    if (!state) return;
    state->client = nullptr;
    state->used = 0;
    state->inUse = false;
    state->data.fill('\0');
}

err_t httpReceive(void* rawState, tcp_pcb* client, pbuf* packet, err_t error) {
    auto* state = static_cast<HttpClientState*>(rawState);
    if (error != ERR_OK || !state) {
        if (packet) pbuf_free(packet);
        if (client) tcp_close(client);
        releaseClient(state);
        return ERR_OK;
    }
    if (!packet) {
        tcp_close(client);
        releaseClient(state);
        return ERR_OK;
    }

    const std::size_t available = state->data.size() - state->used - 1u;
    if (packet->tot_len > available) {
        tcp_recved(client, packet->tot_len);
        pbuf_free(packet);
        sendResponse(client, "413 Payload Too Large", "text/plain", "Request too large");
        releaseClient(state);
        return ERR_OK;
    }

    pbuf_copy_partial(packet, state->data.data() + state->used, packet->tot_len, 0);
    state->used += packet->tot_len;
    state->data[state->used] = '\0';
    tcp_recved(client, packet->tot_len);
    pbuf_free(packet);

    const char* headerEnd = std::strstr(state->data.data(), "\r\n\r\n");
    if (!headerEnd) return ERR_OK;

    const std::size_t headerBytes =
        static_cast<std::size_t>(headerEnd - state->data.data()) + 4u;
    const std::size_t required = headerBytes + contentLength(state->data.data());
    if (state->used < required) return ERR_OK;

    tcp_arg(client, nullptr);
    if (gPortal) {
        gPortal->handleHttpRequest(client, state->data.data(), state->used);
    } else {
        sendResponse(client, "503 Service Unavailable", "text/plain", "Portal unavailable");
    }
    releaseClient(state);
    return ERR_OK;
}

err_t httpAccept(void*, tcp_pcb* client, err_t error) {
    if (error != ERR_OK || !client) return error;
    for (auto& state : gClients) {
        if (state.inUse) continue;
        state.inUse = true;
        state.client = client;
        state.used = 0;
        state.data.fill('\0');
        tcp_arg(client, &state);
        tcp_recv(client, httpReceive);
        return ERR_OK;
    }
    sendResponse(client, "503 Service Unavailable", "text/plain", "Portal busy");
    return ERR_OK;
}

} // namespace

namespace oag::firmware {

bool DiamondWifiPortal::start(DiamondConfigStore& store) {
    if (started_) return true;

    store_ = &store;
    if (!store_->load()) return false;
    if (cyw43_arch_init() != 0) return false;

    cyw43_arch_enable_ap_mode("OAG ABO GEMI", nullptr, CYW43_AUTH_OPEN);

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
    dhcp_server_init(&gDhcpServer, &cyw43_state.netif[CYW43_ITF_AP], &gateway, &mask);
    dns_server_init(&gDnsServer, &cyw43_state.netif[CYW43_ITF_AP], &gateway);

    gHttpListener = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!gHttpListener) {
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
    if (!gHttpListener) {
        cyw43_arch_lwip_end();
        return false;
    }

    gPortal = this;
    tcp_accept(gHttpListener, httpAccept);
    cyw43_arch_lwip_end();
    started_ = true;
    return true;
}

void DiamondWifiPortal::schedulePlayReboot(std::uint32_t delayMs) {
    playRebootAtUs_ =
        time_us_64() +
        static_cast<std::uint64_t>(delayMs) * 1000ull;
    playRebootPending_ = true;
}

void DiamondWifiPortal::task() {
    if (
        playRebootPending_ &&
        time_us_64() >= playRebootAtUs_
    ) {
        playRebootPending_ = false;
        requestOutputProfile(OutputProfileId::Pc);
    }
}

void DiamondWifiPortal::handleHttpRequest(
    void* rawClient,
    const char* request,
    std::size_t requestLength
) {
    (void)requestLength;
    auto* client = static_cast<tcp_pcb*>(rawClient);
    if (!client || !request || !store_) return;

    char method[8] {};
    char path[64] {};
    if (std::sscanf(request, "%7s %63s", method, path) != 2) {
        sendResponse(client, "400 Bad Request", "text/plain", "Bad request");
        return;
    }

    const char* body = std::strstr(request, "\r\n\r\n");
    body = body ? body + 4 : "";

    auto& runtime = store_->config().runtime;

    if (
        std::strcmp(method, "GET") == 0 &&
        std::strcmp(path, "/api/status") == 0
    ) {
        char json[256] {};
        std::snprintf(
            json, sizeof(json),
            "{\"brand\":\"OAG ABO GEMI\",\"directConfig\":true,\"generation\":%lu,\"persistent\":%s}",
            static_cast<unsigned long>(store_->generation()),
            store_->loadedFromFlash() ? "true" : "false"
        );
        sendResponse(client, "200 OK", "application/json; charset=utf-8", json);
        return;
    }

    if (
        std::strcmp(method, "GET") == 0 &&
        std::strcmp(path, "/api/config") == 0
    ) {
        char json[2048] {};
        int used = std::snprintf(
            json, sizeof(json),
            "{\"generation\":%lu,\"activeGame\":%u,\"activeWeapon\":%u,\"controllers\":[",
            static_cast<unsigned long>(store_->generation()),
            static_cast<unsigned>(runtime.activeGame),
            static_cast<unsigned>(runtime.activeWeapon)
        );

        for (std::size_t i = 0; i < runtime.controllers.size(); ++i) {
            const auto& c = runtime.controllers[i];
            used += std::snprintf(
                json + used,
                sizeof(json) - static_cast<std::size_t>(used),
                "%s{\"enabled\":%s,\"leftDeadzone\":%lu,\"rightDeadzone\":%lu}",
                i == 0 ? "" : ",",
                c.enabled ? "true" : "false",
                static_cast<unsigned long>(c.left.deadzone),
                static_cast<unsigned long>(c.right.deadzone)
            );
        }

        used += std::snprintf(
            json + used,
            sizeof(json) - static_cast<std::size_t>(used),
            "],\"games\":["
        );
        for (std::size_t i = 0; i < runtime.games.size(); ++i) {
            used += std::snprintf(
                json + used,
                sizeof(json) - static_cast<std::size_t>(used),
                "%s{\"enabled\":%s}",
                i == 0 ? "" : ",",
                runtime.games[i].enabled ? "true" : "false"
            );
        }
        std::snprintf(
            json + used,
            sizeof(json) - static_cast<std::size_t>(used),
            "]}"
        );
        sendResponse(client, "200 OK", "application/json; charset=utf-8", json);
        return;
    }

    if (
        std::strcmp(method, "POST") == 0 &&
        std::strcmp(path, "/api/controller") == 0
    ) {
        std::uint32_t slot = 0, enabled = 0, leftDz = 0, rightDz = 0;
        if (
            !parseUnsigned(body, "slot", 1, oag::kDiamondControllerSlots, slot) ||
            !parseUnsigned(body, "enabled", 0, 1, enabled) ||
            !parseUnsigned(body, "left_deadzone", 0, 32767, leftDz) ||
            !parseUnsigned(body, "right_deadzone", 0, 32767, rightDz)
        ) {
            sendResponse(client, "400 Bad Request", "text/plain", "Invalid controller values");
            return;
        }

        const auto previous = runtime.controllers[slot - 1u];
        auto& controller = runtime.controllers[slot - 1u];
        controller.enabled = enabled != 0;
        controller.left.deadzone = leftDz;
        controller.right.deadzone = rightDz;

        if (!store_->save()) {
            controller = previous;
            sendResponse(client, "500 Internal Server Error", "text/plain", "Flash save failed");
            return;
        }
        sendResponse(client, "200 OK", "text/plain", "Controller saved");
        return;
    }

    if (
        std::strcmp(method, "POST") == 0 &&
        std::strcmp(path, "/api/profile") == 0
    ) {
        std::uint32_t game = 0, weapon = 0, enabled = 0;
        if (
            !parseUnsigned(body, "game", 1, oag::kDiamondGameSlots, game) ||
            !parseUnsigned(body, "weapon", 1, oag::kDiamondWeaponSlotsPerGame, weapon) ||
            !parseUnsigned(body, "enabled", 0, 1, enabled)
        ) {
            sendResponse(client, "400 Bad Request", "text/plain", "Invalid profile values");
            return;
        }

        const std::uint16_t previousGame = runtime.activeGame;
        const std::uint16_t previousWeapon = runtime.activeWeapon;
        const bool previousEnabled = runtime.games[game - 1u].enabled;

        runtime.activeGame = static_cast<std::uint16_t>(game - 1u);
        runtime.activeWeapon = static_cast<std::uint16_t>(weapon - 1u);
        runtime.games[game - 1u].enabled = enabled != 0;

        if (!store_->save()) {
            runtime.activeGame = previousGame;
            runtime.activeWeapon = previousWeapon;
            runtime.games[game - 1u].enabled = previousEnabled;
            sendResponse(client, "500 Internal Server Error", "text/plain", "Flash save failed");
            return;
        }
        sendResponse(client, "200 OK", "text/plain", "Profile saved");
        return;
    }

    if (
        std::strcmp(method, "POST") == 0 &&
        std::strcmp(path, "/api/save-play") == 0
    ) {
        if (!store_->save()) {
            sendResponse(client, "500 Internal Server Error", "text/plain", "Flash save failed");
            return;
        }
        sendResponse(client, "200 OK", "text/plain", "Saved. Restarting.");
        schedulePlayReboot(1200);
        return;
    }

    if (
        std::strcmp(method, "GET") == 0 &&
        std::strcmp(path, "/") == 0
    ) {
        sendResponse(client, "200 OK", "text/html; charset=utf-8", kDashboardHtml);
        return;
    }

    sendResponse(client, "404 Not Found", "text/plain", "Not found");
}

} // namespace oag::firmware
