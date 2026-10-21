#include "kerneldb/catalog.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace kerneldb {

namespace {

std::vector<std::string> split_columns(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string join_columns(const std::vector<std::string>& cols) {
    std::string out;
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) out += ',';
        out += cols[i];
    }
    return out;
}

} // namespace

Catalog::Catalog(std::filesystem::path directory)
    : directory_(std::move(directory)) {}

// ---------------------------------------------------------------------------
// J5.4-A : format v2 (composite, Oracle-like)
//
//   TABLE <name> <ncols> <pk_count> <uniq_count>
//   <col_name> <type> <max_length> <is_not_null>     ← ncols lignes
//   PK <col1,col2,...>                                ← si pk_count > 0
//   UNIQUE <name> <col1,col2,...>                     ← uniq_count lignes
//   INDEX <table> <name> <cols> <unique>              ← J5.1
//
// Le nom des UNIQUE anonymes est auto-généré à l'écriture
// (uq_<table>_<index>) pour garantir un parsing non ambigu.
// ---------------------------------------------------------------------------

void Catalog::load() {
    tables_.clear();
    std::ifstream in(directory_ / "catalog.meta");
    if (!in) return;

    std::string tag;
    while (in >> tag) {
        if (tag == "TABLE") {
            TableSchema schema;
            std::size_t count = 0;
            std::size_t pk_count = 0;
            std::size_t uniq_count = 0;

            in >> schema.name >> count >> pk_count >> uniq_count;
            if (!in) {
                throw std::runtime_error(
                    "Catalogue corrompu : TABLE attend "
                    "<name> <ncols> <pk_count> <uniq_count>");
            }

            for (std::size_t i = 0; i < count; ++i) {
                Column col;
                int type = 0;
                int nn = 0;
                in >> col.name >> type >> col.max_length >> nn;
                if (!in) {
                    throw std::runtime_error(
                        "Catalogue corrompu : colonne incomplète");
                }
                col.is_not_null = (nn != 0);
                col.type = static_cast<DataType>(type);
                schema.columns.push_back(std::move(col));
            }

            // PK
            if (pk_count > 0) {
                std::string pk_tag;
                in >> pk_tag;
                if (pk_tag != "PK") {
                    throw std::runtime_error(
                        "Catalogue corrompu : attendu PK, trouvé " + pk_tag);
                }
                std::string cols_str;
                in >> cols_str;
                if (!in) {
                    throw std::runtime_error(
                        "Catalogue corrompu : PK incomplète");
                }
                schema.primary_key = split_columns(cols_str);
            }

            // UNIQUE
            for (std::size_t i = 0; i < uniq_count; ++i) {
                std::string uq_tag;
                in >> uq_tag;
                if (uq_tag != "UNIQUE") {
                    throw std::runtime_error(
                        "Catalogue corrompu : attendu UNIQUE, trouvé " + uq_tag);
                }
                UniqueConstraint uc;
                std::string cols_str;
                in >> uc.name >> cols_str;
                if (!in) {
                    throw std::runtime_error(
                        "Catalogue corrompu : UNIQUE incomplète");
                }
                uc.columns = split_columns(cols_str);
                schema.unique_constraints.push_back(std::move(uc));
            }

            tables_.emplace(schema.name, std::move(schema));
        }
        else if (tag == "INDEX") {
            // J5.1 : INDEX <table> <index_name> <col1,col2,...> <unique>
            std::string table_name, index_name, cols_str;
            int unique = 0;
            in >> table_name >> index_name >> cols_str >> unique;
            if (!in) {
                throw std::runtime_error(
                    "Catalogue corrompu : INDEX incomplet");
            }

            IndexDef idx;
            idx.name = index_name;
            idx.unique = (unique != 0);
            idx.columns = split_columns(cols_str);

            auto it = tables_.find(table_name);
            if (it != tables_.end()) {
                it->second.indexes.push_back(std::move(idx));
            }
        }
        else {
            throw std::runtime_error(
                "Catalogue corrompu : tag inconnu '" + tag + "' "
                "(format v2 attendu)");
        }
    }
}

void Catalog::save() const {
    std::filesystem::create_directories(directory_);
    std::ofstream out(directory_ / "catalog.meta", std::ios::trunc);
    if (!out) throw std::runtime_error("Impossible d'écrire le catalogue");

    for (const auto& [name, schema] : tables_) {
        out << "TABLE " << name
            << ' ' << schema.columns.size()
            << ' ' << schema.primary_key.size()
            << ' ' << schema.unique_constraints.size()
            << '\n';

        for (const auto& col : schema.columns) {
            out << col.name << ' '
                << static_cast<int>(col.type) << ' '
                << col.max_length << ' '
                << (col.is_not_null ? 1 : 0) << '\n';
        }

        if (!schema.primary_key.empty()) {
            out << "PK " << join_columns(schema.primary_key) << '\n';
        }

        // UNIQUE : nom auto-généré si anonyme (format sans ambiguïté)
        for (std::size_t ui = 0; ui < schema.unique_constraints.size(); ++ui) {
            const auto& uc = schema.unique_constraints[ui];
            std::string uq_name = uc.name.empty()
                ? ("uq_" + name + "_" + std::to_string(ui))
                : uc.name;
            out << "UNIQUE " << uq_name << ' '
                << join_columns(uc.columns) << '\n';
        }

        for (const auto& idx : schema.indexes) {
            out << "INDEX " << name << ' ' << idx.name << ' '
                << join_columns(idx.columns) << ' '
                << (idx.unique ? 1 : 0) << '\n';
        }
    }
}

// ---------------------------------------------------------------------------
// Gestion des tables
// ---------------------------------------------------------------------------

void Catalog::add_table(const TableSchema& schema) {
    if (contains(schema.name)) {
        throw std::runtime_error("La table existe déjà: " + schema.name);
    }
    tables_.emplace(schema.name, schema);
}

void Catalog::remove_table(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        throw std::runtime_error("Table inconnue: " + name);
    }
    tables_.erase(it);
}

const TableSchema& Catalog::table(const std::string& name) const {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        throw std::runtime_error("Table inconnue: " + name);
    }
    return it->second;
}

bool Catalog::contains(const std::string& name) const {
    return tables_.find(name) != tables_.end();
}

std::vector<std::string> Catalog::table_names() const {
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& [name, _] : tables_) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<TableSchema> Catalog::all_tables() const {
    std::vector<TableSchema> schemas;
    schemas.reserve(tables_.size());
    for (const auto& [_, schema] : tables_) schemas.push_back(schema);
    std::sort(schemas.begin(), schemas.end(),
              [](const TableSchema& a, const TableSchema& b) {
                  return a.name < b.name;
              });
    return schemas;
}

// ---------------------------------------------------------------------------
// Gestion des index (J5.1)
// ---------------------------------------------------------------------------

void Catalog::add_index(const std::string& table, const IndexDef& index) {
    auto it = tables_.find(table);
    if (it == tables_.end()) {
        throw std::runtime_error("Table inconnue: " + table);
    }

    for (const auto& idx : it->second.indexes) {
        if (idx.name == index.name) {
            throw std::runtime_error("Index déjà existant: " + index.name);
        }
    }

    it->second.indexes.push_back(index);
}

void Catalog::remove_index(const std::string& table,
                            const std::string& index_name) {
    auto it = tables_.find(table);
    if (it == tables_.end()) {
        throw std::runtime_error("Table inconnue: " + table);
    }

    auto& idxs = it->second.indexes;
    for (auto i = idxs.begin(); i != idxs.end(); ++i) {
        if (i->name == index_name) {
            idxs.erase(i);
            return;
        }
    }
    throw std::runtime_error("Index inconnu: " + index_name);
}

const std::vector<IndexDef>&
Catalog::indexes_of(const std::string& table) const {
    auto it = tables_.find(table);
    if (it == tables_.end()) {
        throw std::runtime_error("Table inconnue: " + table);
    }
    return it->second.indexes;
}

} // namespace kerneldb