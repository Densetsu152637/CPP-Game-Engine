#pragma once

#include "entity.h"

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
    struct DynamicField { std::string name; DynamicFieldType type; bool operator==(const DynamicField&) const = default; };
    using DynamicValues = std::vector<std::pair<std::string, DynamicValue>>;

    // Runtime-defined components use dense typed columns. Entity lookup is
    // sparse; iteration touches only rows that own the component.
    class DynamicComponentStorage
    {
        struct Column
        {
            std::vector<Entity> entities;
            std::unordered_map<std::uint64_t, size_t> rows;
            std::vector<DynamicField> fields;
            std::vector<std::vector<double>> numbers;
            std::vector<std::vector<std::uint8_t>> booleans;
            std::vector<std::vector<std::string>> strings;
        };
        std::unordered_map<std::string, Column> m_columns;

    public:
        bool registerComponent(std::string_view name, const std::vector<DynamicField>& fields);
        bool hasSchema(std::string_view name) const;
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
