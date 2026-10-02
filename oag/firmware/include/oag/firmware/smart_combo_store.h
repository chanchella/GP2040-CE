#pragma once
#include "oag/config/smart_combo_config.h"
namespace oag::firmware {
class OagSmartComboStore {
public:
    bool activate(std::size_t game);
    const OagSmartGame& active() const { return record_.payload; }
    std::size_t activeGame() const { return activeGame_; }
    std::uint32_t revision() const { return revision_; }
    bool ready() const;
    bool openEditor(std::size_t game); // RAM only; refuses to discard dirty other game
    const OagSmartGame& editor() const { return record_.payload; }
    std::size_t editorGame() const { return editorGame_; }
    bool dirty() const { return dirty_; }
    bool discard(std::size_t game);
    bool preview(std::size_t game, std::size_t slot, const OagSmartProgram&);
    bool save(std::size_t game); // only called by the explicit SAVE endpoint
private:
    bool load(std::size_t game, OagSmartGame&) const;
    // Config and gameplay are mutually exclusive in the stable baseline.
    // One selected game's record is both RAM preview and the Flash source.
    alignas(256) OagSmartRecord record_ {};
    std::size_t activeGame_ = kOagSmartGames, editorGame_ = kOagSmartGames;
    std::uint32_t revision_ = 0;
    bool dirty_ = false;
};
} // namespace oag::firmware
