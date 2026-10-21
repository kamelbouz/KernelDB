#include "kerneldb/validation.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace kerneldb {

// ============================================================================
// Helpers internes
// ============================================================================

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Vérifie qu'une colonne existe dans le schéma. Retourne son index.
std::size_t find_column_index(const TableSchema& schema,
                                const std::string& name) {
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        if (schema.columns[i].name == name) return i;
    }
    throw std::runtime_error("Colonne inconnue dans une contrainte: " + name);
}

// Vérifie qu'une liste de colonnes n'a pas de doublon interne.
void check_no_duplicate_columns(const std::vector<std::string>& cols,
                                 const std::string& context) {
    std::unordered_set<std::string> seen;
    for (const auto& c : cols) {
        if (!seen.insert(c).second) {
            throw std::runtime_error(
                context + " : colonne dupliquée dans la contrainte: " + c);
        }
    }
}

// Retourne true si TOUTES les valeurs du tuple sont NULL.
// Utilisé pour la sémantique Oracle : si toutes les colonnes d'une UNIQUE
// sont NULL, la ligne n'est pas indexée (pas de contrainte).
bool all_values_null(const Row& row,
                      const std::vector<std::size_t>& indices) {
    for (auto i : indices) {
        if (i >= row.values.size()) return false;
        if (!is_null(row.values[i])) return false;
    }
    return true;
}

// Retourne true si DEUX lignes ont la même valeur sur TOUTES les colonnes
// d'un tuple, et si AUCUNE des valeurs n'est NULL.
// Sémantique Oracle : NULL ne contraint pas, NULL != NULL.
bool tuple_equal_non_null(const Row& a, const Row& b,
                           const std::vector<std::size_t>& indices) {
    for (auto i : indices) {
        if (i >= a.values.size() || i >= b.values.size()) return false;
        const auto& va = a.values[i];
        const auto& vb = b.values[i];
        if (is_null(va) || is_null(vb)) return false;   // NULL ne contraint pas
        if (!(va == vb)) return false;
    }
    return true;
}

} // namespace

// ============================================================================
// Validation de schéma (J5.4-A : composite, Oracle-like)
// ============================================================================

void validate_schema(const TableSchema& schema) {
    if (schema.name.empty()) {
        throw std::runtime_error("Schéma invalide: nom de table vide");
    }
    if (schema.columns.empty()) {
        throw std::runtime_error("Schéma invalide: table sans colonne");
    }

    // 1) Colonnes : noms uniques, VARCHAR cohérent
    std::unordered_set<std::string> seen;
    for (const auto& col : schema.columns) {
        if (col.name.empty()) {
            throw std::runtime_error("Schéma invalide: colonne sans nom");
        }
        std::string lower = to_lower(col.name);
        if (!seen.insert(lower).second) {
            throw std::runtime_error(
                "CREATE/ALTER TABLE: colonne dupliquée: " + col.name);
        }
        if (col.type == DataType::Varchar) {
            if (col.max_length == 0) {
                throw std::runtime_error(
                    "Schéma invalide: VARCHAR sans longueur pour " + col.name);
            }
            if (col.max_length > 65535) {
                throw std::runtime_error(
                    "Schéma invalide: VARCHAR(" + std::to_string(col.max_length)
                    + ") dépasse 65535 pour " + col.name);
            }
        }
    }

    // 2) PRIMARY KEY : 0 ou 1, colonnes existantes, pas de doublon
    if (!schema.primary_key.empty()) {
        check_no_duplicate_columns(schema.primary_key,
                                    "PRIMARY KEY (" + schema.name + ")");
        for (const auto& pk_col : schema.primary_key) {
            std::size_t ci = find_column_index(schema, pk_col);
            // PK implique NOT NULL
            if (!schema.columns[ci].is_not_null) {
                throw std::runtime_error(
                    "PRIMARY KEY : la colonne " + pk_col +
                    " doit être NOT NULL");
            }
        }
    }

    // 3) UNIQUE : colonnes existantes, pas de doublon dans chaque contrainte,
    //    pas deux contraintes identiques sur les mêmes colonnes
    std::unordered_set<std::string> seen_unique;
    for (const auto& uc : schema.unique_constraints) {
        if (uc.columns.empty()) {
            throw std::runtime_error(
                "UNIQUE : contrainte sans colonne sur la table " + schema.name);
        }
        check_no_duplicate_columns(uc.columns,
                                    "UNIQUE (" + schema.name + ")");
        for (const auto& c : uc.columns) {
            find_column_index(schema, c);   // throw si absente
        }

        // Clé canonique pour détecter les doublons (triée)
        std::vector<std::string> sorted = uc.columns;
        std::sort(sorted.begin(), sorted.end());
        std::string key;
        for (const auto& c : sorted) { key += c; key += ','; }
        if (!seen_unique.insert(key).second) {
            throw std::runtime_error(
                "UNIQUE : contrainte dupliquée sur la table " + schema.name);
        }
    }
}

// ============================================================================
// Parsing de taille (inchangé)
// ============================================================================

std::uint64_t parse_size_token(const Token& tok,
                                const std::string& context,
                                std::uint64_t max_value) {
    const std::string& text = tok.text;

    if (!text.empty() && (text[0] == '+' || text[0] == '-')) {
        throw std::runtime_error(
            context + ": signe non autorisé dans '" + text + "'");
    }

    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw std::runtime_error(
                context + ": entier invalide '" + text + "'");
        }
    }

    if (text.empty()) {
        throw std::runtime_error(context + ": entier attendu");
    }

    std::size_t pos = 0;
    std::uint64_t value = 0;
    try {
        value = std::stoull(text, &pos);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(context + ": entier invalide '" + text + "'");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(context + ": valeur hors plage '" + text + "'");
    }

    if (pos != text.size()) {
        throw std::runtime_error(context + ": entier invalide '" + text + "'");
    }

    if (max_value != 0 && value > max_value) {
        throw std::runtime_error(
            context + ": valeur " + std::to_string(value)
            + " dépasse le maximum " + std::to_string(max_value));
    }

    return value;
}

// ============================================================================
// Normalisation de valeur (inchangée)
// ============================================================================

Value normalize_value(const Value& v, const Column& col) {
    if (is_null(v)) return v;
    if (is_parameter(v)) return v;

    switch (col.type) {
        case DataType::Integer: {
            if (std::holds_alternative<std::int64_t>(v)) return v;
            if (std::holds_alternative<double>(v)) {
                double d = std::get<double>(v);
                if (!std::isfinite(d)) {
                    throw std::runtime_error(
                        "Colonne " + col.name + " (INTEGER): valeur non finie");
                }
                if (d != std::floor(d)) {
                    throw std::runtime_error(
                        "Colonne " + col.name + " (INTEGER): valeur non entière "
                        + std::to_string(d));
                }
                constexpr double INT64_MIN_D = -9223372036854775808.0;
                constexpr double INT64_MAX_D =  9223372036854775808.0;
                if (d < INT64_MIN_D || d >= INT64_MAX_D) {
                    throw std::runtime_error(
                        "Colonne " + col.name + " (INTEGER): valeur hors plage int64");
                }
                return static_cast<std::int64_t>(d);
            }
            if (std::holds_alternative<bool>(v)) {
                return static_cast<std::int64_t>(std::get<bool>(v) ? 1 : 0);
            }
            throw std::runtime_error(
                "Colonne " + col.name + " (INTEGER): type incompatible");
        }

        case DataType::Double: {
            if (std::holds_alternative<double>(v)) {
                double d = std::get<double>(v);
                if (!std::isfinite(d)) {
                    throw std::runtime_error(
                        "Colonne " + col.name + " (DOUBLE): valeur non finie");
                }
                return v;
            }
            if (std::holds_alternative<std::int64_t>(v)) {
                return static_cast<double>(std::get<std::int64_t>(v));
            }
            if (std::holds_alternative<bool>(v)) {
                return std::get<bool>(v) ? 1.0 : 0.0;
            }
            throw std::runtime_error(
                "Colonne " + col.name + " (DOUBLE): type incompatible");
        }

        case DataType::Varchar: {
            if (std::holds_alternative<std::string>(v)) return v;
            throw std::runtime_error(
                "Colonne " + col.name + " (VARCHAR): type incompatible");
        }

        case DataType::Boolean: {
            if (std::holds_alternative<bool>(v)) return v;
            throw std::runtime_error(
                "Colonne " + col.name + " (BOOLEAN): type incompatible");
        }
    }

    throw std::runtime_error("Type de colonne inconnu pour " + col.name);
}

Row normalize_row(const Row& row, const TableSchema& schema) {
    if (row.values.size() != schema.columns.size()) {
        throw std::runtime_error(
            "Nombre de valeurs incorrect: " + std::to_string(row.values.size())
            + " au lieu de " + std::to_string(schema.columns.size()));
    }

    Row result;
    result.values.reserve(row.values.size());
    for (std::size_t i = 0; i < row.values.size(); ++i) {
        result.values.push_back(normalize_value(row.values[i], schema.columns[i]));
    }
    return result;
}

// ============================================================================
// Validation de contraintes (J5.4-A : composite, Oracle-like)
//
// Sémantique :
//   - PRIMARY KEY : NOT NULL forcé, toutes les lignes indexées, unicité stricte
//   - UNIQUE      : NULL autorisé
//                   - si TOUTES les colonnes de la contrainte sont NULL
//                     → la ligne n'est PAS indexée → pas de contrainte
//                   - sinon, la ligne est indexée et l'unicité s'applique
// ============================================================================

void validate_constraints(const TableSchema& schema,
                           const Row& normalized_row,
                           const std::vector<Row>& existing_normalized) {

    // ----- NOT NULL (par colonne) -----
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        const auto& col = schema.columns[i];
        const auto& val = normalized_row.values[i];
        if (col.is_not_null && is_null(val)) {
            throw std::runtime_error(
                "Contrainte NOT NULL violée sur la colonne " + col.name);
        }
    }

    // ----- PRIMARY KEY -----
    if (!schema.primary_key.empty()) {
        std::vector<std::size_t> pk_idx;
        for (const auto& c : schema.primary_key) {
            pk_idx.push_back(find_column_index(schema, c));
        }

        // PK : aucune valeur NULL (déjà garanti par NOT NULL, mais on vérifie)
        for (auto i : pk_idx) {
            if (is_null(normalized_row.values[i])) {
                throw std::runtime_error(
                    "PRIMARY KEY : valeur NULL interdite");
            }
        }

        // Unicité : aucune ligne existante ne doit avoir le même tuple PK
        for (const auto& existing : existing_normalized) {
            if (tuple_equal_non_null(normalized_row, existing, pk_idx)) {
                throw std::runtime_error(
                    "PRIMARY KEY violée sur (" +
                    [&]{
                        std::string s;
                        for (std::size_t k = 0; k < schema.primary_key.size(); ++k) {
                            if (k) s += ", ";
                            s += schema.primary_key[k];
                        }
                        return s;
                    }() + ")");
            }
        }
    }

    // ----- UNIQUE (N contraintes) -----
    for (const auto& uc : schema.unique_constraints) {
        std::vector<std::size_t> uq_idx;
        for (const auto& c : uc.columns) {
            uq_idx.push_back(find_column_index(schema, c));
        }

        // Oracle : si TOUTES les colonnes sont NULL → ligne non indexée
        if (all_values_null(normalized_row, uq_idx)) {
            continue;
        }

        // Unicité : aucune ligne existante ne doit avoir le même tuple,
        // en ignorant les colonnes NULL
        for (const auto& existing : existing_normalized) {
            if (tuple_equal_non_null(normalized_row, existing, uq_idx)) {
                std::string cols_str;
                for (std::size_t k = 0; k < uc.columns.size(); ++k) {
                    if (k) cols_str += ", ";
                    cols_str += uc.columns[k];
                }
                throw std::runtime_error(
                    "Contrainte UNIQUE violée sur (" + cols_str + ")");
            }
        }
    }
}
// ---------------------------------------------------------------------------
// J5.4-D : validation NOT NULL seule (les PK/UNIQUE passent par l'index)
// ---------------------------------------------------------------------------

void validate_not_null(const TableSchema& schema, const Row& normalized_row) {
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        const auto& col = schema.columns[i];
        if (i >= normalized_row.values.size()) continue;
        const auto& val = normalized_row.values[i];
        if (col.is_not_null && is_null(val)) {
            throw std::runtime_error(
                "Contrainte NOT NULL violée sur la colonne " + col.name);
        }
    }
}
} // namespace kerneldb