#include "kerneldb/validation.hpp"
#include "kerneldb/parser.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace kerneldb {

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}
}

const Token& Parser::peek() const { return tokens_.at(pos_); }

Token Parser::consume() { return tokens_.at(pos_++); }

bool Parser::match(const std::string& text) {
    if (upper(peek().text) == upper(text)) { ++pos_; return true; }
    return false;
}

void Parser::expect(const std::string& text) {
    if (!match(text))
        throw std::runtime_error("Attendu: " + text + ", trouvé: " + peek().text);
}

std::string Parser::identifier() {
    if (peek().kind != TokenKind::Word)
        throw std::runtime_error("Identifiant attendu");
    std::string name = consume().text;

    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

// "name" ou "db.name"
std::pair<std::string, std::string> Parser::parse_table_ref() {
    std::string first = identifier();

    if (peek().kind == TokenKind::Symbol && peek().text == ".") {
        consume();
        std::string table = identifier();
        return {first, table};
    }
    return {"", first};
}

Value Parser::literal() {
    auto token = peek();

    if (token.kind == TokenKind::Symbol && token.text == "?") {
        consume();
        ++parameter_count_;
        return Parameter{parameter_count_};
    }

    consume();
    if (token.kind == TokenKind::String) return token.text;
    if (token.kind == TokenKind::Number) {
        std::size_t pos = 0;
        try {
            if (token.text.find('.') != std::string::npos ||
                token.text.find('e') != std::string::npos ||
                token.text.find('E') != std::string::npos) {
                double d = std::stod(token.text, &pos);
                if (pos != token.text.size())
                    throw std::runtime_error("Nombre decimal invalide: " + token.text);
                return d;
            }
            std::int64_t i = std::stoll(token.text, &pos);
            if (pos != token.text.size())
                throw std::runtime_error("Nombre entier invalide: " + token.text);
            return static_cast<std::int64_t>(i);
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Nombre invalide: " + token.text);
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Nombre hors plage: " + token.text);
        }
    }
    auto up = upper(token.text);
    if (up == "TRUE")  return true;
    if (up == "FALSE") return false;
    if (up == "NULL")  return std::monostate{};
    throw std::runtime_error("Valeur littérale attendue");
}

// ---------- WHERE / HAVING ----------

std::shared_ptr<WhereExpr> Parser::parse_where_opt() {
    if (!match("WHERE")) return nullptr;
    return parse_or();
}

std::shared_ptr<WhereExpr> Parser::parse_having_opt() {
    if (!match("HAVING")) return nullptr;
    return parse_or();
}

std::shared_ptr<WhereExpr> Parser::parse_or() {
    auto left = parse_and();
    while (match("OR")) {
        auto right = parse_and();
        left = WhereExpr::make_or(std::move(left), std::move(right));
    }
    return left;
}

std::shared_ptr<WhereExpr> Parser::parse_and() {
    auto left = parse_primary();
    while (match("AND")) {
        auto right = parse_primary();
        left = WhereExpr::make_and(std::move(left), std::move(right));
    }
    return left;
}

std::shared_ptr<WhereExpr> Parser::parse_primary() {
    if (match("(")) {
        auto expr = parse_or();
        expect(")");
        return expr;
    }
    return WhereExpr::make_cond(parse_condition());
}

Condition Parser::parse_condition() {
    Condition c;
    c.column = identifier();
    auto op = upper(consume().text);

    if (op == "NOT") {
        auto next = upper(peek().text);
        if (next == "LIKE")         { consume(); op = "NOT LIKE"; }
        else if (next == "IN")      { consume(); op = "NOT IN"; }
        else if (next == "BETWEEN") { consume(); op = "NOT BETWEEN"; }
    }

    if (op == "IS") {
        if (upper(peek().text) == "NOT") {
            consume();
            expect("NULL");
            c.op = CompareOp::IsNotNull;
        } else {
            expect("NULL");
            c.op = CompareOp::IsNull;
        }
        return c;
    }

    if (op == "=")              c.op = CompareOp::Equal;
    else if (op == "!=")        c.op = CompareOp::NotEqual;
    else if (op == "<")         c.op = CompareOp::Less;
    else if (op == "<=")        c.op = CompareOp::LessEqual;
    else if (op == ">")         c.op = CompareOp::Greater;
    else if (op == ">=")        c.op = CompareOp::GreaterEqual;
    else if (op == "LIKE")      c.op = CompareOp::Like;
    else if (op == "NOT LIKE")  c.op = CompareOp::NotLike;
    else if (op == "IN")        c.op = CompareOp::In;
    else if (op == "NOT IN")    c.op = CompareOp::NotIn;
    else if (op == "BETWEEN")   c.op = CompareOp::Between;
    else if (op == "NOT BETWEEN") c.op = CompareOp::NotBetween;
    else throw std::runtime_error("Opérateur de comparaison inconnu");

    if (c.op == CompareOp::In || c.op == CompareOp::NotIn) {
        expect("(");
        do { c.values.push_back(literal()); } while (match(","));
        expect(")");
        if (c.values.empty())
            throw std::runtime_error("IN attend au moins une valeur");
    }
    else if (c.op == CompareOp::Between || c.op == CompareOp::NotBetween) {
        c.value = literal();
        expect("AND");
        c.value2 = literal();
    }
    else {
        c.value = literal();
    }
    return c;
}

// ---------- Agrégats ----------

bool Parser::parse_aggregate(Aggregate& out) {
    auto word = upper(peek().text);
    AggFunc func;
    if (word == "MIN")        func = AggFunc::Min;
    else if (word == "MAX")   func = AggFunc::Max;
    else if (word == "SUM")   func = AggFunc::Sum;
    else if (word == "AVG")   func = AggFunc::Avg;
    else if (word == "COUNT") func = AggFunc::Count;
    else return false;

    consume();
    expect("(");
    out.func = func;
    if (match("*")) {
        out.star = true;
    } else {
        out.column = identifier();
    }
    expect(")");
    return true;
}

// ---------------------------------------------------------------------------
// J5.4-A : parse_column_def
//
// La colonne ne porte plus is_primary_key / is_unique.
// Les mots-clés PRIMARY KEY / UNIQUE inline sont signalés au caller
// via les flags `out_inline_pk` / `out_inline_unique`.
//
// NOT NULL reste par colonne.
// ---------------------------------------------------------------------------

Column Parser::parse_column_def(bool& out_inline_pk, bool& out_inline_unique) {
    out_inline_pk = false;
    out_inline_unique = false;

    Column col{identifier(), DataType::Integer, 0};
    auto type = upper(identifier());
    if (type == "INTEGER" || type == "INT")       col.type = DataType::Integer;
    else if (type == "DOUBLE")                    col.type = DataType::Double;
    else if (type == "BOOLEAN" || type == "BOOL") col.type = DataType::Boolean;
    else if (type == "VARCHAR") {
        col.type = DataType::Varchar;
        expect("(");
        col.max_length = static_cast<std::size_t>(
            parse_size_token(consume(), "VARCHAR", 65535));
        expect(")");
    }
    else throw std::runtime_error("Type inconnu: " + type);

    bool progress = true;
    while (progress) {
        progress = false;
        if (match("PRIMARY")) {
            expect("KEY");
            out_inline_pk = true;
            col.is_not_null = true;
            progress = true;
        }
        else if (match("UNIQUE")) {
            out_inline_unique = true;
            progress = true;
        }
        else if (match("NOT")) {
            expect("NULL");
            col.is_not_null = true;
            progress = true;
        }
    }
    return col;
}

// ---------------------------------------------------------------------------
// J5.4-A : parse_column_list — (a, b, c)
// ---------------------------------------------------------------------------

std::vector<std::string> Parser::parse_column_list() {
    expect("(");
    std::vector<std::string> cols;
    do {
        cols.push_back(identifier());
    } while (match(","));
    expect(")");
    return cols;
}

// ---------------------------------------------------------------------------
// J5.4-A : parse_table_constraint
//
// Reconnaît :
//   PRIMARY KEY (a, b)
//   UNIQUE (a, b)
//   CONSTRAINT name PRIMARY KEY (a, b)
//   CONSTRAINT name UNIQUE (a, b)
//
// Retourne true si une contrainte a été lue et ajoutée au schéma.
// ---------------------------------------------------------------------------

bool Parser::parse_table_constraint(TableSchema& schema) {
    // CONSTRAINT name ...
    std::string constraint_name;
    if (match("CONSTRAINT")) {
        constraint_name = identifier();
    }

    if (match("PRIMARY")) {
        expect("KEY");
        auto cols = parse_column_list();

        if (!schema.primary_key.empty()) {
            throw std::runtime_error(
                "CREATE TABLE : plusieurs PRIMARY KEY déclarées");
        }
        schema.primary_key = std::move(cols);

        // PK implique NOT NULL sur ses colonnes
        for (const auto& pk_col : schema.primary_key) {
            bool found = false;
            for (auto& col : schema.columns) {
                if (col.name == pk_col) {
                    col.is_not_null = true;
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error(
                    "CREATE TABLE : colonne PRIMARY KEY inconnue : " + pk_col);
            }
        }
        return true;
    }

    if (match("UNIQUE")) {
        auto cols = parse_column_list();

        UniqueConstraint uc;
        uc.name = std::move(constraint_name);   // vide si anonyme
        uc.columns = std::move(cols);
        schema.unique_constraints.push_back(std::move(uc));
        return true;
    }

    if (!constraint_name.empty()) {
        throw std::runtime_error(
            "CREATE TABLE : CONSTRAINT attend PRIMARY KEY ou UNIQUE");
    }
    return false;
}

// ---------- Point d'entrée (TopLevel) ----------

TopLevelStatement Parser::parse(const std::string& sql) {
    tokens_ = Tokenizer(sql).tokenize();
    pos_ = 0;
    parameter_count_ = 0;

    TopLevelStatement result;

    auto first = upper(peek().text);

    if (first == "CREATE") {
        if (pos_ + 1 < tokens_.size() &&
            upper(tokens_[pos_ + 1].text) == "DATABASE") {
            result = parse_create_database();
        } else {
            Statement stmt = parse_create();
            result = std::visit([](auto&& s) -> TopLevelStatement {
                return TopLevelStatement{std::move(s)};
            }, stmt);
        }
    }
    else if (first == "DROP") {
        if (pos_ + 1 < tokens_.size() &&
            upper(tokens_[pos_ + 1].text) == "DATABASE") {
            result = parse_drop_database();
        } else {
            Statement stmt = parse_drop();
            result = std::visit([](auto&& s) -> TopLevelStatement {
                return TopLevelStatement{std::move(s)};
            }, stmt);
        }
    }
    else if (first == "USE")             result = parse_use();
    else if (first == "SHOW")            result = parse_show();
    else if (first == "PREPARE")         result = parse_prepare();
    else if (first == "EXECUTE")         result = parse_execute();
    else if (first == "DEALLOCATE")      result = parse_deallocate();
    else if (first == "BEGIN")           result = parse_begin();
    else if (first == "COMMIT")          result = parse_commit();
    else if (first == "ROLLBACK")        result = parse_rollback();
    else {
        Statement stmt = parse_statement();
        result = std::visit([](auto&& s) -> TopLevelStatement {
            return TopLevelStatement{std::move(s)};
        }, stmt);
    }

    match(";");
    if (peek().kind != TokenKind::End)
        throw std::runtime_error("Texte inattendu après la commande");
    return result;
}

Statement Parser::parse_statement_only(const std::string& sql) {
    tokens_ = Tokenizer(sql).tokenize();
    pos_ = 0;
    parameter_count_ = 0;

    Statement result = parse_statement();

    match(";");
    if (peek().kind != TokenKind::End)
        throw std::runtime_error("Texte inattendu après la commande");
    return result;
}

Statement Parser::parse_statement() {
    auto first = upper(peek().text);
    if (first == "CREATE")      return parse_create();
    if (first == "INSERT")      return parse_insert();
    if (first == "UPDATE")      return parse_update();
    if (first == "DELETE")      return parse_delete();
    if (first == "DROP")        return parse_drop();
    if (first == "ALTER")       return parse_alter();
    if (first == "SELECT")      return parse_select();
    throw std::runtime_error("Commande SQL non prise en charge");
}

// ---------- PREPARE / EXECUTE / DEALLOCATE ----------

TopLevelStatement Parser::parse_prepare() {
    expect("PREPARE");
    PrepareStatement prep;
    prep.name = identifier();
    expect("AS");
    prep.inner = std::make_shared<Statement>(parse_statement());
    prep.parameter_count = parameter_count_;
    return prep;
}

TopLevelStatement Parser::parse_execute() {
    expect("EXECUTE");
    ExecuteStatement stmt;
    stmt.name = identifier();

    if (match("USING")) {
        expect("(");
        if (!match(")")) {
            do { stmt.values.push_back(literal()); } while (match(","));
            expect(")");
        }
    }
    return stmt;
}

TopLevelStatement Parser::parse_deallocate() {
    expect("DEALLOCATE");
    DeallocateStatement stmt;
    stmt.name = identifier();
    return stmt;
}

// ---------- Transactions ----------

TopLevelStatement Parser::parse_begin() {
    expect("BEGIN");
    match("TRANSACTION");
    return BeginStatement{};
}

TopLevelStatement Parser::parse_commit() {
    expect("COMMIT");
    match("TRANSACTION");
    return CommitStatement{};
}

TopLevelStatement Parser::parse_rollback() {
    expect("ROLLBACK");
    match("TRANSACTION");
    return RollbackStatement{};
}

// ---------- CREATE DATABASE / DROP DATABASE / USE / SHOW ----------

TopLevelStatement Parser::parse_create_database() {
    expect("CREATE");
    expect("DATABASE");

    CreateDatabaseStatement stmt;
    stmt.name = identifier();

    if (match("WITH")) {
        expect("PAGE_SIZE");
        auto tok = consume();
        if (tok.kind != TokenKind::Number)
            throw std::runtime_error("PAGE_SIZE attend un nombre");
        stmt.page_size = static_cast<std::size_t>(
            parse_size_token(tok, "PAGE_SIZE", 65536));
        stmt.has_page_size = true;
    }
    return stmt;
}

TopLevelStatement Parser::parse_drop_database() {
    expect("DROP");
    expect("DATABASE");

    DropDatabaseStatement stmt;
    stmt.name = identifier();
    return stmt;
}

TopLevelStatement Parser::parse_use() {
    expect("USE");

    UseStatement stmt;
    stmt.name = identifier();
    return stmt;
}

TopLevelStatement Parser::parse_show() {
    expect("SHOW");
    expect("DATABASES");
    return ShowDatabasesStatement{};
}

// ---------------------------------------------------------------------------
// J5.4-A : parse_create
//
// Supporte :
//   CREATE TABLE t (a INTEGER, b INTEGER)
//   CREATE TABLE t (a INTEGER PRIMARY KEY, b INTEGER)
//   CREATE TABLE t (a INTEGER, b INTEGER, PRIMARY KEY (a, b))
//   CREATE TABLE t (a INTEGER, b INTEGER, PRIMARY KEY (a, b), UNIQUE (a))
//   CREATE TABLE t (a INTEGER, b INTEGER,
//                   CONSTRAINT pk_t PRIMARY KEY (a, b),
//                   CONSTRAINT uq_t UNIQUE (a))
// ---------------------------------------------------------------------------

Statement Parser::parse_create() {
    expect("CREATE"); expect("TABLE");
    TableSchema schema{identifier(), {}};
    expect("(");

    // Accumule les PRIMARY KEY inline (au plus une) et les UNIQUE inline.
    std::vector<std::string> inline_pk_col;
    std::vector<std::string> inline_unique_cols;

    bool first = true;
    do {
        if (!first) {
            // après une virgule, on peut avoir une contrainte table-level
        }
        first = false;

        // Détection d'une contrainte table-level :
        //   PRIMARY KEY, UNIQUE, CONSTRAINT
        auto next = upper(peek().text);
        if (next == "PRIMARY" || next == "UNIQUE" || next == "CONSTRAINT") {
            parse_table_constraint(schema);
            continue;
        }

        // Sinon : colonne
        bool inline_pk = false;
        bool inline_unique = false;
        Column col = parse_column_def(inline_pk, inline_unique);

        if (inline_pk) {
            if (!inline_pk_col.empty()) {
                throw std::runtime_error(
                    "CREATE TABLE : plusieurs PRIMARY KEY déclarées");
            }
            inline_pk_col.push_back(col.name);
        }
        if (inline_unique) {
            inline_unique_cols.push_back(col.name);
        }

        schema.columns.push_back(std::move(col));
    } while (match(","));

    expect(")");

    // Consolider les PK/UNIQUE inline
    if (!inline_pk_col.empty()) {
        if (!schema.primary_key.empty()) {
            throw std::runtime_error(
                "CREATE TABLE : PRIMARY KEY déclarée deux fois");
        }
        schema.primary_key = std::move(inline_pk_col);
    }
    for (auto& col : inline_unique_cols) {
        UniqueConstraint uc;
        uc.name = "";                   // anonyme
        uc.columns = { col };
        schema.unique_constraints.push_back(std::move(uc));
    }

    return CreateTableStatement{std::move(schema)};
}

Statement Parser::parse_insert() {
    expect("INSERT"); expect("INTO");
    auto ref = parse_table_ref();
    InsertStatement stmt;
    stmt.database = std::move(ref.first);
    stmt.table = std::move(ref.second);
    expect("VALUES"); expect("(");
    do { stmt.values.push_back(literal()); } while (match(","));
    expect(")");
    return stmt;
}

Statement Parser::parse_update() {
    expect("UPDATE");
    auto ref = parse_table_ref();
    UpdateStatement stmt;
    stmt.database = std::move(ref.first);
    stmt.table = std::move(ref.second);
    expect("SET");
    do {
        std::string col = identifier();
        expect("=");
        stmt.columns.push_back(std::move(col));
        stmt.values.push_back(literal());
    } while (match(","));
    stmt.where = parse_where_opt();
    return stmt;
}

Statement Parser::parse_delete() {
    expect("DELETE"); expect("FROM");
    auto ref = parse_table_ref();
    DeleteStatement stmt;
    stmt.database = std::move(ref.first);
    stmt.table = std::move(ref.second);
    stmt.where = parse_where_opt();
    return stmt;
}

Statement Parser::parse_drop() {
    expect("DROP"); expect("TABLE");
    auto ref = parse_table_ref();
    DropTableStatement stmt;
    stmt.database = std::move(ref.first);
    stmt.table = std::move(ref.second);
    return stmt;
}

Statement Parser::parse_alter() {
    expect("ALTER"); expect("TABLE");
    auto ref = parse_table_ref();
    AlterTableStatement stmt;
    stmt.database = std::move(ref.first);
    stmt.table = std::move(ref.second);
    expect("ADD");
    match("COLUMN");

    bool inline_pk = false;
    bool inline_unique = false;
    stmt.new_column = parse_column_def(inline_pk, inline_unique);

    if (inline_pk || inline_unique) {
        throw std::runtime_error(
            "ALTER TABLE ADD COLUMN : PRIMARY KEY / UNIQUE inline non supporté "
            "(utilisez ADD CONSTRAINT — J5.5+)");
    }
    return stmt;
}

Statement Parser::parse_select() {
    expect("SELECT");
    SelectStatement stmt;

    if (match("DISTINCT")) stmt.distinct = true;

    do {
        Aggregate agg;
        if (parse_aggregate(agg)) {
            SelectExpr e;
            e.kind = SelectExpr::Kind::Aggregate;
            e.aggregate = std::move(agg);
            stmt.exprs.push_back(std::move(e));
        }
        else if (match("*")) {
            stmt.select_all = true;
        }
        else {
            SelectExpr e;
            e.kind = SelectExpr::Kind::Column;
            e.column = identifier();
            stmt.exprs.push_back(std::move(e));
        }
    } while (match(","));

    expect("FROM");
    auto ref = parse_table_ref();
    stmt.database = std::move(ref.first);
    stmt.table = std::move(ref.second);

    stmt.where = parse_where_opt();

    if (match("GROUP")) {
        expect("BY");
        do {
            stmt.group_by.push_back(identifier());
        } while (match(","));
    }

    stmt.having = parse_having_opt();

    if (match("ORDER")) {
        expect("BY");
        do {
            OrderByClause clause;
            clause.column = identifier();
            if (match("ASC"))       clause.direction = OrderDirection::Asc;
            else if (match("DESC")) clause.direction = OrderDirection::Desc;
            stmt.order_by.push_back(std::move(clause));
        } while (match(","));
    }

    if (match("LIMIT")) {
        auto tok = consume();
        if (tok.kind != TokenKind::Number)
            throw std::runtime_error("LIMIT attend un nombre");
        stmt.limit = static_cast<std::size_t>(
            parse_size_token(tok, "LIMIT"));
    }

    return stmt;
}

} // namespace kerneldb