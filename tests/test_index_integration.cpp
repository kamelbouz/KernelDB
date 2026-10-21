#include <catch2/catch_test_macros.hpp>
#include "kerneldb/database.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace kerneldb;

namespace {

struct TempDbDir {
    static std::atomic<std::uint64_t> counter;
    std::filesystem::path dir;

    TempDbDir()
        : dir(std::filesystem::temp_directory_path() /
              ("kerneldb_idx_int_" + std::to_string(++counter) + "_" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(dir);
    }
    ~TempDbDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
std::atomic<std::uint64_t> TempDbDir::counter{0};

} // namespace

namespace {
IndexLookupResult select_lookup(Database& db, const std::string& sql) {
    auto stmt = std::get<SelectStatement>(Parser{}.parse(sql));
    return db.index_manager().lookup_equal(db.schema_of(stmt.table), *stmt.where);
}
}

TEST_CASE("J5.5 - choix PK UNIQUE et replis conservateurs") {
    TempDbDir t;
    Database db(t.dir);
    db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, email VARCHAR(40) UNIQUE, score DOUBLE UNIQUE, enabled BOOLEAN UNIQUE)");
    db.execute("INSERT INTO u VALUES (1, 'a@x', 2.5, TRUE)");
    db.execute("INSERT INTO u VALUES (2, 'b@x', -0.0, FALSE)");
    REQUIRE(select_lookup(db, "SELECT * FROM u WHERE score = 0.0").rid.has_value());
    REQUIRE(db.execute("SELECT * FROM u WHERE score = 0.0").rows.size() == 1);
    for (const auto& predicate : {"id = 1", "email = 'a@x'", "score = 2.5",
                                  "enabled = TRUE", "id = 1 AND score > 0"}) {
        auto result = select_lookup(db, std::string("SELECT * FROM u WHERE ") + predicate);
        REQUIRE(result.used_index);
        REQUIRE(result.rid.has_value());
    }
    auto absent = select_lookup(db, "SELECT * FROM u WHERE id = 999");
    REQUIRE(absent.used_index);
    REQUIRE_FALSE(absent.rid.has_value());
    for (const auto& predicate : {"id = 1.0", "score = 2", "id > 0", "email IS NULL",
                                  "id = NULL", "id = 1 OR id = 2",
                                  "id = 1 AND (score > 0 OR enabled = TRUE)"}) {
        REQUIRE_FALSE(select_lookup(db, std::string("SELECT * FROM u WHERE ") + predicate).used_index);
    }
    auto result = db.execute("SELECT id FROM u WHERE id = 1 AND score > 100");
    REQUIRE(result.rows.empty());
    REQUIRE(db.execute("SELECT * FROM u WHERE id = 1 AND id = 2").rows.empty());
    REQUIRE_THROWS(db.execute("SELECT * FROM u WHERE id = 999 AND unknown_col = 1"));
    auto prepared = db.prepare("SELECT id FROM u WHERE email = ?");
    prepared.bind_string(1, "a@x");
    REQUIRE(prepared.execute(db).rows.size() == 1);

    // Conserver la sémantique du scan pour les paramètres non finis.
    auto nan_where = WhereExpr::make_cond(Condition{
        "score", CompareOp::Equal, std::numeric_limits<double>::quiet_NaN(), {}, {}});
    REQUIRE_FALSE(db.index_manager().lookup_equal(db.schema_of("u"), *nan_where).used_index);
}

TEST_CASE("J5.5 - cles composites completes dans l'ordre du schema") {
    TempDbDir t;
    Database db(t.dir);
    db.execute("CREATE TABLE u (tenant INTEGER, id INTEGER, email VARCHAR(40), PRIMARY KEY (tenant, id), UNIQUE (tenant, email))");
    db.execute("INSERT INTO u VALUES (7, 1, 'a@x')");
    db.execute("INSERT INTO u VALUES (8, 1, NULL)");
    for (const auto& predicate : {"id = 1 AND tenant = 7", "email = 'a@x' AND tenant = 7"}) {
        auto sql = std::string("SELECT * FROM u WHERE ") + predicate;
        REQUIRE(select_lookup(db, sql).used_index);
        REQUIRE(db.execute(sql).rows.size() == 1);
    }
    REQUIRE_FALSE(select_lookup(db, "SELECT * FROM u WHERE id = 1").used_index);
    REQUIRE_FALSE(select_lookup(db, "SELECT * FROM u WHERE tenant = 8 AND email = NULL").used_index);
    REQUIRE(db.execute("SELECT * FROM u WHERE id = 1").rows.size() == 2);
    REQUIRE(db.execute("SELECT * FROM u WHERE tenant = 8 AND email IS NULL").rows.size() == 1);
}

TEST_CASE("J5.5 - resultat indexe identique au scan force") {
    TempDbDir t;
    Database db(t.dir);
    db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, email VARCHAR(40) UNIQUE, age INTEGER)");
    db.execute("INSERT INTO u VALUES (1, 'a@x', 10)");
    db.execute("INSERT INTO u VALUES (2, 'b@x', 20)");
    db.execute("INSERT INTO u VALUES (3, NULL, 30)");
    for (const auto& predicate : {"id = 1", "id = 999", "email = 'b@x'",
                                  "id = 2 AND age > 50", "id = 2.0", "email IS NULL"}) {
        auto indexed = db.execute(std::string("SELECT * FROM u WHERE ") + predicate + " ORDER BY id");
        auto scan = db.execute(std::string("SELECT * FROM u WHERE (") + predicate
                               + ") OR id = -999999 ORDER BY id");
        REQUIRE(indexed.columns == scan.columns);
        REQUIRE(indexed.rows.size() == scan.rows.size());
        for (std::size_t i = 0; i < indexed.rows.size(); ++i)
            REQUIRE(indexed.rows[i].values == scan.rows[i].values);
    }
    auto count = db.execute("SELECT COUNT(*) FROM u WHERE id = 999");
    REQUIRE(std::get<std::int64_t>(count.rows[0].values[0]) == 0);
    auto grouped = db.execute("SELECT age, COUNT(*) FROM u WHERE id = 2 GROUP BY age");
    REQUIRE(grouped.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(grouped.rows[0].values[0]) == 20);
    REQUIRE(db.execute("SELECT DISTINCT age FROM u WHERE id = 2 ORDER BY age LIMIT 0").rows.empty());
}

TEST_CASE("J5.5 - SELECT indexe apres mutations rollback et reouverture") {
    TempDbDir t;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, email VARCHAR(40) UNIQUE, name VARCHAR(100))");
        db.execute("INSERT INTO u VALUES (1, 'a@x', 'a')");
        db.execute("UPDATE u SET id = 10, email = 'b@x', name = 'A much longer name' WHERE id = 1");
        REQUIRE(db.execute("SELECT * FROM u WHERE id = 1").rows.empty());
        REQUIRE(db.execute("SELECT * FROM u WHERE email = 'a@x'").rows.empty());
        REQUIRE(db.execute("SELECT * FROM u WHERE id = 10").rows.size() == 1);
        db.begin();
        db.execute("UPDATE u SET id = 20, email = 'c@x' WHERE id = 10");
        REQUIRE(db.execute("SELECT * FROM u WHERE id = 20").rows.size() == 1);
        db.rollback();
        REQUIRE(db.execute("SELECT * FROM u WHERE id = 20").rows.empty());
        REQUIRE(db.execute("SELECT * FROM u WHERE email = 'b@x'").rows.size() == 1);
        db.execute("INSERT INTO u VALUES (2, 'd@x', 'd')");
        db.execute("DELETE FROM u WHERE id = 2");
        REQUIRE(db.execute("SELECT * FROM u WHERE id = 2").rows.empty());
    }
    Database reopened(t.dir);
    REQUIRE(reopened.execute("SELECT * FROM u WHERE id = 10").rows.size() == 1);
    REQUIRE(reopened.execute("SELECT * FROM u WHERE email = 'b@x'").rows.size() == 1);
}

TEST_CASE("J5.5 - recherche ponctuelle ne scanne pas une autre page") {
    TempDbDir t;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, email VARCHAR(40) UNIQUE, body VARCHAR(2000))");
        auto insert = db.prepare("INSERT INTO u VALUES (?, ?, ?)");
        for (int i = 1; i <= 12; ++i) {
            insert.bind_int(1, i);
            insert.bind_string(2, "a" + std::to_string(i) + "@x");
            insert.bind_string(3, std::string(1500, 'x'));
            insert.execute(db);
        }
    }
    // Corrompre uniquement un marqueur sur la dernière page. Le scan complet
    // échouera, mais les recherches sur la première page doivent fonctionner.
    {
        std::fstream file(t.dir / "u.tbl", std::ios::binary | std::ios::in | std::ios::out);
        std::uint32_t count = 0;
        file.seekg(12);
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        REQUIRE(count >= 2);
        const auto offset = FILE_HEADER_SIZE + (count - 1) * PAGE_SIZE_DEFAULT;
        std::vector<std::byte> page(PAGE_SIZE_DEFAULT);
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(reinterpret_cast<char*>(page.data()), page.size());
        REQUIRE(file.good());
        const auto slot = read_slot(page, 0);
        REQUIRE(slot.length > 0);
        file.seekp(static_cast<std::streamoff>(offset + slot.offset));
        file.put('\x7e');
        file.flush();
        REQUIRE(file.good());
    }
    Database db(t.dir);
    REQUIRE(db.execute("SELECT * FROM u WHERE id = 1").rows.size() == 1);
    REQUIRE(db.execute("SELECT * FROM u WHERE email = 'a1@x'").rows.size() == 1);
    REQUIRE(db.execute("SELECT * FROM u WHERE id = 999").rows.empty());
    REQUIRE(db.execute("SELECT * FROM u WHERE email = 'absent@x'").rows.empty());
    REQUIRE_THROWS(db.execute("SELECT * FROM u WHERE id = 1 OR id = 999"));
}

TEST_CASE("J5.5 - RowId absent signale un index incoherent") {
    TempDbDir t;
    TableSchema schema;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY)");
        db.execute("INSERT INTO u VALUES (1)");
        schema = db.schema_of("u");
    }
    {
        BPlusTree tree(t.dir / "u.idx", PAGE_SIZE_DEFAULT);
        tree.insert(IndexManager::encode_key(schema, schema.primary_key,
                     Row{{std::int64_t{999}}}), RowId{999, 0});
    }
    Database db(t.dir);
    REQUIRE_THROWS_AS(db.execute("SELECT * FROM u WHERE id = 999"), std::runtime_error);
    REQUIRE(db.execute("SELECT * FROM u WHERE id = 1").rows.size() == 1);
}

// ---------------------------------------------------------------------------
// E.2.1 — Fichier .idx créé et maintenu
// ---------------------------------------------------------------------------

TEST_CASE("IndexIntegration - PK cree un fichier .idx") {
    TempDbDir t;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR(20))");
        REQUIRE(std::filesystem::exists(t.dir / "u.idx"));
    }
}

TEST_CASE("IndexIntegration - UNIQUE cree un fichier .idx") {
    TempDbDir t;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50) UNIQUE)");
        // Nom de contrainte : uq_u_0 ; chemin : <table>.uq_<contrainte>.idx.
        REQUIRE(std::filesystem::exists(t.dir / "u.uq_uq_u_0.idx"));
        REQUIRE(db.index_manager().unique_index("u", "uq_u_0") != nullptr);
    }
}

TEST_CASE("IndexIntegration - sans contrainte = pas de fichier .idx") {
    TempDbDir t;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
        REQUIRE_FALSE(std::filesystem::exists(t.dir / "u.idx"));
    }
}

TEST_CASE("IndexIntegration - index survit a INSERT/DELETE") {
    TempDbDir t;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR(20))");
        REQUIRE(std::filesystem::exists(t.dir / "u.idx"));

        db.execute("INSERT INTO u VALUES (1, 'a')");
        db.execute("INSERT INTO u VALUES (2, 'b')");
        REQUIRE(std::filesystem::exists(t.dir / "u.idx"));

        db.execute("DELETE FROM u WHERE id = 1");
        REQUIRE(std::filesystem::exists(t.dir / "u.idx"));
    }
}

TEST_CASE("IndexIntegration - DROP TABLE supprime le .idx") {
    TempDbDir t;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY)");
        REQUIRE(std::filesystem::exists(t.dir / "u.idx"));

        db.execute("DROP TABLE u");
        REQUIRE_FALSE(std::filesystem::exists(t.dir / "u.idx"));
    }
}

// ---------------------------------------------------------------------------
// E.2.2 — Persistance : l'index est chargé, pas reconstruit
// ---------------------------------------------------------------------------

TEST_CASE("IndexIntegration - l'index est recharge apres fermeture") {
    TempDbDir t;
    // Créer + insérer
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR(20))");
        db.execute("INSERT INTO u VALUES (1, 'a')");
        db.execute("INSERT INTO u VALUES (2, 'b')");
        db.execute("INSERT INTO u VALUES (3, 'c')");
    }

    // Taille du fichier après création
    auto size_before = std::filesystem::file_size(t.dir / "u.idx");

    // Rouvrir — l'index doit être CHARGÉ, pas reconstruit
    {
        Database db(t.dir);
        // Si l'index était reconstruit, la taille pourrait changer
        // (ou pas, mais on vérifie que les données sont là)
        auto size_after = std::filesystem::file_size(t.dir / "u.idx");
        REQUIRE(size_before == size_after);

        // Les données doivent être accessibles
        auto r = db.execute("SELECT * FROM u ORDER BY id");
        REQUIRE(r.rows.size() == 3);
    }
}

TEST_CASE("IndexIntegration - contraintes respectees apres reouverture") {
    TempDbDir t;
    {
        Database db(t.dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, email VARCHAR(50) UNIQUE)");
        db.execute("INSERT INTO u VALUES (1, 'a@x.com')");
    }
    {
        Database db(t.dir);
        // PK
        REQUIRE_THROWS_AS(db.execute("INSERT INTO u VALUES (1, 'b@x.com')"),
                          std::runtime_error);
        // UNIQUE
        REQUIRE_THROWS_AS(db.execute("INSERT INTO u VALUES (2, 'a@x.com')"),
                          std::runtime_error);
        // OK
        REQUIRE_NOTHROW(db.execute("INSERT INTO u VALUES (2, 'b@x.com')"));
    }
}

// ---------------------------------------------------------------------------
// E.2.3 — UPDATE sur PK patche l'index
// ---------------------------------------------------------------------------

TEST_CASE("IndexIntegration - UPDATE PK patche l'index") {
    TempDbDir t;
    Database db(t.dir);
    db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR(20))");
    db.execute("INSERT INTO u VALUES (1, 'a')");

    // Modifier la PK : 1 → 100
    db.execute("UPDATE u SET id = 100 WHERE id = 1");

    // Vérifier la mise à jour de l'index avant de réutiliser l'ancienne PK.
    const auto& schema = db.schema_of("u");
    const auto* tree = db.index_manager().pk_index("u");
    REQUIRE(tree != nullptr);
    const auto old_key = IndexManager::encode_key(
        schema, schema.primary_key, Row{{std::int64_t{1}, std::string{"a"}}});
    const auto new_key = IndexManager::encode_key(
        schema, schema.primary_key, Row{{std::int64_t{100}, std::string{"a"}}});
    REQUIRE_FALSE(tree->search(old_key).has_value());
    REQUIRE(tree->search(new_key).has_value());

    // L'ancienne valeur est libre ; la nouvelle reste réservée.
    REQUIRE_NOTHROW(db.execute("INSERT INTO u VALUES (1, 'b')"));
    REQUIRE_NOTHROW(db.execute("INSERT INTO u VALUES (2, 'c')"));
    REQUIRE_THROWS_AS(db.execute("INSERT INTO u VALUES (100, 'd')"),
                      std::runtime_error);

    const auto rows = db.execute("SELECT id FROM u ORDER BY id");
    REQUIRE(rows.rows.size() == 3);
    REQUIRE(std::get<std::int64_t>(rows.rows[0].values[0]) == 1);
    REQUIRE(std::get<std::int64_t>(rows.rows[1].values[0]) == 2);
    REQUIRE(std::get<std::int64_t>(rows.rows[2].values[0]) == 100);
}

TEST_CASE("IndexIntegration - UPDATE UNIQUE patche l'index") {
    TempDbDir t;
    Database db(t.dir);
    db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, email VARCHAR(50) UNIQUE)");
    db.execute("INSERT INTO u VALUES (1, 'a@x.com')");

    // Modifier l'email
    db.execute("UPDATE u SET email = 'b@x.com' WHERE id = 1");

    // L'ancienne valeur doit être libre
    REQUIRE_NOTHROW(db.execute("INSERT INTO u VALUES (2, 'a@x.com')"));
    // La nouvelle est prise
    REQUIRE_THROWS_AS(db.execute("INSERT INTO u VALUES (3, 'b@x.com')"),
                      std::runtime_error);
}

TEST_CASE("IndexIntegration - UPDATE avec changement de taille garde l'index coherent") {
    TempDbDir t;
    Database db(t.dir);
    db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR(100))");
    db.execute("INSERT INTO u VALUES (1, 'a')");

    // Augmenter la taille du VARCHAR
    db.execute("UPDATE u SET name = 'a_very_long_name_to_force_reallocation' WHERE id = 1");

    // La PK est toujours là
    REQUIRE_THROWS_AS(db.execute("INSERT INTO u VALUES (1, 'x')"),
                      std::runtime_error);
    // Et l'UPDATE a bien été appliqué
    auto r = db.execute("SELECT name FROM u");
    REQUIRE(std::get<std::string>(r.rows[0].values[0]) ==
            "a_very_long_name_to_force_reallocation");
}
