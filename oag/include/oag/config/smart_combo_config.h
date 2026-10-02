#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace oag {
constexpr std::size_t kOagSmartCombos = 16;
constexpr std::size_t kOagSmartBranches = 4;
constexpr std::size_t kOagSmartConditions = 4;
constexpr std::size_t kOagSmartSequence = 8;
constexpr std::size_t kOagSmartActions = 16;
constexpr std::size_t kOagSmartTargets = 4;
constexpr std::size_t kOagSmartGames = 20;

// Separate append-only schema. Existing Diamond config/game records are frozen.
enum class OagSmartTargetKind : std::uint8_t { Pad, Key, Mouse, Wheel };
struct OagSmartTarget {
    std::uint16_t code = 0;
    OagSmartTargetKind kind = OagSmartTargetKind::Pad;
    std::uint8_t strength = 100;
};
// Pad IDs 1..25 exactly match the existing OAG menu. 26..33 = left stick
// right/left/up/down/up-right/up-left/down-right/down-left; 34 = Share.
// Key IDs are HID usages; E0..E7 are the eight independent modifiers.
// Mouse IDs are button bits (1..32768). Wheel IDs: 1 up, 2 down.
enum class OagSmartTrigger : std::uint8_t {
    Single, Double, Triple, Multi, Long, Held, Release, Hold,
    Chord, Sequence, Stick, Threshold
};
enum class OagSmartJoin : std::uint8_t { And, Or };
enum class OagSmartMode : std::uint8_t {
    Once, Toggle, WhileHeld, RepeatHeld, StopOnRelease, LoopUntilAgain
};
enum class OagSmartActionKind : std::uint8_t {
    Press, Release, Hold, Tap, Pulse, Wait, MultiPress, Repeat, Loop,
    StickDirection, StickValue, TriggerValue, ReleaseAll, Stop,
    WaitUntilPressed, WaitUntilReleased
};
struct OagSmartCondition {
    OagSmartTrigger trigger = OagSmartTrigger::Single;
    OagSmartJoin join = OagSmartJoin::And;
    std::uint8_t negate = 0;
    std::uint8_t targetCount = 1;
    std::uint8_t taps = 1;
    std::uint8_t reserved[3] {};
    std::uint16_t windowMs = 500;
    std::uint16_t holdMs = 800;
    std::uint16_t sequenceMs = 1000;
    std::uint16_t chordMs = 80;
    std::uint16_t thresholdPermille = 500;
    std::uint16_t reserved2 = 0;
    std::array<OagSmartTarget, kOagSmartSequence> targets {};
};
struct OagSmartAction {
    OagSmartActionKind kind = OagSmartActionKind::Tap;
    std::uint8_t targetCount = 1;
    std::uint8_t loopFrom = 0; // zero-based first step in repeat/loop block
    std::uint8_t lifetime = 0; // 0=end, 1=trigger again, 2=trigger released
    std::uint16_t durationMs = 50;
    std::uint16_t releaseMs = 50;
    std::uint16_t beforeMs = 0;
    std::uint16_t afterMs = 0;
    std::uint16_t repeatCount = 1; // 0=infinite (Pulse/Repeat/Loop only)
    std::uint16_t intervalMs = 0;
    std::int16_t valueX = 0; // stick/trigger value in -1000..1000
    std::int16_t valueY = 0;
    std::array<OagSmartTarget, kOagSmartTargets> targets {};
};
struct OagSmartBranch {
    std::uint8_t enabled = 1;
    std::uint8_t conditionCount = 1;
    std::uint8_t actionCount = 1;
    std::uint8_t otherwise = 0; // ELSE after this program's gesture resolves
    OagSmartMode mode = OagSmartMode::Once;
    std::uint8_t cancelable = 1;
    std::uint16_t priority = 0;
    std::array<OagSmartCondition, kOagSmartConditions> conditions {};
    std::array<OagSmartAction, kOagSmartActions> actions {};
};
struct OagSmartProgram {
    std::array<char, 33> name {};
    std::uint8_t enabled = 0;
    std::uint8_t branchCount = 1;
    std::uint8_t consumeInput = 0;
    std::array<OagSmartBranch, kOagSmartBranches> branches {};
};
struct OagSmartGame {
    std::array<OagSmartProgram, kOagSmartCombos> programs {};
};
struct OagSmartRecord {
    static constexpr std::uint32_t kMagic = 0x4F53434Cu; // OSCL
    static constexpr std::uint16_t kVersion = 1;
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    std::uint16_t game = 0;
    std::uint32_t generation = 0;
    std::uint32_t crc = 0;
    OagSmartGame payload {};
};
constexpr std::size_t kOagSmartSlotBytes = 14u * 4096u;
static_assert(sizeof(OagSmartRecord) <= kOagSmartSlotBytes);
static_assert(sizeof(OagSmartTarget) == 4);
bool oagSmartTargetValid(const OagSmartTarget&);
bool oagSmartValidate(const OagSmartProgram&, const char*& error);
std::uint32_t oagSmartCrc(const void*, std::size_t);
bool oagSmartRecordValid(const OagSmartRecord&, std::size_t game);
} // namespace oag
