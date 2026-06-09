//
// Opt-in double-buffered ECS component aliases.
//

#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "component_alias_ops.h"

namespace ecs
{
    template <typename Value, typename Tag>
    struct BufferedComponentAlias : detail::AliasOperators<BufferedComponentAlias<Value, Tag>, Value>
    {
        using value_type = std::remove_cvref_t<Value>;
        using tag_type = std::remove_cvref_t<Tag>;
        using ecs_component_alias_tag = tag_type;
        using ecs_buffered_component_tag = void;
        using ecs_component_value_type = value_type;

    private:
        static constexpr size_t READ_ROLE = 0;
        static constexpr size_t WRITE_ROLE = 1;
        static constexpr std::array<size_t, 2> DEFAULT_ROLE_LOOKUP { READ_ROLE, WRITE_ROLE };

        const size_t* m_roleToBuffer = DEFAULT_ROLE_LOOKUP.data();
        std::array<value_type, 2> m_buffers {};

    public:
        constexpr BufferedComponentAlias() requires std::is_default_constructible_v<value_type> = default;

        constexpr BufferedComponentAlias(const value_type& value)
            : m_buffers { value, value }
        {}

        constexpr BufferedComponentAlias(value_type&& value)
            : m_buffers { value, std::move(value) }
        {}

        constexpr BufferedComponentAlias(const value_type& value, const size_t* roleLookup)
            : BufferedComponentAlias(value)
        { bindRoleLookup(roleLookup); }

        constexpr BufferedComponentAlias(value_type&& value, const size_t* roleLookup)
            : BufferedComponentAlias(std::move(value))
        { bindRoleLookup(roleLookup); }

        constexpr BufferedComponentAlias(const BufferedComponentAlias&) = default;
        constexpr BufferedComponentAlias(BufferedComponentAlias&&) noexcept = default;
        constexpr BufferedComponentAlias& operator=(const BufferedComponentAlias&) = default;
        constexpr BufferedComponentAlias& operator=(BufferedComponentAlias&&) noexcept = default;

        constexpr void bindRoleLookup(const size_t* roleLookup) noexcept
        {
            m_roleToBuffer = nullptr == roleLookup ? DEFAULT_ROLE_LOOKUP.data() : roleLookup;
        }

        constexpr const value_type& read() const noexcept
        { return m_buffers[m_roleToBuffer[READ_ROLE]]; }

        constexpr value_type& write() noexcept
        { return m_buffers[m_roleToBuffer[WRITE_ROLE]]; }

        template <typename... Args>
            requires (sizeof...(Args) > 0 && std::is_constructible_v<value_type, Args...>)
        constexpr explicit BufferedComponentAlias(Args&&... args)
            : BufferedComponentAlias(value_type(std::forward<Args>(args)...))
        {}

        constexpr BufferedComponentAlias& operator=(const value_type& value)
        {
            write() = value;
            return *this;
        }

        constexpr BufferedComponentAlias& operator=(value_type&& value)
        {
            write() = std::move(value);
            return *this;
        }

        template <typename U>
            requires (!detail::is_component_alias<U>::value && std::is_assignable_v<value_type&, U&&>)
        constexpr BufferedComponentAlias& operator=(U&& value)
        {
            write() = std::forward<U>(value);
            return *this;
        }

        constexpr operator const value_type&() const noexcept
        { return read(); }

        constexpr const value_type* operator->() const noexcept
        { return &read(); }
    };

    template <typename Value, typename Tag>
    using BufferedAlias = BufferedComponentAlias<Value, Tag>;
}
