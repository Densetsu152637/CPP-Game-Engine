#pragma once

#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

#include "ecs/aliases/component_alias.h"
#include "ecs/ecs.h"
#include "ecs/processor.h"
#include "logging/logger.h"
#include "rendering/ecs_rendering.h"
#include "rendering/uniform_registry.h"
#include "structs/arraylist.h"
#include "structs/sparse_bit_field.h"
#include "test/rendering/rendering_test_fakes.h"
#include "test/test_assertions.h"

namespace ecs_test
{
    struct PositionTag {};
    struct VelocityTag {};
    struct HealthTag {};
    struct ManaTag {};
    struct RenderableTag {};
    struct SelectedTag {};
    struct HiddenTag {};

    using Position = ecs::BufferedAlias<int, PositionTag>;
    using Velocity = ecs::Alias<int, VelocityTag>;
    using Health = ecs::Alias<int, HealthTag>;
    using Mana = ecs::Alias<int, ManaTag>;

    using test::require;
    using test::require_throws;

    static_assert(
        std::is_same_v<
            decltype(std::declval<ECS&>().registerRenderArchetype<Velocity, Health>()),
            void
        >,
        "render archetype registration should not expose mutable render storage"
    );

    static_assert(
        std::is_const_v<
            std::remove_pointer_t<
                decltype(std::declval<ECS&>().renderArchetypePoolIfExists<Velocity, Health>())
            >
        >,
        "render archetype lookup should expose read-only storage only"
    );

    class CapturingLogger final : public Logger
    {
    public:
        ArrayList<std::string> lines;

        void log(const std::string& message) override
        {
            lines.append(message);
        }
    };

    inline bool contains_line(const ArrayList<std::string>& lines, const std::string& token)
    {
        for (const std::string& line : lines)
        {
            if (line.find(token) != std::string::npos)
                return true;
        }

        return false;
    }

    inline int read_int_uniform(const rendering::ShaderUniformWrite& write)
    {
        int value = 0;
        std::memcpy(&value, write.bytes.data(), sizeof(value));
        return value;
    }
}
