#include "oag/firmware/smart_combo_portal.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "portal_form_parser.h"
#include "smart_combo_assets.h"
namespace {
static_assert(offsetof(oag::OagSmartProgram, branches) == 36);
static_assert(sizeof(oag::OagSmartCondition) == 52);
static_assert(sizeof(oag::OagSmartAction) == 36);
static_assert(sizeof(oag::OagSmartBranch) == 792);
constexpr std::size_t headerBytes = offsetof(oag::OagSmartProgram, branches);
bool uintValue(const char* body, const char* key, std::uint32_t lo, std::uint32_t hi, std::uint32_t& v) {
    char s[16] {}; if (!formValue(body, key, s, sizeof(s)) || !s[0]) return false;
    std::uint64_t n = 0; for (const char* p = s; *p; ++p) { if (*p < '0' || *p > '9') return false; n = n * 10 + (*p - '0'); if (n > hi) return false; }
    if (n < lo) return false;
    v = static_cast<std::uint32_t>(n); return true;
}
void encodeHex(const void* raw, std::size_t size, char* out) {
    static constexpr char chars[] = "0123456789abcdef"; const auto* bytes = static_cast<const std::uint8_t*>(raw);
    for (std::size_t i = 0; i < size; ++i) { out[i * 2] = chars[bytes[i] >> 4]; out[i * 2 + 1] = chars[bytes[i] & 15]; }
    out[size * 2] = 0;
}
bool decodeHex(const char* body, const char* key, void* out, std::size_t size, char* encoded) {
    if (size > sizeof(oag::OagSmartBranch) || !formValue(body, key, encoded, sizeof(oag::OagSmartBranch) * 2 + 1) || std::strlen(encoded) != size * 2) return false;
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    // Validate the whole payload before writing even the staging object.
    for (std::size_t i = 0; i < size * 2; ++i) if (digit(encoded[i]) < 0) return false;
    auto* bytes = static_cast<std::uint8_t*>(out);
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<std::uint8_t>(digit(encoded[i * 2]) * 16 + digit(encoded[i * 2 + 1]));
    return true;
}
bool pathMatches(const char* p, const char* route) { auto n = std::strlen(route); return !std::strncmp(p, route, n) && (!p[n] || p[n] == '?'); }
}
namespace oag::firmware {
bool OagSmartComboPortal::handle(void* client, const char* method, const char* path, const char* body, std::uint64_t now, Reply reply) {
    auto respond = [&](const char* status, const char* type, const char* text) { reply(client, status, type, text); return true; };
    auto bad = [&](const char* text) { return respond("400 Bad Request", "text/plain; charset=utf-8", text); };
    const bool get = !std::strcmp(method, "GET"), post = !std::strcmp(method, "POST");
    if (get) {
        if (pathMatches(path, "/oag-smart")) return respond("200 OK", "text/html; charset=utf-8", kOagSmartHtml);
        for (const auto& asset : kOagSmartAssets) if (pathMatches(path, asset.path)) return respond("200 OK", asset.type, asset.bytes);
    }
    if (std::strncmp(path, "/api/oag-smart/", 15)) return false;
    if (!store_ || !test_ || !store_->ready()) return respond("503 Service Unavailable", "text/plain", "OAG smart storage unavailable; existing combos remain available");
    if (transaction_ && (now >= expiresUs_ || pendingRevision_ != store_->revision())) transaction_ = false;
    auto* json = replyBuffer_.data(); constexpr auto jsonSize = std::size_t(1800);
    std::uint32_t game = 0, slot = 0, b = 0, value = 0, revision = 0;
    const char* query = std::strchr(path, '?'); query = query ? query + 1 : "";
    if (get && pathMatches(path, "/api/oag-smart/program")) {
        if (!uintValue(query, "game", 1, kOagSmartGames, game) || !uintValue(query, "slot", 1, kOagSmartCombos, slot)) return bad("Invalid OAG game/slot");
        if (!store_->openEditor(game - 1)) return respond("409 Conflict", "text/plain", "Save or discard the previous game's RAM edits first");
        auto used = std::snprintf(json, jsonSize, "{\"revision\":%lu,\"dirty\":%s,\"headers\":[", static_cast<unsigned long>(store_->revision()), store_->dirty() ? "true" : "false");
        for (std::size_t i = 0; i < kOagSmartCombos; ++i) {
            char hex[headerBytes * 2 + 1] {}; encodeHex(&store_->editor().programs[i], headerBytes, hex);
            used += std::snprintf(json + used, jsonSize - used, "%s\"%s\"", i ? "," : "", hex);
        }
        used += std::snprintf(json + used, jsonSize - used, "],\"empty\":[");
        for (std::size_t i = 0; i < kOagSmartCombos; ++i)
            used += std::snprintf(json + used, jsonSize - used, "%s%s", i ? "," : "", store_->empty(i) ? "true" : "false");
        std::snprintf(json + used, jsonSize - used, "]}"); return respond("200 OK", "application/json", json);
    }
    if (get && pathMatches(path, "/api/oag-smart/branch")) {
        if (!uintValue(query, "game", 1, kOagSmartGames, game) || !uintValue(query, "slot", 1, kOagSmartCombos, slot) || !uintValue(query, "branch", 0, kOagSmartBranches - 1, b)) return bad("Invalid OAG branch");
        if (!store_->openEditor(game - 1)) return respond("409 Conflict", "text/plain", "Save or discard RAM edits first");
        auto* hex = hexBuffer_.data(); encodeHex(&store_->editor().programs[slot - 1].branches[b], sizeof(OagSmartBranch), hex);
        std::snprintf(json, jsonSize, "{\"hex\":\"%s\",\"revision\":%lu}", hex, static_cast<unsigned long>(store_->revision()));
        return respond("200 OK", "application/json", json);
    }
    if (post && pathMatches(path, "/api/oag-smart/begin")) {
        if (!uintValue(body, "game", 1, kOagSmartGames, game) || !uintValue(body, "slot", 1, kOagSmartCombos, slot) || !uintValue(body, "revision", 0, 0xffffffffu, revision)) return bad("Invalid OAG transaction");
        if (revision != store_->revision()) return respond("409 Conflict", "text/plain", "OAG RAM configuration changed; reload before editing");
        if (transaction_ && now < expiresUs_) return respond("409 Conflict", "text/plain", "Another OAG edit transaction is running");
        pending_.~OagSmartProgram(); new (&pending_) OagSmartProgram;
        if (!decodeHex(body, "header", &pending_, headerBytes, hexBuffer_.data()) || pending_.branchCount < 1 || pending_.branchCount > kOagSmartBranches ||
            pending_.enabled > 1 || pending_.consumeInput > 1 || !std::memchr(pending_.name.data(), 0, pending_.name.size())) return bad("Invalid OAG program header");
        if (!store_->openEditor(game - 1)) return respond("409 Conflict", "text/plain", "Save or discard RAM edits first");
        if (revision != store_->revision()) return respond("409 Conflict", "text/plain", "Load the selected OAG game before beginning an edit");
        pendingGame_ = game - 1; pendingSlot_ = slot - 1; pendingRevision_ = revision;
        received_ = 0; transaction_ = true; expiresUs_ = now + 15000000; ++token_; if (!token_) ++token_;
        std::snprintf(json, jsonSize, "{\"token\":%lu}", static_cast<unsigned long>(token_)); return respond("200 OK", "application/json", json);
    }
    if (post && (pathMatches(path, "/api/oag-smart/part") || pathMatches(path, "/api/oag-smart/preview") || pathMatches(path, "/api/oag-smart/abort"))) {
        if (!uintValue(body, "token", 1, 0xffffffffu, value) || !transaction_ || value != token_ || now >= expiresUs_ || pendingRevision_ != store_->revision()) {
            return respond("409 Conflict", "text/plain", "OAG transaction expired or changed; reload");
        }
        if (pathMatches(path, "/api/oag-smart/abort")) { transaction_ = false; return respond("200 OK", "text/plain", "Aborted"); }
        if (pathMatches(path, "/api/oag-smart/part")) {
            if (!uintValue(body, "branch", 0, pending_.branchCount - 1, b) || !decodeHex(body, "hex", &pending_.branches[b], sizeof(OagSmartBranch), hexBuffer_.data())) return bad("Invalid OAG branch payload");
            received_ |= 1u << b; expiresUs_ = now + 15000000; return respond("200 OK", "text/plain", "Staged");
        }
        if (received_ != (1u << pending_.branchCount) - 1u) return bad("OAG transaction is incomplete");
        const char* error = nullptr; if (!oagSmartValidate(pending_, error)) { transaction_ = false; return bad(error); }
        if (!store_->preview(pendingGame_, pendingSlot_, pending_)) return respond("409 Conflict", "text/plain", "OAG preview failed");
        transaction_ = false; testing_ = false; test_->reset();
        std::snprintf(json, jsonSize, "{\"revision\":%lu,\"dirty\":true,\"empty\":%s}", static_cast<unsigned long>(store_->revision()), store_->empty(pendingSlot_) ? "true" : "false");
        return respond("200 OK", "application/json", json);
    }
    if (post && (pathMatches(path, "/api/oag-smart/save") || pathMatches(path, "/api/oag-smart/discard"))) {
        if (!uintValue(body, "game", 1, kOagSmartGames, game) || !uintValue(body, "revision", 0, 0xffffffffu, revision)) return bad("Invalid OAG save");
        if (transaction_ || revision != store_->revision()) return respond("409 Conflict", "text/plain", "Finish/reload the OAG edit first");
        const bool discard = pathMatches(path, "/api/oag-smart/discard");
        if (!(discard ? store_->discard(game - 1) : store_->save(game - 1))) return respond("500 Internal Server Error", "text/plain", "OAG Flash operation failed; RAM edits retained");
        testing_ = false; test_->reset(); std::snprintf(json, jsonSize, "{\"revision\":%lu,\"dirty\":false}", static_cast<unsigned long>(store_->revision()));
        return respond("200 OK", "application/json", json);
    }
    if (post && pathMatches(path, "/api/oag-smart/test")) {
        if (!uintValue(body, "slot", 1, kOagSmartCombos, slot) || !uintValue(body, "branch", 0, kOagSmartBranches - 1, b) || !uintValue(body, "revision", 0, 0xffffffffu, revision)) return bad("Invalid OAG test");
        if (revision != store_->revision() || transaction_) return respond("409 Conflict", "text/plain", "Preview the OAG changes before testing");
        test_->reset(); if (!test_->test(store_->editor(), slot - 1, b, 0)) return bad("Enable and validate the OAG combo/branch before testing");
        testRevision_ = revision; testing_ = true; testTime_ = 0; test_->tick(store_->editor(), {}, 0);
        return respond("200 OK", "application/json", "{\"simulation\":true,\"limitMs\":5000}");
    }
    if (get && pathMatches(path, "/api/oag-smart/test-state")) {
        if (!uintValue(query, "t", 0, 5000, value)) return bad("OAG test time must be 0..5000 ms");
        if (!testing_ || testRevision_ != store_->revision() || value < testTime_) return respond("409 Conflict", "text/plain", "OAG test stopped or configuration changed");
        if (value - testTime_ > 250) return bad("Advance the OAG test by at most 250 ms per request");
        // Never replay mouse-wheel events across state reads, and preserve all
        // scheduler boundaries while stepping virtual time in 1 ms increments.
        while (testTime_ < value) test_->tick(store_->editor(), {}, std::uint64_t(++testTime_) * 1000);
        const auto& o = test_->output();
        auto used = std::snprintf(json, jsonSize, "{\"simulation\":true,\"active\":%s,\"t\":%lu,\"buttons\":%lu,\"dpad\":%u,\"lx\":%ld,\"ly\":%ld,\"rx\":%ld,\"ry\":%ld,\"l2\":%lu,\"r2\":%lu,\"modifiers\":%u,\"mouse\":%u,\"wheel\":%d,\"keys\":[",
            test_->active() ? "true" : "false", static_cast<unsigned long>(value), static_cast<unsigned long>(o.pad.buttons), o.pad.dpad,
            static_cast<long>(o.pad.lx), static_cast<long>(o.pad.ly), static_cast<long>(o.pad.rx), static_cast<long>(o.pad.ry),
            static_cast<unsigned long>(o.pad.leftTrigger), static_cast<unsigned long>(o.pad.rightTrigger), o.keyboard.modifiers, o.mouse.buttons, o.mouse.wheel);
        bool firstKey = true;
        for (unsigned k = 1; k < KeyboardState::kUsageCount; ++k) if (o.keyboard.pressed(k)) {
            used += std::snprintf(json + used, jsonSize - used, "%s%u", firstKey ? "" : ",", k); firstKey = false;
        }
        std::snprintf(json + used, jsonSize - used, "]}");
        if (value == 5000) { testing_ = false; test_->reset(); }
        return respond("200 OK", "application/json", json);
    }
    if (post && pathMatches(path, "/api/oag-smart/test-stop")) {
        testing_ = false; test_->reset(); return respond("200 OK", "application/json", "{\"stopped\":true}");
    }
    return respond("404 Not Found", "text/plain", "OAG smart route not found");
}
} // namespace oag::firmware
