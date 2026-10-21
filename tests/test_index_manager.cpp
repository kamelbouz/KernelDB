#include <catch2/catch_test_macros.hpp>
#include "kerneldb/index_manager.hpp"
#include "kerneldb/storage.hpp"
#include <chrono>
#include <filesystem>

using namespace kerneldb;

namespace {

struct TempDir {
    std::filesystem::path dir;
    TempDir()
        : dir(std::filesystem::temp_directory_path() /
              ("kerneldb_idx_" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(dir);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};

// Construit un schéma table "u" avec PK sur (id)
TableSchema make_schema_pk_id() {
    TableSchema s;
    s.name = "u";
    s.columns.push_back({"id", DataType::Integer, 0, true});      // NOT NULL
    s.columns.push_back({"name", DataType::Varchar, 50, false});
    s.primary_key = {"id"};
    return s;
}

// Construit un schéma avec PK composite (a, b)
TableSchema make_schema_pk_ab() {
    TableSchema s;
    s.name = "u";
    s.columns.push_back({"a", DataType::Integer, 0, true});
    s.columns.push_back({"b", DataType::Integer, 0, true});
    s.columns.push_back({"c", DataType::Integer, 0, false});
    s.primary_key = {"a", "b"};
    return s;
}

// Construit un schéma avec UNIQUE sur email (nullable)
TableSchema make_schema_unique_email() {
    TableSchema s;
    s.name = "u";
    s.columns.push_back({"id", DataType::Integer, 0, false});
    s.columns.push_back({"email", DataType::Varchar, 50, false});
    s.unique_constraints.push_back({"", {"email"}});
    return s;
}

RowId make_rid(std::uint32_t page, std::uint16_t slot) {
    RowId r; r.page_id = page; r.slot_id = slot; return r;
}

} // namespace

// ---------------------------------------------------------------------------
// J5.4-C — Encodage des clés
// ---------------------------------------------------------------------------

TEST_CASE("IndexManager - encode_key mono-colonne Integer") {
    auto schema = make_schema_pk_id();
    Row row{{std::int64_t{42}, std::string{"Alice"}}};

    auto k = IndexManager::encode_key(schema, {"id"}, row);
    REQUIRE(k.type == IndexKey::Type::Varchar);
    REQUIRE_FALSE(k.str_val.empty());

    // Re-encoder la même chose doit donner le même résultat
    auto k2 = IndexManager::encode_key(schema, {"id"}, row);
    REQUIRE(k.str_val == k2.str_val);
}

TEST_CASE("IndexManager - encode_key mono-colonne Varchar") {
    auto schema = make_schema_unique_email();
    Row row{{std::int64_t{1}, std::string{"alice@x.com"}}};

    auto k = IndexManager::encode_key(schema, {"email"}, row);
    REQUIRE(k.type == IndexKey::Type::Varchar);
    REQUIRE_FALSE(k.str_val.empty());
}

TEST_CASE("IndexManager - encode_key composite ordre préservé") {
    auto schema = make_schema_pk_ab();

    Row r1{{std::int64_t{1}, std::int64_t{2}, std::int64_t{0}}};
    Row r2{{std::int64_t{1}, std::int64_t{3}, std::int64_t{0}}};
    Row r3{{std::int64_t{2}, std::int64_t{1}, std::int64_t{0}}};

    auto k1 = IndexManager::encode_key(schema, {"a", "b"}, r1);
    auto k2 = IndexManager::encode_key(schema, {"a", "b"}, r2);
    auto k3 = IndexManager::encode_key(schema, {"a", "b"}, r3);

    REQUIRE(k1 < k2);
    REQUIRE(k2 < k3);
}

TEST_CASE("IndexManager - encode_key Integer négatif ordre préservé") {
    auto schema = make_schema_pk_id();
    Row r_neg{{std::int64_t{-10}}};
    Row r_zero{{std::int64_t{0}}};
    Row r_pos{{std::int64_t{10}}};

    auto k_neg = IndexManager::encode_key(schema, {"id"}, r_neg);
    auto k_zero = IndexManager::encode_key(schema, {"id"}, r_zero);
    auto k_pos = IndexManager::encode_key(schema, {"id"}, r_pos);

    REQUIRE(k_neg < k_zero);
    REQUIRE(k_zero < k_pos);
}

TEST_CASE("IndexManager - row_all_null") {
    auto schema = make_schema_unique_email();

    Row r_null{{std::int64_t{1}, std::monostate{}}};
    Row r_val{{std::int64_t{2}, std::string{"a"}}};

    REQUIRE(IndexManager::row_all_null(schema, {"email"}, r_null));
    REQUIRE_FALSE(IndexManager::row_all_null(schema, {"email"}, r_val));
}

// ---------------------------------------------------------------------------
// J5.4-C — ensure_indexes / rebuild
// ---------------------------------------------------------------------------

TEST_CASE("IndexManager - ensure_indexes crée l'index PK") {
    TempDir t;
    StorageManager storage(t.dir, 4096);
    auto schema = make_schema_pk_id();
    storage.create_table(schema);

    storage.insert(schema, Row{{std::int64_t{10}, std::string{"Alice"}}});
    storage.insert(schema, Row{{std::int64_t{20}, std::string{"Bob"}}});

    IndexManager im(t.dir, 4096);
    im.ensure_indexes(schema, storage);

    REQUIRE(im.has_indexes("u"));
    const BPlusTree* tree = im.pk_index("u");
    REQUIRE(tree != nullptr);

    // Les deux entrées doivent être retrouvables
    auto k1 = IndexManager::encode_key(schema, {"id"},
        Row{{std::int64_t{10}, std::string{"Alice"}}});
    auto k2 = IndexManager::encode_key(schema, {"id"},
        Row{{std::int64_t{20}, std::string{"Bob"}}});
    REQUIRE(tree->search(k1).has_value());
    REQUIRE(tree->search(k2).has_value());
}

TEST_CASE("IndexManager - ensure_indexes idempotent") {
    TempDir t;
    StorageManager storage(t.dir, 4096);
    auto schema = make_schema_pk_id();
    storage.create_table(schema);
    storage.insert(schema, Row{{std::int64_t{1}, std::string{"x"}}});

    IndexManager im(t.dir, 4096);
    im.ensure_indexes(schema, storage);
    im.ensure_indexes(schema, storage);   // 2e appel — ne doit rien casser
    im.ensure_indexes(schema, storage);   // 3e

    const BPlusTree* tree = im.pk_index("u");
    REQUIRE(tree != nullptr);
    auto k = IndexManager::encode_key(schema, {"id"},
        Row{{std::int64_t{1}, std::string{"x"}}});
    REQUIRE(tree->search(k).has_value());
}

TEST_CASE("IndexManager - reconstruction depuis un fichier existant") {
    TempDir t;
    StorageManager storage(t.dir, 4096);
    auto schema = make_schema_pk_id();
    storage.create_table(schema);
    storage.insert(schema, Row{{std::int64_t{1}, std::string{"a"}}});
    storage.insert(schema, Row{{std::int64_t{2}, std::string{"b"}}});

    {
        IndexManager im(t.dir, 4096);
        im.ensure_indexes(schema, storage);
    }

    // Nouveau manager → doit recharger le fichier existant
    {
        IndexManager im(t.dir, 4096);
        im.ensure_indexes(schema, storage);
        const BPlusTree* tree = im.pk_index("u");
        REQUIRE(tree != nullptr);
        auto k1 = IndexManager::encode_key(schema, {"id"},
            Row{{std::int64_t{1}, std::string{"a"}}});
        REQUIRE(tree->search(k1).has_value());
    }
}

TEST_CASE("IndexManager - drop_indexes supprime les fichiers") {
    TempDir t;
    StorageManager storage(t.dir, 4096);
    auto schema = make_schema_pk_id();
    storage.create_table(schema);
    storage.insert(schema, Row{{std::int64_t{1}, std::string{"a"}}});

    IndexManager im(t.dir, 4096);
    im.ensure_indexes(schema, storage);
    REQUIRE(std::filesystem::exists(t.dir / "u.idx"));

    im.drop_indexes("u");
    REQUIRE_FALSE(std::filesystem::exists(t.dir / "u.idx"));
    REQUIRE_FALSE(im.has_indexes("u"));
}

// ---------------------------------------------------------------------------
// J5.4-C — UNIQUE avec NULL (sémantique Oracle)
// ---------------------------------------------------------------------------

TEST_CASE("IndexManager - UNIQUE ignore les lignes toutes-NULL") {
    TempDir t;
    StorageManager storage(t.dir, 4096);
    auto schema = make_schema_unique_email();
    storage.create_table(schema);

    storage.insert(schema, Row{{std::int64_t{1}, std::string{"a@x.com"}}});
    storage.insert(schema, Row{{std::int64_t{2}, std::monostate{}}});
    storage.insert(schema, Row{{std::int64_t{3}, std::monostate{}}});

    IndexManager im(t.dir, 4096);
    im.ensure_indexes(schema, storage);

    // Trouver l'index UNIQUE (nom auto : uq_u_0)
    const BPlusTree* tree = im.unique_index("u", "uq_u_0");
    REQUIRE(tree != nullptr);

    // L'index ne doit contenir QUE la ligne avec 'a@x.com'
    // (les lignes NULL ne sont pas indexées)
    auto k_null = IndexManager::encode_key(schema, {"email"},
        Row{{std::int64_t{2}, std::monostate{}}});
    REQUIRE_FALSE(tree->search(k_null).has_value());

    auto k_val = IndexManager::encode_key(schema, {"email"},
        Row{{std::int64_t{1}, std::string{"a@x.com"}}});
    REQUIRE(tree->search(k_val).has_value());
}

// ---------------------------------------------------------------------------
// J5.4-C — PK composite
// ---------------------------------------------------------------------------

TEST_CASE("IndexManager - PK composite") {
    TempDir t;
    StorageManager storage(t.dir, 4096);
    auto schema = make_schema_pk_ab();
    storage.create_table(schema);

    storage.insert(schema, Row{{std::int64_t{1}, std::int64_t{2}, std::int64_t{0}}});
    storage.insert(schema, Row{{std::int64_t{1}, std::int64_t{3}, std::int64_t{0}}});
    storage.insert(schema, Row{{std::int64_t{2}, std::int64_t{1}, std::int64_t{0}}});

    IndexManager im(t.dir, 4096);
    im.ensure_indexes(schema, storage);

    const BPlusTree* tree = im.pk_index("u");
    REQUIRE(tree != nullptr);

    auto k1 = IndexManager::encode_key(schema, {"a", "b"},
        Row{{std::int64_t{1}, std::int64_t{2}, std::int64_t{0}}});
    auto k2 = IndexManager::encode_key(schema, {"a", "b"},
        Row{{std::int64_t{1}, std::int64_t{3}, std::int64_t{0}}});
    auto k3 = IndexManager::encode_key(schema, {"a", "b"},
        Row{{std::int64_t{2}, std::int64_t{1}, std::int64_t{0}}});

    REQUIRE(tree->search(k1).has_value());
    REQUIRE(tree->search(k2).has_value());
    REQUIRE(tree->search(k3).has_value());
}

// ---------------------------------------------------------------------------
// J5.4-C — index_names
// ---------------------------------------------------------------------------

TEST_CASE("IndexManager - index_names liste PK et UNIQUE") {
    TempDir t;
    StorageManager storage(t.dir, 4096);
    TableSchema schema;
    schema.name = "u";
    schema.columns.push_back({"id", DataType::Integer, 0, true});
    schema.columns.push_back({"email", DataType::Varchar, 50, false});
    schema.primary_key = {"id"};
    schema.unique_constraints.push_back({"", {"email"}});
    storage.create_table(schema);

    IndexManager im(t.dir, 4096);
    im.ensure_indexes(schema, storage);

    auto names = im.index_names("u");
    REQUIRE(names.size() == 2);
}