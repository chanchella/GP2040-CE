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
#include "hardware/watchdog.h"
#include "pico/rand.h"
#include "pico/time.h"

#include "oag/firmware/diamond_config_store.h"
#include "oag/firmware/diamond_password_service.h"

namespace {

using oag::firmware::DiamondWifiPortal;

dhcp_server_t gDhcpServer {};
dns_server_t gDnsServer {};
tcp_pcb* gHttpListener = nullptr;
DiamondWifiPortal* gPortal = nullptr;

constexpr std::size_t kHttpBufferBytes = 3072;
constexpr std::size_t kHttpClientSlots = 2;
constexpr std::uint64_t kSessionLifetimeUs = 30ull * 60ull * 1000000ull;
constexpr std::uint64_t kLoginBlockUs = 30ull * 1000000ull;
constexpr std::uint8_t kMaxLoginFailures = 5;
constexpr char kDefaultApPassword[] = "OAGABOGEMI";

struct HttpClientState {
    tcp_pcb* client = nullptr;
    std::size_t used = 0;
    bool inUse = false;
    std::array<char, kHttpBufferBytes> data {};
};

std::array<HttpClientState, kHttpClientSlots> gClients {};

constexpr char kCommonStyle[] =
    "body{margin:0;background:#080b12;color:#eef2ff;font-family:Arial,sans-serif}"
    "*{box-sizing:border-box}header{padding:28px 20px;background:#111827;border-bottom:1px solid #283247}"
    "h1{margin:0;font-size:30px}.wrap{max-width:980px;margin:auto;padding:20px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px}"
    ".card{background:#111827;border:1px solid #263247;border-radius:16px;padding:18px}"
    ".tag{display:inline-block;padding:5px 9px;border-radius:99px;background:#1f2937;margin:3px}"
    ".muted,small{color:#94a3b8}.ok{color:#86efac}.bad{color:#fca5a5}"
    "label{display:block;margin:14px 0 6px}input{width:100%;padding:12px;border-radius:10px;"
    "border:1px solid #334155;background:#0f172a;color:#fff}button,.button{display:inline-block;"
    "margin-top:16px;padding:12px 16px;border:0;border-radius:10px;background:#e5e7eb;color:#111827;"
    "font-weight:700;text-decoration:none}h2{font-size:18px;margin:0 0 12px}";

constexpr char kProvisionHtml[] = R"HTML(<!doctype html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>OAG ABO GEMI Setup</title><style>)HTML"
    R"HTML(body{margin:0;background:#080b12;color:#eef2ff;font-family:Arial,sans-serif}*{box-sizing:border-box}.wrap{max-width:560px;margin:40px auto;padding:20px}.card{background:#111827;border:1px solid #263247;border-radius:18px;padding:22px}label{display:block;margin:14px 0 6px}input{width:100%;padding:12px;border-radius:10px;border:1px solid #334155;background:#0f172a;color:#fff}button{width:100%;margin-top:18px;padding:13px;border:0;border-radius:10px;font-weight:700}.muted{color:#94a3b8}.brand{font-size:30px;font-weight:800})HTML"
    R"HTML(</style></head><body><main class='wrap'><section class='card'>
<div class='brand'>OAG ABO GEMI</div><p class='muted'>First-time secure provisioning for this Pico.</p>
<form method='post' action='/provision'>
<label>Admin username</label><input name='username' minlength='3' maxlength='24' required autocomplete='username'>
<label>Admin password</label><input name='password' type='password' minlength='8' maxlength='63' required autocomplete='new-password'>
<label>Confirm admin password</label><input name='confirm' type='password' minlength='8' maxlength='63' required autocomplete='new-password'>
<label>Wi-Fi password (optional)</label><input name='wifi' type='password' minlength='8' maxlength='63' autocomplete='new-password'>
<p class='muted'>Admin login and Wi-Fi password are separate. If Wi-Fi password is left empty, the current setup password stays in use.</p>
<button type='submit'>Provision OAG ABO GEMI</button></form></section></main></body></html>)HTML";

constexpr char kLoginHtml[] = R"HTML(<!doctype html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>OAG ABO GEMI Login</title><style>)HTML"
    R"HTML(body{margin:0;background:#080b12;color:#eef2ff;font-family:Arial,sans-serif}*{box-sizing:border-box}.wrap{max-width:520px;margin:55px auto;padding:20px}.card{background:#111827;border:1px solid #263247;border-radius:18px;padding:22px}label{display:block;margin:14px 0 6px}input{width:100%;padding:12px;border-radius:10px;border:1px solid #334155;background:#0f172a;color:#fff}button{width:100%;margin-top:18px;padding:13px;border:0;border-radius:10px;font-weight:700}.muted{color:#94a3b8}.brand{font-size:30px;font-weight:800})HTML"
    R"HTML(</style></head><body><main class='wrap'><section class='card'>
<div class='brand'>OAG ABO GEMI</div><p class='muted'>Configuration Portal Login</p>
<form method='post' action='/login'><label>Username</label><input name='username' maxlength='24' required autocomplete='username'>
<label>Password</label><input name='password' type='password' maxlength='63' required autocomplete='current-password'>
<button type='submit'>Login</button></form></section></main></body></html>)HTML";

constexpr char kWifiRestartHtml[] = R"HTML(<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>OAG ABO GEMI Wi-Fi Updated</title><style>body{margin:0;background:#080b12;color:#eef2ff;font-family:Arial,sans-serif}.wrap{max-width:560px;margin:55px auto;padding:20px}.card{background:#111827;border:1px solid #263247;border-radius:18px;padding:22px}.ok{color:#86efac}.muted{color:#94a3b8}</style></head><body><main class='wrap'><section class='card'><h1>OAG ABO GEMI</h1><h2 class='ok'>Wi-Fi password saved</h2><p>The Pico will restart now.</p><p class='muted'>Reconnect to OAG ABO GEMI using the new Wi-Fi password, then open 192.168.4.1 and log in again.</p></section></main></body></html>)HTML";

constexpr char kDashboardHtml[] = R"HTML(<!doctype html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'><title>OAG ABO GEMI</title><style>)HTML"
    R"HTML(*{box-sizing:border-box}body{margin:0;background:#080b12;color:#eef2ff;font-family:Arial,sans-serif}header{padding:28px 20px;background:#111827;border-bottom:1px solid #283247}h1{margin:0;font-size:30px}small{color:#94a3b8}.wrap{max-width:980px;margin:auto;padding:20px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px}.card{background:#111827;border:1px solid #263247;border-radius:16px;padding:18px}.tag{display:inline-block;padding:5px 9px;border-radius:99px;background:#1f2937;margin:3px 3px 3px 0}h2{font-size:18px;margin:0 0 12px}.ok{color:#86efac}.muted{color:#94a3b8}button{padding:10px 14px;border:0;border-radius:9px;font-weight:700})HTML"
    R"HTML(</style></head><body><header><h1>OAG ABO GEMI</h1><small>Authenticated Configuration Portal</small></header><main class='wrap'>
<div class='grid'>
<section class='card'><h2>Controllers</h2><div class='tag' data-oag-controller='Controller'>Controller</div><p class='muted'>Live stick monitor, center calibration and anti-drift.</p></section>
<section class='card'><h2>Games</h2><p class='muted'>Add and manage game profiles.</p></section>
<section class='card'><h2>Weapons</h2><div class='tag' data-oag-number='1' data-oag-type='WEAPON'>OAG ABO GEMI WEAPON 1</div><p class='muted'>Weapon names remain original; numbered weapon slots use OAG ABO GEMI WEAPON.</p></section>
<section class='card'><h2>Recoil</h2><p class='muted'>Vertical, horizontal and timing configuration.</p></section>
<section class='card'><h2>Combos</h2><div class='tag' data-oag-number='1' data-oag-type='COMBO'>OAG ABO GEMI COMBO 1</div><p class='muted'>Universal actions for keyboard, mouse and controller triggers.</p></section>
<section class='card'><h2>Input Bindings</h2><p class='muted'>Native K/M, controller and touch bindings.</p></section>
<section class='card'><h2>Profiles</h2><div class='tag' data-oag-number='1'></div><div class='tag' data-oag-number='2'></div><div class='tag' data-oag-number='3'></div><p class='muted'>Game and weapon names stay natural; displayed slot numbers use OAG branding.</p></section>
<section class='card'><h2>System / Security</h2><p class='ok'>Persistent secure config online</p><p class='muted'>A/B flash storage, admin authentication, CSRF protection and session protection are active.</p>
<form method='post' action='/system/admin'><input type='hidden' name='_csrf'>
<label>New admin username (optional)</label><input name='new_username' minlength='3' maxlength='24' autocomplete='username'>
<label>Current admin password</label><input name='current_password' type='password' minlength='8' maxlength='63' required autocomplete='current-password'>
<label>New admin password (optional)</label><input name='new_password' type='password' minlength='8' maxlength='63' autocomplete='new-password'>
<label>Confirm new admin password</label><input name='confirm_password' type='password' maxlength='63' autocomplete='new-password'>
<button type='submit'>Update Admin Login</button></form>
<hr style='border:0;border-top:1px solid #263247;margin:20px 0'>
<form method='post' action='/system/wifi'><input type='hidden' name='_csrf'>
<label>Current admin password</label><input name='current_password' type='password' minlength='8' maxlength='63' required autocomplete='current-password'>
<label>New Wi-Fi password</label><input name='wifi_password' type='password' minlength='8' maxlength='63' required autocomplete='new-password'>
<label>Confirm Wi-Fi password</label><input name='confirm_wifi' type='password' minlength='8' maxlength='63' required autocomplete='new-password'>
<button type='submit'>Save Wi-Fi & Restart</button></form>
<form method='post' action='/logout'><input type='hidden' name='_csrf'><button type='submit'>Logout</button></form></section>
</div></main><script>const OAG_BRAND='OAG ABO GEMI';function oagController(n){return OAG_BRAND+' '+String(n||'Controller');}function oagNumberedItem(n,t){return OAG_BRAND+(t?' '+String(t).toUpperCase():'')+' '+String(n);}document.querySelectorAll('[data-oag-controller]').forEach(e=>e.textContent=oagController(e.dataset.oagController));document.querySelectorAll('[data-oag-number]').forEach(e=>e.textContent=oagNumberedItem(e.dataset.oagNumber,e.dataset.oagType||''));function readCookie(n){const p=document.cookie.split(';').map(v=>v.trim()).find(v=>v.startsWith(n+'='));return p?decodeURIComponent(p.substring(n.length+1)):'';}const csrf=readCookie('OAGCSRF');document.querySelectorAll("input[name='_csrf']").forEach(e=>e.value=csrf);</script></body></html>)HTML";

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decodeFormValue(
    const char* body,
    const char* key,
    char* output,
    std::size_t outputSize
) {
    if (body == nullptr || key == nullptr || output == nullptr || outputSize == 0) {
        return false;
    }

    output[0] = '\0';
    const std::size_t keyLength = std::strlen(key);
    const char* cursor = body;

    while (*cursor != '\0') {
        if (
            (cursor == body || cursor[-1] == '&') &&
            std::strncmp(cursor, key, keyLength) == 0 &&
            cursor[keyLength] == '='
        ) {
            cursor += keyLength + 1u;
            std::size_t written = 0;

            while (*cursor != '\0' && *cursor != '&') {
                char decoded = *cursor++;
                if (decoded == '+') {
                    decoded = ' ';
                } else if (decoded == '%' && cursor[0] != '\0' && cursor[1] != '\0') {
                    const int high = hexValue(cursor[0]);
                    const int low = hexValue(cursor[1]);
                    if (high < 0 || low < 0) {
                        return false;
                    }
                    decoded = static_cast<char>((high << 4) | low);
                    cursor += 2;
                }

                if (
                    decoded == '\0' ||
                    decoded == '\r' ||
                    decoded == '\n' ||
                    written + 1u >= outputSize
                ) {
                    return false;
                }
                output[written++] = decoded;
            }

            output[written] = '\0';
            return true;
        }

        const char* next = std::strchr(cursor, '&');
        if (next == nullptr) {
            break;
        }
        cursor = next + 1;
    }

    return false;
}

void sendResponse(
    tcp_pcb* client,
    const char* status,
    const char* contentType,
    const char* body,
    const char* extraHeaders = nullptr
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
        "Content-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'\r\n"
        "%s"
        "Connection: close\r\n"
        "Content-Length: %u\r\n\r\n",
        status,
        contentType,
        extraHeaders == nullptr ? "" : extraHeaders,
        static_cast<unsigned>(bodyLength)
    );

    if (headerLength > 0 && static_cast<std::size_t>(headerLength) < sizeof(header)) {
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

void sendRedirect(
    tcp_pcb* client,
    const char* location,
    const char* cookieHeader = nullptr
) {
    char extra[320] {};
    std::snprintf(
        extra,
        sizeof(extra),
        "Location: %s\r\n%s",
        location,
        cookieHeader == nullptr ? "" : cookieHeader
    );
    sendResponse(client, "303 See Other", "text/plain", "Redirecting", extra);
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

bool requestCookieEquals(
    const char* request,
    const char* name,
    const char* value
) {
    if (request == nullptr || name == nullptr || value == nullptr) {
        return false;
    }

    const char* headerEnd = std::strstr(request, "\r\n\r\n");
    if (headerEnd == nullptr) {
        return false;
    }

    char expected[96] {};
    const int expectedLength = std::snprintf(
        expected,
        sizeof(expected),
        "%s=%s",
        name,
        value
    );
    if (
        expectedLength <= 0 ||
        static_cast<std::size_t>(expectedLength) >= sizeof(expected)
    ) {
        return false;
    }

    const char* line = request;
    while (line < headerEnd) {
        const char* lineEnd = std::strstr(line, "\r\n");
        if (lineEnd == nullptr || lineEnd > headerEnd) {
            lineEnd = headerEnd;
        }

        static constexpr char kCookieHeader[] = "Cookie:";
        if (
            static_cast<std::size_t>(lineEnd - line) >=
                sizeof(kCookieHeader) - 1u &&
            std::strncmp(
                line,
                kCookieHeader,
                sizeof(kCookieHeader) - 1u
            ) == 0
        ) {
            const char* cursor = line + sizeof(kCookieHeader) - 1u;
            while (cursor < lineEnd) {
                while (
                    cursor < lineEnd &&
                    (*cursor == ' ' || *cursor == ';')
                ) {
                    ++cursor;
                }

                const std::size_t remaining =
                    static_cast<std::size_t>(lineEnd - cursor);
                if (
                    remaining >= static_cast<std::size_t>(expectedLength) &&
                    std::strncmp(
                        cursor,
                        expected,
                        static_cast<std::size_t>(expectedLength)
                    ) == 0
                ) {
                    const char* after = cursor + expectedLength;
                    if (
                        after == lineEnd ||
                        *after == ';' ||
                        *after == ' '
                    ) {
                        return true;
                    }
                }

                const char* separator =
                    static_cast<const char*>(
                        std::memchr(
                            cursor,
                            ';',
                            static_cast<std::size_t>(lineEnd - cursor)
                        )
                    );
                if (separator == nullptr) {
                    break;
                }
                cursor = separator + 1;
            }
        }

        if (lineEnd == headerEnd) {
            break;
        }
        line = lineEnd + 2;
    }

    return false;
}

void randomHex128(char output[33]) {
    rng_128_t random {};
    get_rand_128(&random);
    const auto* bytes =
        reinterpret_cast<const std::uint8_t*>(&random);
    static constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 16; ++i) {
        output[i * 2u] = hex[(bytes[i] >> 4u) & 0x0Fu];
        output[i * 2u + 1u] = hex[bytes[i] & 0x0Fu];
    }
    output[32] = '\0';
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

err_t httpReceive(void* rawState, tcp_pcb* client, pbuf* packet, err_t error) {
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

    const char* headerEnd = std::strstr(state->data.data(), "\r\n\r\n");
    if (headerEnd == nullptr) {
        return ERR_OK;
    }

    const std::size_t headerBytes =
        static_cast<std::size_t>(headerEnd - state->data.data()) + 4u;
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

    const auto& security = store_->config().security;
    const char* apPassword = kDefaultApPassword;
    if (
        security.provisioned &&
        security.wifiPassword[0] != '\0'
    ) {
        apPassword = security.wifiPassword.data();
    }

    cyw43_arch_enable_ap_mode(
        "OAG ABO GEMI",
        apPassword,
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

    gPortal = this;
    tcp_accept(gHttpListener, httpAccept);
    cyw43_arch_lwip_end();

    started_ = true;
    return true;
}

bool DiamondWifiPortal::authorized(const char* request) const {
    return
        sessionActive_ &&
        request != nullptr &&
        time_us_64() < sessionExpiresUs_ &&
        requestCookieEquals(
            request,
            "OAGSESSION",
            sessionToken_
        );
}

bool DiamondWifiPortal::csrfAuthorized(
    const char* request,
    const char* body
) const {
    if (!authorized(request) || body == nullptr) {
        return false;
    }

    char submitted[33] {};
    if (!decodeFormValue(
        body,
        "_csrf",
        submitted,
        sizeof(submitted)
    )) {
        return false;
    }

    return
        std::strlen(submitted) == 32u &&
        std::strcmp(submitted, csrfToken_) == 0 &&
        requestCookieEquals(
            request,
            "OAGCSRF",
            csrfToken_
        );
}

void DiamondWifiPortal::createSession() {
    randomHex128(sessionToken_);
    randomHex128(csrfToken_);
    sessionExpiresUs_ = time_us_64() + kSessionLifetimeUs;
    sessionActive_ = true;
}

void DiamondWifiPortal::clearSession() {
    std::memset(sessionToken_, 0, sizeof(sessionToken_));
    std::memset(csrfToken_, 0, sizeof(csrfToken_));
    sessionExpiresUs_ = 0;
    sessionActive_ = false;
}

void DiamondWifiPortal::scheduleReboot(std::uint32_t delayMs) {
    rebootAtUs_ =
        time_us_64() +
        static_cast<std::uint64_t>(delayMs) * 1000ull;
    rebootPending_ = true;
}

void DiamondWifiPortal::task() {
    if (
        rebootPending_ &&
        time_us_64() >= rebootAtUs_
    ) {
        rebootPending_ = false;
        watchdog_reboot(0, 0, 0);
    }
}

void DiamondWifiPortal::handleHttpRequest(
    void* rawClient,
    const char* request,
    std::size_t requestLength
) {
    (void)requestLength;
    auto* client = static_cast<tcp_pcb*>(rawClient);
    if (client == nullptr || request == nullptr || store_ == nullptr) {
        return;
    }

    char method[8] {};
    char path[64] {};
    if (std::sscanf(request, "%7s %63s", method, path) != 2) {
        sendResponse(client, "400 Bad Request", "text/plain", "Bad request");
        return;
    }

    const char* body = std::strstr(request, "\r\n\r\n");
    body = body == nullptr ? "" : body + 4;

    auto& security = store_->config().security;

    if (!security.provisioned) {
        if (
            std::strcmp(method, "POST") == 0 &&
            std::strcmp(path, "/provision") == 0
        ) {
            char username[oag::kDiamondAdminUsernameBytes] {};
            char password[64] {};
            char confirm[64] {};
            char wifi[oag::kDiamondWifiPasswordBytes] {};

            const bool fieldsOk =
                decodeFormValue(body, "username", username, sizeof(username)) &&
                decodeFormValue(body, "password", password, sizeof(password)) &&
                decodeFormValue(body, "confirm", confirm, sizeof(confirm));

            const bool wifiPresent =
                decodeFormValue(body, "wifi", wifi, sizeof(wifi));

            if (
                !fieldsOk ||
                std::strcmp(password, confirm) != 0 ||
                (wifiPresent && wifi[0] != '\0' &&
                    (std::strlen(wifi) < 8u || std::strlen(wifi) > 63u))
            ) {
                sendResponse(
                    client,
                    "400 Bad Request",
                    "text/plain",
                    "Invalid provisioning values"
                );
                return;
            }

            const auto previous = security;
            if (!DiamondPasswordService::provision(
                security,
                username,
                password
            )) {
                sendResponse(
                    client,
                    "400 Bad Request",
                    "text/plain",
                    "Username must be 3-24 chars and password 8-63 chars"
                );
                return;
            }

            if (wifiPresent && wifi[0] != '\0') {
                std::strncpy(
                    security.wifiPassword.data(),
                    wifi,
                    security.wifiPassword.size() - 1u
                );
            }

            if (!store_->save()) {
                security = previous;
                sendResponse(
                    client,
                    "500 Internal Server Error",
                    "text/plain",
                    "Config save failed"
                );
                return;
            }

            std::memset(password, 0, sizeof(password));
            std::memset(confirm, 0, sizeof(confirm));
            createSession();

            char cookie[160] {};
            std::snprintf(
                cookie,
                sizeof(cookie),
                "Set-Cookie: OAGSESSION=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=1800\r\n",
                sessionToken_
            );
            sendRedirect(client, "/", cookie);
            return;
        }

        sendResponse(
            client,
            "200 OK",
            "text/html; charset=utf-8",
            kProvisionHtml
        );
        return;
    }

    if (
        std::strcmp(method, "POST") == 0 &&
        std::strcmp(path, "/login") == 0
    ) {
        const std::uint64_t now = time_us_64();
        if (now < loginBlockedUntilUs_) {
            sendResponse(
                client,
                "429 Too Many Requests",
                "text/plain",
                "Too many login attempts. Try again shortly."
            );
            return;
        }

        char username[oag::kDiamondAdminUsernameBytes] {};
        char password[64] {};
        const bool parsed =
            decodeFormValue(body, "username", username, sizeof(username)) &&
            decodeFormValue(body, "password", password, sizeof(password));

        const bool valid =
            parsed &&
            DiamondPasswordService::verify(
                security,
                username,
                password
            );
        std::memset(password, 0, sizeof(password));

        if (!valid) {
            ++failedLoginCount_;
            if (failedLoginCount_ >= kMaxLoginFailures) {
                loginBlockedUntilUs_ = now + kLoginBlockUs;
                failedLoginCount_ = 0;
            }
            sendResponse(
                client,
                "401 Unauthorized",
                "text/plain",
                "Invalid username or password"
            );
            return;
        }

        failedLoginCount_ = 0;
        loginBlockedUntilUs_ = 0;
        createSession();

        char cookie[160] {};
        std::snprintf(
            cookie,
            sizeof(cookie),
            "Set-Cookie: OAGSESSION=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=1800\r\n",
            sessionToken_
        );
        sendRedirect(client, "/", cookie);
        return;
    }

    if (!authorized(request)) {
        sendResponse(
            client,
            "200 OK",
            "text/html; charset=utf-8",
            kLoginHtml
        );
        return;
    }

    if (
        std::strcmp(method, "POST") == 0 &&
        std::strcmp(path, "/logout") == 0
    ) {
        clearSession();
        sendRedirect(
            client,
            "/",
            "Set-Cookie: OAGSESSION=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0\r\n"
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
            "{\"brand\":\"OAG ABO GEMI\",\"authenticated\":true,\"generation\":%lu,\"persistent\":%s}",
            static_cast<unsigned long>(store_->generation()),
            store_->loadedFromFlash() ? "true" : "false"
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
        sessionExpiresUs_ = time_us_64() + kSessionLifetimeUs;
        sendResponse(
            client,
            "200 OK",
            "text/html; charset=utf-8",
            kDashboardHtml
        );
        return;
    }

    sendResponse(client, "404 Not Found", "text/plain", "Not found");
}

} // namespace oag::firmware
