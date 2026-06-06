//
// Single-value ECS component aliases.
//

#pragma once

#include <type_traits>
#include <utility>

#include "component_alias_ops.h"

namespace ecs
{
    namespace detail
    {
        template <typename Value>
        inline constexpr bool can_inherit_alias_value_v =
            std::is_class_v<std::remove_cvref_t<Value>> &&
            !std::is_final_v<std::remove_cvref_t<Value>>;

        struct EmptyAliasOperators
        {};

        template <typename Derived, typename Value>
        using ComponentAliasOperators = std::conditional_t<
            can_inherit_alias_value_v<Value>,
            EmptyAliasOperators,
            AliasOperators<Derived, Value>
        >;

        template <typename Value, bool CanInherit = can_inherit_alias_value_v<Value>>
        struct ComponentAliasStorage;

        template <typename Value>
        struct ComponentAliasStorage<Value, true> : public std::remove_cvref_t<Value>
        {
            using value_type = std::remove_cvref_t<Value>;
            using value_type::value_type;

            constexpr ComponentAliasStorage() requires std::is_default_constructible_v<value_type> = default;
            constexpr ComponentAliasStorage(const value_type& value)
                : value_type(value)
            {}
            constexpr ComponentAliasStorage(value_type&& value)
                : value_type(std::move(value))
            {}

            constexpr value_type& value() noexcept
            { return static_cast<value_type&>(*this); }

            constexpr const value_type& value() const noexcept
            { return static_cast<const value_type&>(*this); }
        };

        template <typename Value>
        struct ComponentAliasStorage<Value, false>
        {
            using value_type = std::remove_cvref_t<Value>;

            value_type m_value {};

            constexpr ComponentAliasStorage() requires std::is_default_constructible_v<value_type> = default;
            constexpr ComponentAliasStorage(const value_type& value)
                : m_value(value)
            {}
            constexpr ComponentAliasStorage(value_type&& value)
                : m_value(std::move(value))
            {}

            template <typename... Args>
                requires std::is_constructible_v<value_type, Args...>
            constexpr explicit ComponentAliasStorage(Args&&... args)
                : m_value(std::forward<Args>(args)...)
            {}

            constexpr value_type& value() noexcept
            { return m_value; }

            constexpr const value_type& value() const noexcept
            { return m_value; }
        };
    }

    template <typename Value, typename Tag>
    struct ComponentAlias
        : detail::ComponentAliasStorage<Value>,
          detail::ComponentAliasOperators<ComponentAlias<Value, Tag>, Value>
    {
        using base_type = detail::ComponentAliasStorage<Value>;
        using value_type = typename base_type::value_type;
        using tag_type = std::remove_cvref_t<Tag>;
        using ecs_component_alias_tag = tag_type;
        using ecs_component_value_type = value_type;

        using base_type::base_type;

        constexpr ComponentAlias() requires std::is_default_constructible_v<value_type> = default;

        constexpr const value_type& read() const noexcept
        { return this->value(); }

        constexpr value_type& read() noexcept
        { return this->value(); }

        constexpr value_type& write() noexcept
        { return this->value(); }

        constexpr void swapBuffers() noexcept
        {}

        constexpr ComponentAlias& operator=(const value_type& value)
        {
            this->write() = value;
            return *this;
        }

        constexpr ComponentAlias& operator=(value_type&& value)
        {
            this->write() = std::move(value);
            return *this;
        }

        template <typename U>
            requires (!detail::is_component_alias<U>::value && std::is_assignable_v<value_type&, U&&>)
        constexpr ComponentAlias& operator=(U&& value)
        {
            this->write() = std::forward<U>(value);
            return *this;
        }

        constexpr operator value_type&() noexcept
        { return this->value(); }

        constexpr operator const value_type&() const noexcept
        { return this->value(); }

        constexpr value_type* operator->() noexcept
        { return &this->value(); }

        constexpr const value_type* operator->() const noexcept
        { return &this->value(); }
    };

    template <typename Value, typename Tag>
    using Alias = ComponentAlias<Value, Tag>;
}
