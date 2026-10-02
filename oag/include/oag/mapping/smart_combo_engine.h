#pragma once
#include "oag/mapping/smart_combo_controls.h"
namespace oag {
class OagSmartComboEngine {
public:
    void reset();
    bool active() const;
    bool needsTick() const;
    bool active(std::size_t slot) const;
    const OagSmartOutput& output() const { return output_; }
    const OagSmartInput& filteredInput() const { return filtered_; }
    std::uint32_t starts() const { return starts_; }
    int lastSlot() const { return lastSlot_; }
    int lastBranch() const { return lastBranch_; }
    // All time is supplied by the firmware; no sleep, Flash or transport calls.
    void tick(const OagSmartGame&, const OagSmartInput&, std::uint64_t nowUs);
    bool test(const OagSmartGame&, std::size_t slot, std::size_t branch, std::uint64_t nowUs);
    void stop(std::size_t slot);
    void consumeNativeWheel();
private:
    struct Detector {
        std::uint8_t previous = 0, taps = 0, sequence = 0;
        bool session = false, longFired = false, blocked = false;
        std::uint16_t resolveMs = 0, longLimitMs = 60000;
        std::uint64_t firstUs = 0, downUs = 0, lastTapUs = 0;
        std::uint32_t wheelGeneration = 0;
        bool wheelSeen = false;
        std::array<std::uint64_t, kOagSmartSequence> pressedUs {};
    };
    struct Result { bool value = false, event = false, resolved = false; };
    struct Runner {
        bool active = false, finished = false, entered = false, phaseDown = false;
        bool keepAgain = false, keepRelease = false, test = false;
        std::uint8_t branch = 0, step = 0, stage = 0;
        std::uint16_t pulses = 0;
        std::uint64_t deadlineUs = 0;
        std::array<std::uint16_t, kOagSmartActions> loops {};
        OagSmartOutput held {}, transient {};
    };
    Result detect(const OagSmartCondition&, Detector&, const OagSmartGame&, const OagSmartInput&, std::uint64_t);
    bool anchorHeld(const OagSmartBranch&, const OagSmartInput&) const;
    void claim(const OagSmartBranch&);
    bool overlaps(const OagSmartBranch&, const OagSmartBranch&) const;
    void execute(const OagSmartBranch&, Runner&, const OagSmartInput&, std::uint64_t);
    void start(std::size_t, std::size_t, std::uint64_t, bool test = false);
    struct Horizon { std::uint16_t windowMs = 0, longMs = 60000; };
    void compileHorizons(const OagSmartGame&);
    std::array<Horizon, 307> horizons_ {}; // 34 pad + 255 key + 16 mouse + 2 wheel
    bool compiled_ = false;
    std::array<std::array<std::array<Detector, kOagSmartConditions>, kOagSmartBranches>, kOagSmartCombos> detectors_ {};
    std::array<std::array<bool, kOagSmartBranches>, kOagSmartCombos> previousBranches_ {};
    std::array<Runner, kOagSmartCombos> runners_ {};
    OagSmartOutput output_ {};
    OagSmartInput filtered_ {};
    std::uint32_t starts_ = 0;
    int lastSlot_ = -1, lastBranch_ = -1;
    const OagSmartGame* game_ = nullptr;
};
} // namespace oag
