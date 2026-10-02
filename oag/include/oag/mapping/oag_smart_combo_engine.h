#pragma once
#include <array>
#include "oag/config/oag_smart_config.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {
struct OagSmartInput {
    const KeyboardState* keyboard = nullptr;
    const MouseState* mouse = nullptr;
    LogicalGamepadState gamepad {};
};
struct OagSmartOutput {
    KeyboardState keyboard {};
    MouseState mouse {};
    LogicalGamepadState gamepad {};
    std::uint8_t stickOwnership = 0;
};
class OagSmartComboEngine {
public:
    void configure(const std::array<OagSmartCombo, kDiamondComboSlots>& programs);
    void reset();
    void cancel();
    void test(std::size_t slot, std::size_t branch, std::uint64_t nowUs);
    void tick(const OagSmartInput& input, std::uint64_t nowUs, bool context,
              std::uint16_t reloadMs = 2500);
    LogicalGamepadState merge(LogicalGamepadState base) const;
    const OagSmartOutput& output() const { return output_; }
    void consumeWheel() { output_.mouse.wheel = output_.mouse.pan = 0; }
    bool enabled() const { return enabled_; }
    bool active() const;
    bool reserves(OagControl control) const;
    std::uint32_t executionCount() const { return executionCount_; }
    std::uint8_t lastSlot() const { return lastSlot_; }
    static bool down(OagControl control, const OagSmartInput& input, std::int16_t threshold = 500);
    static std::int16_t axis(std::uint16_t code, const LogicalGamepadState& input);
private:
    static constexpr std::size_t kControls = 320;
    struct History {
        std::uint64_t downSince = 0, clusterSince = 0, lastTap = 0;
        std::uint32_t serial = 0, consumed = 0;
        std::uint16_t window = 0, longLimit = 0;
        std::uint8_t taps = 0, ready = 0;
        bool watched = false, held = false, rise = false, fall = false, cluster = false;
    };
    struct ConditionState {
        std::uint64_t sequenceSince = 0;
        std::uint32_t holdSerial = 0;
        std::uint8_t sequenceIndex = 0;
        bool previous = false;
    };
    struct Run {
        struct Clock {
            std::uint64_t deadline = 0;
            std::uint16_t pulses = 0;
            std::uint8_t phase = 0;
            bool pulseDown = false;
        };
        std::array<std::uint64_t, 5> held {};
        std::array<std::uint8_t, kOagSmartActions> repeats {};
        std::array<Clock, kOagSmartActions> clocks {};
        std::array<std::int16_t, 4> axes {};
        std::array<std::int16_t, 2> triggers {};
        std::uint64_t deadline = 0, testEnd = 0;
        std::uint16_t pulses = 0;
        std::uint8_t branch = 0, step = 0, begin = 0, end = 0, phase = 0, owns = 0;
        bool running = false, latched = false, testRun = false, pulseDown = false, fallback = false;
        std::uint8_t groupEnd = 0;
        bool groupStarted = false;
    };
    struct CancelRun {
        std::array<History, kOagSmartConditions+kOagSmartRefs> history {};
        std::array<std::uint16_t, kOagSmartConditions+kOagSmartRefs> ids {};
        std::array<ConditionState, kOagSmartConditions> conditions {};
        std::uint8_t count = 0;
        bool armed = false;
    };
    struct Verdict { bool value = false, pulse = false; };
    struct Candidate {
        std::uint16_t score = 0;
        std::uint8_t slot = 0, branch = 0, conditions = 0;
        bool fallback = false, used = false;
    };
    static std::size_t id(OagControl c);
    static OagControl control(std::size_t id);
    Verdict evaluate(const OagCondition&, const std::array<OagControl,kOagSmartRefs>&, ConditionState&,
                     const OagSmartInput&, std::uint64_t, int cancelSlot=-1);
    static void updateHistory(History&, OagControl, const OagSmartInput&, std::uint64_t, bool newMouse);
    void configureCancel(std::size_t slot);
    bool cancelMatched(std::size_t slot,const OagSmartInput&,std::uint64_t,bool newMouse);
    void claim(const OagBranch&, std::uint8_t conditionMask, std::array<bool,kControls>& claims, bool consume);
    void start(std::size_t slot, std::size_t branch, bool fallback, std::uint64_t now);
    void execute(std::size_t slot, const OagSmartInput&, std::uint64_t now, std::uint16_t reloadMs);
    bool heldGate(const OagBranch&, const OagSmartInput&, bool fallback) const;
    void emit(OagControl, bool held, Run&, const OagAction* action = nullptr);
    void emitAction(const OagAction&, const OagBranch&, bool held, Run&);
    void rebuildOutput();
    const std::array<OagSmartCombo, kDiamondComboSlots>* programs_ = nullptr;
    std::array<History, kControls> history_ {};
    std::array<std::array<std::array<ConditionState,kOagSmartConditions>,kOagSmartBranches>,kDiamondComboSlots> conditions_ {};
    std::array<std::array<bool,kOagSmartBranches>,kDiamondComboSlots> previous_ {};
    std::array<Run,kDiamondComboSlots> runs_ {};
    std::array<CancelRun,kDiamondComboSlots> cancellation_ {};
    // Main-loop scratch stays in fixed RAM, leaving the existing IRQ stack available.
    std::array<Candidate,kDiamondComboSlots*kOagSmartBranches> candidates_ {};
    std::array<bool,kControls> used_ {}, claims_ {};
    OagSmartOutput output_ {};
    std::uint32_t executionCount_ = 0;
    std::uint8_t lastSlot_ = 255;
    bool enabled_ = false;
    bool mouseSeen_ = false;
    std::uint32_t mouseGeneration_ = 0;
    std::uint64_t mouseTimestamp_ = 0;
};
} // namespace oag
