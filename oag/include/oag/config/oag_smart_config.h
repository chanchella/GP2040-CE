#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "oag/config/diamond_game_library.h"

namespace oag {
constexpr std::size_t kOagSmartBranches = 4;
constexpr std::size_t kOagSmartConditions = 4;
constexpr std::size_t kOagSmartRefs = 8;
constexpr std::size_t kOagSmartActions = 16; // THEN and ELSE share this pool.

enum class OagSource : std::uint8_t { Gamepad, Keyboard, Mouse, Wheel, Stick, Axis };
struct OagControl {
    OagSource source = OagSource::Gamepad;
    std::uint8_t reserved = 0;
    std::uint16_t code = 0;
};
constexpr bool operator==(OagControl a, OagControl b) {
    return a.source == b.source && a.code == b.code;
}
enum class OagTrigger : std::uint8_t {
    Single, Double, Triple, Multi, Long, Held, Release, Hold, Chord, Sequence,
    Analog, Threshold
};
enum class OagJoin : std::uint8_t { And, Or };
enum class OagExecution : std::uint8_t {
    Once, Toggle, WhileHeld, RepeatWhileHeld, StopOnRelease, LoopUntilAgain
};
enum class OagActionKind : std::uint8_t {
    Press, Release, Hold, Tap, Pulse, Wait, MultiPress, Repeat, Loop,
    StickDirection, StickValue, TriggerValue, ReleaseAll, ReloadWait
};
struct OagCondition {
    OagControl control {};
    OagTrigger kind = OagTrigger::Single;
    OagJoin join = OagJoin::And;
    std::uint8_t negate = 0;
    std::uint8_t taps = 2;
    std::uint16_t windowMs = 500;
    std::uint16_t holdMs = 800;
    std::int16_t threshold = 500; // Normalized -1000..1000, independent of transport.
    std::uint8_t refFirst = 0;
    std::uint8_t refCount = 0;
};
struct OagAction {
    OagControl control {};
    OagActionKind kind = OagActionKind::Tap;
    std::uint8_t count = 1;
    std::uint8_t first = 0; // reference-pool offset / backwards REPEAT target.
    std::uint8_t flags = 0;
    std::uint16_t durationMs = 50;
    std::uint16_t beforeMs = 0;
    std::uint16_t afterMs = 0;
    std::uint16_t intervalMs = 50;
    std::int16_t x = 1000;
    std::int16_t y = 0;
};
// Bit 0 joins the previous step's group. Bit 1 makes PRESS duration-based.
// Zero flags preserve V1's explicit latched PRESS behavior when migrating.
constexpr std::uint8_t kOagActionTogether = 1;
constexpr std::uint8_t kOagActionTimed = 2;
struct OagBranch {
    std::uint8_t conditionCount = 1;
    std::uint8_t refCount = 0;
    std::uint8_t thenCount = 0;
    std::uint8_t elseCount = 0;
    std::uint8_t enabled = 1;
    std::uint8_t flags = 0;
    std::uint16_t reserved = 0;
    std::array<OagCondition, kOagSmartConditions> conditions {};
    std::array<OagControl, kOagSmartRefs> refs {};
    std::array<OagAction, kOagSmartActions> actions {};
};
struct OagCancelLogic {
    std::uint8_t enabled = 0;
    std::uint8_t conditionCount = 0;
    std::uint8_t refCount = 0;
    std::uint8_t reserved = 0;
    std::array<OagCondition, kOagSmartConditions> conditions {};
    std::array<OagControl, kOagSmartRefs> refs {};
};
struct OagSmartCombo {
    std::array<char, 65> name {};
    std::uint8_t enabled = 0;
    OagExecution mode = OagExecution::Once;
    std::uint8_t cancelable = 1;
    std::uint8_t branchCount = 1;
    std::array<OagBranch, kOagSmartBranches> branches {};
    OagCancelLogic cancel {};
};
// Frozen V1 prefix/layout: read existing experimental saves without rewriting Flash.
struct OagSmartComboV1 {
    std::array<char, 65> name {};
    std::uint8_t enabled = 0;
    OagExecution mode = OagExecution::Once;
    std::uint8_t cancelable = 1;
    std::uint8_t branchCount = 1;
    std::array<OagBranch, kOagSmartBranches> branches {};
};

enum class OagRecoilCurve : std::uint8_t { Direct, Linear, EaseIn, Stages };
enum class OagFiringMode : std::uint8_t { Continuous, Single, Burst };
struct OagWeaponSettings {
    std::uint8_t configured = 0; // Saved/new profile marker; zero disables effects, with no legacy fallback.
    std::uint8_t enabled = 0;
    std::uint8_t antiShake = 0;
    std::uint8_t smoothing = 0;
    std::int16_t horizontal = 0; // UI +/-2.00 in 0.01 fixed-point units.
    std::int16_t vertical = 0;
    std::uint16_t tickMs = 40;
    std::uint16_t rpm = 600;
    std::uint16_t reloadMs = 2500;
    std::uint16_t startDelayMs = 0;
    std::uint16_t rampMs = 0;
    std::uint8_t syncRpm = 0;
    std::uint8_t adsOverride = 0;
    std::int16_t adsHorizontal = 0;
    std::int16_t adsVertical = 0;
    std::int16_t firstHorizontal = 0;
    std::int16_t firstVertical = 0;
    std::uint8_t firstShot = 0;
    OagRecoilCurve curve = OagRecoilCurve::Direct;
    OagFiringMode firingMode = OagFiringMode::Continuous;
    std::uint8_t burstCount = 3;
    std::uint16_t triggerThreshold = 50;
    std::uint16_t pressMs = 0; // Optional compensation envelope; never changes game RPM.
    std::uint16_t releaseMs = 0;
    std::array<std::uint16_t, 3> stageMs {500, 1000, 1500};
    std::array<std::int16_t, 3> stageX {};
    std::array<std::int16_t, 3> stageY {};
};
struct OagSmartGame {
    std::array<OagSmartCombo, kDiamondComboSlots> combos {};
    std::array<OagWeaponSettings, kDiamondWeaponSlotsPerGame> weapons {};
};
struct OagSmartGameV1 {
    std::array<OagSmartComboV1, kDiamondComboSlots> combos {};
    std::array<OagWeaponSettings, kDiamondWeaponSlotsPerGame> weapons {};
};
struct OagSmartRecord {
    static constexpr std::uint32_t kMagic = 0x4F414753u; // OAGS
    static constexpr std::uint16_t kVersion = 2;
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    std::uint16_t game = 0;
    std::uint32_t generation = 0;
    std::uint32_t crc = 0;
    OagSmartGame payload {};
};
static_assert(sizeof(OagControl) == 4);
static_assert(sizeof(OagCondition) == 16);
static_assert(sizeof(OagAction) == 20);
static_assert(sizeof(OagBranch) == 424);
static_assert(sizeof(OagCancelLogic) == 100);
static_assert(sizeof(OagSmartGameV1) == 29600);
static_assert(offsetof(OagSmartCombo, cancel) == sizeof(OagSmartComboV1));
static_assert(sizeof(OagWeaponSettings) == 56);
static_assert(sizeof(OagSmartRecord) <= 32768, "Smart game must fit its independent A/B slot");

bool oagValidControl(OagControl c, bool output = false);
bool oagValidateCombo(const OagSmartCombo& c);
bool oagValidateComboV1(const OagSmartComboV1& c);
bool oagValidateCancel(const OagCancelLogic& c);
void oagMigrateCombo(const OagSmartComboV1& from, OagSmartCombo& to);
bool oagValidateWeapon(const OagWeaponSettings& w);
} // namespace oag
