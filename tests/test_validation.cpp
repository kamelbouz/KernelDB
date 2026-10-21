#include <catch2/catch_test_macros.hpp>
#include "kerneldb/validation.hpp"

using namespace kerneldb;

// ============================================================================
// validate_schema
// ============================================================================

TEST_CASE("validate_schema - schema minimal valide") {
    TableSchema s{"t", {{"id", DataType::Integer, 0}}};
    REQUIRE_NOTHROW(validate_schema(s));
}

TEST_CASE("validate_schema - refuse nom de table vide") {
    TableSchema s{"", {{"id", DataType::Integer, 0}}};
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - refuse table sans colonne") {
    TableSchema s{"t", {}};
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - refuse colonne dupliquee") {
    TableSchema s{"t", {
        {"id", DataType::Integer, 0},
        {"id", DataType::Varchar, 20}
    }};
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - refuse colonne dupliquee case-insensitive") {
    TableSchema s{"t", {
        {"id", DataType::Integer, 0},
        {"ID", DataType::Varchar, 20}
    }};
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

// ---------------------------------------------------------------------------
// J5.4-A : PRIMARY KEY composite
// ---------------------------------------------------------------------------

TEST_CASE("validate_schema - accepte PK mono-colonne NOT NULL") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, true});   // NOT NULL
    s.columns.push_back({"b", DataType::Integer, 0, false});
    s.primary_key = {"a"};
    REQUIRE_NOTHROW(validate_schema(s));
}

TEST_CASE("validate_schema - refuse PK sur colonne nullable") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});  // nullable
    s.primary_key = {"a"};
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - accepte PK composite") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, true});
    s.columns.push_back({"b", DataType::Integer, 0, true});
    s.primary_key = {"a", "b"};
    REQUIRE_NOTHROW(validate_schema(s));
}

TEST_CASE("validate_schema - refuse PK sur colonne inexistante") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, true});
    s.primary_key = {"zzz"};
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - refuse PK avec colonne dupliquee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, true});
    s.primary_key = {"a", "a"};
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

// ---------------------------------------------------------------------------
// J5.4-A : UNIQUE composite
// ---------------------------------------------------------------------------

TEST_CASE("validate_schema - accepte UNIQUE mono-colonne") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"email", DataType::Varchar, 50, false});
    s.unique_constraints.push_back({"", {"email"}});
    REQUIRE_NOTHROW(validate_schema(s));
}

TEST_CASE("validate_schema - accepte UNIQUE composite") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});
    s.columns.push_back({"b", DataType::Integer, 0, false});
    s.unique_constraints.push_back({"uq_ab", {"a", "b"}});
    REQUIRE_NOTHROW(validate_schema(s));
}

TEST_CASE("validate_schema - refuse UNIQUE sur colonne inexistante") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});
    s.unique_constraints.push_back({"", {"zzz"}});
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - refuse UNIQUE vide") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});
    s.unique_constraints.push_back({"", {}});
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - refuse UNIQUE avec doublon") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});
    s.unique_constraints.push_back({"", {"a", "a"}});
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - refuse deux UNIQUE identiques") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});
    s.columns.push_back({"b", DataType::Integer, 0, false});
    s.unique_constraints.push_back({"", {"a", "b"}});
    s.unique_constraints.push_back({"", {"b", "a"}});   // même ensemble
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - refuse VARCHAR sans longueur") {
    TableSchema s{"t", {{"name", DataType::Varchar, 0}}};
    REQUIRE_THROWS_AS(validate_schema(s), std::runtime_error);
}

TEST_CASE("validate_schema - accepte VARCHAR avec longueur") {
    TableSchema s{"t", {{"name", DataType::Varchar, 50}}};
    REQUIRE_NOTHROW(validate_schema(s));
}

// ============================================================================
// parse_size_token
// ============================================================================

TEST_CASE("parse_size_token - entier simple") {
    Token t{TokenKind::Number, "10"};
    REQUIRE(parse_size_token(t, "LIMIT") == 10);
}

TEST_CASE("parse_size_token - refuse signe negatif") {
    Token t{TokenKind::Number, "-1"};
    REQUIRE_THROWS_AS(parse_size_token(t, "LIMIT"), std::runtime_error);
}

TEST_CASE("parse_size_token - refuse signe positif") {
    Token t{TokenKind::Number, "+1"};
    REQUIRE_THROWS_AS(parse_size_token(t, "LIMIT"), std::runtime_error);
}

TEST_CASE("parse_size_token - refuse nombre decimal") {
    Token t{TokenKind::Number, "1.2"};
    REQUIRE_THROWS_AS(parse_size_token(t, "LIMIT"), std::runtime_error);
}

TEST_CASE("parse_size_token - refuse lettres") {
    Token t{TokenKind::Number, "1abc"};
    REQUIRE_THROWS_AS(parse_size_token(t, "LIMIT"), std::runtime_error);
}

TEST_CASE("parse_size_token - respecte max_value") {
    Token t{TokenKind::Number, "100"};
    REQUIRE_THROWS_AS(parse_size_token(t, "LIMIT", 50), std::runtime_error);
    REQUIRE_NOTHROW(parse_size_token(t, "LIMIT", 200));
}

// ============================================================================
// normalize_value
// ============================================================================

TEST_CASE("normalize_value - INT64 reste INT64") {
    Column col{"id", DataType::Integer, 0};
    Value v = std::int64_t{42};
    auto n = normalize_value(v, col);
    REQUIRE(std::holds_alternative<std::int64_t>(n));
    REQUIRE(std::get<std::int64_t>(n) == 42);
}

TEST_CASE("normalize_value - INT64 max preserve") {
    Column col{"id", DataType::Integer, 0};
    Value v = std::int64_t{9007199254740993LL};
    auto n = normalize_value(v, col);
    REQUIRE(std::get<std::int64_t>(n) == 9007199254740993LL);
}

TEST_CASE("normalize_value - DOUBLE convertit INT64") {
    Column col{"d", DataType::Double, 0};
    Value v = std::int64_t{42};
    auto n = normalize_value(v, col);
    REQUIRE(std::holds_alternative<double>(n));
    REQUIRE(std::get<double>(n) == 42.0);
}

TEST_CASE("normalize_value - DOUBLE garde DOUBLE") {
    Column col{"d", DataType::Double, 0};
    Value v = 1.5;
    auto n = normalize_value(v, col);
    REQUIRE(std::get<double>(n) == 1.5);
}

TEST_CASE("normalize_value - INT64 refuse fraction") {
    Column col{"i", DataType::Integer, 0};
    Value v = 1.5;
    REQUIRE_THROWS_AS(normalize_value(v, col), std::runtime_error);
}

TEST_CASE("normalize_value - INT64 accepte double entier") {
    Column col{"i", DataType::Integer, 0};
    Value v = 42.0;
    auto n = normalize_value(v, col);
    REQUIRE(std::holds_alternative<std::int64_t>(n));
    REQUIRE(std::get<std::int64_t>(n) == 42);
}

TEST_CASE("normalize_value - NULL reste NULL") {
    Column col{"i", DataType::Integer, 0};
    Value v = std::monostate{};
    auto n = normalize_value(v, col);
    REQUIRE(is_null(n));
}

// ============================================================================
// normalize_row
// ============================================================================

TEST_CASE("normalize_row - normalise toutes les valeurs") {
    TableSchema schema{"t", {
        {"id", DataType::Integer, 0},
        {"d", DataType::Double, 0}
    }};
    Row r{{std::int64_t{42}, std::int64_t{7}}};
    auto n = normalize_row(r, schema);
    REQUIRE(std::get<std::int64_t>(n.values[0]) == 42);
    REQUIRE(std::get<double>(n.values[1]) == 7.0);
}

TEST_CASE("normalize_row - refuse nombre de valeurs incorrect") {
    TableSchema schema{"t", {{"id", DataType::Integer, 0}}};
    Row r{{std::int64_t{1}, std::int64_t{2}}};
    REQUIRE_THROWS_AS(normalize_row(r, schema), std::runtime_error);
}

// ============================================================================
// validate_constraints — NOT NULL
// ============================================================================

TEST_CASE("validate_constraints - NOT NULL respecte") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"id", DataType::Integer, 0, true});
    Row r{{std::int64_t{1}}};
    REQUIRE_NOTHROW(validate_constraints(s, r, {}));
}

TEST_CASE("validate_constraints - NOT NULL viole") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"id", DataType::Integer, 0, true});
    Row r{{std::monostate{}}};
    REQUIRE_THROWS_AS(validate_constraints(s, r, {}), std::runtime_error);
}

// ============================================================================
// validate_constraints — PRIMARY KEY
// ============================================================================

TEST_CASE("validate_constraints - PK mono-colonne respectee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"id", DataType::Integer, 0, true});
    s.primary_key = {"id"};

    Row r{{std::int64_t{2}}};
    Row existing{{std::int64_t{1}}};
    REQUIRE_NOTHROW(validate_constraints(s, r, {existing}));
}

TEST_CASE("validate_constraints - PK mono-colonne violee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"id", DataType::Integer, 0, true});
    s.primary_key = {"id"};

    Row r{{std::int64_t{1}}};
    Row existing{{std::int64_t{1}}};
    REQUIRE_THROWS_AS(validate_constraints(s, r, {existing}),
                      std::runtime_error);
}

TEST_CASE("validate_constraints - PK composite respectee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, true});
    s.columns.push_back({"b", DataType::Integer, 0, true});
    s.primary_key = {"a", "b"};

    Row r{{std::int64_t{1}, std::int64_t{2}}};
    Row existing{{std::int64_t{1}, std::int64_t{3}}};   // b diffère
    REQUIRE_NOTHROW(validate_constraints(s, r, {existing}));
}

TEST_CASE("validate_constraints - PK composite violee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, true});
    s.columns.push_back({"b", DataType::Integer, 0, true});
    s.primary_key = {"a", "b"};

    Row r{{std::int64_t{1}, std::int64_t{2}}};
    Row existing{{std::int64_t{1}, std::int64_t{2}}};
    REQUIRE_THROWS_AS(validate_constraints(s, r, {existing}),
                      std::runtime_error);
}

// ============================================================================
// validate_constraints — UNIQUE (Oracle-like)
// ============================================================================

TEST_CASE("validate_constraints - UNIQUE mono-colonne respectee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"email", DataType::Varchar, 50, false});
    s.unique_constraints.push_back({"", {"email"}});

    Row r{{std::string{"b@x.com"}}};
    Row existing{{std::string{"a@x.com"}}};
    REQUIRE_NOTHROW(validate_constraints(s, r, {existing}));
}

TEST_CASE("validate_constraints - UNIQUE mono-colonne violee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"email", DataType::Varchar, 50, false});
    s.unique_constraints.push_back({"", {"email"}});

    Row r{{std::string{"a@x.com"}}};
    Row existing{{std::string{"a@x.com"}}};
    REQUIRE_THROWS_AS(validate_constraints(s, r, {existing}),
                      std::runtime_error);
}

TEST_CASE("validate_constraints - UNIQUE avec NULL n'est pas contraint") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"email", DataType::Varchar, 50, false});
    s.unique_constraints.push_back({"", {"email"}});

    Row r{{std::monostate{}}};
    Row existing{{std::monostate{}}};
    REQUIRE_NOTHROW(validate_constraints(s, r, {existing}));
}

TEST_CASE("validate_constraints - UNIQUE composite respectee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});
    s.columns.push_back({"b", DataType::Integer, 0, false});
    s.unique_constraints.push_back({"", {"a", "b"}});

    Row r{{std::int64_t{1}, std::int64_t{2}}};
    Row existing{{std::int64_t{1}, std::int64_t{3}}};
    REQUIRE_NOTHROW(validate_constraints(s, r, {existing}));
}

TEST_CASE("validate_constraints - UNIQUE composite violee") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});
    s.columns.push_back({"b", DataType::Integer, 0, false});
    s.unique_constraints.push_back({"", {"a", "b"}});

    Row r{{std::int64_t{1}, std::int64_t{2}}};
    Row existing{{std::int64_t{1}, std::int64_t{2}}};
    REQUIRE_THROWS_AS(validate_constraints(s, r, {existing}),
                      std::runtime_error);
}

TEST_CASE("validate_constraints - UNIQUE composite partielle avec NULL") {
    TableSchema s;
    s.name = "t";
    s.columns.push_back({"a", DataType::Integer, 0, false});
    s.columns.push_back({"b", DataType::Integer, 0, false});
    s.unique_constraints.push_back({"", {"a", "b"}});

    // a=1, b=NULL — Oracle : NULL empêche la contrainte
    Row r{{std::int64_t{1}, std::monostate{}}};
    Row existing{{std::int64_t{1}, std::monostate{}}};
    REQUIRE_NOTHROW(validate_constraints(s, r, {existing}));
}

// ---------------------------------------------------------------------------
// B01 — Conversion DOUBLE → INTEGER : refus des valeurs hors plage
// ---------------------------------------------------------------------------

TEST_CASE("B01 - refuse 2^63 (borne haute int64)") {
    Column col{"x", DataType::Integer, 0};
    Value v = 9223372036854775808.0;
    REQUIRE_THROWS_AS(normalize_value(v, col), std::runtime_error);
}

TEST_CASE("B01 - accepte INT64_MAX representable en double") {
    Column col{"x", DataType::Integer, 0};
    Value v = 9223372036854774784.0;
    REQUIRE_NOTHROW(normalize_value(v, col));
}

TEST_CASE("B01 - accepte -2^63 (borne basse int64)") {
    Column col{"x", DataType::Integer, 0};
    Value v = -9223372036854775808.0;
    auto n = normalize_value(v, col);
    REQUIRE(std::get<std::int64_t>(n) == std::numeric_limits<std::int64_t>::min());
}

TEST_CASE("B01 - refuse -2^63 - 1 (hors plage basse)") {
    Column col{"x", DataType::Integer, 0};
    Value v = -9223372036854777856.0;
    REQUIRE_THROWS_AS(normalize_value(v, col), std::runtime_error);
}

TEST_CASE("B01 - refuse NaN") {
    Column col{"x", DataType::Integer, 0};
    Value v = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_THROWS_AS(normalize_value(v, col), std::runtime_error);
}

TEST_CASE("B01 - refuse +infini") {
    Column col{"x", DataType::Integer, 0};
    Value v = std::numeric_limits<double>::infinity();
    REQUIRE_THROWS_AS(normalize_value(v, col), std::runtime_error);
}

TEST_CASE("B01 - refuse -infini") {
    Column col{"x", DataType::Integer, 0};
    Value v = -std::numeric_limits<double>::infinity();
    REQUIRE_THROWS_AS(normalize_value(v, col), std::runtime_error);
}