#include <catch2/catch_test_macros.hpp>
#include "kerneldb/parser.hpp"

using namespace kerneldb;

TEST_CASE("Parser - UPDATE simple") {
    Parser p;
    auto stmt = std::get<UpdateStatement>(p.parse("UPDATE users SET age = 30 WHERE id = 1"));
    REQUIRE(stmt.where != nullptr);
    REQUIRE(stmt.where->cond.op == CompareOp::Equal);
}

TEST_CASE("Parser - DELETE avec WHERE") {
    Parser p;
    auto stmt = std::get<DeleteStatement>(p.parse("DELETE FROM users WHERE age < 18"));
    REQUIRE(stmt.where->cond.op == CompareOp::Less);
}

TEST_CASE("Parser - DROP TABLE") {
    Parser p;
    auto stmt = std::get<DropTableStatement>(p.parse("DROP TABLE users"));
    REQUIRE(stmt.table == "users");
}

TEST_CASE("Parser - LIKE") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM users WHERE name LIKE 'A%'"));
    REQUIRE(stmt.where->cond.op == CompareOp::Like);
}

TEST_CASE("Parser - IN") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM users WHERE age IN (25, 30, 40)"));
    REQUIRE(stmt.where->cond.op == CompareOp::In);
}

TEST_CASE("Parser - BETWEEN") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM users WHERE age BETWEEN 25 AND 35"));
    REQUIRE(stmt.where->cond.op == CompareOp::Between);
}

TEST_CASE("Parser - AND") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM u WHERE age > 18 AND age < 65"));
    REQUIRE(stmt.where->kind == WhereExpr::Kind::And);
}

TEST_CASE("Parser - OR avec priorité") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM u WHERE a = 1 OR b = 2 AND c = 3"));
    REQUIRE(stmt.where->kind == WhereExpr::Kind::Or);
    REQUIRE(stmt.where->right->kind == WhereExpr::Kind::And);
}

TEST_CASE("Parser - parenthèses") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM u WHERE (a = 1 OR b = 2) AND c = 3"));
    REQUIRE(stmt.where->kind == WhereExpr::Kind::And);
    REQUIRE(stmt.where->left->kind == WhereExpr::Kind::Or);
}

// ------- GROUP BY / HAVING -------

TEST_CASE("Parser - GROUP BY une colonne") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT age, COUNT(*) FROM u GROUP BY age"));
    REQUIRE(stmt.group_by.size() == 1);
    REQUIRE(stmt.group_by[0] == "age");

    REQUIRE(stmt.exprs.size() == 2);
    REQUIRE(stmt.exprs[0].kind == SelectExpr::Kind::Column);
    REQUIRE(stmt.exprs[0].column == "age");
    REQUIRE(stmt.exprs[1].kind == SelectExpr::Kind::Aggregate);
    REQUIRE(stmt.exprs[1].aggregate.func == AggFunc::Count);
}

TEST_CASE("Parser - GROUP BY plusieurs colonnes") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT age, city, COUNT(*) FROM u GROUP BY age, city"));
    REQUIRE(stmt.group_by.size() == 2);
    REQUIRE(stmt.group_by[0] == "age");
    REQUIRE(stmt.group_by[1] == "city");
}

TEST_CASE("Parser - GROUP BY avec WHERE") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT age, COUNT(*) FROM u WHERE age > 18 GROUP BY age"));
    REQUIRE(stmt.where != nullptr);
    REQUIRE(stmt.group_by.size() == 1);
}

TEST_CASE("Parser - GROUP BY avec HAVING") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT age, COUNT(*) FROM u GROUP BY age HAVING age > 30"));
    REQUIRE(stmt.group_by.size() == 1);
    REQUIRE(stmt.having != nullptr);
    REQUIRE(stmt.having->kind == WhereExpr::Kind::Cond);
    REQUIRE(stmt.having->cond.op == CompareOp::Greater);
}

TEST_CASE("Parser - GROUP BY avec HAVING AND") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT age, COUNT(*) FROM u GROUP BY age HAVING age > 18 AND age < 65"));
    REQUIRE(stmt.having->kind == WhereExpr::Kind::And);
}

TEST_CASE("Parser - GROUP BY avec ORDER BY et LIMIT") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT age, COUNT(*) FROM u GROUP BY age ORDER BY age DESC LIMIT 5"));
    REQUIRE(stmt.group_by.size() == 1);
    REQUIRE(stmt.order_by.size() == 1);
    REQUIRE(stmt.order_by[0].direction == OrderDirection::Desc);
    REQUIRE(stmt.limit.has_value());
    REQUIRE(*stmt.limit == 5);
}

TEST_CASE("Parser - GROUP BY minuscule") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("select age, count(*) from u group by age"));
    REQUIRE(stmt.group_by.size() == 1);
    REQUIRE(stmt.group_by[0] == "age");
}

TEST_CASE("Parser - SELECT sans GROUP BY") {
    Parser p;
    auto stmt = std::get<SelectStatement>(p.parse("SELECT * FROM u"));
    REQUIRE(stmt.group_by.empty());
    REQUIRE(stmt.having == nullptr);
}

TEST_CASE("Parser - ALTER ADD COLUMN") {
    Parser p;
    auto stmt = std::get<AlterTableStatement>(
        p.parse("ALTER TABLE users ADD COLUMN email VARCHAR(50)"));
    REQUIRE(stmt.new_column.name == "email");
}

TEST_CASE("Parser - SELECT avec DISTINCT") {
    Parser p;
    auto stmt = std::get<SelectStatement>(p.parse("SELECT DISTINCT name FROM u"));
    REQUIRE(stmt.distinct);
}

TEST_CASE("Parser - erreurs") {
    Parser p;
    REQUIRE_THROWS_AS(p.parse("UPDATE users age = 1"), std::runtime_error);
    REQUIRE_THROWS_AS(p.parse("DELETE users"), std::runtime_error);
}

// ------- case-insensitivity des identifiants -------

TEST_CASE("Parser - identifiant minuscule normalise") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE Users (id INTEGER)"));
    REQUIRE(stmt.schema.name == "users");
}

TEST_CASE("Parser - identifiant majuscule normalise") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE USERS (id INTEGER)"));
    REQUIRE(stmt.schema.name == "users");
}

TEST_CASE("Parser - noms de colonnes normalises") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE Users (ID INTEGER, Name VARCHAR(20))"));
    REQUIRE(stmt.schema.name == "users");
    REQUIRE(stmt.schema.columns[0].name == "id");
    REQUIRE(stmt.schema.columns[1].name == "name");
}

TEST_CASE("Parser - SELECT normalise le nom de table") {
    Parser p;
    auto stmt = std::get<SelectStatement>(p.parse("SELECT * FROM Users"));
    REQUIRE(stmt.table == "users");
}

TEST_CASE("Parser - SELECT normalise les colonnes") {
    Parser p;
    auto stmt = std::get<SelectStatement>(p.parse("SELECT ID, Name FROM Users"));
    REQUIRE(stmt.exprs.size() == 2);
    REQUIRE(stmt.exprs[0].kind == SelectExpr::Kind::Column);
    REQUIRE(stmt.exprs[0].column == "id");
    REQUIRE(stmt.exprs[1].kind == SelectExpr::Kind::Column);
    REQUIRE(stmt.exprs[1].column == "name");
}

TEST_CASE("Parser - INSERT normalise") {
    Parser p;
    auto stmt = std::get<InsertStatement>(p.parse("INSERT INTO USERS VALUES (1)"));
    REQUIRE(stmt.table == "users");
}

TEST_CASE("Parser - WHERE normalise la colonne") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM Users WHERE Age > 18"));
    REQUIRE(stmt.where->kind == WhereExpr::Kind::Cond);
    REQUIRE(stmt.where->cond.column == "age");
}

TEST_CASE("Parser - PREPARE normalise le nom") {
    Parser p;
    auto stmt = std::get<PrepareStatement>(
        p.parse("PREPARE MyStmt AS SELECT * FROM Users"));
    REQUIRE(stmt.name == "mystmt");
}

TEST_CASE("Parser - chaine inchangee (case-sensitive)") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM Users WHERE Name = 'Alice'"));
    REQUIRE(std::get<std::string>(stmt.where->cond.value) == "Alice");
}

TEST_CASE("Parser - chaine minuscule inchangee") {
    Parser p;
    auto stmt = std::get<SelectStatement>(
        p.parse("SELECT * FROM Users WHERE Name = 'alice'"));
    REQUIRE(std::get<std::string>(stmt.where->cond.value) == "alice");
}

// ============================================================================
// J5.4-A : contraintes inline (mono-colonne)
// ============================================================================

TEST_CASE("Parser - NOT NULL") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE u (id INTEGER NOT NULL)"));
    REQUIRE(stmt.schema.columns.size() == 1);
    REQUIRE(stmt.schema.columns[0].is_not_null);
    REQUIRE(stmt.schema.primary_key.empty());
    REQUIRE(stmt.schema.unique_constraints.empty());
}

TEST_CASE("Parser - UNIQUE inline (mono-colonne)") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE u (email VARCHAR(50) UNIQUE)"));
    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns[0] == "email");
    REQUIRE_FALSE(stmt.schema.columns[0].is_not_null);
}

TEST_CASE("Parser - PRIMARY KEY inline (mono-colonne)") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE u (id INTEGER PRIMARY KEY)"));
    REQUIRE(stmt.schema.primary_key.size() == 1);
    REQUIRE(stmt.schema.primary_key[0] == "id");
    // PRIMARY KEY implique NOT NULL
    REQUIRE(stmt.schema.columns[0].is_not_null);
}

TEST_CASE("Parser - UNIQUE NOT NULL inline") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE u (email VARCHAR(50) UNIQUE NOT NULL)"));
    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns[0] == "email");
    REQUIRE(stmt.schema.columns[0].is_not_null);
}

TEST_CASE("Parser - NOT NULL UNIQUE inline (ordre inverse)") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE u (email VARCHAR(50) NOT NULL UNIQUE)"));
    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns[0] == "email");
    REQUIRE(stmt.schema.columns[0].is_not_null);
}

TEST_CASE("Parser - contraintes inline multi-colonnes") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE u (id INTEGER PRIMARY KEY, "
                "email VARCHAR(50) UNIQUE NOT NULL, "
                "age INTEGER)"));
    REQUIRE(stmt.schema.columns.size() == 3);
    REQUIRE(stmt.schema.primary_key.size() == 1);
    REQUIRE(stmt.schema.primary_key[0] == "id");
    REQUIRE(stmt.schema.columns[0].is_not_null);

    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns[0] == "email");
    REQUIRE(stmt.schema.columns[1].is_not_null);
    REQUIRE_FALSE(stmt.schema.columns[2].is_not_null);
}

TEST_CASE("Parser - contraintes minuscules inline") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("create table u (id integer primary key, "
                "name varchar(20) unique not null)"));
    REQUIRE(stmt.schema.primary_key.size() == 1);
    REQUIRE(stmt.schema.primary_key[0] == "id");
    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns[0] == "name");
    REQUIRE(stmt.schema.columns[1].is_not_null);
}

TEST_CASE("Parser - sans contraintes (valeurs par defaut)") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE u (id INTEGER, name VARCHAR(20))"));
    REQUIRE(stmt.schema.primary_key.empty());
    REQUIRE(stmt.schema.unique_constraints.empty());
    REQUIRE_FALSE(stmt.schema.columns[0].is_not_null);
    REQUIRE_FALSE(stmt.schema.columns[1].is_not_null);
}

// ============================================================================
// J5.4-A : contraintes table-level
// ============================================================================

TEST_CASE("Parser - PRIMARY KEY table-level mono-colonne") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE t (a INTEGER, b INTEGER, PRIMARY KEY (a))"));
    REQUIRE(stmt.schema.primary_key.size() == 1);
    REQUIRE(stmt.schema.primary_key[0] == "a");
    // PK force NOT NULL
    REQUIRE(stmt.schema.columns[0].is_not_null);
    REQUIRE_FALSE(stmt.schema.columns[1].is_not_null);
}

TEST_CASE("Parser - PRIMARY KEY table-level composite") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE t (a INTEGER, b INTEGER, c INTEGER, PRIMARY KEY (a, b))"));
    REQUIRE(stmt.schema.primary_key.size() == 2);
    REQUIRE(stmt.schema.primary_key[0] == "a");
    REQUIRE(stmt.schema.primary_key[1] == "b");
    REQUIRE(stmt.schema.columns[0].is_not_null);
    REQUIRE(stmt.schema.columns[1].is_not_null);
    REQUIRE_FALSE(stmt.schema.columns[2].is_not_null);
}

TEST_CASE("Parser - UNIQUE table-level mono-colonne") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE t (a INTEGER, b INTEGER, UNIQUE (a))"));
    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns[0] == "a");
    REQUIRE(stmt.schema.unique_constraints[0].name.empty());
}

TEST_CASE("Parser - UNIQUE table-level composite") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE t (a INTEGER, b INTEGER, UNIQUE (a, b))"));
    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns.size() == 2);
    REQUIRE(stmt.schema.unique_constraints[0].columns[0] == "a");
    REQUIRE(stmt.schema.unique_constraints[0].columns[1] == "b");
}

TEST_CASE("Parser - CONSTRAINT nomme PRIMARY KEY") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE t (a INTEGER, b INTEGER, "
                "CONSTRAINT pk_t PRIMARY KEY (a, b))"));
    REQUIRE(stmt.schema.primary_key.size() == 2);
    REQUIRE(stmt.schema.primary_key[0] == "a");
    REQUIRE(stmt.schema.primary_key[1] == "b");
}

TEST_CASE("Parser - CONSTRAINT nomme UNIQUE") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE t (a INTEGER, b INTEGER, "
                "CONSTRAINT uq_ab UNIQUE (a, b))"));
    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].name == "uq_ab");
    REQUIRE(stmt.schema.unique_constraints[0].columns.size() == 2);
}

TEST_CASE("Parser - PK + UNIQUE table-level") {
    Parser p;
    auto stmt = std::get<CreateTableStatement>(
        p.parse("CREATE TABLE t (a INTEGER, b INTEGER, c INTEGER, "
                "PRIMARY KEY (a), UNIQUE (b, c))"));
    REQUIRE(stmt.schema.primary_key.size() == 1);
    REQUIRE(stmt.schema.primary_key[0] == "a");
    REQUIRE(stmt.schema.unique_constraints.size() == 1);
    REQUIRE(stmt.schema.unique_constraints[0].columns.size() == 2);
}

TEST_CASE("Parser - deux PRIMARY KEY refusees") {
    Parser p;
    REQUIRE_THROWS_AS(
        p.parse("CREATE TABLE t (a INTEGER, b INTEGER, "
                "PRIMARY KEY (a), PRIMARY KEY (b))"),
        std::runtime_error);
}

TEST_CASE("Parser - PRIMARY KEY inline et table-level refusees") {
    Parser p;
    REQUIRE_THROWS_AS(
        p.parse("CREATE TABLE t (a INTEGER PRIMARY KEY, b INTEGER, PRIMARY KEY (b))"),
        std::runtime_error);
}

// ============================================================================
// J5.4-A : ALTER TABLE
// ============================================================================

TEST_CASE("Parser - ALTER TABLE ADD COLUMN simple") {
    Parser p;
    auto stmt = std::get<AlterTableStatement>(
        p.parse("ALTER TABLE u ADD COLUMN email VARCHAR(50)"));
    REQUIRE(stmt.new_column.name == "email");
    REQUIRE_FALSE(stmt.new_column.is_not_null);
}

TEST_CASE("Parser - ALTER TABLE ADD COLUMN NOT NULL") {
    Parser p;
    auto stmt = std::get<AlterTableStatement>(
        p.parse("ALTER TABLE u ADD COLUMN email VARCHAR(50) NOT NULL"));
    REQUIRE(stmt.new_column.name == "email");
    REQUIRE(stmt.new_column.is_not_null);
}

TEST_CASE("Parser - ALTER TABLE ADD COLUMN UNIQUE inline refuse") {
    Parser p;
    REQUIRE_THROWS_AS(
        p.parse("ALTER TABLE u ADD COLUMN email VARCHAR(50) UNIQUE"),
        std::runtime_error);
}

TEST_CASE("Parser - ALTER TABLE ADD COLUMN PRIMARY KEY inline refuse") {
    Parser p;
    REQUIRE_THROWS_AS(
        p.parse("ALTER TABLE u ADD COLUMN id INTEGER PRIMARY KEY"),
        std::runtime_error);
}

// ============================================================================
// F15 — Nombres invalides
// ============================================================================

TEST_CASE("F15 - nombre avec plusieurs points est refuse") {
    Parser p;
    REQUIRE_THROWS_AS(p.parse("INSERT INTO t VALUES (7.1.2)"),
                      std::runtime_error);
}

TEST_CASE("F15 - nombre suivi de lettres est refuse") {
    Parser p;
    REQUIRE_THROWS_AS(p.parse("INSERT INTO t VALUES (123abc)"),
                      std::runtime_error);
}

TEST_CASE("F15 - nombre entier valide est accepte") {
    Parser p;
    REQUIRE_NOTHROW(p.parse("INSERT INTO t VALUES (42)"));
}

TEST_CASE("F15 - nombre decimal valide est accepte") {
    Parser p;
    REQUIRE_NOTHROW(p.parse("INSERT INTO t VALUES (7.1)"));
}

TEST_CASE("F15 - nombre negatif valide est accepte") {
    Parser p;
    REQUIRE_NOTHROW(p.parse("INSERT INTO t VALUES (-42)"));
}

// ============================================================================
// R05 — Conversions de taille
// ============================================================================

TEST_CASE("R05 - LIMIT 1.2 refuse") {
    Parser p;
    REQUIRE_THROWS_AS(p.parse("SELECT * FROM t LIMIT 1.2"),
                      std::runtime_error);
}

TEST_CASE("R05 - LIMIT 1abc refuse") {
    Parser p;
    REQUIRE_THROWS_AS(p.parse("SELECT * FROM t LIMIT 1abc"),
                      std::runtime_error);
}

TEST_CASE("R05 - VARCHAR(7.1.2) refuse") {
    Parser p;
    REQUIRE_THROWS_AS(p.parse("CREATE TABLE t (x VARCHAR(7.1.2))"),
                      std::runtime_error);
}

TEST_CASE("R05 - PAGE_SIZE 8192.5 refuse") {
    Parser p;
    REQUIRE_THROWS_AS(p.parse("CREATE DATABASE d WITH PAGE_SIZE 8192.5"),
                      std::runtime_error);
}

TEST_CASE("R05 - LIMIT entier valide accepte") {
    Parser p;
    REQUIRE_NOTHROW(p.parse("SELECT * FROM t LIMIT 10"));
}

TEST_CASE("R05 - VARCHAR(50) valide accepte") {
    Parser p;
    REQUIRE_NOTHROW(p.parse("CREATE TABLE t (x VARCHAR(50))"));
}

TEST_CASE("R05 - PAGE_SIZE 16384 valide accepte") {
    Parser p;
    REQUIRE_NOTHROW(p.parse("CREATE DATABASE d WITH PAGE_SIZE 16384"));
}