#include <catch2/catch_test_macros.hpp>
#include "kerneldb/database.hpp"
#include <atomic>
#include <cstdint>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <thread>
#include "kerneldb/lock.hpp"
using namespace kerneldb;

struct TempDb {
    static std::atomic<std::uint64_t> counter;
    std::filesystem::path dir;
    Database db;
    TempDb()
        : dir(std::filesystem::temp_directory_path() /
              ("kerneldb_test_" + std::to_string(++counter) + "_" +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()))),
          db(dir) {}
    ~TempDb() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};
std::atomic<std::uint64_t> TempDb::counter{0};


TEST_CASE("Database - UPDATE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'a')");
    t.db.execute("INSERT INTO u VALUES (2, 'b')");

    t.db.execute("UPDATE u SET name = 'X' WHERE id = 2");
    auto sel = t.db.execute("SELECT * FROM u ORDER BY id");
    REQUIRE(std::get<std::string>(sel.rows[1].values[1]) == "X");
}

TEST_CASE("Database - DELETE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1)");
    t.db.execute("INSERT INTO u VALUES (2)");
    t.db.execute("INSERT INTO u VALUES (3)");

    t.db.execute("DELETE FROM u WHERE id >= 2");
    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);
}

TEST_CASE("Database - DISTINCT") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("INSERT INTO u VALUES (2, 'Bob')");
    t.db.execute("INSERT INTO u VALUES (3, 'Alice')");

    auto r = t.db.execute("SELECT DISTINCT name FROM u");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Database - LIKE et IN et BETWEEN") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("INSERT INTO u VALUES (2, 'Bob')");
    t.db.execute("INSERT INTO u VALUES (3, 'Anna')");

    REQUIRE(t.db.execute("SELECT * FROM u WHERE name LIKE 'A%'").rows.size() == 2);
    REQUIRE(t.db.execute("SELECT * FROM u WHERE id IN (1, 3)").rows.size() == 2);
    REQUIRE(t.db.execute("SELECT * FROM u WHERE id BETWEEN 1 AND 2").rows.size() == 2);
}

TEST_CASE("Database - IS NULL") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50))");
    t.db.execute("INSERT INTO u VALUES (1, NULL)");
    t.db.execute("INSERT INTO u VALUES (2, 'a@b.c')");
    t.db.execute("INSERT INTO u VALUES (3, NULL)");

    auto r = t.db.execute("SELECT * FROM u WHERE email IS NULL");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Database - COUNT(*)") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50))");
    t.db.execute("INSERT INTO u VALUES (1, NULL)");
    t.db.execute("INSERT INTO u VALUES (2, 'a')");
    t.db.execute("INSERT INTO u VALUES (3, NULL)");

    auto r = t.db.execute("SELECT COUNT(*) FROM u");
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 3);
}

TEST_CASE("Database - COUNT(col) ignore les NULL") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50))");
    t.db.execute("INSERT INTO u VALUES (1, NULL)");
    t.db.execute("INSERT INTO u VALUES (2, 'a')");
    t.db.execute("INSERT INTO u VALUES (3, NULL)");

    auto r = t.db.execute("SELECT COUNT(email) FROM u");
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 1);
}

TEST_CASE("Database - AND / OR") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, age INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1, 15)");
    t.db.execute("INSERT INTO u VALUES (2, 30)");
    t.db.execute("INSERT INTO u VALUES (3, 45)");
    t.db.execute("INSERT INTO u VALUES (4, 70)");

    REQUIRE(t.db.execute("SELECT * FROM u WHERE age > 18 AND age < 65").rows.size() == 2);
    REQUIRE(t.db.execute("SELECT * FROM u WHERE age < 18 OR age > 65").rows.size() == 2);
}

TEST_CASE("Database - GROUP BY une colonne") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, age INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1, 25)");
    t.db.execute("INSERT INTO u VALUES (2, 30)");
    t.db.execute("INSERT INTO u VALUES (3, 25)");
    t.db.execute("INSERT INTO u VALUES (4, 30)");
    t.db.execute("INSERT INTO u VALUES (5, 25)");

    auto r = t.db.execute("SELECT age, COUNT(*) FROM u GROUP BY age");
    REQUIRE(r.columns.size() == 2);
    REQUIRE(r.rows.size() == 2);

    bool found_25 = false, found_30 = false;
    for (const auto& row : r.rows) {
        auto age = std::get<std::int64_t>(row.values[0]);
        auto cnt = std::get<std::int64_t>(row.values[1]);
        if (age == 25) { REQUIRE(cnt == 3); found_25 = true; }
        if (age == 30) { REQUIRE(cnt == 2); found_30 = true; }
    }
    REQUIRE(found_25);
    REQUIRE(found_30);
}

TEST_CASE("Database - GROUP BY plusieurs colonnes") {
    TempDb t;
    t.db.execute("CREATE TABLE u (age INTEGER, city VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (25, 'Paris')");
    t.db.execute("INSERT INTO u VALUES (25, 'Lyon')");
    t.db.execute("INSERT INTO u VALUES (25, 'Paris')");
    t.db.execute("INSERT INTO u VALUES (30, 'Paris')");

    auto r = t.db.execute("SELECT age, city, COUNT(*) FROM u GROUP BY age, city");
    REQUIRE(r.rows.size() == 3);
}

TEST_CASE("Database - GROUP BY avec WHERE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, age INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1, 15)");
    t.db.execute("INSERT INTO u VALUES (2, 25)");
    t.db.execute("INSERT INTO u VALUES (3, 25)");
    t.db.execute("INSERT INTO u VALUES (4, 35)");

    auto r = t.db.execute("SELECT age, COUNT(*) FROM u WHERE age > 18 GROUP BY age");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Database - GROUP BY avec SUM et AVG") {
    TempDb t;
    t.db.execute("CREATE TABLE u (cat INTEGER, price INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1, 10)");
    t.db.execute("INSERT INTO u VALUES (1, 20)");
    t.db.execute("INSERT INTO u VALUES (2, 5)");
    t.db.execute("INSERT INTO u VALUES (2, 15)");

    auto r = t.db.execute("SELECT cat, SUM(price), AVG(price) FROM u GROUP BY cat");
    REQUIRE(r.rows.size() == 2);
    for (const auto& row : r.rows) {
        auto cat = std::get<std::int64_t>(row.values[0]);
        auto sum = std::get<std::int64_t>(row.values[1]);
        auto avg = std::get<double>(row.values[2]);
        if (cat == 1) { REQUIRE(sum == 30); REQUIRE(avg == 15.0); }
        if (cat == 2) { REQUIRE(sum == 20); REQUIRE(avg == 10.0); }
    }
}

TEST_CASE("Database - GROUP BY avec MIN et MAX") {
    TempDb t;
    t.db.execute("CREATE TABLE u (cat INTEGER, v INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1, 10)");
    t.db.execute("INSERT INTO u VALUES (1, 50)");
    t.db.execute("INSERT INTO u VALUES (1, 30)");
    t.db.execute("INSERT INTO u VALUES (2, 5)");
    t.db.execute("INSERT INTO u VALUES (2, 100)");

    auto r = t.db.execute("SELECT cat, MIN(v), MAX(v) FROM u GROUP BY cat");
    REQUIRE(r.rows.size() == 2);
    for (const auto& row : r.rows) {
        auto cat = std::get<std::int64_t>(row.values[0]);
        auto mn = std::get<std::int64_t>(row.values[1]);
        auto mx = std::get<std::int64_t>(row.values[2]);
        if (cat == 1) { REQUIRE(mn == 10); REQUIRE(mx == 50); }
        if (cat == 2) { REQUIRE(mn == 5);  REQUIRE(mx == 100); }
    }
}

TEST_CASE("F08 - ordre des colonnes suit SELECT (colonnes simples)") {
    TempDb t;
    t.db.execute("CREATE TABLE g (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO g VALUES (1, 10)");
    t.db.execute("INSERT INTO g VALUES (2, 20)");

    auto r = t.db.execute("SELECT b, a FROM g GROUP BY a, b");
    REQUIRE(r.columns.size() == 2);
    REQUIRE(r.columns[0] == "b");
    REQUIRE(r.columns[1] == "a");
}

TEST_CASE("F08 - ordre des colonnes avec COUNT a la fin") {
    TempDb t;
    t.db.execute("CREATE TABLE g (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO g VALUES (1, 10)");
    t.db.execute("INSERT INTO g VALUES (1, 20)");

    auto r = t.db.execute("SELECT b, a, COUNT(*) FROM g GROUP BY a, b");
    REQUIRE(r.columns.size() == 3);
    REQUIRE(r.columns[0] == "b");
    REQUIRE(r.columns[1] == "a");
    REQUIRE(r.columns[2] == "count");
}

TEST_CASE("F08 - ordre des colonnes avec COUNT au milieu",
          "[!mayfail]") {
    TempDb t;
    t.db.execute("CREATE TABLE g (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO g VALUES (1, 10)");
    t.db.execute("INSERT INTO g VALUES (1, 20)");

    auto r = t.db.execute("SELECT b, COUNT(*), a FROM g GROUP BY a, b");
    REQUIRE(r.columns.size() == 3);
    REQUIRE(r.columns[0] == "b");
    REQUIRE(r.columns[1] == "count");
    REQUIRE(r.columns[2] == "a");
}

TEST_CASE("Database - GROUP BY plusieurs agregats") {
    TempDb t;
    t.db.execute("CREATE TABLE u (cat INTEGER, v INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1, 10)");
    t.db.execute("INSERT INTO u VALUES (1, 20)");
    t.db.execute("INSERT INTO u VALUES (2, 5)");

    auto r = t.db.execute(
        "SELECT cat, COUNT(*), SUM(v), AVG(v), MIN(v), MAX(v) FROM u GROUP BY cat");
    REQUIRE(r.columns.size() == 6);
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Database - GROUP BY avec HAVING sur colonne de groupe") {
    TempDb t;
    t.db.execute("CREATE TABLE u (age INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (25, 'a')");
    t.db.execute("INSERT INTO u VALUES (25, 'b')");
    t.db.execute("INSERT INTO u VALUES (30, 'c')");
    t.db.execute("INSERT INTO u VALUES (30, 'd')");
    t.db.execute("INSERT INTO u VALUES (40, 'e')");

    auto r = t.db.execute(
        "SELECT age, COUNT(*) FROM u GROUP BY age HAVING age > 25");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Database - GROUP BY avec ORDER BY") {
    TempDb t;
    t.db.execute("CREATE TABLE u (age INTEGER)");
    t.db.execute("INSERT INTO u VALUES (30)");
    t.db.execute("INSERT INTO u VALUES (25)");
    t.db.execute("INSERT INTO u VALUES (30)");
    t.db.execute("INSERT INTO u VALUES (40)");

    auto r = t.db.execute("SELECT age, COUNT(*) FROM u GROUP BY age ORDER BY age");
    REQUIRE(r.rows.size() == 3);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 25);
    REQUIRE(std::get<std::int64_t>(r.rows[1].values[0]) == 30);
    REQUIRE(std::get<std::int64_t>(r.rows[2].values[0]) == 40);
}

TEST_CASE("Database - GROUP BY colonne doit etre dans le groupe") {
    TempDb t;
    t.db.execute("CREATE TABLE u (age INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (25, 'a')");

    REQUIRE_THROWS_AS(
        t.db.execute("SELECT age, name, COUNT(*) FROM u GROUP BY age"),
        std::runtime_error);
}

TEST_CASE("Database - GROUP BY avec NULL") {
    TempDb t;
    t.db.execute("CREATE TABLE u (cat INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1)");
    t.db.execute("INSERT INTO u VALUES (NULL)");
    t.db.execute("INSERT INTO u VALUES (1)");
    t.db.execute("INSERT INTO u VALUES (NULL)");

    auto r = t.db.execute("SELECT cat, COUNT(*) FROM u GROUP BY cat");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Database - GROUP BY sur colonne inexistante echoue") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1)");

    REQUIRE_THROWS_AS(
        t.db.execute("SELECT nexistepas, COUNT(*) FROM u GROUP BY nexistepas"),
        std::runtime_error);
}

TEST_CASE("Database - GROUP BY avec LIMIT") {
    TempDb t;
    t.db.execute("CREATE TABLE u (v INTEGER)");
    for (int i = 0; i < 10; ++i)
        t.db.execute("INSERT INTO u VALUES (" + std::to_string(i) + ")");

    auto r = t.db.execute("SELECT v, COUNT(*) FROM u GROUP BY v LIMIT 3");
    REQUIRE(r.rows.size() == 3);
}

TEST_CASE("Database - GROUP BY minuscule") {
    TempDb t;
    t.db.execute("CREATE TABLE u (age INTEGER)");
    t.db.execute("INSERT INTO u VALUES (25)");
    t.db.execute("INSERT INTO u VALUES (25)");

    auto r = t.db.execute("select age, count(*) from u group by age");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[1]) == 2);
}

TEST_CASE("Database - persistance") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_persist_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        Database db(dir);
        db.execute("CREATE TABLE u (id INTEGER)");
        db.execute("INSERT INTO u VALUES (42)");
    }
    {
        Database db(dir);
        auto sel = db.execute("SELECT * FROM u");
        REQUIRE(sel.rows.size() == 1);
    }
    std::error_code ec; std::filesystem::remove_all(dir, ec);
}

TEST_CASE("PreparedStatement - INSERT simple") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?, ?)");
    REQUIRE(stmt.parameter_count() == 2);

    stmt.bind_int(1, 1);
    stmt.bind_string(2, "Alice");
    stmt.execute(t.db);

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(sel.rows[0].values[0]) == 1);
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "Alice");
}

TEST_CASE("PreparedStatement - reutilisation") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?, ?)");

    stmt.bind_int(1, 1);
    stmt.bind_string(2, "Alice");
    stmt.execute(t.db);

    stmt.reset();

    stmt.bind_int(1, 2);
    stmt.bind_string(2, "Bob");
    stmt.execute(t.db);

    auto sel = t.db.execute("SELECT * FROM u ORDER BY id");
    REQUIRE(sel.rows.size() == 2);
    REQUIRE(std::get<std::string>(sel.rows[1].values[1]) == "Bob");
}

TEST_CASE("PreparedStatement - injection SQL impossible") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(50))");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?, ?)");
    stmt.bind_int(1, 1);
    stmt.bind_string(2, "O'Brien'; DROP TABLE u; --");
    stmt.execute(t.db);

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "O'Brien'; DROP TABLE u; --");
}

TEST_CASE("PreparedStatement - SELECT avec WHERE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("INSERT INTO u VALUES (2, 'Bob')");

    auto stmt = t.db.prepare("SELECT * FROM u WHERE id = ?");
    stmt.bind_int(1, 2);
    auto r = stmt.execute(t.db);
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Bob");
}

TEST_CASE("PreparedStatement - SELECT par nom") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("INSERT INTO u VALUES (2, 'Bob')");

    auto stmt = t.db.prepare("SELECT * FROM u WHERE name = ?");
    stmt.bind_string(1, "Alice");
    auto r = stmt.execute(t.db);
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 1);
}

TEST_CASE("PreparedStatement - UPDATE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("INSERT INTO u VALUES (2, 'Bob')");

    auto stmt = t.db.prepare("UPDATE u SET name = ? WHERE id = ?");
    stmt.bind_string(1, "Alicia");
    stmt.bind_int(2, 1);
    stmt.execute(t.db);

    auto sel = t.db.execute("SELECT * FROM u ORDER BY id");
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "Alicia");
    REQUIRE(std::get<std::string>(sel.rows[1].values[1]) == "Bob");
}

TEST_CASE("PreparedStatement - DELETE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1)");
    t.db.execute("INSERT INTO u VALUES (2)");
    t.db.execute("INSERT INTO u VALUES (3)");

    auto stmt = t.db.prepare("DELETE FROM u WHERE id = ?");
    stmt.bind_int(1, 2);
    stmt.execute(t.db);

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 2);
}

TEST_CASE("PreparedStatement - bind NULL") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50))");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?, ?)");
    stmt.bind_int(1, 1);
    stmt.bind_null(2);
    stmt.execute(t.db);

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(is_null(sel.rows[0].values[1]));
}

TEST_CASE("PreparedStatement - bind DOUBLE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (price DOUBLE)");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?)");
    stmt.bind_double(1, 19.99);
    stmt.execute(t.db);

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(std::get<double>(sel.rows[0].values[0]) == 19.99);
}

TEST_CASE("PreparedStatement - bind BOOLEAN") {
    TempDb t;
    t.db.execute("CREATE TABLE u (active BOOLEAN)");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?)");
    stmt.bind_bool(1, true);
    stmt.execute(t.db);

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(std::get<bool>(sel.rows[0].values[0]) == true);
}

TEST_CASE("PreparedStatement - erreur param non lie") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?, ?)");
    stmt.bind_int(1, 1);

    REQUIRE_THROWS_AS(stmt.execute(t.db), std::runtime_error);
}

TEST_CASE("PreparedStatement - erreur index hors plage") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?)");
    REQUIRE_THROWS_AS(stmt.bind_int(2, 42), std::runtime_error);
    REQUIRE_THROWS_AS(stmt.bind_int(0, 42), std::runtime_error);
}

TEST_CASE("PreparedStatement - execute_with") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    auto stmt = t.db.prepare("INSERT INTO u VALUES (?, ?)");
    stmt.execute_with(t.db, { std::int64_t{1}, std::string{"Alice"} });
    stmt.execute_with(t.db, { std::int64_t{2}, std::string{"Bob"} });

    auto sel = t.db.execute("SELECT * FROM u ORDER BY id");
    REQUIRE(sel.rows.size() == 2);
}

TEST_CASE("PreparedStatement - parameter_count correct") {
    TempDb t;
    t.db.execute("CREATE TABLE u (a INTEGER, b INTEGER, c INTEGER)");

    auto stmt0 = t.db.prepare("SELECT * FROM u");
    REQUIRE(stmt0.parameter_count() == 0);

    auto stmt3 = t.db.prepare("INSERT INTO u VALUES (?, ?, ?)");
    REQUIRE(stmt3.parameter_count() == 3);
}

TEST_CASE("SQL - PREPARE / EXECUTE / DEALLOCATE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    auto r1 = t.db.execute("PREPARE add_user AS INSERT INTO u VALUES (?, ?)");
    REQUIRE(r1.message.find("add_user") != std::string::npos);

    auto r2 = t.db.execute("EXECUTE add_user USING (1, 'Alice')");
    REQUIRE(r2.message.find("1 ligne") != std::string::npos);

    auto r3 = t.db.execute("EXECUTE add_user USING (2, 'Bob')");
    REQUIRE(r3.message.find("1 ligne") != std::string::npos);

    auto sel = t.db.execute("SELECT * FROM u ORDER BY id");
    REQUIRE(sel.rows.size() == 2);
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "Alice");
    REQUIRE(std::get<std::string>(sel.rows[1].values[1]) == "Bob");

    auto r4 = t.db.execute("DEALLOCATE add_user");
    REQUIRE(r4.message.find("add_user") != std::string::npos);

    REQUIRE_THROWS_AS(t.db.execute("EXECUTE add_user USING (3, 'Charlie')"),
                      std::runtime_error);
}

TEST_CASE("SQL - PREPARE / EXECUTE avec SELECT") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("INSERT INTO u VALUES (2, 'Bob')");

    t.db.execute("PREPARE find_user AS SELECT * FROM u WHERE id = ?");

    auto r1 = t.db.execute("EXECUTE find_user USING (1)");
    REQUIRE(r1.rows.size() == 1);
    REQUIRE(std::get<std::string>(r1.rows[0].values[1]) == "Alice");

    auto r2 = t.db.execute("EXECUTE find_user USING (2)");
    REQUIRE(r2.rows.size() == 1);
    REQUIRE(std::get<std::string>(r2.rows[0].values[1]) == "Bob");

    t.db.execute("DEALLOCATE find_user");
}

TEST_CASE("SQL - PREPARE / EXECUTE avec UPDATE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");

    t.db.execute("PREPARE rename AS UPDATE u SET name = ? WHERE id = ?");
    t.db.execute("EXECUTE rename USING ('Alicia', 1)");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "Alicia");

    t.db.execute("DEALLOCATE rename");
}

TEST_CASE("SQL - PREPARE / EXECUTE avec DELETE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1)");
    t.db.execute("INSERT INTO u VALUES (2)");
    t.db.execute("INSERT INTO u VALUES (3)");

    t.db.execute("PREPARE del AS DELETE FROM u WHERE id = ?");
    t.db.execute("EXECUTE del USING (2)");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 2);

    t.db.execute("DEALLOCATE del");
}

TEST_CASE("SQL - PREPARE sans parametre") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1)");

    auto r1 = t.db.execute("PREPARE count_all AS SELECT COUNT(*) FROM u");
    REQUIRE(r1.message.find("0 paramètre") != std::string::npos);

    auto r2 = t.db.execute("EXECUTE count_all");
    REQUIRE(std::get<std::int64_t>(r2.rows[0].values[0]) == 1);

    t.db.execute("DEALLOCATE count_all");
}

TEST_CASE("SQL - PREPARE avec NULL") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50))");

    t.db.execute("PREPARE ins AS INSERT INTO u VALUES (?, ?)");
    t.db.execute("EXECUTE ins USING (1, NULL)");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(is_null(sel.rows[0].values[1]));

    t.db.execute("DEALLOCATE ins");
}

TEST_CASE("SQL - PREPARE erreur doublon") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");

    t.db.execute("PREPARE dup AS INSERT INTO u VALUES (?)");

    REQUIRE_THROWS_AS(t.db.execute("PREPARE dup AS INSERT INTO u VALUES (?)"),
                      std::runtime_error);

    t.db.execute("DEALLOCATE dup");
}

TEST_CASE("SQL - EXECUTE statement inconnu") {
    TempDb t;
    REQUIRE_THROWS_AS(t.db.execute("EXECUTE nexistepas USING (1)"),
                      std::runtime_error);
}

TEST_CASE("SQL - EXECUTE mauvais nombre de parametres") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("PREPARE ins AS INSERT INTO u VALUES (?, ?)");

    REQUIRE_THROWS_AS(t.db.execute("EXECUTE ins USING (1)"),
                      std::runtime_error);
    REQUIRE_THROWS_AS(t.db.execute("EXECUTE ins USING (1, 'x', 2)"),
                      std::runtime_error);

    t.db.execute("DEALLOCATE ins");
}

TEST_CASE("SQL - DEALLOCATE statement inconnu") {
    TempDb t;
    REQUIRE_THROWS_AS(t.db.execute("DEALLOCATE nexistepas"),
                      std::runtime_error);
}

TEST_CASE("SQL - PREPARE injection SQL impossible") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(50))");
    t.db.execute("PREPARE ins AS INSERT INTO u VALUES (?, ?)");

    t.db.execute("EXECUTE ins USING (1, 'X\\'; DROP TABLE u; --')");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "X'; DROP TABLE u; --");

    t.db.execute("DEALLOCATE ins");
}

TEST_CASE("Transaction - BEGIN seul") {
    TempDb t;
    REQUIRE_FALSE(t.db.in_transaction());

    auto r = t.db.execute("BEGIN");
    REQUIRE(r.message.find("démarrée") != std::string::npos);
    REQUIRE(t.db.in_transaction());
}

TEST_CASE("Transaction - BEGIN puis COMMIT") {
    TempDb t;
    t.db.execute("BEGIN");
    REQUIRE(t.db.in_transaction());

    auto r = t.db.execute("COMMIT");
    REQUIRE(r.message.find("validée") != std::string::npos);
    REQUIRE_FALSE(t.db.in_transaction());
}

TEST_CASE("Transaction - BEGIN puis ROLLBACK") {
    TempDb t;
    t.db.execute("BEGIN");
    REQUIRE(t.db.in_transaction());

    auto r = t.db.execute("ROLLBACK");
    REQUIRE(r.message.find("annulée") != std::string::npos);
    REQUIRE_FALSE(t.db.in_transaction());
}

TEST_CASE("Transaction - COMMIT sans BEGIN echoue") {
    TempDb t;
    REQUIRE_THROWS_AS(t.db.execute("COMMIT"), std::runtime_error);
}

TEST_CASE("Transaction - ROLLBACK sans BEGIN echoue") {
    TempDb t;
    REQUIRE_THROWS_AS(t.db.execute("ROLLBACK"), std::runtime_error);
}

TEST_CASE("Transaction - BEGIN imbrique echoue") {
    TempDb t;
    t.db.execute("BEGIN");
    REQUIRE_THROWS_AS(t.db.execute("BEGIN"), std::runtime_error);
    t.db.execute("COMMIT");
}

TEST_CASE("Transaction - API C++ begin/commit") {
    TempDb t;
    REQUIRE_FALSE(t.db.in_transaction());

    t.db.begin();
    REQUIRE(t.db.in_transaction());

    t.db.commit();
    REQUIRE_FALSE(t.db.in_transaction());
}

TEST_CASE("Transaction - API C++ begin/rollback") {
    TempDb t;
    t.db.begin();
    REQUIRE(t.db.in_transaction());

    t.db.rollback();
    REQUIRE_FALSE(t.db.in_transaction());
}

TEST_CASE("Transaction - API C++ commit sans begin echoue") {
    TempDb t;
    REQUIRE_THROWS_AS(t.db.commit(), std::runtime_error);
}

TEST_CASE("Transaction - API C++ rollback sans begin echoue") {
    TempDb t;
    REQUIRE_THROWS_AS(t.db.rollback(), std::runtime_error);
}

TEST_CASE("Transaction - BEGIN/COMMIT avec minuscules") {
    TempDb t;
    t.db.execute("begin");
    REQUIRE(t.db.in_transaction());
    t.db.execute("commit");
    REQUIRE_FALSE(t.db.in_transaction());
}

TEST_CASE("Transaction - BEGIN TRANSACTION") {
    TempDb t;
    t.db.execute("BEGIN TRANSACTION");
    REQUIRE(t.db.in_transaction());
    t.db.execute("COMMIT TRANSACTION");
    REQUIRE_FALSE(t.db.in_transaction());
}

TEST_CASE("Transaction - ROLLBACK annule INSERT") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("INSERT INTO u VALUES (2, 'Bob')");
    t.db.execute("ROLLBACK");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.empty());
}

TEST_CASE("Transaction - COMMIT conserve INSERT") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("COMMIT");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "Alice");
}

TEST_CASE("Transaction - ROLLBACK annule UPDATE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");

    t.db.execute("BEGIN");
    t.db.execute("UPDATE u SET name = 'Alicia' WHERE id = 1");
    t.db.execute("ROLLBACK");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "Alice");
}

TEST_CASE("Transaction - COMMIT conserve UPDATE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");

    t.db.execute("BEGIN");
    t.db.execute("UPDATE u SET name = 'Alicia' WHERE id = 1");
    t.db.execute("COMMIT");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "Alicia");
}

TEST_CASE("Transaction - ROLLBACK annule DELETE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");
    t.db.execute("INSERT INTO u VALUES (1)");
    t.db.execute("INSERT INTO u VALUES (2)");
    t.db.execute("INSERT INTO u VALUES (3)");

    t.db.execute("BEGIN");
    t.db.execute("DELETE FROM u WHERE id = 2");
    t.db.execute("ROLLBACK");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 3);
}

TEST_CASE("Transaction - ROLLBACK multi-operations") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (2, 'Bob')");
    t.db.execute("UPDATE u SET name = 'Alicia' WHERE id = 1");
    t.db.execute("DELETE FROM u WHERE id = 1");
    t.db.execute("ROLLBACK");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(sel.rows[0].values[0]) == 1);
    REQUIRE(std::get<std::string>(sel.rows[0].values[1]) == "Alice");
}

TEST_CASE("Transaction - COMMIT apres ROLLBACK repart propre") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (1)");
    t.db.execute("ROLLBACK");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (2)");
    t.db.execute("COMMIT");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(sel.rows[0].values[0]) == 2);
}

TEST_CASE("Transaction - SELECT pendant transaction voit les modifs") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 1);

    t.db.execute("ROLLBACK");
}

TEST_CASE("Transaction - ROLLBACK avec tables multiples") {
    TempDb t;
    t.db.execute("CREATE TABLE a (id INTEGER)");
    t.db.execute("CREATE TABLE b (id INTEGER)");
    t.db.execute("INSERT INTO a VALUES (1)");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO a VALUES (2)");
    t.db.execute("INSERT INTO b VALUES (10)");
    t.db.execute("ROLLBACK");

    auto sel_a = t.db.execute("SELECT * FROM a");
    REQUIRE(sel_a.rows.size() == 1);

    auto sel_b = t.db.execute("SELECT * FROM b");
    REQUIRE(sel_b.rows.empty());
}

TEST_CASE("Transaction - plusieurs COMMIT successifs") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (1)");
    t.db.execute("COMMIT");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (2)");
    t.db.execute("COMMIT");

    auto sel = t.db.execute("SELECT * FROM u");
    REQUIRE(sel.rows.size() == 2);
}

TEST_CASE("Transaction - persistance apres ROLLBACK") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_tx_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        Database db(dir);
        db.execute("CREATE TABLE u (id INTEGER)");
        db.execute("INSERT INTO u VALUES (1)");

        db.execute("BEGIN");
        db.execute("INSERT INTO u VALUES (2)");
        db.execute("ROLLBACK");
    }
    {
        Database db(dir);
        auto sel = db.execute("SELECT * FROM u");
        REQUIRE(sel.rows.size() == 1);
        REQUIRE(std::get<std::int64_t>(sel.rows[0].values[0]) == 1);
    }
    std::error_code ec; std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Case - CREATE puis SELECT avec casse differente") {
    TempDb t;
    t.db.execute("CREATE TABLE Users (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO USERS VALUES (1, 'Alice')");

    auto r = t.db.execute("SELECT * FROM users");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Alice");
}

TEST_CASE("Case - CREATE TABLE conflit avec casse differente") {
    TempDb t;
    t.db.execute("CREATE TABLE Users (id INTEGER)");
    REQUIRE_THROWS_AS(t.db.execute("CREATE TABLE USERS (id INTEGER)"),
                      std::runtime_error);
}

TEST_CASE("Case - colonnes insensibles a la casse") {
    TempDb t;
    t.db.execute("CREATE TABLE users (id INTEGER, Name VARCHAR(20))");
    t.db.execute("INSERT INTO users VALUES (1, 'Alice')");

    auto r = t.db.execute("SELECT ID, NAME FROM users");
    REQUIRE(r.columns.size() == 2);
    REQUIRE(r.columns[0] == "id");
    REQUIRE(r.columns[1] == "name");
}

TEST_CASE("Case - WHERE insensible a la casse") {
    TempDb t;
    t.db.execute("CREATE TABLE users (id INTEGER, age INTEGER)");
    t.db.execute("INSERT INTO users VALUES (1, 30)");
    t.db.execute("INSERT INTO users VALUES (2, 15)");

    auto r = t.db.execute("SELECT * FROM Users WHERE Age > 18");
    REQUIRE(r.rows.size() == 1);
}

TEST_CASE("Case - DROP insensible a la casse") {
    TempDb t;
    t.db.execute("CREATE TABLE Users (id INTEGER)");
    t.db.execute("DROP TABLE users");
    REQUIRE_FALSE(t.db.has_table("users"));
}

TEST_CASE("Case - UPDATE insensible a la casse") {
    TempDb t;
    t.db.execute("CREATE TABLE users (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO users VALUES (1, 'Alice')");

    t.db.execute("UPDATE Users SET Name = 'Alicia' WHERE ID = 1");
    auto r = t.db.execute("SELECT * FROM users");
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Alicia");
}

TEST_CASE("Case - DELETE insensible a la casse") {
    TempDb t;
    t.db.execute("CREATE TABLE users (id INTEGER)");
    t.db.execute("INSERT INTO users VALUES (1)");
    t.db.execute("INSERT INTO users VALUES (2)");

    t.db.execute("DELETE FROM USERS WHERE ID = 1");
    auto r = t.db.execute("SELECT * FROM users");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 2);
}

TEST_CASE("Case - ALTER TABLE insensible a la casse") {
    TempDb t;
    t.db.execute("CREATE TABLE users (id INTEGER)");
    t.db.execute("ALTER TABLE USERS ADD COLUMN Email VARCHAR(50)");

    auto schemas = t.db.all_schemas();
    REQUIRE(schemas[0].columns.size() == 2);
    REQUIRE(schemas[0].columns[1].name == "email");
}

TEST_CASE("Case - PREPARE + EXECUTE insensible a la casse") {
    TempDb t;
    t.db.execute("CREATE TABLE users (id INTEGER, name VARCHAR(20))");

    t.db.execute("PREPARE add_user AS INSERT INTO Users VALUES (?, ?)");
    t.db.execute("EXECUTE ADD_USER USING (1, 'Alice')");

    auto r = t.db.execute("SELECT * FROM users");
    REQUIRE(r.rows.size() == 1);
}

TEST_CASE("Case - chaines restent case-sensitive") {
    TempDb t;
    t.db.execute("CREATE TABLE users (id INTEGER, name VARCHAR(20))");
    t.db.execute("INSERT INTO users VALUES (1, 'Alice')");

    auto r = t.db.execute("SELECT * FROM users WHERE name = 'alice'");
    REQUIRE(r.rows.empty());

    auto r2 = t.db.execute("SELECT * FROM users WHERE name = 'Alice'");
    REQUIRE(r2.rows.size() == 1);
}

TEST_CASE("TxAborted - une erreur passe la transaction en Aborted") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    REQUIRE(t.db.tx_state() == Database::TxState::Active);

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1)"),
                      std::runtime_error);

    REQUIRE(t.db.tx_state() == Database::TxState::Aborted);

    t.db.execute("ROLLBACK");
}

TEST_CASE("TxAborted - INSERT interdit en Aborted") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1)"),
                      std::runtime_error);

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (2, 'Bob')"),
                      std::runtime_error);

    t.db.execute("ROLLBACK");
}

TEST_CASE("TxAborted - SELECT interdit en Aborted") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1)"),
                      std::runtime_error);

    REQUIRE_THROWS_AS(t.db.execute("SELECT * FROM u"),
                      std::runtime_error);

    t.db.execute("ROLLBACK");
}

TEST_CASE("TxAborted - UPDATE interdit en Aborted") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1)"),
                      std::runtime_error);

    REQUIRE_THROWS_AS(t.db.execute("UPDATE u SET name = 'X'"),
                      std::runtime_error);

    t.db.execute("ROLLBACK");
}

TEST_CASE("TxAborted - COMMIT interdit en Aborted") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1)"),
                      std::runtime_error);

    REQUIRE_THROWS_AS(t.db.execute("COMMIT"),
                      std::runtime_error);

    REQUIRE(t.db.tx_state() == Database::TxState::Aborted);

    t.db.execute("ROLLBACK");
}

TEST_CASE("TxAborted - ROLLBACK est autorise") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1)"),
                      std::runtime_error);

    REQUIRE(t.db.tx_state() == Database::TxState::Aborted);

    REQUIRE_NOTHROW(t.db.execute("ROLLBACK"));
    REQUIRE(t.db.tx_state() == Database::TxState::None);
}

TEST_CASE("TxAborted - ROLLBACK annule tout apres erreur") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (2)"),
                      std::runtime_error);

    t.db.execute("ROLLBACK");

    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(r.rows.empty());
}

TEST_CASE("TxAborted - reprise apres ROLLBACK") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (2)"),
                      std::runtime_error);
    t.db.execute("ROLLBACK");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (3, 'Charlie')");
    t.db.execute("COMMIT");

    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Charlie");
}

TEST_CASE("TxAborted - API C++ tx_state") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER)");

    REQUIRE(t.db.tx_state() == Database::TxState::None);

    t.db.begin();
    REQUIRE(t.db.tx_state() == Database::TxState::Active);

    t.db.rollback();
    REQUIRE(t.db.tx_state() == Database::TxState::None);
}

TEST_CASE("TxAborted - plusieurs erreurs successives") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    t.db.execute("BEGIN");
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1)"),
                      std::runtime_error);
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1)"),
                      std::runtime_error);
    REQUIRE_THROWS_AS(t.db.execute("SELECT * FROM u"),
                      std::runtime_error);
    t.db.execute("ROLLBACK");

    REQUIRE(t.db.tx_state() == Database::TxState::None);
}

// ============================================================================
// J5.4-A : contraintes — accès via primary_key / unique_constraints
// ============================================================================

TEST_CASE("Constraints - PRIMARY KEY stockee dans le schema") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY)");

    const auto& s = t.db.schema_of("u");
    REQUIRE(s.columns.size() == 1);
    REQUIRE(s.primary_key.size() == 1);
    REQUIRE(s.primary_key[0] == "id");
    REQUIRE(s.columns[0].is_not_null);   // PK implique NOT NULL
}

TEST_CASE("Constraints - UNIQUE stockee dans le schema") {
    TempDb t;
    t.db.execute("CREATE TABLE u (email VARCHAR(50) UNIQUE)");

    const auto& s = t.db.schema_of("u");
    REQUIRE(s.unique_constraints.size() == 1);
    REQUIRE(s.unique_constraints[0].columns.size() == 1);
    REQUIRE(s.unique_constraints[0].columns[0] == "email");
    REQUIRE(s.primary_key.empty());
    REQUIRE_FALSE(s.columns[0].is_not_null);
}

TEST_CASE("Constraints - NOT NULL stockee dans le schema") {
    TempDb t;
    t.db.execute("CREATE TABLE u (name VARCHAR(20) NOT NULL)");

    const auto& s = t.db.schema_of("u");
    REQUIRE(s.columns[0].is_not_null);
    REQUIRE(s.unique_constraints.empty());
    REQUIRE(s.primary_key.empty());
}

TEST_CASE("Constraints - plusieurs contraintes sur une colonne") {
    TempDb t;
    t.db.execute("CREATE TABLE u (email VARCHAR(50) UNIQUE NOT NULL)");

    const auto& s = t.db.schema_of("u");
    REQUIRE(s.unique_constraints.size() == 1);
    REQUIRE(s.unique_constraints[0].columns[0] == "email");
    REQUIRE(s.columns[0].is_not_null);
}

TEST_CASE("Constraints - persistance apres reouverture") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_constraints_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        Database db(dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, "
                   "email VARCHAR(50) UNIQUE NOT NULL, "
                   "age INTEGER)");
    }
    {
        Database db(dir);
        const auto& s = db.schema_of("u");
        REQUIRE(s.columns.size() == 3);

        REQUIRE(s.primary_key.size() == 1);
        REQUIRE(s.primary_key[0] == "id");
        REQUIRE(s.columns[0].is_not_null);

        REQUIRE(s.unique_constraints.size() == 1);
        REQUIRE(s.unique_constraints[0].columns[0] == "email");
        REQUIRE(s.columns[1].is_not_null);

        REQUIRE_FALSE(s.columns[2].is_not_null);
    }
    std::error_code ec; std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Constraints - sans contraintes = pas de PK/UNIQUE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20))");

    const auto& s = t.db.schema_of("u");
    REQUIRE(s.primary_key.empty());
    REQUIRE(s.unique_constraints.empty());
    for (const auto& col : s.columns) {
        REQUIRE_FALSE(col.is_not_null);
    }
}

TEST_CASE("Constraints - NOT NULL sur INSERT") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20) NOT NULL)");

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1, NULL)"),
                      std::runtime_error);

    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(r.rows.size() == 1);
}

TEST_CASE("Constraints - UNIQUE sur INSERT") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50) UNIQUE)");
    t.db.execute("INSERT INTO u VALUES (1, 'a@x.com')");

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (2, 'a@x.com')"),
                      std::runtime_error);

    t.db.execute("INSERT INTO u VALUES (2, 'b@x.com')");
    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Constraints - UNIQUE permet plusieurs NULL") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50) UNIQUE)");

    t.db.execute("INSERT INTO u VALUES (1, NULL)");
    t.db.execute("INSERT INTO u VALUES (2, NULL)");
    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Constraints - PRIMARY KEY sur INSERT") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR(20))");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1, 'Bob')"),
                      std::runtime_error);

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (NULL, 'Bob')"),
                      std::runtime_error);
}

TEST_CASE("Constraints - NOT NULL sur UPDATE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, name VARCHAR(20) NOT NULL)");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");

    REQUIRE_THROWS_AS(t.db.execute("UPDATE u SET name = NULL WHERE id = 1"),
                      std::runtime_error);

    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Alice");
}

TEST_CASE("Constraints - UNIQUE sur UPDATE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50) UNIQUE)");
    t.db.execute("INSERT INTO u VALUES (1, 'a@x.com')");
    t.db.execute("INSERT INTO u VALUES (2, 'b@x.com')");

    REQUIRE_THROWS_AS(t.db.execute("UPDATE u SET email = 'a@x.com' WHERE id = 2"),
                      std::runtime_error);

    auto r = t.db.execute("SELECT * FROM u ORDER BY id");
    REQUIRE(std::get<std::string>(r.rows[1].values[1]) == "b@x.com");
}

TEST_CASE("Constraints - UNIQUE sur UPDATE sans conflit sur soi-meme") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50) UNIQUE)");
    t.db.execute("INSERT INTO u VALUES (1, 'a@x.com')");

    t.db.execute("UPDATE u SET email = 'a@x.com' WHERE id = 1");
    t.db.execute("UPDATE u SET email = 'b@x.com' WHERE id = 1");

    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "b@x.com");
}

TEST_CASE("Constraints - NOT NULL et UNIQUE combines") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER, email VARCHAR(50) UNIQUE NOT NULL)");

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1, NULL)"),
                      std::runtime_error);

    t.db.execute("INSERT INTO u VALUES (1, 'a@x.com')");

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (2, 'a@x.com')"),
                      std::runtime_error);
}

TEST_CASE("Constraints - plusieurs colonnes UNIQUE") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, "
                 "email VARCHAR(50) UNIQUE, "
                 "username VARCHAR(20) UNIQUE)");

    t.db.execute("INSERT INTO u VALUES (1, 'a@x.com', 'alice')");

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (2, 'a@x.com', 'bob')"),
                      std::runtime_error);

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (3, 'b@x.com', 'alice')"),
                      std::runtime_error);

    t.db.execute("INSERT INTO u VALUES (4, 'b@x.com', 'bob')");
    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("Constraints - persistance et activation apres reouverture") {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("kerneldb_cst_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        Database db(dir);
        db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR(20) NOT NULL)");
        db.execute("INSERT INTO u VALUES (1, 'Alice')");
    }
    {
        Database db(dir);
        REQUIRE_THROWS_AS(db.execute("INSERT INTO u VALUES (2, NULL)"),
                          std::runtime_error);
        REQUIRE_THROWS_AS(db.execute("INSERT INTO u VALUES (1, 'Bob')"),
                          std::runtime_error);

        db.execute("INSERT INTO u VALUES (2, 'Bob')");
        auto r = db.execute("SELECT * FROM u");
        REQUIRE(r.rows.size() == 2);
    }
    std::error_code ec; std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Constraints - rollback restaure l'integrite") {
    TempDb t;
    t.db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR(20) NOT NULL)");

    t.db.execute("BEGIN");
    t.db.execute("INSERT INTO u VALUES (1, 'Alice')");
    t.db.execute("ROLLBACK");

    t.db.execute("INSERT INTO u VALUES (1, 'Bob')");
    auto r = t.db.execute("SELECT * FROM u");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[1]) == "Bob");
}

// ============================================================================
// J5.4-A : PK / UNIQUE composite
// ============================================================================

TEST_CASE("Constraints - PRIMARY KEY composite") {
    TempDb t;
    t.db.execute("CREATE TABLE u (a INTEGER, b INTEGER, "
                 "PRIMARY KEY (a, b))");
    t.db.execute("INSERT INTO u VALUES (1, 2)");

    // Même (a, b) → refusé
    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1, 2)"),
                      std::runtime_error);
    // a identique, b différent → accepté
    REQUIRE_NOTHROW(t.db.execute("INSERT INTO u VALUES (1, 3)"));
    // b identique, a différent → accepté
    REQUIRE_NOTHROW(t.db.execute("INSERT INTO u VALUES (2, 2)"));
}

TEST_CASE("Constraints - UNIQUE composite") {
    TempDb t;
    t.db.execute("CREATE TABLE u (a INTEGER, b INTEGER, "
                 "UNIQUE (a, b))");
    t.db.execute("INSERT INTO u VALUES (1, 2)");

    REQUIRE_THROWS_AS(t.db.execute("INSERT INTO u VALUES (1, 2)"),
                      std::runtime_error);
    REQUIRE_NOTHROW(t.db.execute("INSERT INTO u VALUES (1, 3)"));
    REQUIRE_NOTHROW(t.db.execute("INSERT INTO u VALUES (2, 2)"));
}

TEST_CASE("Constraints - UNIQUE composite avec NULL") {
    TempDb t;
    t.db.execute("CREATE TABLE u (a INTEGER, b INTEGER, "
                 "UNIQUE (a, b))");
    t.db.execute("INSERT INTO u VALUES (NULL, 2)");
    // Oracle : (NULL, 2) ne contraint pas → (NULL, 2) de nouveau accepté
    REQUIRE_NOTHROW(t.db.execute("INSERT INTO u VALUES (NULL, 2)"));
    // (1, NULL) ne contraint pas non plus
    t.db.execute("INSERT INTO u VALUES (1, NULL)");
    REQUIRE_NOTHROW(t.db.execute("INSERT INTO u VALUES (1, NULL)"));
}

// ---------------------------------------------------------------------------
// F01 — prepared statements + transactions
// ---------------------------------------------------------------------------

TEST_CASE("F01 - prepared statement refuse d'ecrire en transaction Aborted") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    t.db.execute("INSERT INTO t VALUES (1)");

    auto stmt = t.db.prepare("INSERT INTO t VALUES (?)");
    t.db.begin();

    try { t.db.execute("INSERT INTO t VALUES (1)"); } catch (...) {}

    REQUIRE(t.db.tx_state() == Database::TxState::Aborted);

    stmt.bind_int(1, 2);
    REQUIRE_THROWS_AS(stmt.execute(t.db), std::runtime_error);

    t.db.rollback();

    auto r = t.db.execute("SELECT * FROM t WHERE id = 2");
    REQUIRE(r.rows.empty());
}

TEST_CASE("F01 - prepared statement respecte le rollback") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");

    auto stmt = t.db.prepare("INSERT INTO t VALUES (?)");
    stmt.bind_int(1, 42);

    t.db.begin();
    stmt.execute(t.db);
    t.db.rollback();

    auto r = t.db.execute("SELECT * FROM t");
    REQUIRE(r.rows.empty());
}

// ---------------------------------------------------------------------------
// F03 — DDL interdit dans une transaction
// ---------------------------------------------------------------------------

TEST_CASE("F03 - CREATE TABLE interdit en transaction") {
    TempDb t;
    t.db.begin();
    REQUIRE_THROWS_AS(
        t.db.execute("CREATE TABLE x (id INTEGER)"),
        std::runtime_error);
    t.db.rollback();
}

TEST_CASE("F03 - DROP TABLE interdit en transaction") {
    TempDb t;
    t.db.execute("CREATE TABLE r (id INTEGER)");
    t.db.begin();
    REQUIRE_THROWS_AS(
        t.db.execute("DROP TABLE r"),
        std::runtime_error);
    t.db.rollback();

    REQUIRE(t.db.has_table("r"));
}

TEST_CASE("F03 - ALTER TABLE interdit en transaction") {
    TempDb t;
    t.db.execute("CREATE TABLE r (id INTEGER)");
    t.db.begin();
    REQUIRE_THROWS_AS(
        t.db.execute("ALTER TABLE r ADD c INTEGER"),
        std::runtime_error);
    t.db.rollback();
}

// ---------------------------------------------------------------------------
// F12 — WHERE colonne < colonne non supporté
// ---------------------------------------------------------------------------

TEST_CASE("F12 - WHERE colonne vs colonne (limitation connue)",
          "[!mayfail]") {
    TempDb t;
    t.db.execute("CREATE TABLE n (x INTEGER, y INTEGER)");
    t.db.execute("INSERT INTO n VALUES (1, 2)");

    auto r = t.db.execute("SELECT * FROM n WHERE x < y");
    REQUIRE(r.rows.size() == 1);
}

// ---------------------------------------------------------------------------
// F05 — DISTINCT/GROUP BY injectifs
// ---------------------------------------------------------------------------

TEST_CASE("F05 - DISTINCT distingue NULL et chaine 'NULL'") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER, s VARCHAR(20))");
    t.db.execute("INSERT INTO t VALUES (1, NULL)");
    t.db.execute("INSERT INTO t VALUES (2, 'NULL')");

    auto r = t.db.execute("SELECT DISTINCT s FROM t");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("F05 - DISTINCT distingue les separateurs") {
    TempDb t;
    t.db.execute("CREATE TABLE t (a VARCHAR(20), b VARCHAR(20))");
    t.db.execute("INSERT INTO t VALUES ('x', 'y')");
    t.db.execute("INSERT INTO t VALUES ('x', 'y')");

    auto r = t.db.execute("SELECT DISTINCT a, b FROM t");
    REQUIRE(r.rows.size() == 1);

    auto r2 = t.db.execute("SELECT DISTINCT a FROM t");
    REQUIRE(r2.rows.size() == 1);
}

TEST_CASE("F05 - GROUP BY distingue NULL et chaine 'NULL'") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER, s VARCHAR(20))");
    t.db.execute("INSERT INTO t VALUES (1, NULL)");
    t.db.execute("INSERT INTO t VALUES (2, 'NULL')");

    auto r = t.db.execute("SELECT s, COUNT(*) FROM t GROUP BY s");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("F05 - DISTINCT distingue int 1 et chaine '1'") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER, v VARCHAR(10))");
    t.db.execute("INSERT INTO t VALUES (1, '1')");
    t.db.execute("INSERT INTO t VALUES (2, '1')");

    t.db.execute("INSERT INTO t VALUES (3, '01')");
    auto r = t.db.execute("SELECT DISTINCT v FROM t");
    REQUIRE(r.rows.size() == 2);
}

// ---------------------------------------------------------------------------
// F06 — NOT IN avec NULL
// ---------------------------------------------------------------------------

TEST_CASE("F06 - NOT IN avec NULL retourne UNKNOWN (0 ligne)") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");
    t.db.execute("INSERT INTO t VALUES (3)");

    auto r = t.db.execute("SELECT * FROM t WHERE id NOT IN (3, NULL)");
    REQUIRE(r.rows.empty());
}

TEST_CASE("F06 - NOT IN sans NULL fonctionne") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");
    t.db.execute("INSERT INTO t VALUES (3)");

    auto r = t.db.execute("SELECT * FROM t WHERE id NOT IN (1, 2)");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 3);
}

TEST_CASE("F06 - IN avec NULL") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");

    auto r = t.db.execute("SELECT * FROM t WHERE id IN (1, NULL)");
    REQUIRE(r.rows.size() == 1);

    auto r2 = t.db.execute("SELECT * FROM t WHERE id IN (2, NULL)");
    REQUIRE(r2.rows.empty());
}

// ---------------------------------------------------------------------------
// F07 — ALTER ADD NOT NULL sur table non vide
// ---------------------------------------------------------------------------

TEST_CASE("F07 - ALTER ADD NOT NULL sur table remplie est refuse") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");

    REQUIRE_THROWS_AS(
        t.db.execute("ALTER TABLE t ADD required INTEGER NOT NULL"),
        std::runtime_error);
}

TEST_CASE("F07 - ALTER ADD nullable sur table remplie est accepte") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");

    REQUIRE_NOTHROW(
        t.db.execute("ALTER TABLE t ADD extra INTEGER"));

    auto r = t.db.execute("SELECT * FROM t");
    REQUIRE(r.rows.size() == 2);
    REQUIRE(is_null(r.rows[0].values[1]));
}

TEST_CASE("F07 - ALTER ADD NOT NULL sur table vide est accepte") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");

    REQUIRE_NOTHROW(
        t.db.execute("ALTER TABLE t ADD required INTEGER NOT NULL"));

    t.db.execute("INSERT INTO t VALUES (1, 42)");
    auto r = t.db.execute("SELECT * FROM t");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[1]) == 42);
}

// ---------------------------------------------------------------------------
// F16 — Colonnes dupliquées
// ---------------------------------------------------------------------------

TEST_CASE("F16 - colonnes dupliquees refusees") {
    TempDb t;
    REQUIRE_THROWS_AS(
        t.db.execute("CREATE TABLE t (id INTEGER, id VARCHAR(20))"),
        std::runtime_error);
}

TEST_CASE("F16 - colonnes distinctes acceptees") {
    TempDb t;
    REQUIRE_NOTHROW(
        t.db.execute("CREATE TABLE t (id INTEGER, name VARCHAR(20))"));
}

// ---------------------------------------------------------------------------
// F17 — Plusieurs PRIMARY KEY
// ---------------------------------------------------------------------------

TEST_CASE("F17 - plusieurs PRIMARY KEY refusees") {
    TempDb t;
    REQUIRE_THROWS_AS(
        t.db.execute("CREATE TABLE t (a INTEGER PRIMARY KEY, b INTEGER PRIMARY KEY)"),
        std::runtime_error);
}

TEST_CASE("F17 - une seule PRIMARY KEY acceptee") {
    TempDb t;
    REQUIRE_NOTHROW(
        t.db.execute("CREATE TABLE t (a INTEGER PRIMARY KEY, b INTEGER)"));
}

// ---------------------------------------------------------------------------
// F14 — LIMIT 0
// ---------------------------------------------------------------------------

TEST_CASE("F14 - LIMIT 0 sur SELECT simple") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");

    auto r = t.db.execute("SELECT * FROM t LIMIT 0");
    REQUIRE(r.rows.empty());
}

TEST_CASE("F14 - LIMIT 0 avec COUNT retourne 0 ligne") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");

    auto r = t.db.execute("SELECT COUNT(*) FROM t LIMIT 0");
    REQUIRE(r.rows.empty());
}

TEST_CASE("F14 - LIMIT 0 avec GROUP BY retourne 0 ligne") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER, k INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1, 10)");
    t.db.execute("INSERT INTO t VALUES (2, 20)");

    auto r = t.db.execute("SELECT k, COUNT(*) FROM t GROUP BY k LIMIT 0");
    REQUIRE(r.rows.empty());
}

TEST_CASE("F14 - LIMIT 1 fonctionne toujours") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");
    t.db.execute("INSERT INTO t VALUES (3)");

    auto r = t.db.execute("SELECT * FROM t LIMIT 1");
    REQUIRE(r.rows.size() == 1);
}

TEST_CASE("F14 - LIMIT 1 avec COUNT retourne 1 ligne") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");

    auto r = t.db.execute("SELECT COUNT(*) FROM t LIMIT 1");
    REQUIRE(r.rows.size() == 1);
}

// ---------------------------------------------------------------------------
// F13 — MIN/MAX/SUM préservent int64
// ---------------------------------------------------------------------------

TEST_CASE("F13 - MIN preserve les grands entiers") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (9007199254740993)");

    auto r = t.db.execute("SELECT MIN(id) FROM t");
    REQUIRE(std::holds_alternative<std::int64_t>(r.rows[0].values[0]));
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 9007199254740993LL);
}

TEST_CASE("F13 - MAX preserve les grands entiers") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (9007199254740993)");

    auto r = t.db.execute("SELECT MAX(id) FROM t");
    REQUIRE(std::holds_alternative<std::int64_t>(r.rows[0].values[0]));
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 9007199254740993LL);
}

TEST_CASE("F13 - SUM preserve les grands entiers") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (9007199254740993)");

    auto r = t.db.execute("SELECT SUM(id) FROM t");
    REQUIRE(std::holds_alternative<std::int64_t>(r.rows[0].values[0]));
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 9007199254740993LL);
}

TEST_CASE("F13 - MIN/MAX sur doubles restent en double") {
    TempDb t;
    t.db.execute("CREATE TABLE t (d DOUBLE)");
    t.db.execute("INSERT INTO t VALUES (1.5)");
    t.db.execute("INSERT INTO t VALUES (2.5)");

    auto r = t.db.execute("SELECT MIN(d), MAX(d) FROM t");
    REQUIRE(std::get<double>(r.rows[0].values[0]) == 1.5);
    REQUIRE(std::get<double>(r.rows[0].values[1]) == 2.5);
}

TEST_CASE("F13 - AVG reste en double") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");

    auto r = t.db.execute("SELECT AVG(id) FROM t");
    REQUIRE(std::holds_alternative<double>(r.rows[0].values[0]));
    REQUIRE(std::get<double>(r.rows[0].values[0]) == 1.5);
}

// ---------------------------------------------------------------------------
// F18 — SELECT colonne + COUNT sans GROUP BY
// ---------------------------------------------------------------------------

TEST_CASE("F18 - SELECT colonne + COUNT sans GROUP BY est refuse") {
    TempDb t;
    t.db.execute("CREATE TABLE t (k INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");

    REQUIRE_THROWS_AS(
        t.db.execute("SELECT k, COUNT(*) FROM t"),
        std::runtime_error);
}

TEST_CASE("F18 - SELECT COUNT(*) seul est accepte") {
    TempDb t;
    t.db.execute("CREATE TABLE t (k INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");

    REQUIRE_NOTHROW(
        t.db.execute("SELECT COUNT(*) FROM t"));
}

TEST_CASE("F18 - SELECT SUM(k) seul est accepte") {
    TempDb t;
    t.db.execute("CREATE TABLE t (k INTEGER)");
    t.db.execute("INSERT INTO t VALUES (10)");
    t.db.execute("INSERT INTO t VALUES (20)");

    auto r = t.db.execute("SELECT SUM(k) FROM t");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 30);
}

TEST_CASE("F18 - SELECT MIN(k), MAX(k) sans GROUP BY est accepte") {
    TempDb t;
    t.db.execute("CREATE TABLE t (k INTEGER)");
    t.db.execute("INSERT INTO t VALUES (5)");
    t.db.execute("INSERT INTO t VALUES (15)");

    auto r = t.db.execute("SELECT MIN(k), MAX(k) FROM t");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 5);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[1]) == 15);
}

// ---------------------------------------------------------------------------
// R01 — ORDER BY dans GROUP BY
// ---------------------------------------------------------------------------

TEST_CASE("R01 - ORDER BY count avec 2 colonnes de groupe") {
    TempDb t;
    t.db.execute("CREATE TABLE t (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1, 20)");
    t.db.execute("INSERT INTO t VALUES (2, 10)");

    auto r = t.db.execute("SELECT COUNT(*) FROM t GROUP BY a, b ORDER BY count");
    REQUIRE(r.rows.size() == 2);
}

TEST_CASE("R01 - ORDER BY b sur projection b, a") {
    TempDb t;
    t.db.execute("CREATE TABLE t (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1, 20)");
    t.db.execute("INSERT INTO t VALUES (2, 10)");

    auto r = t.db.execute("SELECT b, a FROM t GROUP BY a, b ORDER BY b");
    REQUIRE(r.rows.size() == 2);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 10);
    REQUIRE(std::get<std::int64_t>(r.rows[1].values[0]) == 20);
}

TEST_CASE("R01 - ORDER BY a sur projection b, a") {
    TempDb t;
    t.db.execute("CREATE TABLE t (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO t VALUES (2, 20)");
    t.db.execute("INSERT INTO t VALUES (1, 10)");

    auto r = t.db.execute("SELECT b, a FROM t GROUP BY a, b ORDER BY a");
    REQUIRE(r.rows.size() == 2);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[1]) == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[1].values[1]) == 2);
}

// ---------------------------------------------------------------------------
// R04 — Dépassement signé
// ---------------------------------------------------------------------------

TEST_CASE("R04 - SUM detecte l'overflow int64") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (9223372036854775807)");
    t.db.execute("INSERT INTO t VALUES (1)");

    REQUIRE_THROWS_AS(
        t.db.execute("SELECT SUM(id) FROM t"),
        std::runtime_error);
}

TEST_CASE("R04 - MIN/MAX sur INT64_MAX sans overflow") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (9223372036854775807)");
    t.db.execute("INSERT INTO t VALUES (1)");

    auto r = t.db.execute("SELECT MIN(id), MAX(id) FROM t");
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[1]) == 9223372036854775807LL);
}

TEST_CASE("R04 - SUM normal fonctionne") {
    TempDb t;
    t.db.execute("CREATE TABLE t (id INTEGER)");
    t.db.execute("INSERT INTO t VALUES (10)");
    t.db.execute("INSERT INTO t VALUES (20)");

    auto r = t.db.execute("SELECT SUM(id) FROM t");
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 30);
}

TEST_CASE("R04 - SUM sur DOUBLE ne detecte pas d'overflow int64") {
    TempDb t;
    t.db.execute("CREATE TABLE t (d DOUBLE)");
    t.db.execute("INSERT INTO t VALUES (100.0)");
    t.db.execute("INSERT INTO t VALUES (200.0)");

    auto r = t.db.execute("SELECT SUM(d) FROM t");
    REQUIRE(std::holds_alternative<double>(r.rows[0].values[0]));
    REQUIRE(std::get<double>(r.rows[0].values[0]) == 300.0);
}

// ---------------------------------------------------------------------------
// R06 — ALTER PK
// ---------------------------------------------------------------------------

TEST_CASE("R06 - ALTER refuse une 2e PRIMARY KEY") {
    TempDb t;
    t.db.execute("CREATE TABLE t (x INTEGER PRIMARY KEY)");

    REQUIRE_THROWS_AS(
        t.db.execute("ALTER TABLE t ADD y INTEGER PRIMARY KEY"),
        std::runtime_error);
}

TEST_CASE("R06 - ALTER accepte colonne NON-PK sur table avec PK") {
    TempDb t;
    t.db.execute("CREATE TABLE t (x INTEGER PRIMARY KEY)");

    REQUIRE_NOTHROW(
        t.db.execute("ALTER TABLE t ADD y INTEGER"));
}

// ---------------------------------------------------------------------------
// R05-bis — Notation scientifique
// ---------------------------------------------------------------------------

TEST_CASE("R05-bis - INSERT avec 1e308") {
    TempDb t;
    t.db.execute("CREATE TABLE t (d DOUBLE)");
    t.db.execute("INSERT INTO t VALUES (1e308)");

    auto r = t.db.execute("SELECT d FROM t");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<double>(r.rows[0].values[0]) == 1e308);
}

TEST_CASE("R05-bis - INSERT avec 1.5e-3") {
    TempDb t;
    t.db.execute("CREATE TABLE t (d DOUBLE)");
    t.db.execute("INSERT INTO t VALUES (1.5e-3)");

    auto r = t.db.execute("SELECT d FROM t");
    REQUIRE(std::get<double>(r.rows[0].values[0]) == 1.5e-3);
}

// ---------------------------------------------------------------------------
// A02 — UNIQUE sur valeurs normalisées
// ---------------------------------------------------------------------------

TEST_CASE("A02 - UNIQUE detecte les doublons apres conversion int64->double") {
    TempDb t;
    t.db.execute("CREATE TABLE d (x DOUBLE UNIQUE)");
    t.db.execute("INSERT INTO d VALUES (9007199254740992)");

    REQUIRE_THROWS_AS(
        t.db.execute("INSERT INTO d VALUES (9007199254740993)"),
        std::runtime_error);
}

TEST_CASE("A02 - UNIQUE sur DOUBLE normal fonctionne") {
    TempDb t;
    t.db.execute("CREATE TABLE d (x DOUBLE UNIQUE)");
    t.db.execute("INSERT INTO d VALUES (1.5)");

    REQUIRE_THROWS_AS(
        t.db.execute("INSERT INTO d VALUES (1.5)"),
        std::runtime_error);
}

TEST_CASE("A02 - UNIQUE sur DOUBLE accepte valeurs distinctes") {
    TempDb t;
    t.db.execute("CREATE TABLE d (x DOUBLE UNIQUE)");
    t.db.execute("INSERT INTO d VALUES (1.5)");
    REQUIRE_NOTHROW(
        t.db.execute("INSERT INTO d VALUES (2.5)"));
}

TEST_CASE("A02 - UPDATE detecte les doublons apres conversion int64->double") {
    TempDb t;
    t.db.execute("CREATE TABLE d (id INTEGER, x DOUBLE UNIQUE)");
    t.db.execute("INSERT INTO d VALUES (1, 9007199254740992)");
    t.db.execute("INSERT INTO d VALUES (2, 1.5)");

    REQUIRE_THROWS_AS(
        t.db.execute("UPDATE d SET x = 9007199254740993 WHERE id = 2"),
        std::runtime_error);
}

// ---------------------------------------------------------------------------
// A01 — Comparaison int64/double avec fraction négative
// ---------------------------------------------------------------------------

TEST_CASE("A01 - comparaison avec fraction negative") {
    TempDb t;
    t.db.execute("CREATE TABLE n (x INTEGER)");
    t.db.execute("INSERT INTO n VALUES (-1)");

    REQUIRE(t.db.execute("SELECT * FROM n WHERE x = -1.5").rows.empty());
    REQUIRE(t.db.execute("SELECT * FROM n WHERE x > -1.5").rows.size() == 1);
    REQUIRE(t.db.execute("SELECT * FROM n WHERE x < -1.5").rows.empty());
    REQUIRE(t.db.execute("SELECT * FROM n WHERE x >= -1.5").rows.size() == 1);
    REQUIRE(t.db.execute("SELECT * FROM n WHERE x <= -1.5").rows.empty());
}

TEST_CASE("A01 - comparaison avec fraction positive") {
    TempDb t;
    t.db.execute("CREATE TABLE n (x INTEGER)");
    t.db.execute("INSERT INTO n VALUES (1)");

    REQUIRE(t.db.execute("SELECT * FROM n WHERE x = 1.5").rows.empty());
    REQUIRE(t.db.execute("SELECT * FROM n WHERE x < 1.5").rows.size() == 1);
    REQUIRE(t.db.execute("SELECT * FROM n WHERE x > 1.5").rows.empty());
}

TEST_CASE("A01 - comparaison avec valeur entiere") {
    TempDb t;
    t.db.execute("CREATE TABLE n (x INTEGER)");
    t.db.execute("INSERT INTO n VALUES (1)");

    REQUIRE(t.db.execute("SELECT * FROM n WHERE x = 1.0").rows.size() == 1);
    REQUIRE(t.db.execute("SELECT * FROM n WHERE x = -1.0").rows.empty());
}

// ---------------------------------------------------------------------------
// A06 — Projection unifiée
// ---------------------------------------------------------------------------

TEST_CASE("A06.1 - ordre mixte COUNT(*), b") {
    TempDb t;
    t.db.execute("CREATE TABLE t (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1, 10)");
    t.db.execute("INSERT INTO t VALUES (2, 20)");

    auto r = t.db.execute("SELECT COUNT(*), b FROM t GROUP BY b");
    REQUIRE(r.columns.size() == 2);
    REQUIRE(r.columns[0] == "count");
    REQUIRE(r.columns[1] == "b");
}

TEST_CASE("A06.2 - SELECT * suit l'ordre du schema") {
    TempDb t;
    t.db.execute("CREATE TABLE t (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1, 10)");
    t.db.execute("INSERT INTO t VALUES (2, 20)");

    auto r = t.db.execute("SELECT * FROM t GROUP BY b, a");
    REQUIRE(r.columns.size() == 2);
    REQUIRE(r.columns[0] == "a");
    REQUIRE(r.columns[1] == "b");
}

TEST_CASE("A06.3 - DISTINCT apres projection GROUP BY") {
    TempDb t;
    t.db.execute("CREATE TABLE t (a INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1)");
    t.db.execute("INSERT INTO t VALUES (2)");

    auto r = t.db.execute("SELECT DISTINCT COUNT(*) FROM t GROUP BY a");
    REQUIRE(r.rows.size() == 1);
}

TEST_CASE("A06.4 - ORDER BY colonne de groupe non projetee") {
    TempDb t;
    t.db.execute("CREATE TABLE t (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO t VALUES (1, 20)");
    t.db.execute("INSERT INTO t VALUES (2, 10)");

    REQUIRE_NOTHROW(
        t.db.execute("SELECT COUNT(*) FROM t GROUP BY b ORDER BY b"));
}

// ---------------------------------------------------------------------------
// B02 — 0.0 et -0.0
// ---------------------------------------------------------------------------

TEST_CASE("B02 - DISTINCT fusionne 0.0 et -0.0") {
    TempDb t;
    t.db.execute("CREATE TABLE zeros (x DOUBLE)");
    t.db.execute("INSERT INTO zeros VALUES (0.0)");
    t.db.execute("INSERT INTO zeros VALUES (-0.0)");

    auto r = t.db.execute("SELECT DISTINCT x FROM zeros");
    REQUIRE(r.rows.size() == 1);
}

TEST_CASE("B02 - GROUP BY fusionne 0.0 et -0.0") {
    TempDb t;
    t.db.execute("CREATE TABLE zeros (x DOUBLE)");
    t.db.execute("INSERT INTO zeros VALUES (0.0)");
    t.db.execute("INSERT INTO zeros VALUES (-0.0)");

    auto r = t.db.execute("SELECT x, COUNT(*) FROM zeros GROUP BY x");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[1]) == 2);
}

TEST_CASE("B02 - 0.0 et 1.0 restent distincts") {
    TempDb t;
    t.db.execute("CREATE TABLE d (x DOUBLE)");
    t.db.execute("INSERT INTO d VALUES (0.0)");
    t.db.execute("INSERT INTO d VALUES (1.0)");

    auto r = t.db.execute("SELECT DISTINCT x FROM d");
    REQUIRE(r.rows.size() == 2);
}

// ---------------------------------------------------------------------------
// B03 — INSERT et UPDATE : contrat de conversion
// ---------------------------------------------------------------------------

TEST_CASE("B03 - INSERT accepte 1.0 pour INTEGER") {
    TempDb t;
    t.db.execute("CREATE TABLE convert (x INTEGER)");
    REQUIRE_NOTHROW(t.db.execute("INSERT INTO convert VALUES (1.0)"));
}

TEST_CASE("B03 - UPDATE accepte 2.0 pour INTEGER") {
    TempDb t;
    t.db.execute("CREATE TABLE convert (x INTEGER)");
    t.db.execute("INSERT INTO convert VALUES (1)");
    REQUIRE_NOTHROW(t.db.execute("UPDATE convert SET x = 2.0"));

    auto r = t.db.execute("SELECT x FROM convert");
    REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 2);
}

TEST_CASE("B03 - INSERT refuse 1.5 pour INTEGER") {
    TempDb t;
    t.db.execute("CREATE TABLE convert (x INTEGER)");
    REQUIRE_THROWS_AS(
        t.db.execute("INSERT INTO convert VALUES (1.5)"),
        std::runtime_error);
}

TEST_CASE("B03 - UPDATE refuse 2.5 pour INTEGER") {
    TempDb t;
    t.db.execute("CREATE TABLE convert (x INTEGER)");
    t.db.execute("INSERT INTO convert VALUES (1)");
    REQUIRE_THROWS_AS(
        t.db.execute("UPDATE convert SET x = 2.5"),
        std::runtime_error);
}

// ---------------------------------------------------------------------------
// J3.1 — Verrou exclusif
// ---------------------------------------------------------------------------

TEST_CASE("J3.1 - deux Database sur le meme repertoire") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_j31_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db1(dir);
        REQUIRE_THROWS_AS(Database(dir), std::runtime_error);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("J3.1 - apres fermeture, on peut rouvrir") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_j31b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db1(dir);
    }
    REQUIRE_NOTHROW(Database(dir));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("J3.1 - bases differentes = pas de conflit") {
    auto dir1 = std::filesystem::temp_directory_path() /
        ("kerneldb_j31c_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    auto dir2 = std::filesystem::temp_directory_path() /
        ("kerneldb_j31d_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    Database db1(dir1);
    REQUIRE_NOTHROW(Database(dir2));

    std::error_code ec;
    std::filesystem::remove_all(dir1, ec);
    std::filesystem::remove_all(dir2, ec);
}

// ---------------------------------------------------------------------------
// J3.2 — Journal persistant
// ---------------------------------------------------------------------------

TEST_CASE("J3.2 - le journal existe pendant une transaction") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_j32_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (id INTEGER)");
        db.execute("INSERT INTO t VALUES (1)");
        db.begin();
        db.execute("INSERT INTO t VALUES (2)");

        REQUIRE(std::filesystem::exists(dir / "undo.log"));
    }

    {
        Database db(dir);
        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.rows.size() == 1);
        REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 1);
        REQUIRE_FALSE(std::filesystem::exists(dir / "undo.log"));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("J3.2 - COMMIT supprime le journal") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_j32b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (id INTEGER)");
        db.begin();
        db.execute("INSERT INTO t VALUES (1)");
        REQUIRE(std::filesystem::exists(dir / "undo.log"));
        db.commit();
        REQUIRE_FALSE(std::filesystem::exists(dir / "undo.log"));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("J3.2 - ROLLBACK supprime le journal") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_j32c_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (id INTEGER)");
        db.begin();
        db.execute("INSERT INTO t VALUES (1)");
        REQUIRE(std::filesystem::exists(dir / "undo.log"));
        db.rollback();
        REQUIRE_FALSE(std::filesystem::exists(dir / "undo.log"));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C04 — commit propage l'erreur
// ---------------------------------------------------------------------------

TEST_CASE("C04 - commit reussi quand le journal est supprimable") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c04_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (id INTEGER)");
        db.begin();
        db.execute("INSERT INTO t VALUES (1)");
        REQUIRE(std::filesystem::exists(dir / "undo.log"));
        db.commit();
        REQUIRE_FALSE(std::filesystem::exists(dir / "undo.log"));

        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.rows.size() == 1);
    }

    {
        Database db(dir);
        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.rows.size() == 1);
        REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 1);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("C04 - rollback supprime le journal") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c04b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (id INTEGER)");
        db.execute("INSERT INTO t VALUES (1)");
        db.begin();
        db.execute("INSERT INTO t VALUES (2)");
        REQUIRE(std::filesystem::exists(dir / "undo.log"));
        db.rollback();
        REQUIRE_FALSE(std::filesystem::exists(dir / "undo.log"));

        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.rows.size() == 1);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C05 — fsync
// ---------------------------------------------------------------------------

TEST_CASE("C05 - le journal survit à une fermeture propre") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c05_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (id INTEGER)");
        db.execute("INSERT INTO t VALUES (1)");
        db.begin();
        db.execute("INSERT INTO t VALUES (2)");
    }

    {
        Database db(dir);
        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.rows.size() == 1);
        REQUIRE(std::get<std::int64_t>(r.rows[0].values[0]) == 1);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C01 — Verrou atomique
// ---------------------------------------------------------------------------

TEST_CASE("C01 - acquisition concurrente du verrou") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c01_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    constexpr int N = 8;
    std::atomic<int> success{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&dir, &success]() {
            try {
                DatabaseLock lock(dir);
                ++success;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } catch (...) {
            }
        });
    }

    for (auto& t : threads) t.join();

    REQUIRE(success.load() == 1);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("C01 - apres liberation, on peut reprendre") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c01b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        DatabaseLock lock(dir);
    }
    REQUIRE_NOTHROW(DatabaseLock(dir));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C07 — ALTER : rewrite d'abord
// ---------------------------------------------------------------------------

TEST_CASE("C07 - ALTER reussi met a jour catalogue et donnees") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c07_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (x INTEGER)");
        db.execute("INSERT INTO t VALUES (1)");
        db.execute("ALTER TABLE t ADD y INTEGER");

        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.columns.size() == 2);
        REQUIRE(r.rows.size() == 1);
    }

    {
        Database db(dir);
        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.columns.size() == 2);
        REQUIRE(r.rows.size() == 1);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("C07 - ALTER refuse garde le catalogue intact") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c07b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (x INTEGER)");
        db.execute("INSERT INTO t VALUES (1)");

        REQUIRE_THROWS_AS(
            db.execute("ALTER TABLE t ADD y INTEGER NOT NULL"),
            std::runtime_error);

        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.columns.size() == 1);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C08 — rewrite atomique
// ---------------------------------------------------------------------------

TEST_CASE("C08 - rewrite ne perd pas la table en cas d'echec") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c08_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (x INTEGER)");
        db.execute("INSERT INTO t VALUES (1)");
        db.execute("INSERT INTO t VALUES (2)");
        db.execute("INSERT INTO t VALUES (3)");
        db.execute("UPDATE t SET x = 10 WHERE x = 2");

        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.rows.size() == 3);
    }

    {
        Database db(dir);
        auto r = db.execute("SELECT * FROM t");
        REQUIRE(r.rows.size() == 3);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("C08 - pas de fichier .tmp residuel apres rewrite") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c08b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (x INTEGER)");
        db.execute("INSERT INTO t VALUES (1)");
        db.execute("UPDATE t SET x = 2 WHERE x = 1");

        REQUIRE_FALSE(std::filesystem::exists(dir / "t.tbl.tmp"));
        REQUIRE(std::filesystem::exists(dir / "t.tbl"));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C10 — NaN rejeté
// ---------------------------------------------------------------------------

TEST_CASE("C10 - NaN rejete pour DOUBLE") {
    TempDb t;
    t.db.execute("CREATE TABLE d (x DOUBLE)");

    auto stmt = t.db.prepare("INSERT INTO d VALUES (?)");
    stmt.bind_double(1, std::numeric_limits<double>::quiet_NaN());
    REQUIRE_THROWS_AS(stmt.execute(t.db), std::runtime_error);
}

TEST_CASE("C10 - infini rejete pour DOUBLE") {
    TempDb t;
    t.db.execute("CREATE TABLE d (x DOUBLE)");

    auto stmt = t.db.prepare("INSERT INTO d VALUES (?)");
    stmt.bind_double(1, std::numeric_limits<double>::infinity());
    REQUIRE_THROWS_AS(stmt.execute(t.db), std::runtime_error);
}

TEST_CASE("C10 - -infini rejete pour DOUBLE") {
    TempDb t;
    t.db.execute("CREATE TABLE d (x DOUBLE)");

    auto stmt = t.db.prepare("INSERT INTO d VALUES (?)");
    stmt.bind_double(1, -std::numeric_limits<double>::infinity());
    REQUIRE_THROWS_AS(stmt.execute(t.db), std::runtime_error);
}

// ---------------------------------------------------------------------------
// C11 — Clauses SQL refusées
// ---------------------------------------------------------------------------

TEST_CASE("C11.1 - HAVING sans GROUP BY est refuse") {
    TempDb t;
    t.db.execute("CREATE TABLE g (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO g VALUES (1, 10)");

    REQUIRE_THROWS_AS(
        t.db.execute("SELECT * FROM g HAVING a = 999"),
        std::runtime_error);
}

TEST_CASE("C11.2 - SELECT *, colonne est refuse") {
    TempDb t;
    t.db.execute("CREATE TABLE g (a INTEGER, b INTEGER)");

    REQUIRE_THROWS_AS(
        t.db.execute("SELECT *, a FROM g"),
        std::runtime_error);
}

TEST_CASE("C11.3 - ORDER BY avec agrégat sans GROUP BY est refuse") {
    TempDb t;
    t.db.execute("CREATE TABLE g (a INTEGER, b INTEGER)");
    t.db.execute("INSERT INTO g VALUES (1, 10)");

    REQUIRE_THROWS_AS(
        t.db.execute("SELECT COUNT(*) FROM g ORDER BY nonexistent"),
        std::runtime_error);
}

// ---------------------------------------------------------------------------
// C03 — Verrou orphelin
// ---------------------------------------------------------------------------

TEST_CASE("C03 - verrou orphelin est ignore") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c03_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    std::filesystem::create_directories(dir);

    {
        std::ofstream out(dir / ".lock");
        out << "pid=999999\n";
        out << "timestamp=0\n";
        out << "version=1\n";
    }

    {
        Database db(dir);

        std::ifstream in(dir / ".lock");
        REQUIRE(in.good());
        std::string line;
        std::getline(in, line);
        REQUIRE(line.rfind("pid=", 0) == 0);
        long pid = std::stol(line.substr(4));
        REQUIRE(pid > 0);
        REQUIRE(pid != 999999);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("C03 - verrou actif est respecte") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c03b_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    Database db1(dir);

    REQUIRE_THROWS_AS(Database(dir), std::runtime_error);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C06 — Journal tronqué toléré
// ---------------------------------------------------------------------------

TEST_CASE("C06 - journal tronque n'empeche pas l'ouverture") {
    auto dir = std::filesystem::temp_directory_path() /
        ("kerneldb_c06_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    {
        Database db(dir);
        db.execute("CREATE TABLE t (id INTEGER)");
        db.execute("INSERT INTO t VALUES (1)");
        db.begin();
        db.execute("INSERT INTO t VALUES (2)");
    }

    {
        std::ofstream out(dir / "undo.log", std::ios::binary | std::ios::app);
        out.put('\xFF');
    }

    REQUIRE_NOTHROW([&]() { Database db(dir); }());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// C12 — Littéraux texte
// ---------------------------------------------------------------------------

TEST_CASE("C12 - apostrophe doublee dans une chaine") {
    TempDb t;
    t.db.execute("CREATE TABLE s (name VARCHAR(50))");
    t.db.execute("INSERT INTO s VALUES ('O''Brien')");

    auto r = t.db.execute("SELECT * FROM s");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[0]) == "O'Brien");
}

TEST_CASE("C12 - backslash preserve dans une chaine") {
    TempDb t;
    t.db.execute("CREATE TABLE p (path VARCHAR(100))");
    t.db.execute("INSERT INTO p VALUES ('C:\\temp')");

    auto r = t.db.execute("SELECT * FROM p");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(std::get<std::string>(r.rows[0].values[0]) == "C:\\temp");
}

TEST_CASE("C12 - apostrophe simple reste OK") {
    TempDb t;
    t.db.execute("CREATE TABLE s (name VARCHAR(50))");
    t.db.execute("INSERT INTO s VALUES ('Alice')");

    auto r = t.db.execute("SELECT * FROM s");
    REQUIRE(std::get<std::string>(r.rows[0].values[0]) == "Alice");
}