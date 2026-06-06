//
// Shared operators for ECS component aliases.
//

#pragma once

#include <type_traits>
#include <utility>

#include "component_alias_traits.h"

namespace ecs::detail
{
    template <typename Derived, typename Value>
    struct AliasOperators
    {
        using value_type = std::remove_cvref_t<Value>;

        constexpr const Derived& derived() const noexcept
        { return static_cast<const Derived&>(*this); }

        constexpr Derived& derived() noexcept
        { return static_cast<Derived&>(*this); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs + std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator+(Rhs&& rhs) const
        { return derived().read() + std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs - std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator-(Rhs&& rhs) const
        { return derived().read() - std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs * std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator*(Rhs&& rhs) const
        { return derived().read() * std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs / std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator/(Rhs&& rhs) const
        { return derived().read() / std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs % std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator%(Rhs&& rhs) const
        { return derived().read() % std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs + std::forward<Rhs>(rhs); }
        constexpr Derived& operator+=(Rhs&& rhs)
        {
            derived().write() = derived().read() + std::forward<Rhs>(rhs);
            return derived();
        }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs - std::forward<Rhs>(rhs); }
        constexpr Derived& operator-=(Rhs&& rhs)
        {
            derived().write() = derived().read() - std::forward<Rhs>(rhs);
            return derived();
        }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs * std::forward<Rhs>(rhs); }
        constexpr Derived& operator*=(Rhs&& rhs)
        {
            derived().write() = derived().read() * std::forward<Rhs>(rhs);
            return derived();
        }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs / std::forward<Rhs>(rhs); }
        constexpr Derived& operator/=(Rhs&& rhs)
        {
            derived().write() = derived().read() / std::forward<Rhs>(rhs);
            return derived();
        }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs % std::forward<Rhs>(rhs); }
        constexpr Derived& operator%=(Rhs&& rhs)
        {
            derived().write() = derived().read() % std::forward<Rhs>(rhs);
            return derived();
        }

        constexpr decltype(auto) operator+() const
            requires requires(const value_type& value) { +value; }
        { return +derived().read(); }

        constexpr decltype(auto) operator-() const
            requires requires(const value_type& value) { -value; }
        { return -derived().read(); }

        constexpr decltype(auto) operator!() const
            requires requires(const value_type& value) { !value; }
        { return !derived().read(); }

        constexpr decltype(auto) operator~() const
            requires requires(const value_type& value) { ~value; }
        { return ~derived().read(); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs == std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator==(Rhs&& rhs) const
        { return derived().read() == std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs != std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator!=(Rhs&& rhs) const
        { return derived().read() != std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs < std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator<(Rhs&& rhs) const
        { return derived().read() < std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs <= std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator<=(Rhs&& rhs) const
        { return derived().read() <= std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs > std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator>(Rhs&& rhs) const
        { return derived().read() > std::forward<Rhs>(rhs); }

        template <typename Rhs>
            requires requires(const value_type& lhs, Rhs&& rhs) { lhs >= std::forward<Rhs>(rhs); }
        constexpr decltype(auto) operator>=(Rhs&& rhs) const
        { return derived().read() >= std::forward<Rhs>(rhs); }

        template <typename Lhs>
            requires (!is_component_alias<Lhs>::value && requires(Lhs&& lhs, const value_type& rhs) { std::forward<Lhs>(lhs) + rhs; })
        friend constexpr decltype(auto) operator+(Lhs&& lhs, const AliasOperators& rhs)
        { return std::forward<Lhs>(lhs) + rhs.derived().read(); }

        template <typename Lhs>
            requires (!is_component_alias<Lhs>::value && requires(Lhs&& lhs, const value_type& rhs) { std::forward<Lhs>(lhs) - rhs; })
        friend constexpr decltype(auto) operator-(Lhs&& lhs, const AliasOperators& rhs)
        { return std::forward<Lhs>(lhs) - rhs.derived().read(); }

        template <typename Lhs>
            requires (!is_component_alias<Lhs>::value && requires(Lhs&& lhs, const value_type& rhs) { std::forward<Lhs>(lhs) * rhs; })
        friend constexpr decltype(auto) operator*(Lhs&& lhs, const AliasOperators& rhs)
        { return std::forward<Lhs>(lhs) * rhs.derived().read(); }

        template <typename Lhs>
            requires (!is_component_alias<Lhs>::value && requires(Lhs&& lhs, const value_type& rhs) { std::forward<Lhs>(lhs) / rhs; })
        friend constexpr decltype(auto) operator/(Lhs&& lhs, const AliasOperators& rhs)
        { return std::forward<Lhs>(lhs) / rhs.derived().read(); }

        template <typename Lhs>
            requires (!is_component_alias<Lhs>::value && requires(Lhs&& lhs, const value_type& rhs) { std::forward<Lhs>(lhs) % rhs; })
        friend constexpr decltype(auto) operator%(Lhs&& lhs, const AliasOperators& rhs)
        { return std::forward<Lhs>(lhs) % rhs.derived().read(); }
    };
}
