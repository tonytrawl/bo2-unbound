#pragma once

#include "platform.hpp"

namespace unbound {

template<class Lookup>
GameLaunchTarget chooseGameLaunch(Game game, const GameSku* preferred, Lookup lookup) {
    // A matching disc wins even if the chosen install destination belongs to
    // another edition. The install destination itself never changes here.
    for (const auto device : {Device::Disc, Device::USB, Device::MLC}) {
        if (preferred && preferred->game == game) {
            if (auto target = lookup(*preferred, device); target.titleId) return target;
        }
        for (const auto& sku : gameSkus(game)) {
            if (&sku == preferred) continue;
            if (auto target = lookup(sku, device); target.titleId) return target;
        }
    }
    return {};
}

} // namespace unbound
