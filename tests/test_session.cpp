#include <catch2/catch_test_macros.hpp>
#include "kerneldb/session.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace kerneldb;


// Chaque test utilise un dossier temporaire isolé
struct TempSession {
    static std::atomic<std::uint64_t> counter;
    std::filesystem::path dir;
    Session session;

    TempSession()
        : dir(std::filesystem::temp_directory_path() /
              ("kerneldb_session_" + std::to_string(++counter) + "_" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()))),
          session(dir) {}

    ~TempSession() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
std::atomic<std::uint64_t> TempSession::counter{0};

// ------- Gestion des bases -------


TEST_CASE("Session - aucune base au depart") {
    TempSession t;
    REQUIRE(t.session.database_names().empty());
    REQUIRE_FALSE(t.session.has_active());
}

TEST_CASE("Session - CREATE DATABASE") {
    TempSession t;
    auto r = t.session.execute("CREATE DATABASE base1");
    REQUIRE(r.message.find("créée") != std::string::npos);
    REQUIRE(t.session.has_database("base1"));
}

TEST_CASE("Session - CREATE DATABASE avec PAGE_SIZE") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1 WITH PAGE_SIZE 16384");
    REQUIRE(t.session.has_database("base1"));

    // Vérifie le config.meta
    std::ifstream in(t.dir / "base1" / "config.meta");
    std::string key;
    std::size_t value = 0;
    in >> key >> value;
    REQUIRE(key == "page_size");
    REQUIRE(value == 16384);
}

TEST_CASE("Session - CREATE DATABASE doublon echoue") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1");
    REQUIRE_THROWS_AS(t.session.execute("CREATE DATABASE base1"),
                      std::runtime_error);
}

TEST_CASE("Session - CREATE DATABASE nom invalide") {
    TempSession t;
    REQUIRE_THROWS_AS(t.session.execute("CREATE DATABASE a/b"),
                      std::runtime_error);
    REQUIRE_THROWS_AS(t.session.execute("CREATE DATABASE .hidden"),
                      std::runtime_error);
}

TEST_CASE("Session - DROP DATABASE") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1");
    REQUIRE(t.session.has_database("base1"));

    auto r = t.session.execute("DROP DATABASE base1");
    REQUIRE(r.message.find("supprimée") != std::string::npos);
    REQUIRE_FALSE(t.session.has_database("base1"));
}

TEST_CASE("Session - DROP DATABASE inconnue echoue") {
    TempSession t;
    REQUIRE_THROWS_AS(t.session.execute("DROP DATABASE nexistepas"),
                      std::runtime_error);
}

TEST_CASE("Session - USE active une base") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1");
    REQUIRE_FALSE(t.session.has_active());

    t.session.execute("USE base1");
    REQUIRE(t.session.has_active());
    REQUIRE(t.session.active_name() == "base1");
}

TEST_CASE("Session - USE base inconnue echoue") {
    TempSession t;
    REQUIRE_THROWS_AS(t.session.execute("USE nexistepas"),
                      std::runtime_error);
}

TEST_CASE("Session - SHOW DATABASES") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1");
    t.session.execute("CREATE DATABASE base2");

    auto r = t.session.execute("SHOW DATABASES");
    REQUIRE(r.columns.size() == 1);
    REQUIRE(r.columns[0] == "name");
    REQUIRE(r.rows.size() == 2);
}

// ------- Commandes SQL sans base active -------

TEST_CASE("Session - CREATE TABLE sans base active echoue") {
    TempSession t;
    REQUIRE_THROWS_AS(
        t.session.execute("CREATE TABLE users (id INTEGER)"),
        std::runtime_error);
}

TEST_CASE("Session - SELECT sans base active echoue") {
    TempSession t;
    REQUIRE_THROWS_AS(
        t.session.execute("SELECT * FROM users"),
        std::runtime_error);
}

// ------- SQL dans une base active -------

TEST_CASE("Session - CREATE TABLE dans base active") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1");
    t.session.execute("USE base1");
    t.session.execute("CREATE TABLE users (id INTEGER, name VARCHAR(20))");
    t.session.execute("INSERT INTO users VALUES (1, 'Alice')");

    auto r = t.session.execute("SELECT * FROM users");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Alice");
}

// ------- Cross-database -------

TEST_CASE("Session - SELECT cross-database") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1");
    t.session.execute("CREATE DATABASE base2");

    t.session.execute("USE base1");
    t.session.execute("CREATE TABLE users (id INTEGER, name VARCHAR(20))");
    t.session.execute("INSERT INTO users VALUES (1, 'Alice')");

    t.session.execute("USE base2");
    t.session.execute("CREATE TABLE products (id INTEGER)");

    // Cross-database : depuis base2, on lit base1.users
    auto r = t.session.execute("SELECT * FROM base1.users");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Alice");
}

TEST_CASE("Session - INSERT cross-database") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1");
    t.session.execute("CREATE DATABASE base2");

    t.session.execute("USE base1");
    t.session.execute("CREATE TABLE users (id INTEGER, name VARCHAR(20))");

    t.session.execute("USE base2");
    // INSERT dans base1.users
    t.session.execute("INSERT INTO base1.users VALUES (1, 'Bob')");

    t.session.execute("USE base1");
    auto r = t.session.execute("SELECT * FROM users");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Bob");
}

TEST_CASE("Session - SELECT cross-database sans USE") {
    TempSession t;
    t.session.execute("CREATE DATABASE base1");
    t.session.execute("USE base1");
    t.session.execute("CREATE TABLE users (id INTEGER)");
    t.session.execute("INSERT INTO users VALUES (42)");

    // Quitte la base
    // (pas de "USE" qui désactive ; on crée une nouvelle session)
    // En pratique, on peut faire USE d'une autre base :
    t.session.execute("CREATE DATABASE base2");
    t.session.execute("USE base2");

    // Maintenant on lit base1.users
    auto r = t.session.execute("SELECT * FROM base1.users");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 42);
}

// ------- Persistance -------

TEST_CASE("Session - bases visibles apres reouverture") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_sess_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session session(dir);
        session.execute("CREATE DATABASE base1");
        session.execute("CREATE DATABASE base2");
        session.execute("USE base1");
        session.execute("CREATE TABLE users (id INTEGER)");
        session.execute("INSERT INTO users VALUES (42)");
    }

    {
        Session session(dir);
        auto names = session.database_names();
        REQUIRE(names.size() == 2);

        session.execute("USE base1");
        auto r = session.execute("SELECT * FROM users");
        REQUIRE(r.rows.size() == 1);
        REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 42);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Session - page_size persiste apres reouverture") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_sess_page_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session session(dir);
        session.execute("CREATE DATABASE base16 WITH PAGE_SIZE 16384");
    }

    {
        Session session(dir);
        session.execute("USE base16");
        session.execute("CREATE TABLE t (id INTEGER)");
        session.execute("INSERT INTO t VALUES (1)");

        auto r = session.execute("SELECT * FROM t");
        REQUIRE(r.rows.size() == 1);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}


// ---------------------------------------------------------------------------
// F10 — Routage multi-base après réouverture
// ---------------------------------------------------------------------------

TEST_CASE("F10 - cross-database apres reouverture") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_f10_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    // Session 1 : créer a et b, données dans b.users
    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("CREATE DATABASE b");
        s.execute("USE b");
        s.execute("CREATE TABLE users (id INTEGER)");
        s.execute("INSERT INTO users VALUES (42)");
    }

    // Session 2 : USE a, puis SELECT sur b.users
    {
        Session s(dir);
        s.execute("USE a");
        auto r = s.execute("SELECT * FROM b.users");
        REQUIRE(r.rows.size() == 1);
        REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 42);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("F10 - has_database voit les bases sur disque") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_f10b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("CREATE DATABASE b");
    }

    {
        Session s(dir);
        REQUIRE(s.has_database("a"));
        REQUIRE(s.has_database("b"));
        REQUIRE_FALSE(s.has_database("c"));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("F10 - base inconnue reste refusee") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_f10c_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    Session s(dir);
    REQUIRE_THROWS_AS(s.database("nexistepas"), std::runtime_error);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}


// ---------------------------------------------------------------------------
// R07 — PREPARE référence une autre base
// ---------------------------------------------------------------------------

TEST_CASE("R07 - PREPARE reference une autre base") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_r07_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("CREATE DATABASE b");
        s.execute("USE a");
        s.execute("CREATE TABLE t (id INTEGER)");
        s.execute("INSERT INTO t VALUES (1)");
        s.execute("USE b");
        s.execute("CREATE TABLE t (id INTEGER)");
        s.execute("INSERT INTO t VALUES (2)");
    }

    {
        Session s(dir);
        s.execute("USE a");
        s.execute("PREPARE p AS SELECT * FROM b.t");
        auto r = s.execute("EXECUTE p");
        REQUIRE(r.rows.size() == 1);
        // Doit retourner 2 (b.t), pas 1 (a.t)
        REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 2);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("R07 - PREPARE sur la base active continue de marcher") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_r07b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("USE a");
        s.execute("CREATE TABLE t (id INTEGER)");
        s.execute("INSERT INTO t VALUES (42)");
        s.execute("PREPARE p AS SELECT * FROM t");
        auto r = s.execute("EXECUTE p");
        REQUIRE(r.rows.size() == 1);
        REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 42);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("R07 - EXECUTE sans PREPARE prealable echoue") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_r07c_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("USE a");
        REQUIRE_THROWS_AS(
            s.execute("EXECUTE nexistepas"),
            std::runtime_error);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// A03 — DEALLOCATE route vers la base propriétaire
// ---------------------------------------------------------------------------

TEST_CASE("A03 - DEALLOCATE route vers la base cible") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_a03_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("CREATE DATABASE b");
        s.execute("USE a");
        s.execute("CREATE TABLE t (id INTEGER)");
        s.execute("INSERT INTO t VALUES (1)");
        s.execute("USE b");
        s.execute("CREATE TABLE t (id INTEGER)");
        s.execute("INSERT INTO t VALUES (2)");
        s.execute("USE a");

        s.execute("PREPARE remote AS SELECT * FROM b.t");
        auto r = s.execute("EXECUTE remote");
        REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 2);

        // A03 : DEALLOCATE doit router vers b
        REQUIRE_NOTHROW(s.execute("DEALLOCATE remote"));
        // Maintenant EXECUTE doit échouer
        REQUIRE_THROWS_AS(s.execute("EXECUTE remote"), std::runtime_error);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("A03 - DEALLOCATE d'un nom inconnu echoue") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_a03b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("USE a");
        REQUIRE_THROWS_AS(s.execute("DEALLOCATE nexistepas"),
                          std::runtime_error);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// A04 — Noms de prepared statements uniques par session
// ---------------------------------------------------------------------------

TEST_CASE("A04 - PREPARE refuse les noms dupliques") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_a04_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("CREATE DATABASE b");
        s.execute("USE a");
        s.execute("CREATE TABLE t (id INTEGER)");
        s.execute("PREPARE same AS SELECT * FROM t");

        s.execute("USE b");
        s.execute("CREATE TABLE t (id INTEGER)");
        // A04 : refuse le doublon
        REQUIRE_THROWS_AS(s.execute("PREPARE same AS SELECT * FROM t"),
                          std::runtime_error);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("A04 - DEALLOCATE puis PREPARE du meme nom fonctionne") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_a04b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("USE a");
        s.execute("CREATE TABLE t (id INTEGER)");
        s.execute("PREPARE p AS SELECT * FROM t");
        s.execute("DEALLOCATE p");
        // Maintenant on peut réutiliser le nom
        REQUIRE_NOTHROW(s.execute("PREPARE p AS SELECT * FROM t"));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("A04 - DROP DATABASE nettoie les prepared statements") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_a04c_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s(dir);
        s.execute("CREATE DATABASE a");
        s.execute("CREATE DATABASE b");
        s.execute("USE b");
        s.execute("CREATE TABLE t (id INTEGER)");
        s.execute("PREPARE p AS SELECT * FROM t");

        // DROP b → p doit être nettoyé
        s.execute("DROP DATABASE b");

        // Recréer b : PREPARE p doit être possible
        s.execute("CREATE DATABASE b");
        s.execute("USE b");
        s.execute("CREATE TABLE t (id INTEGER)");
        REQUIRE_NOTHROW(s.execute("PREPARE p AS SELECT * FROM t"));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C02 — DROP DATABASE protégé par le verrou
// ---------------------------------------------------------------------------

TEST_CASE("C02 - DROP refuse si la base est ouverte ailleurs") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c02_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Session s1(dir);
        s1.execute("CREATE DATABASE a");
        s1.execute("USE a");   // la base a est verrouillée par s1

        Session s2(dir);
        REQUIRE_THROWS_AS(s2.execute("DROP DATABASE a"), std::runtime_error);
    }

    {
        Session s(dir);
        s.execute("DROP DATABASE a");
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
