#include "kerneldb/validation.hpp"
#include "kerneldb/database.hpp"
#include "kerneldb/parser.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <limits>
#include <cmath>
namespace kerneldb {

namespace {
// Forward declarations (A06)
std::size_t column_index(const TableSchema& schema, const std::string& name);
std::string aggregate_name(const Aggregate& agg);

// A06 : un élément de projection (colonne ou agrégat), dans l'ordre.
struct OutItem {
    enum class Kind { Column, Aggregate };
    Kind kind;
    std::size_t col_idx = 0;      // pour Kind::Column
    Aggregate aggregate;          // pour Kind::Aggregate
};

// A06 : convertit stmt.exprs en liste ordonnée d'items de projection.
void build_out_items(const SelectStatement& stmt,
                     const TableSchema& schema,
                     const std::vector<std::size_t>& group_idx,
                     std::vector<std::string>& out_col_names,
                     std::vector<OutItem>& out_items) {
    if (stmt.select_all) {
        for (std::size_t i = 0; i < schema.columns.size(); ++i) {
            if (std::find(group_idx.begin(), group_idx.end(), i) != group_idx.end()) {
                out_col_names.push_back(schema.columns[i].name);
                OutItem item;
                item.kind = OutItem::Kind::Column;
                item.col_idx = i;
                out_items.push_back(item);
            }
        }
    } else {
        for (const auto& e : stmt.exprs) {
            if (e.kind == SelectExpr::Kind::Column) {
                out_col_names.push_back(e.column);
                OutItem item;
                item.kind = OutItem::Kind::Column;
                item.col_idx = column_index(schema, e.column);
                out_items.push_back(item);
            } else {
                out_col_names.push_back(aggregate_name(e.aggregate));
                OutItem item;
                item.kind = OutItem::Kind::Aggregate;
                item.aggregate = e.aggregate;
                out_items.push_back(item);
            }
        }
    }
}

std::size_t column_index(const TableSchema& schema, const std::string& name) {
    for (std::size_t i = 0; i < schema.columns.size(); ++i)
        if (schema.columns[i].name == name) return i;
    throw std::runtime_error("Colonne inconnue: " + name);
}

int compare_int_double(std::int64_t i, double d) {
    if (d != d) return 1;

    if (d < static_cast<double>(std::numeric_limits<std::int64_t>::min())) {
        return 1;
    }
    if (d >= 9223372036854775808.0) {
        return -1;
    }

    double d_floor = std::floor(d);
    std::int64_t d_int = static_cast<std::int64_t>(d_floor);

    if (i < d_int) return -1;
    if (i > d_int) return 1;

    double d_frac = d - d_floor;
    if (d_frac > 0.0) return -1;
    if (d_frac < 0.0) return 1;
    return 0;
}

int compare(const Value& a, const Value& b) {
    if (std::holds_alternative<std::int64_t>(a) &&
        std::holds_alternative<std::int64_t>(b)) {
        auto x = std::get<std::int64_t>(a);
        auto y = std::get<std::int64_t>(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (std::holds_alternative<double>(a) &&
        std::holds_alternative<double>(b)) {
        auto x = std::get<double>(a);
        auto y = std::get<double>(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (std::holds_alternative<std::int64_t>(a) &&
        std::holds_alternative<double>(b)) {
        return compare_int_double(std::get<std::int64_t>(a),
                                   std::get<double>(b));
    }
    if (std::holds_alternative<double>(a) &&
        std::holds_alternative<std::int64_t>(b)) {
        return -compare_int_double(std::get<std::int64_t>(b),
                                    std::get<double>(a));
    }
    auto x = value_to_string(a), y = value_to_string(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

bool like_match(const std::string& text, const std::string& pattern) {
    std::size_t ti = 0, pi = 0;
    std::size_t star_pi = std::string::npos;
    std::size_t star_ti = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '_' ||
            (pattern[pi] != '%' && pattern[pi] == text[ti]))) {
            ++ti; ++pi;
        }
        else if (pi < pattern.size() && pattern[pi] == '%') {
            star_pi = pi++;
            star_ti = ti;
        }
        else if (star_pi != std::string::npos) {
            pi = star_pi + 1;
            ti = ++star_ti;
        }
        else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '%') ++pi;
    return pi == pattern.size();
}

bool eval_condition(const Row& row, std::size_t ci, const Condition& c) {
    const Value& value = row.values[ci];

    if (c.op == CompareOp::IsNull)    return is_null(value);
    if (c.op == CompareOp::IsNotNull) return !is_null(value);
    if (is_null(value)) return false;

    if (c.op == CompareOp::In || c.op == CompareOp::NotIn) {
        bool found = false;
        bool has_null = false;
        for (const auto& v : c.values) {
            if (is_null(v)) { has_null = true; continue; }
            if (compare(value, v) == 0) { found = true; break; }
        }
        if (found) return c.op == CompareOp::In;
        if (has_null) return false;
        return c.op == CompareOp::NotIn;
    }
    if (c.op == CompareOp::Between || c.op == CompareOp::NotBetween) {
        if (is_null(c.value) || is_null(c.value2)) return false;
        bool in_range = compare(value, c.value) >= 0 && compare(value, c.value2) <= 0;
        return c.op == CompareOp::Between ? in_range : !in_range;
    }
    if (c.op == CompareOp::Like || c.op == CompareOp::NotLike) {
        if (is_null(c.value)) return false;
        if (!std::holds_alternative<std::string>(value) ||
            !std::holds_alternative<std::string>(c.value))
            return false;
        bool m = like_match(std::get<std::string>(value),
                            std::get<std::string>(c.value));
        return c.op == CompareOp::Like ? m : !m;
    }
    if (is_null(c.value)) return false;
    int n = compare(value, c.value);
    switch (c.op) {
        case CompareOp::Equal:        return n == 0;
        case CompareOp::NotEqual:     return n != 0;
        case CompareOp::Less:         return n < 0;
        case CompareOp::LessEqual:    return n <= 0;
        case CompareOp::Greater:      return n > 0;
        case CompareOp::GreaterEqual: return n >= 0;
        default:                      return false;
    }
}

bool eval_where(const Row& row, const TableSchema& schema, const WhereExpr& expr) {
    if (expr.kind == WhereExpr::Kind::Cond) {
        std::size_t ci = column_index(schema, expr.cond.column);
        return eval_condition(row, ci, expr.cond);
    }
    if (expr.kind == WhereExpr::Kind::And) {
        return eval_where(row, schema, *expr.left) && eval_where(row, schema, *expr.right);
    }
    return eval_where(row, schema, *expr.left) || eval_where(row, schema, *expr.right);
}

void validate_where(const TableSchema& schema, const WhereExpr& expr) {
    if (expr.kind == WhereExpr::Kind::Cond) {
        column_index(schema, expr.cond.column);
        return;
    }
    validate_where(schema, *expr.left);
    validate_where(schema, *expr.right);
}

void encode_value(std::string& out, const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) {
        out += 'N';
    } else if (std::holds_alternative<std::int64_t>(v)) {
        out += 'I';
        auto i = std::get<std::int64_t>(v);
        out.append(reinterpret_cast<const char*>(&i), sizeof(i));
    } else if (std::holds_alternative<double>(v)) {
        out += 'D';
        double d = std::get<double>(v);
        if (d == 0.0) d = 0.0;
        out.append(reinterpret_cast<const char*>(&d), sizeof(d));
    } else if (std::holds_alternative<bool>(v)) {
        out += 'B';
        out += std::get<bool>(v) ? '1' : '0';
    } else if (std::holds_alternative<std::string>(v)) {
        out += 'S';
        const auto& s = std::get<std::string>(v);
        auto len = s.size();
        out.append(reinterpret_cast<const char*>(&len), sizeof(len));
        out.append(s);
    } else {
        out += '?';
    }
    out += '\x1E';
}

std::string row_key(const Row& row) {
    std::string key;
    key.reserve(row.values.size() * 16);
    for (const auto& v : row.values) {
        encode_value(key, v);
    }
    return key;
}

std::string group_key(const Row& row, const std::vector<std::size_t>& group_cols) {
    std::string key;
    key.reserve(group_cols.size() * 16);
    for (auto ci : group_cols) {
        encode_value(key, row.values[ci]);
    }
    return key;
}

bool to_double(const Value& v, double& out) {
    if (std::holds_alternative<std::int64_t>(v)) {
        out = static_cast<double>(std::get<std::int64_t>(v));
        return true;
    }
    if (std::holds_alternative<double>(v)) {
        out = std::get<double>(v);
        return true;
    }
    return false;
}

Value compute_aggregate(const Aggregate& agg,
                        const TableSchema& schema,
                        const std::vector<Row>& rows) {
    if (agg.func == AggFunc::Count && agg.star) {
        return static_cast<std::int64_t>(rows.size());
    }

    std::size_t ci = column_index(schema, agg.column);

    if (agg.func == AggFunc::Count) {
        std::int64_t n = 0;
        for (const auto& row : rows)
            if (!is_null(row.values[ci])) ++n;
        return n;
    }

    bool all_int = true;
    bool first = true;
    std::int64_t acc_i = 0;
    double acc_d = 0.0;
    bool sum_overflow = false;
    std::int64_t min_i = 0, max_i = 0;
    double min_d = 0.0, max_d = 0.0;
    std::size_t count = 0;

    for (const auto& row : rows) {
        const auto& v = row.values[ci];
        if (is_null(v)) continue;

        if (std::holds_alternative<std::int64_t>(v)) {
            std::int64_t iv = std::get<std::int64_t>(v);
            if (first) {
                min_i = max_i = iv;
                min_d = max_d = static_cast<double>(iv);
                if (agg.func == AggFunc::Sum) acc_i = iv;
                acc_d = static_cast<double>(iv);
                first = false;
            } else {
                if (iv < min_i) min_i = iv;
                if (iv > max_i) max_i = iv;
                if (iv < min_d) min_d = static_cast<double>(iv);
                if (iv > max_d) max_d = static_cast<double>(iv);
                if (agg.func == AggFunc::Sum) {
                    std::int64_t tmp = 0;
                    if (__builtin_add_overflow(acc_i, iv, &tmp)) {
                        sum_overflow = true;
                    } else {
                        acc_i = tmp;
                    }
                }
                acc_d += static_cast<double>(iv);
            }
        } else if (std::holds_alternative<double>(v)) {
            all_int = false;
            double dv = std::get<double>(v);
            if (first) {
                min_d = max_d = dv;
                acc_d = dv;
                first = false;
            } else {
                if (dv < min_d) min_d = dv;
                if (dv > max_d) max_d = dv;
                acc_d += dv;
            }
        } else {
            throw std::runtime_error(
                "Agrégat numérique sur colonne non numérique: " + agg.column);
        }
        ++count;
    }

    if (first) {
        return std::monostate{};
    }

    switch (agg.func) {
        case AggFunc::Sum:
            if (all_int && sum_overflow) {
                throw std::runtime_error(
                    "SUM: dépassement de la capacité int64 "
                    "(utilisez des valeurs plus petites ou DOUBLE)");
            }
            return all_int ? Value{acc_i} : Value{acc_d};
        case AggFunc::Avg:
            return acc_d / static_cast<double>(count);
        case AggFunc::Min:
            return all_int ? Value{min_i} : Value{min_d};
        case AggFunc::Max:
            return all_int ? Value{max_i} : Value{max_d};
        default:
            return std::monostate{};
    }
}

std::string aggregate_name(const Aggregate& agg) {
    switch (agg.func) {
        case AggFunc::Count: return agg.star ? "count" : ("count_" + agg.column);
        case AggFunc::Min:   return "min";
        case AggFunc::Max:   return "max";
        case AggFunc::Sum:   return "sum";
        case AggFunc::Avg:   return "avg";
    }
    return "?";
}

Value default_value_for(const Column&) {
    return std::monostate{};
}

void validate_group_by(const TableSchema& schema,
                        const SelectStatement& stmt) {
    if (stmt.group_by.empty()) return;

    std::unordered_set<std::size_t> group_idx;
    for (const auto& col : stmt.group_by)
        group_idx.insert(column_index(schema, col));

    if (stmt.select_all) {
        if (group_idx.size() != schema.columns.size())
            throw std::runtime_error("SELECT * avec GROUP BY : précisez les colonnes ou groupez sur toutes les colonnes");
    } else {
        for (const auto& e : stmt.exprs) {
            if (e.kind != SelectExpr::Kind::Column) continue;
            std::size_t ci = column_index(schema, e.column);
            if (group_idx.find(ci) == group_idx.end())
                throw std::runtime_error(
                    "Colonne " + e.column + " doit être dans GROUP BY ou dans un agrégat");
        }
    }
}

void assert_no_parameters(const Value& v) {
    if (is_parameter(v))
        throw std::runtime_error("Parameter '?' non substitué (bug interne)");
}

void assert_no_parameters_in_condition(const Condition& c) {
    assert_no_parameters(c.value);
    assert_no_parameters(c.value2);
    for (const auto& v : c.values) assert_no_parameters(v);
}

void assert_no_parameters_in_where(const WhereExpr& e) {
    if (e.kind == WhereExpr::Kind::Cond) {
        assert_no_parameters_in_condition(e.cond);
        return;
    }
    assert_no_parameters_in_where(*e.left);
    assert_no_parameters_in_where(*e.right);
}

// ---------- SUBSTITUTION DES PARAMÈTRES ----------

Value substitute_value(const Value& v, const std::vector<Value>& values) {
    if (is_parameter(v)) {
        std::size_t idx = as_parameter(v).index;
        if (idx == 0 || idx > values.size())
            throw std::runtime_error("Paramètre '?' avec index invalide: " + std::to_string(idx));
        return values[idx - 1];
    }
    return v;
}

Condition substitute_condition(const Condition& c, const std::vector<Value>& values) {
    Condition out = c;
    out.value  = substitute_value(c.value, values);
    out.value2 = substitute_value(c.value2, values);
    out.values.clear();
    for (const auto& v : c.values)
        out.values.push_back(substitute_value(v, values));
    return out;
}

std::shared_ptr<WhereExpr> substitute_where(const std::shared_ptr<WhereExpr>& e,
                                              const std::vector<Value>& values) {
    if (!e) return nullptr;
    if (e->kind == WhereExpr::Kind::Cond) {
        return WhereExpr::make_cond(substitute_condition(e->cond, values));
    }
    if (e->kind == WhereExpr::Kind::And) {
        return WhereExpr::make_and(substitute_where(e->left, values),
                                    substitute_where(e->right, values));
    }
    return WhereExpr::make_or(substitute_where(e->left, values),
                                substitute_where(e->right, values));
}

Statement substitute_statement(const Statement& stmt, const std::vector<Value>& values) {
    return std::visit([&](const auto& s) -> Statement {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, CreateTableStatement>) {
            return s;
        }
        else if constexpr (std::is_same_v<T, InsertStatement>) {
            InsertStatement out = s;
            for (auto& v : out.values) v = substitute_value(v, values);
            return out;
        }
        else if constexpr (std::is_same_v<T, UpdateStatement>) {
            UpdateStatement out = s;
            for (auto& v : out.values) v = substitute_value(v, values);
            out.where = substitute_where(s.where, values);
            return out;
        }
        else if constexpr (std::is_same_v<T, DeleteStatement>) {
            DeleteStatement out = s;
            out.where = substitute_where(s.where, values);
            return out;
        }
        else if constexpr (std::is_same_v<T, DropTableStatement>) {
            return s;
        }
        else if constexpr (std::is_same_v<T, AlterTableStatement>) {
            return s;
        }
        else if constexpr (std::is_same_v<T, SelectStatement>) {
            SelectStatement out = s;
            out.where  = substitute_where(s.where, values);
            out.having = substitute_where(s.having, values);
            return out;
        }
    }, stmt);
}

} // namespace

// ---------- PreparedStatement ----------

std::size_t PreparedStatement::bound_count() const {
    std::size_t n = 0;
    for (const auto& p : params_) if (p.has_value()) ++n;
    return n;
}

void PreparedStatement::bind_value(std::size_t index, Value value) {
    if (index == 0 || index > parameter_count_)
        throw std::runtime_error("Index de paramètre hors plage: " + std::to_string(index)
                                  + " (1.." + std::to_string(parameter_count_) + ")");
    params_[index - 1] = std::move(value);
}

void PreparedStatement::bind_null(std::size_t index)   { bind_value(index, std::monostate{}); }
void PreparedStatement::bind_int(std::size_t index, std::int64_t value) { bind_value(index, value); }
void PreparedStatement::bind_double(std::size_t index, double value)    { bind_value(index, value); }
void PreparedStatement::bind_bool(std::size_t index, bool value)        { bind_value(index, value); }
void PreparedStatement::bind_string(std::size_t index, std::string value) { bind_value(index, std::move(value)); }

void PreparedStatement::reset() {
    for (auto& p : params_) p.reset();
}

QueryResult PreparedStatement::execute(Database& db) {
    if (!is_fully_bound()) {
        throw std::runtime_error("Tous les paramètres ne sont pas liés ("
                                  + std::to_string(bound_count()) + "/"
                                  + std::to_string(parameter_count_) + ")");
    }

    std::vector<Value> values;
    values.reserve(parameter_count_);
    for (const auto& p : params_) values.push_back(*p);

    Statement final_stmt = substitute_statement(statement_, values);
    return db.execute_statement(final_stmt);
}

QueryResult PreparedStatement::execute_with(Database& db, const std::vector<Value>& values) {
    if (values.size() != parameter_count_)
        throw std::runtime_error("Nombre de valeurs incorrect: "
                                  + std::to_string(values.size()) + " au lieu de "
                                  + std::to_string(parameter_count_));
    Statement final_stmt = substitute_statement(statement_, values);
    return db.execute_statement(final_stmt);
}

// ---------- Database ----------

Database::Database(const std::filesystem::path& directory,
                    DatabaseOptions opts)
    : lock_(directory),
      journal_(directory),
      catalog_(directory),
      storage_(directory, opts.page_size),
      index_manager_(directory, opts.page_size),   // ← AJOUT 1
      directory_(directory),
      options_(opts) {
    validate_page_size(opts.page_size);
    std::filesystem::create_directories(directory);
    catalog_.load();

    // J3.2 : recovery — annuler une transaction non commitée
    auto pending = journal_.read_pending(catalog_);
    if (!pending.empty()) {
        for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
            const auto& schema = catalog_.table(it->table);
            storage_.rewrite(schema, it->rows_before);
        }
        journal_.clear();
    }

    // ← AJOUT 2 : ici, à la fin du constructeur
    // J5.4-D : charger/créer les index de toutes les tables
    for (const auto& schema : catalog_.all_tables()) {
        index_manager_.ensure_indexes(schema, storage_);
    }
}


// ---------- Transactions ----------

void Database::check_executable(bool is_rollback) const {
    if (tx_state_ == TxState::Aborted && !is_rollback) {
        throw std::runtime_error("Transaction en erreur (aborted) : seul ROLLBACK est autorisé");
    }
}

void Database::begin() {
    if (tx_state_ != TxState::None)
        throw std::runtime_error("Transaction déjà en cours (BEGIN imbriqué non supporté)");
    tx_state_ = TxState::Active;
    undo_log_.clear();
    journal_.begin();
}

void Database::commit() {
    if (tx_state_ == TxState::None)
        throw std::runtime_error("Aucune transaction en cours (COMMIT sans BEGIN)");
    if (tx_state_ == TxState::Aborted) {
        throw std::runtime_error("Transaction en erreur (aborted) : ROLLBACK requis, COMMIT interdit");
    }
    journal_.commit();
    tx_state_ = TxState::None;
    undo_log_.clear();
}

void Database::rollback() {
    if (tx_state_ == TxState::None)
        throw std::runtime_error("Aucune transaction en cours (ROLLBACK sans BEGIN)");

    for (auto it = undo_log_.rbegin(); it != undo_log_.rend(); ++it) {
        const auto& schema = catalog_.table(it->table);
        storage_.rewrite(schema, it->rows_before);
        index_manager_.rebuild(schema, storage_);   // ← AJOUT
    }

    undo_log_.clear();
    journal_.clear();
    tx_state_ = TxState::None;
}

void Database::capture_before(const std::string& table_name) {
    const auto& schema = catalog_.table(table_name);
    auto rows = storage_.scan(schema);
    journal_.record_table(table_name, schema, rows);
    undo_log_.push_back({table_name, std::move(rows)});
}

// ---------- Exécution ----------

QueryResult Database::execute(const std::string& sql) {
    auto top = Parser{}.parse(sql);
    return execute_top_level(top);
}

QueryResult Database::execute_top_level(const TopLevelStatement& top) {
    return std::visit([this](const auto& s) -> QueryResult {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, CreateDatabaseStatement> ||
                      std::is_same_v<T, DropDatabaseStatement> ||
                      std::is_same_v<T, UseStatement> ||
                      std::is_same_v<T, ShowDatabasesStatement>) {
            throw std::runtime_error(
                "Commande multi-base exécutée sur une Database (bug interne)");
        }

        else if constexpr (std::is_same_v<T, PrepareStatement>) {
            check_executable(false);
            if (named_prepared_.count(s.name))
                throw std::runtime_error("Prepared statement déjà existant: " + s.name);

            PreparedStatement prep;
            prep.statement_ = *s.inner;
            prep.parameter_count_ = s.parameter_count;
            prep.params_.resize(prep.parameter_count_);
            named_prepared_.emplace(s.name, std::move(prep));

            return {{}, {}, "Statement préparé: " + s.name + " (" +
                              std::to_string(s.parameter_count) + " paramètre(s))"};
        }

        else if constexpr (std::is_same_v<T, ExecuteStatement>) {
            check_executable(false);
            auto it = named_prepared_.find(s.name);
            if (it == named_prepared_.end())
                throw std::runtime_error("Prepared statement inconnu: " + s.name);

            auto& prep = it->second;

            if (s.values.size() != prep.parameter_count()) {
                throw std::runtime_error("EXECUTE " + s.name + " attend "
                                          + std::to_string(prep.parameter_count())
                                          + " valeur(s), " + std::to_string(s.values.size())
                                          + " fournie(s)");
            }

            return prep.execute_with(*this, s.values);
        }

        else if constexpr (std::is_same_v<T, DeallocateStatement>) {
            check_executable(false);
            auto it = named_prepared_.find(s.name);
            if (it == named_prepared_.end())
                throw std::runtime_error("Prepared statement inconnu: " + s.name);

            named_prepared_.erase(it);
            return {{}, {}, "Statement libéré: " + s.name};
        }

        else if constexpr (std::is_same_v<T, BeginStatement>) {
            begin();
            return {{}, {}, "Transaction démarrée"};
        }

        else if constexpr (std::is_same_v<T, CommitStatement>) {
            commit();
            return {{}, {}, "Transaction validée"};
        }

        else if constexpr (std::is_same_v<T, RollbackStatement>) {
            rollback();
            return {{}, {}, "Transaction annulée"};
        }

        else {
            check_executable(false);

            try {
                Statement stmt = s;
                return execute_statement(stmt);
            } catch (...) {
                if (tx_state_ == TxState::Active) {
                    tx_state_ = TxState::Aborted;
                }
                throw;
            }
        }
    }, top);
}

PreparedStatement Database::prepare(const std::string& sql) {
    Parser p;
    Statement stmt = p.parse_statement_only(sql);

    PreparedStatement prep;
    prep.statement_ = std::move(stmt);
    prep.parameter_count_ = p.parameter_count();
    prep.params_.resize(prep.parameter_count_);
    return prep;
}

std::vector<std::string> Database::table_names() const {
    return catalog_.table_names();
}

std::vector<TableSchema> Database::all_schemas() const {
    return catalog_.all_tables();
}

bool Database::has_table(const std::string& name) const {
    return catalog_.contains(name);
}

const TableSchema& Database::schema_of(const std::string& name) const {
    return catalog_.table(name);
}

std::filesystem::path Database::data_directory() const {
    return directory_;
}

QueryResult Database::execute_statement(const Statement& statement) {
    check_executable(false);

    try {
        QueryResult result = std::visit([this](const auto& stmt) -> QueryResult {
            using T = std::decay_t<decltype(stmt)>;

    if constexpr (std::is_same_v<T, CreateTableStatement>) {
    if (tx_state_ != TxState::None) {
        throw std::runtime_error(
            "DDL (CREATE/DROP/ALTER TABLE) interdit dans une transaction "
            "(utilisez COMMIT ou ROLLBACK d'abord)");
    }
    validate_schema(stmt.schema);
    catalog_.add_table(stmt.schema);
    storage_.create_table(stmt.schema);
    catalog_.save();
    index_manager_.ensure_indexes(stmt.schema, storage_);
    return {{}, {}, "Table " + stmt.schema.name + " créée"};
}

else if constexpr (std::is_same_v<T, InsertStatement>) {
    const auto& schema = catalog_.table(stmt.table);
    for (const auto& v : stmt.values) assert_no_parameters(v);

    // A02 : normaliser la nouvelle ligne AVANT validation
    Row new_row = normalize_row(Row{stmt.values}, schema);

    // J5.4-D : NOT NULL (O(1))
    validate_not_null(schema, new_row);

    // J5.4-D : PK / UNIQUE via index (O(log n))
    if (!index_manager_.can_insert_pk(schema, new_row)) {
        throw std::runtime_error(
            "PRIMARY KEY violée sur la table " + stmt.table);
    }
    if (!index_manager_.can_insert_unique(schema, new_row)) {
        throw std::runtime_error(
            "Contrainte UNIQUE violée sur la table " + stmt.table);
    }

    if (tx_state_ == TxState::Active) {
        capture_before(stmt.table);
    }

    // J5.4-D : insert dans la table, puis patch de l'index.
    // Si on_insert échoue (bug interne — can_insert_* a validé),
    // la ligne reste dans la table mais pas dans l'index.
    RowId rid = storage_.insert(schema, new_row);
    index_manager_.on_insert(schema, new_row, rid);

    return {{}, {}, "1 ligne insérée"};
}
else if constexpr (std::is_same_v<T, UpdateStatement>) {
    const auto& schema = catalog_.table(stmt.table);

    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < stmt.columns.size(); ++i) {
        auto ci = column_index(schema, stmt.columns[i]);
        idx.push_back(ci);
    }

    if (stmt.where) {
        assert_no_parameters_in_where(*stmt.where);
        validate_where(schema, *stmt.where);
    }

    if (tx_state_ == TxState::Active) {
        capture_before(stmt.table);
    }

    // J5.4-D : scan avec RowId, update_by_id + patch index
    auto rows = storage_.scan_with_ids(schema);
    std::size_t modified = 0;

    for (const auto& rw : rows) {
        if (stmt.where && !eval_where(rw.row, schema, *stmt.where))
            continue;

        Row new_row = rw.row;
        for (std::size_t i = 0; i < idx.size(); ++i)
            new_row.values[idx[i]] = stmt.values[i];

        new_row = normalize_row(new_row, schema);

        // NOT NULL (O(1))
        validate_not_null(schema, new_row);

        // PK / UNIQUE via index (O(log n)), en excluant la ligne courante
        if (!index_manager_.can_update_pk(schema, new_row, rw.rid)) {
            throw std::runtime_error(
                "PRIMARY KEY violée sur la table " + stmt.table);
        }
        if (!index_manager_.can_update_unique(schema, new_row, rw.rid)) {
            throw std::runtime_error(
                "Contrainte UNIQUE violée sur la table " + stmt.table);
        }

        // update_by_id : peut changer le RowId si la taille augmente
        RowId new_rid = storage_.update_by_id(schema, rw.rid, new_row);

        // Patch de l'index
        index_manager_.on_update(schema, rw.row, rw.rid, new_row, new_rid);

        ++modified;
    }

    return {{}, {}, std::to_string(modified) + " ligne(s) mise(s) à jour"};
}
        else if constexpr (std::is_same_v<T, DeleteStatement>) {
            const auto& schema = catalog_.table(stmt.table);

            if (stmt.where) {
                assert_no_parameters_in_where(*stmt.where);
                validate_where(schema, *stmt.where);
            }

            if (tx_state_ == TxState::Active) {
                capture_before(stmt.table);
            }

            // J5.4-D : scan avec RowId, delete_by_id + patch index
            auto rows = storage_.scan_with_ids(schema);
            std::size_t deleted = 0;

            for (const auto& rw : rows) {
                if (stmt.where && !eval_where(rw.row, schema, *stmt.where))
                    continue;

                storage_.delete_by_id(schema, rw.rid);
                index_manager_.on_delete(schema, rw.row);
                ++deleted;
            }

            return {{}, {}, std::to_string(deleted) + " ligne(s) supprimée(s)"};
        }
       else if constexpr (std::is_same_v<T, DropTableStatement>) {
         if (tx_state_ != TxState::None) {
           throw std::runtime_error(
              "DDL (CREATE/DROP/ALTER TABLE) interdit dans une transaction "
              "(utilisez COMMIT ou ROLLBACK d'abord)");
         }
        if (!catalog_.contains(stmt.table))
          throw std::runtime_error("Table inconnue: " + stmt.table);
            index_manager_.drop_indexes(stmt.table);   // J5.4-D
            storage_.drop_table(stmt.table);
            catalog_.remove_table(stmt.table);
            catalog_.save();
            return {{}, {}, "Table " + stmt.table + " supprimée"};
        }

       else if constexpr (std::is_same_v<T, AlterTableStatement>) {
    if (tx_state_ != TxState::None) {
        throw std::runtime_error(
            "DDL (CREATE/DROP/ALTER TABLE) interdit dans une transaction "
            "(utilisez COMMIT ou ROLLBACK d'abord)");
    }
    if (!catalog_.contains(stmt.table))
        throw std::runtime_error("Table inconnue: " + stmt.table);
            TableSchema old_schema = catalog_.table(stmt.table);
            for (const auto& c : old_schema.columns) {
                if (c.name == stmt.new_column.name)
                    throw std::runtime_error("Colonne déjà existante: " + stmt.new_column.name);
            }

            TableSchema new_schema = old_schema;
            new_schema.columns.push_back(stmt.new_column);
            validate_schema(new_schema);

            auto rows = storage_.scan(old_schema);

            // F07 : NOT NULL sur table non vide
            // (depuis J5.4-A, `new_column` n'a plus is_primary_key/is_unique)
            if (stmt.new_column.is_not_null && !rows.empty()) {
                throw std::runtime_error(
                    "ALTER TABLE: impossible d'ajouter une colonne NOT NULL "
                    "sur une table non vide sans valeur DEFAULT");
            }

            Value def = default_value_for(stmt.new_column);
            for (auto& row : rows) row.values.push_back(def);

            storage_.rewrite(new_schema, rows);

            catalog_.remove_table(stmt.table);
            catalog_.add_table(new_schema);
            catalog_.save();

            return {{}, {}, "Colonne " + stmt.new_column.name + " ajoutée à " + stmt.table};
        }

        else {
            const auto& schema = catalog_.table(stmt.table);

            if (stmt.where) {
                assert_no_parameters_in_where(*stmt.where);
                validate_where(schema, *stmt.where);
            }

            std::vector<Row> source;
            bool used_index = false;
            if (stmt.where) {
                const auto lookup = index_manager_.lookup_equal(schema, *stmt.where);
                used_index = lookup.used_index;
                if (lookup.used_index && lookup.rid.has_value()) {
                    auto row = storage_.read_by_id(schema, *lookup.rid);
                    if (!row.has_value()) {
                        throw std::runtime_error(
                            "Index incoherent : RowId absent de la table " + stmt.table);
                    }
                    source.push_back(std::move(*row));
                }
            }
            if (!used_index) source = storage_.scan(schema);

            if (stmt.having) assert_no_parameters_in_where(*stmt.having);

            std::vector<Row> filtered;
            filtered.reserve(source.size());
            for (auto& row : source) {
                if (stmt.where && !eval_where(row, schema, *stmt.where))
                    continue;
                filtered.push_back(std::move(row));
            }

            if (!stmt.group_by.empty()) {
                validate_group_by(schema, stmt);

                std::vector<std::size_t> group_idx;
                for (const auto& col : stmt.group_by)
                    group_idx.push_back(column_index(schema, col));

                std::map<std::string, std::vector<Row>> groups;
                for (auto& row : filtered) {
                    auto key = group_key(row, group_idx);
                    groups[key].push_back(std::move(row));
                }

                QueryResult result;

                std::vector<std::string> out_col_names;
                std::vector<OutItem> out_items;
                build_out_items(stmt, schema, group_idx, out_col_names, out_items);

                result.columns = out_col_names;

                std::vector<std::size_t> extra_group_cols;
                for (std::size_t gi = 0; gi < group_idx.size(); ++gi) {
                    std::size_t ci = group_idx[gi];
                    bool already = false;
                    for (const auto& item : out_items) {
                        if (item.kind == OutItem::Kind::Column && item.col_idx == ci) {
                            already = true;
                            break;
                        }
                    }
                    if (!already) extra_group_cols.push_back(ci);
                }
                for (auto& [key, rows] : groups) {
                    Row out;
                    for (const auto& item : out_items) {
                        if (item.kind == OutItem::Kind::Column) {
                            out.values.push_back(rows[0].values[item.col_idx]);
                        } else {
                            out.values.push_back(
                                compute_aggregate(item.aggregate, schema, rows));
                        }
                    }
                    for (auto ci : extra_group_cols) {
                        out.values.push_back(rows[0].values[ci]);
                    }
                    if (stmt.having) {
                        Row group_row;
                        for (auto ci : group_idx)
                            group_row.values.push_back(rows[0].values[ci]);

                        // J5.4-A : construction explicite du schéma virtuel
                        TableSchema virt_schema;
                        virt_schema.name = stmt.table;
                        for (auto ci : group_idx)
                            virt_schema.columns.push_back(schema.columns[ci]);

                        if (!eval_where(group_row, virt_schema, *stmt.having))
                            continue;
                    }

                    result.rows.push_back(std::move(out));
                }

                if (stmt.distinct) {
                    std::unordered_set<std::string> seen;
                    std::vector<Row> unique;
                    unique.reserve(result.rows.size());
                    for (auto& r : result.rows) {
                        Row projected_only;
                        projected_only.values.reserve(out_items.size());
                        for (std::size_t k = 0; k < out_items.size(); ++k) {
                            projected_only.values.push_back(r.values[k]);
                        }
                        auto key = row_key(projected_only);
                        if (seen.insert(key).second)
                            unique.push_back(std::move(r));
                    }
                    result.rows = std::move(unique);
                }
                if (!stmt.order_by.empty()) {
                    std::vector<std::pair<std::size_t, OrderDirection>> keys;
                    for (const auto& ob : stmt.order_by) {
                        bool found = false;
                        for (std::size_t i = 0; i < out_col_names.size(); ++i) {
                            if (out_col_names[i] == ob.column) {
                                keys.emplace_back(i, ob.direction);
                                found = true;
                                break;
                            }
                        }
                    if (!found) {
                        for (std::size_t i = 0; i < out_items.size(); ++i) {
                            if (out_items[i].kind == OutItem::Kind::Aggregate &&
                                aggregate_name(out_items[i].aggregate) == ob.column) {
                                keys.emplace_back(i, ob.direction);
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        for (std::size_t i = 0; i < stmt.group_by.size(); ++i) {
                            if (stmt.group_by[i] == ob.column) {
                                std::size_t pos = out_items.size();
                                for (std::size_t j = 0; j < extra_group_cols.size(); ++j) {
                                    if (extra_group_cols[j] == group_idx[i]) {
                                        pos = out_items.size() + j;
                                        break;
                                    }
                                }
                                keys.emplace_back(pos, ob.direction);
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found)
                        throw std::runtime_error(
                            "ORDER BY sur colonne inconnue dans GROUP BY : " + ob.column);
                    }

                    std::sort(result.rows.begin(), result.rows.end(),
                        [&](const Row& a, const Row& b) {
                            for (auto [ci, dir] : keys) {
                                int c = compare(a.values[ci], b.values[ci]);
                                if (c != 0)
                                    return dir == OrderDirection::Asc ? c < 0 : c > 0;
                            }
                            return false;
                        });
                }
                if (!extra_group_cols.empty()) {
                    for (auto& r : result.rows) {
                        r.values.resize(out_items.size());
                    }
                }
                if (stmt.limit && result.rows.size() > *stmt.limit)
                    result.rows.resize(*stmt.limit);

                result.message = std::to_string(result.rows.size()) + " ligne(s)";
                return result;
            }

            bool has_aggregate = false;
            bool has_column = false;
            std::string first_column;
            for (const auto& e : stmt.exprs) {
                if (e.kind == SelectExpr::Kind::Aggregate) has_aggregate = true;
                else {
                    has_column = true;
                    if (first_column.empty()) first_column = e.column;
                }
            }

            if (has_aggregate) {
                if (has_column && stmt.group_by.empty()) {
                    throw std::runtime_error(
                        "SELECT: la colonne '" + first_column +
                        "' doit être dans GROUP BY ou dans un agrégat");
                }

                if (!stmt.order_by.empty()) {
                    throw std::runtime_error(
                        "ORDER BY avec agrégat sans GROUP BY n'est pas supporté");
                }
                QueryResult result;
                Row r;
                for (const auto& e : stmt.exprs) {
                    if (e.kind != SelectExpr::Kind::Aggregate) continue;
                    result.columns.push_back(aggregate_name(e.aggregate));
                    r.values.push_back(compute_aggregate(e.aggregate, schema, filtered));
                }
                result.rows.push_back(std::move(r));

                if (stmt.limit && result.rows.size() > *stmt.limit)
                    result.rows.resize(*stmt.limit);

                result.message = std::to_string(result.rows.size()) + " ligne(s)";
                return result;
            }

            std::vector<std::size_t> selected;
            if (stmt.select_all)
                for (std::size_t i = 0; i < schema.columns.size(); ++i)
                    selected.push_back(i);
            else
                for (const auto& e : stmt.exprs) {
                    if (e.kind != SelectExpr::Kind::Column) continue;
                    selected.push_back(column_index(schema, e.column));
                }

            if (stmt.having && stmt.group_by.empty()) {
                throw std::runtime_error(
                    "HAVING sans GROUP BY n'est pas supporté");
            }

            if (stmt.select_all && !stmt.exprs.empty()) {
                throw std::runtime_error(
                    "SELECT *, colonnes n'est pas supporté");
            }
            if (!stmt.order_by.empty()) {
                std::vector<std::pair<std::size_t, OrderDirection>> keys;
                for (const auto& ob : stmt.order_by)
                    keys.emplace_back(column_index(schema, ob.column), ob.direction);

                std::sort(filtered.begin(), filtered.end(),
                    [&](const Row& a, const Row& b) {
                        for (auto [ci, dir] : keys) {
                            int c = compare(a.values[ci], b.values[ci]);
                            if (c != 0)
                                return dir == OrderDirection::Asc ? c < 0 : c > 0;
                        }
                        return false;
                    });
            }

            std::vector<Row> projected;
            projected.reserve(filtered.size());
            for (auto& row : filtered) {
                Row p;
                for (auto i : selected) p.values.push_back(row.values[i]);
                projected.push_back(std::move(p));
            }

            if (stmt.distinct) {
                std::unordered_set<std::string> seen;
                std::vector<Row> unique;
                unique.reserve(projected.size());
                for (auto& r : projected) {
                    auto key = row_key(r);
                    if (seen.insert(key).second)
                        unique.push_back(std::move(r));
                }
                projected = std::move(unique);
            }

            if (stmt.limit && projected.size() > *stmt.limit)
                projected.resize(*stmt.limit);

            QueryResult result;
            for (auto i : selected) result.columns.push_back(schema.columns[i].name);
            result.rows = std::move(projected);
            result.message = std::to_string(result.rows.size()) + " ligne(s)";
            return result;
        }
        }, statement);

        return result;
    } catch (...) {
        if (tx_state_ == TxState::Active) {
            tx_state_ = TxState::Aborted;
        }
        throw;
    }
}
} // namespace kerneldb
