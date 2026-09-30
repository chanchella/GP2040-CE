#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/config/diamond_game_library.h"

namespace oag::firmware {

class DiamondGameLibraryStore {
public:
    bool initialize(const oag::DiamondPersistentConfig& legacy);
    bool activate(
        std::size_t gameIndex,
        const oag::DiamondPersistentConfig& legacy
    );

    bool loadGame(
        std::size_t gameIndex,
        const oag::DiamondPersistentConfig& legacy,
        oag::DiamondGameContent& out
    ) const;
    bool saveGame(std::size_t gameIndex, const oag::DiamondGameContent& game);

    const oag::DiamondGameContent& active() const { return active_; }
    std::size_t activeGame() const { return activeGame_; }
    bool storageReady() const;

private:
    bool loadStored(std::size_t gameIndex, oag::DiamondGameContent& out) const;
    void synthesizeLegacy(
        std::size_t gameIndex,
        const oag::DiamondPersistentConfig& legacy,
        bool includeLegacyCombos,
        oag::DiamondGameContent& out
    ) const;

    oag::DiamondGameContent active_ {};
    std::size_t activeGame_ = 0;
};

} // namespace oag::firmware
