//
// Shared view storage mode and cached match types.
//

#pragma once

#include <array>
#include <cstddef>

#include "../core/entity.h"

enum class ViewStorage
{
    Simulation,
    Rendering
};

namespace ecs_view_detail
{
    template <size_t ComponentCount>
    struct ViewMatch
    {
        size_t entityIndex = Entity::N_POS;
        std::array<size_t, ComponentCount> denseIndices {};
    };
}
