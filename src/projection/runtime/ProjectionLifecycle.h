// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// ProjectionState resource preparation, teardown and world identity checks.
// Callers retain ownership of locking, anchor consumption and activation order.

#pragma once

#include <memory>

class Actor;
class BaseActorRenderContext;
class IClientInstance;

namespace lholo::structure {
struct LoadedStructure;
}

namespace lholo::projection::detail {

struct ProjectionState;

enum class ProjectionContextStatus : unsigned char {
    Current,
    DimensionChanged,
    Unavailable,
    WorldChanged,
};

bool prepareProjectionState(
    ProjectionState&                              state,
    BaseActorRenderContext&                       renderContext,
    std::shared_ptr<structure::LoadedStructure const> loaded
);

void resetProjectionState(ProjectionState& state);
void suspendProjectionState(ProjectionState& state);

ProjectionContextStatus classifyProjectionContext(
    ProjectionState const& state,
    IClientInstance&       client,
    Actor*                 player
);

} // namespace lholo::projection::detail
