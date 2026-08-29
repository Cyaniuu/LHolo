// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/MaterialTracker.h"

#include "block/BlockPlacementRules.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/locale/I18n.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/ItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/level/block/Block.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lholo::structure::detail {
namespace {

constexpr int kInventorySlots = 36;
constexpr std::uint64_t kAvailabilityRefreshMs = 400;

std::string localizedBlockName(Block const& block, std::string_view localeCode) {
    auto const& typeName = block.getTypeName();
    auto const itemId = ItemRegistry::getBlockItemId(block);
    auto const item = ItemRegistryManager::getItemRegistry().getItem(itemId);
    if (auto* itemPtr = item.get()) {
        ItemStack const itemStack(*itemPtr, 1, 0, nullptr);
        auto const name = itemStack.getName();
        if (!name.empty() && name != typeName) return name;
    }

    auto const translationKey = block.buildDescriptionName();
    if (!translationKey.empty()) {
        auto& i18n = ::getI18n();
        auto locale = localeCode.empty()
            ? i18n.getCurrentLanguage().get()
            : i18n.getLocaleFor(std::string{localeCode});
        if (locale) {
            auto const localized = i18n.get(translationKey, std::vector<std::string>{}, locale);
            if (!localized.empty() && localized != translationKey) return localized;
        }
    }

    auto name = block.getDisplayName();
    if (name.empty()) name = typeName;
    return name;
}

std::vector<MaterialRequirement> collectMaterials(
    std::vector<LoadedStructure::RenderBlock> const& renderBlocks,
    std::string_view localeCode
) {
    std::map<std::string, MaterialRequirement> byType;
    std::map<std::string, MaterialRequirement> byLiquidType;
    auto aggregate = [&](auto& destination, Block const* blockValue) {
        if (!blockValue) return;

        std::string const typeName{blockValue->getTypeName()};
        if (typeName == "minecraft:bubble_column"
            || typeName == "minecraft:piston_arm_collision"
            || typeName == "minecraft:sticky_piston_arm_collision"
            || typeName == "minecraft:moving_block") {
            return;
        }

        std::string key;
        MaterialRequirement requirement;
        requirement.typeName = typeName;
        if (typeName == "minecraft:water" || typeName == "minecraft:flowing_water") {
            key = "minecraft:water";
            requirement.displayName = "水";
        } else if (typeName == "minecraft:lava" || typeName == "minecraft:flowing_lava") {
            key = "minecraft:lava";
            requirement.displayName = "熔岩";
        } else if (auto const item = block::resolvePlacementItem(*blockValue); item.valid) {
            key = "item:" + item.itemId;
            requirement.displayName = item.displayName;
            requirement.itemId = item.itemId;
            requirement.stackSize = item.stackSize;
        } else {
            key = typeName;
            requirement.displayName = localizedBlockName(*blockValue, localeCode);
        }

        auto const result = destination.try_emplace(key, std::move(requirement));
        if (result.first->second.count != std::numeric_limits<std::uint64_t>::max()) {
            ++result.first->second.count;
        }
    };

    for (auto const& entry : renderBlocks) {
        aggregate(byType, entry.block);
        aggregate(byLiquidType, entry.liquid);
    }

    std::vector<MaterialRequirement> materials;
    materials.reserve(byType.size() + byLiquidType.size());
    auto appendSorted = [&materials](auto& source) {
        std::vector<MaterialRequirement> sorted;
        sorted.reserve(source.size());
        for (auto& entry : source) sorted.push_back(std::move(entry.second));
        std::sort(sorted.begin(), sorted.end(), [](auto const& left, auto const& right) {
            if (left.count != right.count) return left.count > right.count;
            return left.typeName < right.typeName;
        });
        materials.insert(
            materials.end(),
            std::make_move_iterator(sorted.begin()),
            std::make_move_iterator(sorted.end())
        );
    };
    appendSorted(byType);
    appendSorted(byLiquidType);
    return materials;
}

void processPendingMaterialList(LocalPlayer& player) {
    auto& ui = StructureUiState::getInstance();
    if (!ui.consumeMaterialListRequest()) return;

    auto const loaded = StructureSession::getInstance().loaded();
    std::vector<MaterialRequirement> materials;
    if (loaded) materials = collectMaterials(loaded->renderBlocks, player.getLocaleCode());
    ui.replaceMaterialRequirements(std::move(materials));
}

void refreshAvailability(LocalPlayer& player) {
    static std::uint64_t lastRefreshMs{};
    auto& ui = StructureUiState::getInstance();
    if (!ui.materialHudEnabled() && !ui.guiVisible()) return;

    auto const requirements = ui.materialRequirements();
    if (requirements.empty()) return;
    auto const now = GetTickCount64();
    if (lastRefreshMs != 0 && now - lastRefreshMs < kAvailabilityRefreshMs) return;
    lastRefreshMs = now;

    std::unordered_map<std::string, int> inventoryCounts;
    auto& inventory = player.getInventory();
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        auto const& item = inventory.getItem(slot);
        if (!item.isNull()) inventoryCounts[item.getTypeName()] += static_cast<int>(item.mCount);
    }

    std::vector<int> available(requirements.size(), 0);
    for (std::size_t index = 0; index < requirements.size(); ++index) {
        auto const& itemId = requirements[index].itemId;
        if (itemId.empty()) continue;
        if (auto const found = inventoryCounts.find(itemId); found != inventoryCounts.end()) {
            available[index] = found->second;
        }
    }
    ui.setMaterialAvailability(std::move(available));
}

} // namespace

void requestMaterialListRefresh() {
    StructureUiState::getInstance().requestMaterialList();
}

void invalidateMaterialList() {
    auto& ui = StructureUiState::getInstance();
    ui.clearMaterials();
    if (ui.materialHudEnabled()) ui.requestMaterialList();
}

void tickMaterialTracker(LocalPlayer& player) {
    processPendingMaterialList(player);
    refreshAvailability(player);
}

} // namespace lholo::structure::detail
