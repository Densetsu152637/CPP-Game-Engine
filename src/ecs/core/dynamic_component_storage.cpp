#include "dynamic_component_storage.h"

#include <algorithm>
#include <stdexcept>

namespace ecs
{
    bool DynamicComponentStorage::registerComponent(const std::string_view name, const std::vector<DynamicField>& fields)
    {
        if (name.empty() || fields.empty()) return false;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (fields[i].name.empty()) return false;
            for (size_t j = 0; j < i; ++j)
                if (fields[i].name == fields[j].name) return false;
        }
        const auto found = m_columns.find(std::string(name));
        if (found != m_columns.end()) return found->second.fields == fields;
        Column column;
        column.fields = fields;
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

    bool DynamicComponentStorage::unregisterComponent(const std::string_view name, const bool eraseRows)
    {
        const auto found = m_columns.find(std::string(name));
        if (found == m_columns.end() || (!eraseRows && !found->second.entities.empty())) return false;
        m_columns.erase(found);
        return true;
    }

    bool DynamicComponentStorage::set(const Entity& entity, const std::string_view name, const DynamicValues& values)
    {
        auto found = m_columns.find(std::string(name));
        if (!entity.valid() || found == m_columns.end()) return false;
        Column& column = found->second;
        const auto rowFound = column.rows.find(entity.packed());
        const bool inserting = rowFound == column.rows.end();
        if (values.size() != column.fields.size()) return false;
        std::vector<const DynamicValue*> ordered(column.fields.size(), nullptr);
        for (const auto& [fieldName, value] : values)
        {
            const auto field = std::find_if(column.fields.begin(), column.fields.end(), [&](const DynamicField& f) { return f.name == fieldName; });
            if (field == column.fields.end()) return false;
            const size_t index = static_cast<size_t>(field - column.fields.begin());
            if (ordered[index]) return false;
            const bool typeOk = (field->type == DynamicFieldType::Number && std::holds_alternative<double>(value)) ||
                (field->type == DynamicFieldType::Boolean && std::holds_alternative<bool>(value)) ||
                (field->type == DynamicFieldType::String && std::holds_alternative<std::string>(value));
            if (!typeOk) return false;
            ordered[index] = &value;
        }
        if (std::find(ordered.begin(), ordered.end(), nullptr) != ordered.end()) return false;
        size_t row = 0;
        if (inserting)
        {
            row = column.entities.size();
            column.entities.push_back(entity);
            column.rows.emplace(entity.packed(), row);
            size_t numberIndex = 0, booleanIndex = 0, stringIndex = 0;
            for (size_t field = 0; field < column.fields.size(); ++field)
            {
                const auto& value = *ordered[field];
                if (column.fields[field].type == DynamicFieldType::Number) column.numbers[numberIndex++].push_back(std::get<double>(value));
                else if (column.fields[field].type == DynamicFieldType::Boolean) column.booleans[booleanIndex++].push_back(static_cast<uint8_t>(std::get<bool>(value)));
                else column.strings[stringIndex++].push_back(std::get<std::string>(value));
            }
            return true;
        }
        row = rowFound->second;
        size_t numberIndex = 0, booleanIndex = 0, stringIndex = 0;
        for (size_t field = 0; field < column.fields.size(); ++field)
        {
            const auto type = column.fields[field].type;
            if (type == DynamicFieldType::Number)
            {
                if (ordered[field]) column.numbers[numberIndex][row] = std::get<double>(*ordered[field]);
                ++numberIndex;
            }
            else if (type == DynamicFieldType::Boolean)
            {
                if (ordered[field]) column.booleans[booleanIndex][row] = static_cast<uint8_t>(std::get<bool>(*ordered[field]));
                ++booleanIndex;
            }
            else
            {
                if (ordered[field]) column.strings[stringIndex][row] = std::get<std::string>(*ordered[field]);
                ++stringIndex;
            }
        }
        return true;
    }

    bool DynamicComponentStorage::validate(const std::string_view name, const DynamicValues& values) const
    {
        const auto found = m_columns.find(std::string(name));
        if (found == m_columns.end() || values.size() != found->second.fields.size()) return false;
        for (size_t i = 0; i < found->second.fields.size(); ++i)
        {
            const auto& field = found->second.fields[i];
            const auto value = std::find_if(values.begin(), values.end(), [&](const auto& item) { return item.first == field.name; });
            if (value == values.end()) return false;
            if (std::count_if(values.begin(), values.end(), [&](const auto& item) { return item.first == field.name; }) != 1) return false;
            if ((field.type == DynamicFieldType::Number && !std::holds_alternative<double>(value->second)) ||
                (field.type == DynamicFieldType::Boolean && !std::holds_alternative<bool>(value->second)) ||
                (field.type == DynamicFieldType::String && !std::holds_alternative<std::string>(value->second))) return false;
        }
        return true;
    }

    std::optional<DynamicValues> DynamicComponentStorage::get(const Entity& entity, const std::string_view name) const
    {
        const auto found = m_columns.find(std::string(name));
        if (found == m_columns.end()) return std::nullopt;
        const Column& column = found->second;
        const auto rowFound = column.rows.find(entity.packed());
        if (!entity.valid() || rowFound == column.rows.end()) return std::nullopt;
        const size_t row = rowFound->second;
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
        const auto rowFound = column.rows.find(entity.packed());
        if (!entity.valid() || rowFound == column.rows.end()) return false;
        const size_t row = rowFound->second, last = column.entities.size() - 1;
        if (row != last)
        {
            const Entity moved = column.entities[last];
            column.entities[row] = moved;
            column.rows[moved.packed()] = row;
            for (auto& values : column.numbers) values[row] = values[last];
            for (auto& values : column.booleans) values[row] = values[last];
            for (auto& values : column.strings) values[row] = std::move(values[last]);
        }
        column.entities.pop_back();
        for (auto& values : column.numbers) values.pop_back();
        for (auto& values : column.booleans) values.pop_back();
        for (auto& values : column.strings) values.pop_back();
        column.rows.erase(rowFound);
        return true;
    }

    std::vector<Entity> DynamicComponentStorage::query(const std::vector<std::string>& all) const
    {
        if (all.empty()) return {};
        const Column* candidate = nullptr;
        for (const auto& name : all)
        {
            const auto found = m_columns.find(name);
            if (found == m_columns.end()) return {};
            if (!candidate || found->second.entities.size() < candidate->entities.size()) candidate = &found->second;
        }
        std::vector<Entity> result;
        for (const Entity& entity : candidate->entities)
        {
            bool matches = true;
            for (const auto& name : all)
                if (!m_columns.at(name).rows.contains(entity.packed())) { matches = false; break; }
            if (matches) result.push_back(entity);
        }
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
    { for (auto& [name, column] : m_columns) { (void)name; column.entities.clear(); column.rows.clear(); for (auto& v : column.numbers) v.clear(); for (auto& v : column.booleans) v.clear(); for (auto& v : column.strings) v.clear(); } }
}
