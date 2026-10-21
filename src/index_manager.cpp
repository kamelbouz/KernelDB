#include "kerneldb/index_manager.hpp"
#include "kerneldb/storage.hpp"
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace kerneldb {

IndexManager::IndexManager(std::filesystem::path directory,
                            std::size_t page_size)
    : dir_(std::move(directory)), page_size_(page_size) {
    validate_page_size(page_size);
    std::filesystem::create_directories(dir_);
}

// ---------------------------------------------------------------------------
// Chemins des fichiers d'index
// ---------------------------------------------------------------------------

std::filesystem::path
IndexManager::pk_path(const std::string& table) const {
    return dir_ / (table + ".idx");
}

std::filesystem::path
IndexManager::unique_path(const std::string& table,
                           const std::string& constraint_name) const {
    return dir_ / (table + ".uq_" + constraint_name + ".idx");
}

// ---------------------------------------------------------------------------
// Encodage des clés composites
// ---------------------------------------------------------------------------

namespace {

void write_u8(std::vector<std::byte>& out, std::uint8_t v) {
    out.push_back(static_cast<std::byte>(v));
}

void write_u16_be(std::vector<std::byte>& out, std::uint16_t v) {
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(v & 0xFF));
}

void write_i64_be(std::vector<std::byte>& out, std::int64_t v) {
    std::uint64_t u = static_cast<std::uint64_t>(v) ^ 0x8000000000000000ULL;
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<std::byte>((u >> (i * 8)) & 0xFF));
    }
}

// Nom canonique d'une contrainte UNIQUE (auto-généré si anonyme).
std::string canon_unique_name(const TableSchema& schema, std::size_t i) {
    const auto& uc = schema.unique_constraints[i];
    return uc.name.empty()
        ? ("uq_" + schema.name + "_" + std::to_string(i))
        : uc.name;
}

} // namespace

IndexKey
IndexManager::encode_key(const TableSchema& schema,
                          const std::vector<std::string>& columns,
                          const Row& row) {
    std::vector<std::byte> buf;

    for (const auto& col_name : columns) {
        std::size_t ci = std::string::npos;
        for (std::size_t i = 0; i < schema.columns.size(); ++i) {
            if (schema.columns[i].name == col_name) { ci = i; break; }
        }
        if (ci == std::string::npos) {
            throw std::runtime_error(
                "encode_key : colonne inconnue : " + col_name);
        }
        if (ci >= row.values.size()) {
            throw std::runtime_error(
                "encode_key : index de colonne hors de la ligne");
        }

        const auto& v = row.values[ci];

        if (is_null(v)) {
            write_u8(buf, 0xFF);
            write_u16_be(buf, 0);
        } else if (std::holds_alternative<std::int64_t>(v)) {
            write_u8(buf, 0x01);
            write_u16_be(buf, 8);
            write_i64_be(buf, std::get<std::int64_t>(v));
                } else if (std::holds_alternative<std::string>(v)) {
            const auto& s = std::get<std::string>(v);
            write_u8(buf, 0x02);
            write_u16_be(buf, static_cast<std::uint16_t>(s.size()));
            for (char c : s) buf.push_back(static_cast<std::byte>(c));
        } else if (std::holds_alternative<double>(v)) {
            double d = std::get<double>(v);
            if (d == 0.0) d = 0.0;   // canonicalise -0.0
            write_u8(buf, 0x03);
            write_u16_be(buf, 8);
            std::uint64_t u = 0;
            std::memcpy(&u, &d, 8);
            for (int k = 7; k >= 0; --k) {
                buf.push_back(static_cast<std::byte>((u >> (k * 8)) & 0xFF));
            }
        } else if (std::holds_alternative<bool>(v)) {
            write_u8(buf, 0x04);
            write_u16_be(buf, 1);
            buf.push_back(std::get<bool>(v) ? std::byte{1} : std::byte{0});
        } else {
            throw std::runtime_error(
                "encode_key : type non supporté pour l'index");
        }
    }

    IndexKey key;
    key.type = IndexKey::Type::Varchar;
    key.str_val.resize(buf.size());
    if (!buf.empty()) {
        std::memcpy(key.str_val.data(), buf.data(), buf.size());
    }
    return key;
}

bool IndexManager::row_all_null(const TableSchema& schema,
                                 const std::vector<std::string>& columns,
                                 const Row& row) {
    for (const auto& col_name : columns) {
        std::size_t ci = std::string::npos;
        for (std::size_t i = 0; i < schema.columns.size(); ++i) {
            if (schema.columns[i].name == col_name) { ci = i; break; }
        }
        if (ci == std::string::npos) {
            throw std::runtime_error(
                "row_all_null : colonne inconnue : " + col_name);
        }
        if (ci >= row.values.size()) return false;
        if (!is_null(row.values[ci])) return false;
    }
    return true;
}
bool IndexManager::row_any_null(const TableSchema& schema,
                                 const std::vector<std::string>& columns,
                                 const Row& row) {
    for (const auto& col_name : columns) {
        std::size_t ci = std::string::npos;
        for (std::size_t i = 0; i < schema.columns.size(); ++i) {
            if (schema.columns[i].name == col_name) { ci = i; break; }
        }
        if (ci == std::string::npos) {
            throw std::runtime_error(
                "row_any_null : colonne inconnue : " + col_name);
        }
        if (ci < row.values.size() && is_null(row.values[ci])) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// load_or_create
// ---------------------------------------------------------------------------

std::unique_ptr<BPlusTree>
IndexManager::load_or_create(const std::filesystem::path& path,
                              const TableSchema& schema,
                              const std::vector<std::string>& columns,
                              const StorageManager& storage,
                              bool is_pk) {
    auto tree = std::make_unique<BPlusTree>(path, page_size_);

    if (std::filesystem::exists(path)) {
        return tree;
    }

    tree->create();

    auto rows = storage.scan_with_ids(schema);
    for (const auto& rw : rows) {
        if (!is_pk && row_any_null(schema, columns, rw.row)) {
            continue;
        }
        IndexKey key = encode_key(schema, columns, rw.row);
        tree->insert(key, rw.rid);
    }

    return tree;
}

// ---------------------------------------------------------------------------
// ensure_indexes
// ---------------------------------------------------------------------------

void IndexManager::ensure_indexes(const TableSchema& schema,
                                    const StorageManager& storage) {
    const std::string& table = schema.name;

    if (!schema.primary_key.empty()) {
        if (indexes_.find(table) == indexes_.end()) {
            auto tree = load_or_create(pk_path(table), schema,
                                        schema.primary_key, storage,
                                        /*is_pk=*/true);
            indexes_.emplace(table, std::move(tree));
        }
    }

    for (std::size_t i = 0; i < schema.unique_constraints.size(); ++i) {
        std::string cname = canon_unique_name(schema, i);
        std::string map_key = table + "." + cname;

        if (indexes_.find(map_key) == indexes_.end()) {
            auto tree = load_or_create(unique_path(table, cname), schema,
                                        schema.unique_constraints[i].columns,
                                        storage, /*is_pk=*/false);
            indexes_.emplace(map_key, std::move(tree));
        }
    }
}

// ---------------------------------------------------------------------------
// drop_indexes
// ---------------------------------------------------------------------------

void IndexManager::drop_indexes(const std::string& table) {
    std::string prefix_pk = table + ".idx";
    std::string prefix_uq = table + ".uq_";

    if (std::filesystem::exists(dir_)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            if (!entry.is_regular_file()) continue;
            std::string fname = entry.path().filename().string();

            bool is_pk = (fname == prefix_pk);
            bool is_uq = (fname.rfind(prefix_uq, 0) == 0 &&
                          fname.size() > prefix_uq.size() + 4 &&
                          fname.substr(fname.size() - 4) == ".idx");

            if (is_pk || is_uq) {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }

    std::string map_uq_prefix = table + ".";
    indexes_.erase(table);
    for (auto it = indexes_.begin(); it != indexes_.end(); ) {
        if (it->first.rfind(map_uq_prefix, 0) == 0) {
            it = indexes_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// rebuild
// ---------------------------------------------------------------------------

void IndexManager::rebuild(const TableSchema& schema,
                            const StorageManager& storage) {
    drop_indexes(schema.name);
    ensure_indexes(schema, storage);
}

// ---------------------------------------------------------------------------
// Accès
// ---------------------------------------------------------------------------

BPlusTree* IndexManager::pk_index(const std::string& table) {
    auto it = indexes_.find(table);
    return it == indexes_.end() ? nullptr : it->second.get();
}

const BPlusTree* IndexManager::pk_index(const std::string& table) const {
    auto it = indexes_.find(table);
    return it == indexes_.end() ? nullptr : it->second.get();
}

BPlusTree* IndexManager::unique_index(const std::string& table,
                                       const std::string& constraint_name) {
    auto it = indexes_.find(table + "." + constraint_name);
    return it == indexes_.end() ? nullptr : it->second.get();
}

const BPlusTree* IndexManager::unique_index(
        const std::string& table,
        const std::string& constraint_name) const {
    auto it = indexes_.find(table + "." + constraint_name);
    return it == indexes_.end() ? nullptr : it->second.get();
}

bool IndexManager::has_indexes(const std::string& table) const {
    if (indexes_.count(table) > 0) return true;
    std::string prefix = table + ".";
    for (const auto& [k, _] : indexes_) {
        if (k.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

std::vector<std::string>
IndexManager::index_names(const std::string& table) const {
    std::vector<std::string> names;

    if (indexes_.count(table) > 0) {
        names.push_back("pk_" + table);
    }

    std::string prefix = table + ".";
    for (const auto& [k, _] : indexes_) {
        if (k.rfind(prefix, 0) == 0) {
            names.push_back(k.substr(prefix.size()));
        }
    }
    return names;
}

// ---------------------------------------------------------------------------
// J5.4-D : vérifications avant insert/update
// ---------------------------------------------------------------------------

IndexLookupResult IndexManager::lookup_equal(const TableSchema& schema,
                                               const WhereExpr& where) const {
    std::vector<const Condition*> equalities;
    const auto collect = [&](const auto& self, const WhereExpr& expr) -> bool {
        if (expr.kind == WhereExpr::Kind::Or) return false;
        if (expr.kind == WhereExpr::Kind::And) {
            if (!expr.left || !expr.right) return false;
            return self(self, *expr.left) && self(self, *expr.right);
        }
        if (expr.cond.op == CompareOp::Equal) {
            equalities.push_back(&expr.cond);
        }
        return true;
    };
    if (!collect(collect, where)) return {};

    // Ne pas appliquer les conversions d'écriture aux comparaisons SQL.
    const auto same_type = [](const Value& value, DataType type) {
        switch (type) {
            case DataType::Integer:
                return std::holds_alternative<std::int64_t>(value);
            case DataType::Double:
                return std::holds_alternative<double>(value)
                    && std::isfinite(std::get<double>(value));
            case DataType::Varchar:
                return std::holds_alternative<std::string>(value);
            case DataType::Boolean:
                return std::holds_alternative<bool>(value);
        }
        return false;
    };

    const auto probe = [&](const std::vector<std::string>& columns,
                           const BPlusTree* tree) -> IndexLookupResult {
        if (!tree || columns.empty()) return {};
        Row lookup_row;
        lookup_row.values.resize(schema.columns.size(), null_value());
        for (const auto& name : columns) {
            const Condition* equality = nullptr;
            for (const auto* condition : equalities) {
                if (condition->column == name) {
                    equality = condition;
                    break;
                }
            }
            if (!equality) return {};
            std::size_t ci = 0;
            while (ci < schema.columns.size() && schema.columns[ci].name != name)
                ++ci;
            if (ci == schema.columns.size()) return {};
            const Value& value = equality->value;
            if (!same_type(value, schema.columns[ci].type)) return {};
            lookup_row.values[ci] = value;
        }
        const auto key = encode_key(schema, columns, lookup_row);
        return {true, tree->search(key)};
    };

    auto result = probe(schema.primary_key, pk_index(schema.name));
    if (result.used_index) return result;
    for (std::size_t i = 0; i < schema.unique_constraints.size(); ++i) {
        result = probe(schema.unique_constraints[i].columns,
                       unique_index(schema.name, canon_unique_name(schema, i)));
        if (result.used_index) return result;
    }
    return {};
}

bool IndexManager::can_insert_pk(const TableSchema& schema,
                                   const Row& row) const {
    if (schema.primary_key.empty()) return true;

    const BPlusTree* tree = pk_index(schema.name);
    if (!tree) return true;

    IndexKey key = encode_key(schema, schema.primary_key, row);
    return !tree->search(key).has_value();
}

bool IndexManager::can_insert_unique(const TableSchema& schema,
                                       const Row& row) const {
    for (std::size_t i = 0; i < schema.unique_constraints.size(); ++i) {
        const auto& uc = schema.unique_constraints[i];

        if (row_any_null(schema, uc.columns, row)) continue;

        const BPlusTree* tree = unique_index(schema.name,
                                              canon_unique_name(schema, i));
        if (!tree) continue;

        IndexKey key = encode_key(schema, uc.columns, row);
        if (tree->search(key).has_value()) return false;
    }
    return true;
}

bool IndexManager::can_update_pk(const TableSchema& schema,
                                   const Row& new_row,
                                   const RowId& exclude_rid) const {
    if (schema.primary_key.empty()) return true;

    const BPlusTree* tree = pk_index(schema.name);
    if (!tree) return true;

    IndexKey key = encode_key(schema, schema.primary_key, new_row);
    auto found = tree->search(key);
    if (!found.has_value()) return true;
    return found.value() == exclude_rid;
}

bool IndexManager::can_update_unique(const TableSchema& schema,
                                       const Row& new_row,
                                       const RowId& exclude_rid) const {
    for (std::size_t i = 0; i < schema.unique_constraints.size(); ++i) {
        const auto& uc = schema.unique_constraints[i];

        if (row_any_null(schema, uc.columns, new_row)) continue;

        const BPlusTree* tree = unique_index(schema.name,
                                              canon_unique_name(schema, i));
        if (!tree) continue;

        IndexKey key = encode_key(schema, uc.columns, new_row);
        auto found = tree->search(key);
        if (!found.has_value()) continue;
        if (!(found.value() == exclude_rid)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// J5.4-D : maintenance unitaire
// ---------------------------------------------------------------------------

void IndexManager::on_insert(const TableSchema& schema,
                              const Row& row,
                              const RowId& rid) {
    if (!schema.primary_key.empty()) {
        BPlusTree* tree = pk_index(schema.name);
        if (tree) {
            IndexKey key = encode_key(schema, schema.primary_key, row);
            tree->insert(key, rid);
        }
    }

    for (std::size_t i = 0; i < schema.unique_constraints.size(); ++i) {
        const auto& uc = schema.unique_constraints[i];

        if (row_any_null(schema, uc.columns, row)) continue;

        BPlusTree* tree = unique_index(schema.name,
                                        canon_unique_name(schema, i));
        if (tree) {
            IndexKey key = encode_key(schema, uc.columns, row);
            tree->insert(key, rid);
        }
    }
}

void IndexManager::on_delete(const TableSchema& schema, const Row& old_row) {
    if (!schema.primary_key.empty()) {
        BPlusTree* tree = pk_index(schema.name);
        if (tree) {
            IndexKey key = encode_key(schema, schema.primary_key, old_row);
            tree->remove(key);
        }
    }

    for (std::size_t i = 0; i < schema.unique_constraints.size(); ++i) {
        const auto& uc = schema.unique_constraints[i];

        if (row_any_null(schema, uc.columns, old_row)) continue;

        BPlusTree* tree = unique_index(schema.name,
                                        canon_unique_name(schema, i));
        if (tree) {
            IndexKey key = encode_key(schema, uc.columns, old_row);
            tree->remove(key);
        }
    }
}

void IndexManager::on_update(const TableSchema& schema,
                              const Row& old_row, const RowId& old_rid,
                              const Row& new_row, const RowId& new_rid) {
    // PK
    if (!schema.primary_key.empty()) {
        IndexKey old_key = encode_key(schema, schema.primary_key, old_row);
        IndexKey new_key = encode_key(schema, schema.primary_key, new_row);
        bool key_changed = !(old_key == new_key);
        bool rid_changed = !(old_rid == new_rid);

        if (key_changed || rid_changed) {
            BPlusTree* tree = pk_index(schema.name);
            if (tree) {
                if (key_changed) tree->remove(old_key);
                tree->insert(new_key, new_rid);
            }
        }
    }

    // UNIQUE
    for (std::size_t i = 0; i < schema.unique_constraints.size(); ++i) {
        const auto& uc = schema.unique_constraints[i];

        // Oracle strict : dès qu'une colonne de la contrainte est NULL,
        // la ligne n'est pas indexée (NULL n'est jamais égal à NULL).
        bool old_indexed = !row_any_null(schema, uc.columns, old_row);
        bool new_indexed = !row_any_null(schema, uc.columns, new_row);

        bool key_changed = (old_indexed != new_indexed);
        if (!key_changed && old_indexed && new_indexed) {
            IndexKey old_key = encode_key(schema, uc.columns, old_row);
            IndexKey new_key = encode_key(schema, uc.columns, new_row);
            key_changed = !(old_key == new_key);
        }
        bool rid_changed = !(old_rid == new_rid);

        if (!key_changed && !rid_changed) continue;

        BPlusTree* tree = unique_index(schema.name,
                                        canon_unique_name(schema, i));
        if (!tree) continue;

        if (old_indexed) {
            tree->remove(encode_key(schema, uc.columns, old_row));
        }
        if (new_indexed) {
            tree->insert(encode_key(schema, uc.columns, new_row), new_rid);
        }
    }
}

} // namespace kerneldb
