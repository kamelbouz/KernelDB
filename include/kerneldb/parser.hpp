#pragma once
#include "kerneldb/tokenizer.hpp"
#include "kerneldb/types.hpp"
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kerneldb {

// ---------- Types de base ----------

struct CreateTableStatement {
    TableSchema schema;
};

struct CreateDatabaseStatement {
    std::string name;
    std::size_t page_size = 0;
    bool has_page_size = false;
};

struct UseStatement {
    std::string name;
};

struct DropDatabaseStatement {
    std::string name;
};

struct ShowDatabasesStatement {};

// ---------- Statements avec référence à une table ----------

struct InsertStatement {
    std::string database;
    std::string table;
    std::vector<Value> values;
};

struct UpdateStatement {
    std::string database;
    std::string table;
    std::vector<std::string> columns;
    std::vector<Value> values;
    std::shared_ptr<WhereExpr> where;
};

struct DeleteStatement {
    std::string database;
    std::string table;
    std::shared_ptr<WhereExpr> where;
};

struct DropTableStatement {
    std::string database;
    std::string table;
};

// J5.4-A : ALTER TABLE ADD COLUMN.
// La colonne peut porter NOT NULL, mais plus is_primary_key / is_unique
// (ces contraintes se posent via ADD CONSTRAINT — J5.5+).
struct AlterTableStatement {
    std::string database;
    std::string table;
    Column new_column;
};

// ---------- Agrégats et SELECT ----------

enum class AggFunc { Count, Min, Max, Sum, Avg };

struct Aggregate {
    AggFunc func;
    std::string column;
    bool star = false;
};

// A06 : une expression de projection (colonne simple ou agrégat),
// dans l'ordre exact du SELECT.
struct SelectExpr {
    enum class Kind { Column, Aggregate };
    Kind kind = Kind::Column;
    std::string column;       // pour Kind::Column
    Aggregate aggregate;      // pour Kind::Aggregate
};

struct SelectStatement {
    bool select_all = false;
    bool distinct = false;
    std::vector<SelectExpr> exprs;
    std::string database;
    std::string table;
    std::shared_ptr<WhereExpr> where;
    std::vector<std::string> group_by;
    std::shared_ptr<WhereExpr> having;
    std::vector<OrderByClause> order_by;
    std::optional<std::size_t> limit;
};

// ---------- Statements "classiques" ----------

using Statement = std::variant<
    CreateTableStatement,
    InsertStatement,
    UpdateStatement,
    DeleteStatement,
    DropTableStatement,
    AlterTableStatement,
    SelectStatement
>;

// ---------- Prepared statements ----------

struct PrepareStatement {
    std::string name;
    std::shared_ptr<Statement> inner;
    std::size_t parameter_count = 0;
};

struct ExecuteStatement {
    std::string name;
    std::vector<Value> values;
};

struct DeallocateStatement {
    std::string name;
};

// ---------- Transactions ----------

struct BeginStatement {};
struct CommitStatement {};
struct RollbackStatement {};

// ---------- TopLevel ----------

using TopLevelStatement = std::variant<
    CreateTableStatement,
    CreateDatabaseStatement,
    UseStatement,
    DropDatabaseStatement,
    ShowDatabasesStatement,
    InsertStatement,
    UpdateStatement,
    DeleteStatement,
    DropTableStatement,
    AlterTableStatement,
    SelectStatement,
    PrepareStatement,
    ExecuteStatement,
    DeallocateStatement,
    BeginStatement,
    CommitStatement,
    RollbackStatement
>;

// ---------- Parser ----------

class Parser {
public:
    TopLevelStatement parse(const std::string& sql);
    Statement parse_statement_only(const std::string& sql);
    std::size_t parameter_count() const { return parameter_count_; }

private:
    const Token& peek() const;
    Token consume();
    bool match(const std::string& text);
    void expect(const std::string& text);
    std::string identifier();
    Value literal();
    bool parse_aggregate(Aggregate& out);

    // J5.4-A : parse une définition de colonne.
    // Les mots-clés PRIMARY KEY / UNIQUE inline sont signalés via
    // out_inline_pk / out_inline_unique ; ils seront remontés au niveau
    // table par parse_create.
    // NOT NULL reste sur la colonne.
    Column parse_column_def(bool& out_inline_pk, bool& out_inline_unique);

    // J5.4-A : parse une liste de colonnes entre parenthèses : (a, b, c)
    std::vector<std::string> parse_column_list();

    // J5.4-A : parse une contrainte table-level :
    //   PRIMARY KEY (a, b)
    //   UNIQUE (a, b)
    //   CONSTRAINT name PRIMARY KEY (a, b)
    //   CONSTRAINT name UNIQUE (a, b)
    // Remplit schema.primary_key ou schema.unique_constraints.
    // Retourne true si une contrainte a été lue.
    bool parse_table_constraint(TableSchema& schema);

    // "name" ou "db.name"
    std::pair<std::string, std::string> parse_table_ref();

    std::shared_ptr<WhereExpr> parse_where_opt();
    std::shared_ptr<WhereExpr> parse_or();
    std::shared_ptr<WhereExpr> parse_and();
    std::shared_ptr<WhereExpr> parse_primary();
    Condition parse_condition();

    std::shared_ptr<WhereExpr> parse_having_opt();

    Statement parse_statement();

    Statement parse_create();
    Statement parse_insert();
    Statement parse_update();
    Statement parse_delete();
    Statement parse_drop();
    Statement parse_alter();
    Statement parse_select();

    TopLevelStatement parse_prepare();
    TopLevelStatement parse_execute();
    TopLevelStatement parse_deallocate();

    TopLevelStatement parse_begin();
    TopLevelStatement parse_commit();
    TopLevelStatement parse_rollback();

    TopLevelStatement parse_create_database();
    TopLevelStatement parse_drop_database();
    TopLevelStatement parse_use();
    TopLevelStatement parse_show();

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    std::size_t parameter_count_ = 0;
};

} // namespace kerneldb