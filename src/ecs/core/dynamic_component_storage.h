#pragma once

#include "entity.h"

#include <entt/entity/runtime_view.hpp>
#include <entt/entity/sparse_set.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ecs
{
    enum class DynamicFieldType { Number, Boolean, String };
    using DynamicValue = std::variant<double, bool, std::string>;
    // Names and versions identify a stable property contract across script reloads.
    // A default permits omission only when writing a complete replacement row.
    struct DynamicField
    {
        std::string name;
        DynamicFieldType type;
        std::uint32_t version = 1;
        std::optional<DynamicValue> default_value;
        bool operator==(const DynamicField&) const = default;
    };
    using DynamicValues = std::vector<std::pair<std::string, DynamicValue>>;
    inline constexpr size_t max_dynamic_component_types = 64;
    inline constexpr size_t max_dynamic_fields_per_component = 32;
    inline constexpr size_t max_dynamic_properties_per_project = 512;
    inline constexpr size_t max_dynamic_name_bytes = 64;
    inline constexpr size_t max_dynamic_string_bytes = 4096;

    // Runtime-defined components use dense typed columns. Entity lookup is
    // sparse; iteration touches only rows that own the component.
    class DynamicComponentStorage
    {
        struct Column
        {
            // The sparse set stores EnTT-native 64-bit IDs (engine generation - 1).
            // The typed vectors stay row-aligned with its packed entity array.
            entt::basic_sparse_set<std::uint64_t> entities;
            std::vector<DynamicField> fields;
            std::uint32_t version = 1;
            std::vector<std::vector<double>> numbers;
            std::vector<std::vector<std::uint8_t>> booleans;
            std::vector<std::vector<std::string>> strings;
        };
        std::unordered_map<std::string, Column> m_columns;

    public:
        bool registerComponent(std::string_view name, const std::vector<DynamicField>& fields,
            std::uint32_t version = 1);
        bool hasSchema(std::string_view name) const;
        std::optional<std::uint32_t> schemaVersion(std::string_view name) const;
        std::optional<std::vector<DynamicField>> schemaFields(std::string_view name) const;
        bool unregisterComponent(std::string_view name, bool eraseRows = false);
        size_t pruneEmptySchemas();
        bool set(const Entity& entity, std::string_view name, const DynamicValues& values);
        bool validate(std::string_view name, const DynamicValues& values) const;
        std::optional<DynamicValues> get(const Entity& entity, std::string_view name) const;
        bool remove(const Entity& entity, std::string_view name);
        std::vector<Entity> query(const std::vector<std::string>& all) const;
        bool eraseEntity(const Entity& entity);
        void clear();
    };
}
