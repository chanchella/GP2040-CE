#include "oag/firmware/diamond_password_service.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pico/rand.h"
#include "pico/sha256.h"

namespace {

constexpr std::size_t kShaBytes = SHA256_RESULT_BYTES;
constexpr std::size_t kShaBlockBytes = 64;
constexpr std::size_t kMaxUsernameLength =
    oag::kDiamondAdminUsernameBytes - 1u;
constexpr std::size_t kMinUsernameLength = 3;
constexpr std::size_t kMinPasswordLength = 8;
constexpr std::size_t kMaxPasswordLength = 63;
constexpr std::uint32_t kMinIterations = 12000;

struct Chunk {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

bool sha256Chunks(
    const Chunk* chunks,
    std::size_t count,
    std::uint8_t out[kShaBytes]
) {
    pico_sha256_state_t state {};
    if (
        pico_sha256_start_blocking(
            &state,
            SHA256_BIG_ENDIAN,
            false
        ) != PICO_OK
    ) {
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (chunks[i].size == 0) {
            continue;
        }
        pico_sha256_update_blocking(
            &state,
            chunks[i].data,
            chunks[i].size
        );
    }

    sha256_result_t result {};
    pico_sha256_finish(&state, &result);
    std::memcpy(out, result.bytes, kShaBytes);
    return true;
}

bool hmacSha256(
    const std::uint8_t* key,
    std::size_t keyLength,
    const Chunk* chunks,
    std::size_t count,
    std::uint8_t out[kShaBytes]
) {
    std::array<std::uint8_t, kShaBlockBytes> normalizedKey {};

    if (keyLength > normalizedKey.size()) {
        const Chunk keyChunk {key, keyLength};
        if (!sha256Chunks(&keyChunk, 1, normalizedKey.data())) {
            return false;
        }
    } else if (keyLength != 0) {
        std::memcpy(normalizedKey.data(), key, keyLength);
    }

    std::array<std::uint8_t, kShaBlockBytes> innerPad {};
    std::array<std::uint8_t, kShaBlockBytes> outerPad {};
    for (std::size_t i = 0; i < normalizedKey.size(); ++i) {
        innerPad[i] = static_cast<std::uint8_t>(
            normalizedKey[i] ^ 0x36u
        );
        outerPad[i] = static_cast<std::uint8_t>(
            normalizedKey[i] ^ 0x5Cu
        );
    }

    pico_sha256_state_t innerState {};
    if (
        pico_sha256_start_blocking(
            &innerState,
            SHA256_BIG_ENDIAN,
            false
        ) != PICO_OK
    ) {
        return false;
    }

    pico_sha256_update_blocking(
        &innerState,
        innerPad.data(),
        innerPad.size()
    );
    for (std::size_t i = 0; i < count; ++i) {
        if (chunks[i].size != 0) {
            pico_sha256_update_blocking(
                &innerState,
                chunks[i].data,
                chunks[i].size
            );
        }
    }

    sha256_result_t innerResult {};
    pico_sha256_finish(&innerState, &innerResult);

    const Chunk outerChunks[] = {
        {outerPad.data(), outerPad.size()},
        {innerResult.bytes, kShaBytes},
    };
    return sha256Chunks(outerChunks, 2, out);
}

bool deriveVerifier(
    const char* username,
    const char* password,
    const std::uint8_t* salt,
    std::uint32_t iterations,
    std::uint8_t out[kShaBytes]
) {
    if (
        username == nullptr ||
        password == nullptr ||
        salt == nullptr ||
        iterations < kMinIterations
    ) {
        return false;
    }

    const std::size_t usernameLength = std::strlen(username);
    const std::size_t passwordLength = std::strlen(password);
    if (
        usernameLength < kMinUsernameLength ||
        usernameLength > kMaxUsernameLength ||
        passwordLength < kMinPasswordLength ||
        passwordLength > kMaxPasswordLength
    ) {
        return false;
    }

    const auto* passwordBytes =
        reinterpret_cast<const std::uint8_t*>(password);
    const auto* usernameBytes =
        reinterpret_cast<const std::uint8_t*>(username);

    const std::uint8_t blockIndex[4] = {0, 0, 0, 1};
    const Chunk firstChunks[] = {
        {salt, oag::kDiamondPasswordSaltBytes},
        {usernameBytes, usernameLength},
        {blockIndex, sizeof(blockIndex)},
    };

    std::array<std::uint8_t, kShaBytes> u {};
    if (!hmacSha256(
        passwordBytes,
        passwordLength,
        firstChunks,
        3,
        u.data()
    )) {
        return false;
    }

    std::memcpy(out, u.data(), kShaBytes);

    for (std::uint32_t round = 1; round < iterations; ++round) {
        const Chunk roundChunk {u.data(), u.size()};
        std::array<std::uint8_t, kShaBytes> next {};
        if (!hmacSha256(
            passwordBytes,
            passwordLength,
            &roundChunk,
            1,
            next.data()
        )) {
            return false;
        }

        u = next;
        for (std::size_t i = 0; i < kShaBytes; ++i) {
            out[i] ^= u[i];
        }
    }

    return true;
}

bool constantTimeEqual(
    const std::uint8_t* a,
    const std::uint8_t* b,
    std::size_t size
) {
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < size; ++i) {
        difference |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    }
    return difference == 0;
}

} // namespace

namespace oag::firmware {

bool DiamondPasswordService::provision(
    oag::DiamondSecurityConfig& security,
    const char* username,
    const char* password
) {
    if (username == nullptr || password == nullptr) {
        return false;
    }

    const std::size_t usernameLength = std::strlen(username);
    const std::size_t passwordLength = std::strlen(password);
    if (
        usernameLength < kMinUsernameLength ||
        usernameLength > kMaxUsernameLength ||
        passwordLength < kMinPasswordLength ||
        passwordLength > kMaxPasswordLength
    ) {
        return false;
    }

    oag::DiamondSecurityConfig next {};
    next.passwordIterations = kMinIterations;
    std::memcpy(
        next.adminUsername.data(),
        username,
        usernameLength
    );

    rng_128_t randomSalt {};
    get_rand_128(&randomSalt);
    static_assert(
        sizeof(randomSalt) >= oag::kDiamondPasswordSaltBytes
    );
    std::memcpy(
        next.passwordSalt.data(),
        &randomSalt,
        oag::kDiamondPasswordSaltBytes
    );

    if (!deriveVerifier(
        next.adminUsername.data(),
        password,
        next.passwordSalt.data(),
        next.passwordIterations,
        next.passwordVerifier.data()
    )) {
        return false;
    }

    next.provisioned = true;
    security = next;
    return true;
}

bool DiamondPasswordService::verify(
    const oag::DiamondSecurityConfig& security,
    const char* username,
    const char* password
) {
    if (
        !security.provisioned ||
        username == nullptr ||
        password == nullptr
    ) {
        return false;
    }

    if (
        std::strncmp(
            security.adminUsername.data(),
            username,
            security.adminUsername.size()
        ) != 0
    ) {
        return false;
    }

    std::array<std::uint8_t, kShaBytes> candidate {};
    if (!deriveVerifier(
        security.adminUsername.data(),
        password,
        security.passwordSalt.data(),
        security.passwordIterations,
        candidate.data()
    )) {
        return false;
    }

    return constantTimeEqual(
        candidate.data(),
        security.passwordVerifier.data(),
        candidate.size()
    );
}

} // namespace oag::firmware
