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

constexpr char kDashboardHtml[] = R"HTML(<!doctype html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'><title>OAG ABO GEMI</title><style>
*{box-sizing:border-box}body{margin:0;background:#080b12;color:#eef2ff;font-family:Arial,sans-serif}
header{padding:24px 18px;background:#111827;border-bottom:1px solid #283247}h1{margin:0;font-size:30px}
small,.muted{color:#94a3b8}.wrap{max-width:1040px;margin:auto;padding:18px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(285px,1fr));gap:14px}
.card{background:#111827;border:1px solid #263247;border-radius:16px;padding:18px}
.tag{display:inline-block;padding:5px 9px;border-radius:99px;background:#1f2937;margin:3px}
h2{font-size:18px;margin:0 0 12px}.ok{color:#86efac}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
label{display:block;margin:10px 0 5px;font-size:13px;color:#cbd5e1}
input,select{width:100%;padding:10px;border-radius:9px;border:1px solid #334155;background:#0f172a;color:#fff}
.check{display:flex;gap:8px;align-items:center;margin-top:12px}.check input{width:auto}
button{padding:11px 14px;border:0;border-radius:9px;font-weight:800;cursor:pointer;margin-top:12px}
.primary{background:#e5e7eb;color:#111827}.play{width:100%;background:#86efac;color:#052e16;font-size:16px}
.status{min-height:20px;margin-top:10px;font-size:13px}.future{opacity:.7}
</style></head><body><header><h1>OAG ABO GEMI</h1><small>Direct Live Configuration</small></header>
<main class='wrap'><div class='grid'>
<section class='card'><h2>Controllers</h2>
<label>Controller</label><select id='controllerIndex'></select>
<label class='check'><input id='calEnabled' type='checkbox'> Enable anti-drift calibration</label>
<div class='row'><div><label>Left Stick Deadzone</label><input id='leftDz' type='number' min='0' max='32767'></div>
<div><label>Right Stick Deadzone</label><input id='rightDz' type='number' min='0' max='32767'></div></div>
<button class='primary' id='saveController'>Save Controller</button><div class='status' id='controllerStatus'></div>
<p class='muted'>Center capture/live monitor comes in the next calibration stage.</p></section>

<section class='card'><h2>Profiles</h2>
<div class='row'><div><label>Active Game</label><select id='activeGame'></select></div>
<div><label>Active Weapon</label><select id='activeWeapon'></select></div></div>
<label class='check'><input id='gameEnabled' type='checkbox'> Enable selected game profile</label>
<button class='primary' id='saveProfile'>Save Profile</button><div class='status' id='profileStatus'></div>
<p class='muted'>Game names stay natural. Numbered slots use OAG branding.</p></section>

<section class='card future'><h2>Games</h2><p class='muted'>Game naming and per-game settings are reserved for the next stage.</p></section>
<section class='card future'><h2>Weapons</h2><div class='tag'>OAG ABO GEMI WEAPON 1</div><p class='muted'>24 weapon slots are already reserved per game.</p></section>
<section class='card future'><h2>Recoil</h2><p class='muted'>Per-weapon horizontal, vertical and timing controls are reserved.</p></section>
<section class='card future'><h2>Combos</h2><div class='tag'>OAG ABO GEMI COMBO 1</div><p class='muted'>Universal action bindings and timed macros are reserved.</p></section>
<section class='card future'><h2>Input Bindings</h2><p class='muted'>Keyboard, mouse and controller action triggers are reserved.</p></section>

<section class='card'><h2>System</h2><p class='ok'>A/B persistent configuration active</p>
<p class='muted' id='generation'>Loading configuration...</p>
<button class='play' id='savePlay'>SAVE & PLAY</button><div class='status' id='playStatus'></div>
<p class='muted'>Saves the current configuration, exits Config Mode and restarts into PC/Gaming mode.</p></section>
</div></main><script>
const $=id=>document.getElementById(id);let cfg=null;
function fillSelect(el,count,label){el.innerHTML='';for(let i=1;i<=count;i++){const o=document.createElement('option');o.value=i;o.textContent='OAG ABO GEMI '+label+' '+i;el.appendChild(o);}}
fillSelect($('controllerIndex'),8,'CONTROLLER');fillSelect($('activeGame'),8,'GAME');fillSelect($('activeWeapon'),24,'WEAPON');
async function load(){const r=await fetch('/api/config',{cache:'no-store'});cfg=await r.json();$('generation').textContent='Flash generation: '+cfg.generation;
$('activeGame').value=cfg.activeGame+1;$('activeWeapon').value=cfg.activeWeapon+1;showController();showGame();}
function showController(){if(!cfg)return;const i=+$('controllerIndex').value-1,c=cfg.controllers[i];$('calEnabled').checked=c.enabled;$('leftDz').value=c.leftDeadzone;$('rightDz').value=c.rightDeadzone;}
function showGame(){if(!cfg)return;const i=+$('activeGame').value-1;$('gameEnabled').checked=cfg.games[i].enabled;}
async function post(path,data,status){$(status).textContent='Saving...';const body=new URLSearchParams(data);const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const t=await r.text();if(!r.ok){$(status).textContent=t;return false;}$(status).textContent='Saved';await load();return true;}
$('controllerIndex').onchange=showController;$('activeGame').onchange=showGame;
$('saveController').onclick=()=>post('/api/controller',{slot:$('controllerIndex').value,enabled:$('calEnabled').checked?'1':'0',left_deadzone:$('leftDz').value,right_deadzone:$('rightDz').value},'controllerStatus');
$('saveProfile').onclick=()=>post('/api/profile',{game:$('activeGame').value,weapon:$('activeWeapon').value,enabled:$('gameEnabled').checked?'1':'0'},'profileStatus');
$('savePlay').onclick=async()=>{const ok=await post('/api/save-play',{},'playStatus');if(ok)$('playStatus').textContent='Saved. Restarting into Gaming Mode...';};
load().catch(()=>{$('generation').textContent='Could not load configuration';});
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
