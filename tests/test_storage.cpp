// tests/test_storage.cpp (version propre)
#include <catch2/catch_test_macros.hpp>
#include "kerneldb/storage.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
using namespace kerneldb;

TEST_CASE("Storage - read_by_id lit un slot sans parcourir la table") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_point_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    {
        StorageManager storage(dir);
        TableSchema schema{"u", {{"id", DataType::Integer, 0},
                                  {"name", DataType::Varchar, 50}}};
        storage.create_table(schema);
        auto first = storage.insert(schema, Row{{std::int64_t{1}, std::string{"Alice"}}});
        auto second = storage.insert(schema, Row{{std::int64_t{2}, std::string{"Bob"}}});
        auto row = storage.read_by_id(schema, second);
        REQUIRE(row.has_value());
        REQUIRE(std::get<std::int64_t>(row->values[0]) == 2);
        REQUIRE(std::get<std::string>(row->values[1]) == "Bob");
        REQUIRE_FALSE(storage.read_by_id(schema, RowId{999, 0}).has_value());
        REQUIRE_FALSE(storage.read_by_id(schema, RowId{first.page_id, 65535}).has_value());
        storage.delete_by_id(schema, first);
        REQUIRE_FALSE(storage.read_by_id(schema, first).has_value());
        auto moved = storage.update_by_id(schema, second,
            Row{{std::int64_t{2}, std::string{"A longer name"}}});
        REQUIRE(std::get<std::string>(storage.read_by_id(schema, moved)->values[1])
                == "A longer name");
    }
    std::filesystem::remove_all(dir);
}

struct TempDir {
    std::filesystem::path dir;
    TempDir()
        : dir(std::filesystem::temp_directory_path() /
              ("kerneldb_store_" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(dir);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};

TEST_CASE("Storage — insert puis scan") {
    TempDir t;
    StorageManager s(t.dir);
    TableSchema schema{"u", {{"id", DataType::Integer, 0},
                             {"name", DataType::Varchar, 20}}};
    s.create_table(schema);
    s.insert(schema, Row{{std::int64_t{1}, std::string{"Alice"}}});
    s.insert(schema, Row{{std::int64_t{2}, std::string{"Bob"}}});

    auto rows = s.scan(schema);
    REQUIRE(rows.size() == 2);
    REQUIRE(std::get<std::string>(rows[0].values[1]) == "Alice");
}

TEST_CASE("Storage — rewrite remplace tout") {
    TempDir t;
    StorageManager s(t.dir);
    TableSchema schema{"u", {{"id", DataType::Integer, 0}}};
    s.create_table(schema);
    s.insert(schema, Row{{std::int64_t{1}}});
    s.insert(schema, Row{{std::int64_t{2}}});

    std::vector<Row> kept{ Row{{std::int64_t{2}}} };
    s.rewrite(schema, kept);

    auto rows = s.scan(schema);
    REQUIRE(rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(rows[0].values[0]) == 2);
}


// ------- NOUVEAU : validation de page_size -------

TEST_CASE("Storage - page_size valide") {
    TempDir t;
    // Toutes ces tailles sont valides
    REQUIRE_NOTHROW(kerneldb::StorageManager(t.dir, 4096));
    REQUIRE_NOTHROW(kerneldb::StorageManager(t.dir, 8192));
    REQUIRE_NOTHROW(kerneldb::StorageManager(t.dir, 16384));
    REQUIRE_NOTHROW(kerneldb::StorageManager(t.dir, 32768));
    REQUIRE_NOTHROW(kerneldb::StorageManager(t.dir, 65536));
}

TEST_CASE("Storage - page_size invalide (trop petit)") {
    TempDir t;
    REQUIRE_THROWS_AS(kerneldb::StorageManager(t.dir, 2048), std::runtime_error);
    REQUIRE_THROWS_AS(kerneldb::StorageManager(t.dir, 0), std::runtime_error);
}

TEST_CASE("Storage - page_size invalide (trop grand)") {
    TempDir t;
    REQUIRE_THROWS_AS(kerneldb::StorageManager(t.dir, 131072), std::runtime_error);
}

TEST_CASE("Storage - page_size invalide (pas puissance de 2)") {
    TempDir t;
    REQUIRE_THROWS_AS(kerneldb::StorageManager(t.dir, 5000), std::runtime_error);
    REQUIRE_THROWS_AS(kerneldb::StorageManager(t.dir, 10000), std::runtime_error);
}

TEST_CASE("Storage - creation de table avec page_size 16K") {
    TempDir t;
    kerneldb::StorageManager s(t.dir, 8192);

    TableSchema schema{"u", {{"id", DataType::Integer, 0}}};
    s.create_table(schema, 16384);

    REQUIRE(s.page_size_of("u") == 16384);
}

TEST_CASE("Storage - creation de table avec page_size par defaut") {
    TempDir t;
    kerneldb::StorageManager s(t.dir, 8192);

    TableSchema schema{"u", {{"id", DataType::Integer, 0}}};
    s.create_table(schema);   // utilise la taille par défaut

    REQUIRE(s.page_size_of("u") == 8192);
}

TEST_CASE("Storage - insert et scan apres pagination") {
    TempDir t;
    kerneldb::StorageManager s(t.dir, 8192);

    TableSchema schema{"u", {{"id", DataType::Integer, 0},
                              {"name", DataType::Varchar, 50}}};
    s.create_table(schema);
    s.insert(schema, Row{{std::int64_t{1}, std::string{"Alice"}}});
    s.insert(schema, Row{{std::int64_t{2}, std::string{"Bob"}}});

    auto rows = s.scan(schema);
    REQUIRE(rows.size() == 2);
    REQUIRE(std::get<std::int64_t>(rows[0].values[0]) == 1);
    REQUIRE(std::get<std::string>(rows[1].values[1]) == "Bob");
}

TEST_CASE("Storage - read_page / write_page") {
    TempDir t;
    kerneldb::StorageManager s(t.dir, 8192);

    TableSchema schema{"u", {{"id", DataType::Integer, 0}}};
    s.create_table(schema);

    // Crée une page, la remplit, la relit
    kerneldb::Page p(0, 8192);
    p.data[0] = std::byte{42};
    p.data[100] = std::byte{99};

    s.write_page("u", p);

    auto p2 = s.read_page("u", 0);
    REQUIRE(p2.page_id == 0);
    REQUIRE(p2.page_size == 8192);
    REQUIRE(p2.data[0] == std::byte{42});
    REQUIRE(p2.data[100] == std::byte{99});
}


// ---------------------------------------------------------------------------
// F09 — En-tête tronqué doit être refusé (pas interprété comme table vide)
// ---------------------------------------------------------------------------

TEST_CASE("F09 - fichier avec seulement le magic est refuse") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_f09_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);

    // Créer un fichier de table contenant seulement "MDBP"
    {
        std::ofstream out(dir / "broken.tbl", std::ios::binary);
        out.write("MDBP", 4);
    }

    StorageManager sm(dir);
    TableSchema schema{"broken", {{"id", DataType::Integer, 0}}};

    REQUIRE_THROWS_AS(sm.scan(schema), std::runtime_error);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("F09 - en-tete partiel (magic + version) est refuse") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_f09b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);

    // Magic + version seulement (8 octets sur 24)
    {
        std::ofstream out(dir / "broken.tbl", std::ios::binary);
        out.write("MDBP", 4);
        std::uint32_t version = 1;
        out.write(reinterpret_cast<const char*>(&version), 4);
    }

    StorageManager sm(dir);
    TableSchema schema{"broken", {{"id", DataType::Integer, 0}}};

    REQUIRE_THROWS_AS(sm.scan(schema), std::runtime_error);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}


// ---------------------------------------------------------------------------
// C09 — Validation des métadonnées
// ---------------------------------------------------------------------------

TEST_CASE("C09 - version de fichier invalide est rejetee") {
    TempDir t;
    {
        StorageManager sm(t.dir);
        TableSchema schema{"t", {{"id", DataType::Integer, 0}}};
        sm.create_table(schema);
        sm.insert(schema, Row{{std::int64_t{1}}});
    }

    // Modifier la version dans le fichier (offset 4-7) → 99
    {
        std::fstream f(t.dir / "t.tbl", std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(f.good());
        f.seekp(4);
        std::uint32_t bad_version = 99;
        f.write(reinterpret_cast<const char*>(&bad_version), 4);
    }

    {
        StorageManager sm(t.dir);
        TableSchema schema{"t", {{"id", DataType::Integer, 0}}};
        REQUIRE_THROWS_AS(sm.scan(schema), std::runtime_error);
    }
}

TEST_CASE("C09 - taille de page invalide est rejetee") {
    TempDir t;
    {
        StorageManager sm(t.dir);
        TableSchema schema{"t", {{"id", DataType::Integer, 0}}};
        sm.create_table(schema);
    }

    // Modifier la taille de page (offset 8-11) → 1
    {
        std::fstream f(t.dir / "t.tbl", std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(f.good());
        f.seekp(8);
        std::uint32_t bad_page_size = 1;
        f.write(reinterpret_cast<const char*>(&bad_page_size), 4);
    }

    {
        StorageManager sm(t.dir);
        REQUIRE_THROWS_AS(sm.page_size_of("t"), std::runtime_error);
    }
}

TEST_CASE("C09 - marqueur invalide est rejete") {
    TempDir t;
    {
        StorageManager sm(t.dir);
        TableSchema schema{"t", {{"id", DataType::Integer, 0}}};
        sm.create_table(schema);
        sm.insert(schema, Row{{std::int64_t{1}}});
    }

    // Lire la page 0, modifier le marker de la première ligne
    {
        StorageManager sm(t.dir);
        TableSchema schema{"t", {{"id", DataType::Integer, 0}}};
        Page page = sm.read_page("t", 0);

        // Le slot 0 pointe vers la ligne 0
        Slot slot = read_slot(page.data, 0);
        REQUIRE(slot.length > 0);
        // Modifier le marker (1er octet de la ligne)
        page.data[slot.offset] = std::byte{0x42};

        sm.write_page("t", page);
    }

    // Reouvrir : la lecture doit échouer
    {
        StorageManager sm(t.dir);
        TableSchema schema{"t", {{"id", DataType::Integer, 0}}};
        REQUIRE_THROWS_AS(sm.scan(schema), std::runtime_error);
    }
}

TEST_CASE("Storage - scan_with_ids retourne les RowId") {
    TempDir t;
    StorageManager s(t.dir);
    TableSchema schema{"u", {{"id", DataType::Integer, 0}}};
    s.create_table(schema);

    auto rid1 = s.insert(schema, Row{{std::int64_t{10}}});
    auto rid2 = s.insert(schema, Row{{std::int64_t{20}}});
    auto rid3 = s.insert(schema, Row{{std::int64_t{30}}});

    auto rows = s.scan_with_ids(schema);
    REQUIRE(rows.size() == 3);

    // Vérifier que chaque rid retourné correspond à un insert
    std::set<std::pair<std::uint32_t, std::uint16_t>> rid_set;
    rid_set.insert({rid1.page_id, rid1.slot_id});
    rid_set.insert({rid2.page_id, rid2.slot_id});
    rid_set.insert({rid3.page_id, rid3.slot_id});

    for (const auto& rw : rows) {
        REQUIRE(rid_set.count({rw.rid.page_id, rw.rid.slot_id}) == 1);
    }
}

TEST_CASE("Storage - delete_by_id supprime un slot") {
    TempDir t;
    StorageManager s(t.dir);
    TableSchema schema{"u", {{"id", DataType::Integer, 0}}};
    s.create_table(schema);

    auto rid1 = s.insert(schema, Row{{std::int64_t{10}}});
    auto rid2 = s.insert(schema, Row{{std::int64_t{20}}});
    auto rid3 = s.insert(schema, Row{{std::int64_t{30}}});

    s.delete_by_id(schema, rid2);

    auto rows = s.scan_with_ids(schema);
    REQUIRE(rows.size() == 2);

    // Vérifier que rid1 et rid3 sont toujours là, avec les mêmes RowId
    bool found_rid1 = false, found_rid3 = false, found_rid2 = false;
    for (const auto& rw : rows) {
        if (rw.rid.page_id == rid1.page_id && rw.rid.slot_id == rid1.slot_id) {
            found_rid1 = true;
            REQUIRE(std::get<std::int64_t>(rw.row.values[0]) == 10);
        }
        if (rw.rid.page_id == rid3.page_id && rw.rid.slot_id == rid3.slot_id) {
            found_rid3 = true;
            REQUIRE(std::get<std::int64_t>(rw.row.values[0]) == 30);
        }
        if (rw.rid.page_id == rid2.page_id && rw.rid.slot_id == rid2.slot_id) {
            found_rid2 = true;
        }
    }
    REQUIRE(found_rid1);
    REQUIRE(found_rid3);
    REQUIRE_FALSE(found_rid2);
}
TEST_CASE("Storage - update_by_id in-place (taille compatible)") {
    TempDir t;
    StorageManager s(t.dir);
    TableSchema schema{"u", {{"id", DataType::Integer, 0},
                              {"name", DataType::Varchar, 50}}};
    s.create_table(schema);

    auto rid = s.insert(schema, Row{{std::int64_t{1}, std::string{"Alice"}}});

    // Remplacer "Alice" par "Bob" (plus court) → in-place
    RowId new_rid = s.update_by_id(schema, rid,
        Row{{std::int64_t{1}, std::string{"Bob"}}});

    // Le RowId doit être inchangé
    REQUIRE(new_rid.page_id == rid.page_id);
    REQUIRE(new_rid.slot_id == rid.slot_id);

    // La valeur doit être mise à jour
    auto rows = s.scan_with_ids(schema);
    REQUIRE(rows.size() == 1);
    REQUIRE(std::get<std::string>(rows[0].row.values[1]) == "Bob");
}

TEST_CASE("Storage - update_by_id avec taille plus grande") {
    TempDir t;
    StorageManager s(t.dir);
    TableSchema schema{"u", {{"id", DataType::Integer, 0},
                              {"name", DataType::Varchar, 50}}};
    s.create_table(schema);

    auto rid = s.insert(schema, Row{{std::int64_t{1}, std::string{"Bob"}}});

    // Remplacer "Bob" par "Alice" (plus long) → delete + insert
    RowId new_rid = s.update_by_id(schema, rid,
        Row{{std::int64_t{1}, std::string{"Alice"}}});

    // Le RowId peut changer (ou pas selon l'espace libre), mais la valeur doit être correcte
    auto rows = s.scan_with_ids(schema);
    REQUIRE(rows.size() == 1);
    REQUIRE(std::get<std::string>(rows[0].row.values[1]) == "Alice");
}
