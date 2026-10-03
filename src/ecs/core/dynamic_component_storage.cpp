#include "dynamic_component_storage.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ecs
{
    namespace
    {
        using NativeEntityTraits = entt::entt_traits<std::uint64_t>;
        using NativeEntity = NativeEntityTraits::value_type;
        using DynamicEntitySet = entt::basic_sparse_set<NativeEntity>;

        bool toNativeEntity(const Entity& entity, NativeEntity& native)
        {
            // Match EntityRegistry's EnTT encoding. EnTT reserves the maximum
            // index and generation; the engine stores generations one higher.
            if (!entity.valid() || entity.index >= NativeEntityTraits::entity_mask || entity.version == 0)
                return false;
            native = NativeEntityTraits::construct(static_cast<NativeEntityTraits::entity_type>(entity.index),
                entity.version - 1);
            return true;
        }

        Entity fromNativeEntity(const NativeEntity entity)
        {
            return {NativeEntityTraits::to_entity(entity),
                static_cast<std::uint32_t>(NativeEntityTraits::to_version(entity) + 1)};
        }

        template<typename T>
        void reserveForAppend(std::vector<T>& values)
        {
            const size_t required = values.size() + 1;
            if (required <= values.capacity()) return;
            const size_t capacity = values.capacity();
            const size_t doubled = capacity > values.max_size() / 2 ? values.max_size() : capacity * 2;
            values.reserve(std::max(required, std::max(size_t{1}, doubled)));
        }

        bool validType(const DynamicFieldType type)
        {
            return type == DynamicFieldType::Number || type == DynamicFieldType::Boolean ||
                type == DynamicFieldType::String;
        }

        bool validName(const std::string_view name)
        {
            if (name.empty() || name.size() > max_dynamic_name_bytes) return false;
            const auto letter = [](const char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
            if (!letter(name.front()) && name.front() != '_') return false;
            return std::all_of(name.begin() + 1, name.end(), [&](const char c)
            { return letter(c) || (c >= '0' && c <= '9') || c == '_'; });
        }

        bool validValue(const DynamicFieldType type, const DynamicValue& value)
        {
            switch (type)
            {
            case DynamicFieldType::Number:
                return std::holds_alternative<double>(value) && std::isfinite(std::get<double>(value));
            case DynamicFieldType::Boolean: return std::holds_alternative<bool>(value);
            case DynamicFieldType::String:
                return std::holds_alternative<std::string>(value) &&
                    std::get<std::string>(value).size() <= max_dynamic_string_bytes;
            }
            return false;
        }

        std::optional<std::vector<DynamicValue>> orderedValues(
            const std::vector<DynamicField>& fields, const DynamicValues& values)
        {
            if (values.size() > fields.size()) return std::nullopt;
            std::vector<DynamicValue> ordered;
            ordered.reserve(fields.size());
            for (const auto& field : fields)
            {
                const auto found = std::find_if(values.begin(), values.end(), [&](const auto& item)
                { return item.first == field.name; });
                if (found == values.end())
                {
                    if (!field.default_value) return std::nullopt;
                    ordered.push_back(*field.default_value);
                }
                else
                {
                    if (!validValue(field.type, found->second)) return std::nullopt;
                    ordered.push_back(found->second);
                }
            }
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (std::none_of(fields.begin(), fields.end(), [&](const auto& field)
                    { return field.name == values[i].first; })) return std::nullopt;
                for (size_t j = 0; j < i; ++j)
                    if (values[j].first == values[i].first) return std::nullopt;
            }
            return ordered;
        }
    }

    bool DynamicComponentStorage::registerComponent(const std::string_view name,
        const std::vector<DynamicField>& fields, const std::uint32_t version)
    {
        if (!validName(name) || version == 0 || fields.empty() ||
            fields.size() > max_dynamic_fields_per_component) return false;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (!validName(fields[i].name) || !validType(fields[i].type) ||
                fields[i].version == 0 || fields[i].version > version ||
                (fields[i].default_value && !validValue(fields[i].type, *fields[i].default_value))) return false;
            for (size_t j = 0; j < i; ++j)
                if (fields[i].name == fields[j].name) return false;
        }
        const auto found = m_columns.find(std::string(name));
        if (found != m_columns.end()) return found->second.version == version && found->second.fields == fields;
        if (m_columns.size() >= max_dynamic_component_types) return false;
        size_t propertyCount = fields.size();
        for (const auto& [registeredName, column] : m_columns)
        { (void)registeredName; propertyCount += column.fields.size(); }
        if (propertyCount > max_dynamic_properties_per_project) return false;
        Column column;
        column.fields = fields;
        column.version = version;
        for (const auto& field : fields)
        {
            if (field.type == DynamicFieldType::Number) column.numbers.emplace_back();
            else if (field.type == DynamicFieldType::Boolean) column.booleans.emplace_back();
            else column.strings.emplace_back();
        }
        m_columns.emplace(std::string(name), std::move(column));
        return true;
    }

    bool DynamicComponentStorage::hasSchema(const std::string_view name) const
    { return m_columns.contains(std::string(name)); }

    std::optional<std::uint32_t> DynamicComponentStorage::schemaVersion(const std::string_view name) const
    {
        const auto found = m_columns.find(std::string(name));
        if (found == m_columns.end()) return std::nullopt;
        return found->second.version;
    }

    std::optional<std::vector<DynamicField>> DynamicComponentStorage::schemaFields(const std::string_view name) const
    {
        const auto found = m_columns.find(std::string(name));
        if (found == m_columns.end()) return std::nullopt;
        return found->second.fields;
    }

    bool DynamicComponentStorage::unregisterComponent(const std::string_view name, const bool eraseRows)
    {
        const auto found = m_columns.find(std::string(name));
        if (found == m_columns.end() || (!eraseRows && !found->second.entities.empty())) return false;
        m_columns.erase(found);
        return true;
    }

    size_t DynamicComponentStorage::pruneEmptySchemas()
    {
        size_t removed = 0;
        for (auto it = m_columns.begin(); it != m_columns.end(); )
        {
            if (it->second.entities.empty()) { it = m_columns.erase(it); ++removed; }
            else ++it;
        }
        return removed;
    }

    bool DynamicComponentStorage::set(const Entity& entity, const std::string_view name, const DynamicValues& values)
    {
        auto found = m_columns.find(std::string(name));
        NativeEntity native{};
        if (!toNativeEntity(entity, native) || found == m_columns.end()) return false;
        Column& column = found->second;
        const bool inserting = !column.entities.contains(native);
        if (inserting && column.entities.current(native) != NativeEntityTraits::version_mask)
            return false; // Another generation still owns this sparse index.
        auto ordered = orderedValues(column.fields, values);
        if (!ordered) return false;
        size_t row = 0;
        if (inserting)
        {
            row = column.entities.size();
            // Complete all potentially-throwing allocations before publishing
            // membership; after push, typed row construction uses noexcept moves.
            for (auto& values : column.numbers) reserveForAppend(values);
            for (auto& values : column.booleans) reserveForAppend(values);
            for (auto& values : column.strings) reserveForAppend(values);
            column.entities.push(native);
            size_t numberIndex = 0, booleanIndex = 0, stringIndex = 0;
            for (size_t field = 0; field < column.fields.size(); ++field)
            {
                const auto& value = (*ordered)[field];
                if (column.fields[field].type == DynamicFieldType::Number) column.numbers[numberIndex++].push_back(std::get<double>(value));
                else if (column.fields[field].type == DynamicFieldType::Boolean) column.booleans[booleanIndex++].push_back(static_cast<uint8_t>(std::get<bool>(value)));
                else column.strings[stringIndex++].push_back(std::get<std::string>(std::move((*ordered)[field])));
            }
            return true;
        }
        row = column.entities.index(native);
        size_t numberIndex = 0, booleanIndex = 0, stringIndex = 0;
        for (size_t field = 0; field < column.fields.size(); ++field)
        {
            const auto type = column.fields[field].type;
            if (type == DynamicFieldType::Number)
            {
                column.numbers[numberIndex][row] = std::get<double>((*ordered)[field]);
                ++numberIndex;
            }
            else if (type == DynamicFieldType::Boolean)
            {
                column.booleans[booleanIndex][row] = static_cast<uint8_t>(std::get<bool>((*ordered)[field]));
                ++booleanIndex;
            }
            else
            {
                column.strings[stringIndex][row] = std::get<std::string>((*ordered)[field]);
                ++stringIndex;
            }
        }
        return true;
    }

    bool DynamicComponentStorage::validate(const std::string_view name, const DynamicValues& values) const
    {
        const auto found = m_columns.find(std::string(name));
        return found != m_columns.end() && orderedValues(found->second.fields, values).has_value();
    }

    std::optional<DynamicValues> DynamicComponentStorage::get(const Entity& entity, const std::string_view name) const
    {
        const auto found = m_columns.find(std::string(name));
        if (found == m_columns.end()) return std::nullopt;
        const Column& column = found->second;
        NativeEntity native{};
        if (!toNativeEntity(entity, native) || !column.entities.contains(native)) return std::nullopt;
        const size_t row = column.entities.index(native);
        DynamicValues result; result.reserve(column.fields.size());
        size_t numberIndex = 0, booleanIndex = 0, stringIndex = 0;
        for (const auto& field : column.fields)
        {
            if (field.type == DynamicFieldType::Number) result.emplace_back(field.name, column.numbers[numberIndex++][row]);
            else if (field.type == DynamicFieldType::Boolean) result.emplace_back(field.name, column.booleans[booleanIndex++][row] != 0);
            else result.emplace_back(field.name, column.strings[stringIndex++][row]);
        }
        return result;
    }

    bool DynamicComponentStorage::remove(const Entity& entity, const std::string_view name)
    {
        const auto found = m_columns.find(std::string(name));
        if (found == m_columns.end()) return false;
        Column& column = found->second;
        NativeEntity native{};
        if (!toNativeEntity(entity, native) || !column.entities.contains(native)) return false;
        const size_t row = column.entities.index(native), last = column.entities.size() - 1;
        if (row != last)
        {
            for (auto& values : column.numbers) values[row] = values[last];
            for (auto& values : column.booleans) values[row] = values[last];
            for (auto& values : column.strings) values[row] = std::move(values[last]);
        }
        for (auto& values : column.numbers) values.pop_back();
        for (auto& values : column.booleans) values.pop_back();
        for (auto& values : column.strings) values.pop_back();
        column.entities.erase(native);
        return true;
    }

    std::vector<Entity> DynamicComponentStorage::query(const std::vector<std::string>& all) const
    {
        if (all.empty()) return {};
        entt::basic_runtime_view<const DynamicEntitySet> view;
        for (const auto& name : all)
        {
            const auto found = m_columns.find(name);
            if (found == m_columns.end()) return {};
            view.iterate(found->second.entities);
        }
        std::vector<Entity> result;
        result.reserve(view.size_hint());
        for (const NativeEntity entity : view) result.push_back(fromNativeEntity(entity));
        std::sort(result.begin(), result.end());
        return result;
    }

    bool DynamicComponentStorage::eraseEntity(const Entity& entity)
    {
        bool removed = false;
        for (auto& [name, column] : m_columns) { (void)column; removed = remove(entity, name) || removed; }
        return removed;
    }

    void DynamicComponentStorage::clear()
    { for (auto& [name, column] : m_columns) { (void)name; column.entities.clear(); for (auto& v : column.numbers) v.clear(); for (auto& v : column.booleans) v.clear(); for (auto& v : column.strings) v.clear(); } }
}
