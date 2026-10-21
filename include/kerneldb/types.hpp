#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kerneldb {

enum class DataType { Integer, Double, Varchar, Boolean };

// ---------------------------------------------------------------------------
// J5.4-A : colonne — plus de flags PK/UNIQUE ici
//
// La PRIMARY KEY et les contraintes UNIQUE sont désormais des objets
// table-level (voir TableSchema), pour supporter le composite (Oracle-like).
//
// `is_not_null` reste par colonne : c'est un attribut de la colonne,
// pas de la contrainte.
// ---------------------------------------------------------------------------

struct Column {
    std::string name;
    DataType type;
    std::size_t max_length = 0;
    bool is_not_null = false;
};

// ---------------------------------------------------------------------------
// J5.4-A : contrainte UNIQUE
//
// Une contrainte UNIQUE porte sur une ou plusieurs colonnes.
// Peut être nommée par l'utilisateur (CONSTRAINT uq_x UNIQUE (a, b))
// ou anonyme (UNIQUE (a, b)) — dans ce cas `name` est généré.
//
// Sémantique Oracle : NULL autorisé, plusieurs NULL possibles
// (les lignes dont TOUTES les colonnes sont NULL ne sont pas indexées).
// ---------------------------------------------------------------------------

struct UniqueConstraint {
    std::string name;                     // vide = anonyme
    std::vector<std::string> columns;     // 1..N colonnes
};

// J5.1 : définition d'un index
struct IndexDef {
    std::string name;                     // ex. "users_pk"
    std::vector<std::string> columns;     // ex. {"id"}
    bool unique = false;                  // PK ou UNIQUE
};

// ---------------------------------------------------------------------------
// J5.4-A : schéma de table — Oracle-like
//
//  - `primary_key` : 0 ou 1 PK. Si non vide, ses colonnes sont NOT NULL
//                    (forcé par le parser et validé).
//  - `unique_constraints` : 0..N UNIQUE, chacune sur 1..N colonnes.
//  - `indexes` : index explicites (CREATE INDEX) — J5.5
// ---------------------------------------------------------------------------

struct TableSchema {
    std::string name;
    std::vector<Column> columns;
    std::vector<std::string> primary_key;              // 0 ou 1 PK
    std::vector<UniqueConstraint> unique_constraints;  // 0..N UNIQUE
    std::vector<IndexDef> indexes;                     // J5.1 (CREATE INDEX)
};

// J5.2 : référence vers une ligne (page_id, slot_id)
struct RowId {
    std::uint32_t page_id = 0;
    std::uint16_t slot_id = 0;

    bool operator==(const RowId& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }
};

// Placeholder '?' dans les requêtes préparées
struct Parameter {
    std::size_t index = 0;
    bool operator==(const Parameter& other) const {
        return index == other.index;
    }
};

using Value = std::variant<
    std::monostate,   // NULL
    std::int64_t,     // INTEGER
    double,           // DOUBLE
    bool,             // BOOLEAN
    std::string,      // VARCHAR
    Parameter         // ? (placeholder)
>;

struct Row {
    std::vector<Value> values;
};

enum class CompareOp {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Like,
    NotLike,
    In,
    NotIn,
    Between,
    NotBetween,
    IsNull,
    IsNotNull
};

struct Condition {
    std::string column;
    CompareOp op;
    Value value;
    Value value2;
    std::vector<Value> values;
};

struct WhereExpr {
    enum class Kind { Cond, And, Or };

    Kind kind = Kind::Cond;
    Condition cond;
    std::shared_ptr<WhereExpr> left;
    std::shared_ptr<WhereExpr> right;

    static std::shared_ptr<WhereExpr> make_cond(Condition c) {
        auto e = std::make_shared<WhereExpr>();
        e->kind = Kind::Cond;
        e->cond = std::move(c);
        return e;
    }
    static std::shared_ptr<WhereExpr> make_and(std::shared_ptr<WhereExpr> l,
                                                std::shared_ptr<WhereExpr> r) {
        auto e = std::make_shared<WhereExpr>();
        e->kind = Kind::And;
        e->left = std::move(l);
        e->right = std::move(r);
        return e;
    }
    static std::shared_ptr<WhereExpr> make_or(std::shared_ptr<WhereExpr> l,
                                               std::shared_ptr<WhereExpr> r) {
        auto e = std::make_shared<WhereExpr>();
        e->kind = Kind::Or;
        e->left = std::move(l);
        e->right = std::move(r);
        return e;
    }
};

enum class OrderDirection { Asc, Desc };

struct OrderByClause {
    std::string column;
    OrderDirection direction = OrderDirection::Asc;
};

std::string data_type_name(DataType type);
std::string value_to_string(const Value& value);
bool value_matches_type(const Value& value, const Column& column);

inline Value null_value() { return std::monostate{}; }
inline bool is_null(const Value& v) { return std::holds_alternative<std::monostate>(v); }
inline bool is_parameter(const Value& v) { return std::holds_alternative<Parameter>(v); }
inline const Parameter& as_parameter(const Value& v) { return std::get<Parameter>(v); }

} // namespace kerneldb