#include "kerneldb/types.hpp"
#include <sstream>
#include <stdexcept>

namespace kerneldb {

std::string data_type_name(DataType type) {
    switch (type) {
        case DataType::Integer: return "INTEGER";
        case DataType::Double:  return "DOUBLE";
        case DataType::Varchar: return "VARCHAR";
        case DataType::Boolean: return "BOOLEAN";
    }
    return "UNKNOWN";
}

std::string value_to_string(const Value& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "NULL";
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        }
        else if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        }
        else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream out;
            out << v;
            return out.str();
        }
        else if constexpr (std::is_same_v<T, Parameter>) {
            // Un paramètre non substitué ne devrait jamais arriver à l'affichage
            return "?" + std::to_string(v.index);
        }
        else {
            return std::to_string(v);
        }
    }, value);
}

bool value_matches_type(const Value& value, const Column& column) {
    // NULL est accepté partout
    if (is_null(value)) return true;

    // Un Parameter ne peut pas être validé ici : il n'a pas encore de valeur
    if (is_parameter(value)) return true;

    if (column.type == DataType::Integer)
        return std::holds_alternative<std::int64_t>(value);
    if (column.type == DataType::Double)
        return std::holds_alternative<double>(value) ||
               std::holds_alternative<std::int64_t>(value);
    if (column.type == DataType::Boolean)
        return std::holds_alternative<bool>(value);
    return std::holds_alternative<std::string>(value) &&
           std::get<std::string>(value).size() <= column.max_length;
}

} // namespace kerneldb