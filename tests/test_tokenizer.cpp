// tests/test_tokenizer.cpp
#include <catch2/catch_test_macros.hpp>
#include "kerneldb/tokenizer.hpp"

using namespace kerneldb;

TEST_CASE("Tokenizer — mots-clés et identifiants") {
    auto toks = Tokenizer("SELECT id FROM users").tokenize();
    REQUIRE(toks.size() == 5);   // 4 mots + End
    REQUIRE(toks[0].text == "SELECT");
    REQUIRE(toks[3].text == "users");
    REQUIRE(toks[4].kind == TokenKind::End);
}

TEST_CASE("Tokenizer — nombres et chaînes") {
    auto toks = Tokenizer("INSERT 42 'abc' 3.14").tokenize();
    //  0: INSERT (Word)
    //  1: 42     (Number)
    //  2: 'abc'  (String, contenu "abc")
    //  3: 3.14   (Number)
    //  4: End
    REQUIRE(toks[0].kind == TokenKind::Word);
    REQUIRE(toks[0].text == "INSERT");
    REQUIRE(toks[1].kind == TokenKind::Number);
    REQUIRE(toks[1].text == "42");
    REQUIRE(toks[2].kind == TokenKind::String);
    REQUIRE(toks[2].text == "abc");
    REQUIRE(toks[3].kind == TokenKind::Number);
    REQUIRE(toks[3].text == "3.14");
}

TEST_CASE("Tokenizer — symboles multi-caractères") {
    auto toks = Tokenizer("a <= b != c >= d").tokenize();
    REQUIRE(toks[1].text == "<=");
    REQUIRE(toks[3].text == "!=");
    REQUIRE(toks[5].text == ">=");
}

TEST_CASE("Tokenizer — erreur chaîne non terminée") {
    REQUIRE_THROWS_AS(Tokenizer("SELECT 'abc").tokenize(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// R05-bis — Notation scientifique
// ---------------------------------------------------------------------------

TEST_CASE("R05-bis - tokenizer reconnait 1e10") {
    Tokenizer t("1e10");
    auto tokens = t.tokenize();
    REQUIRE(tokens.size() == 2);   // Number + End
    REQUIRE(tokens[0].kind == TokenKind::Number);
    REQUIRE(tokens[0].text == "1e10");
}

TEST_CASE("R05-bis - tokenizer reconnait 1.5e-3") {
    Tokenizer t("1.5e-3");
    auto tokens = t.tokenize();
    REQUIRE(tokens[0].kind == TokenKind::Number);
    REQUIRE(tokens[0].text == "1.5e-3");
}

TEST_CASE("R05-bis - tokenizer reconnait 2E+5") {
    Tokenizer t("2E+5");
    auto tokens = t.tokenize();
    REQUIRE(tokens[0].kind == TokenKind::Number);
    REQUIRE(tokens[0].text == "2E+5");
}

TEST_CASE("R05-bis - tokenizer ne consomme pas e sans exposant") {
    Tokenizer t("1e");
    auto tokens = t.tokenize();
    REQUIRE(tokens[0].kind == TokenKind::Number);
    REQUIRE(tokens[0].text == "1");
    REQUIRE(tokens[1].kind == TokenKind::Word);
    REQUIRE(tokens[1].text == "e");
}
