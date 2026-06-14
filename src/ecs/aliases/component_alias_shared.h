//
// Shared ECS component aliases.
//

#pragma once

#include <type_traits>
#include <utility>

#include "component_alias_basic.h"

namespace ecs
{
    template <typename Value, typename Tag>
    struct SharedComponentAlias : ComponentAlias<Value, Tag>
    {
        using base_type = ComponentAlias<Value, Tag>;
        using value_type = typename base_type::value_type;
        using tag_type = typename base_type::tag_type;
        using ecs_component_alias_tag = tag_type;
        using ecs_shared_component_alias_tag = void;
        using ecs_component_value_type = value_type;

        using base_type::base_type;
        using base_type::operator=;

        constexpr SharedComponentAlias() requires std::is_default_constructible_v<value_type> = default;
        constexpr SharedComponentAlias(const SharedComponentAlias&) = default;
        constexpr SharedComponentAlias(SharedComponentAlias&&) noexcept = default;
        constexpr SharedComponentAlias& operator=(const SharedComponentAlias&) = default;
        constexpr SharedComponentAlias& operator=(SharedComponentAlias&&) noexcept = default;

        constexpr SharedComponentAlias& operator=(const value_type& value)
        {
            base_type::operator=(value);
            return *this;
        }

        constexpr SharedComponentAlias& operator=(value_type&& value)
        {
            base_type::operator=(std::move(value));
            return *this;
        }

        template <typename U>
            requires (!detail::is_component_alias<U>::value && std::is_assignable_v<value_type&, U&&>)
        constexpr SharedComponentAlias& operator=(U&& value)
        {
            base_type::operator=(std::forward<U>(value));
            return *this;
        }
    };

    template <typename Value, typename Tag>
    using SharedAlias = SharedComponentAlias<Value, Tag>;
}
