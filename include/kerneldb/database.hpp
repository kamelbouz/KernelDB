#pragma once
#include "kerneldb/catalog.hpp"
#include "kerneldb/lock.hpp"
#include "kerneldb/journal.hpp"
#include "kerneldb/page.hpp"
#include "kerneldb/parser.hpp"
#include "kerneldb/storage.hpp"
#include "kerneldb/index_manager.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kerneldb {

struct QueryResult {
    std::vector<std::string> columns;
    std::vector<Row> rows;
    std::string message;
};

// Options d'une base
struct DatabaseOptions {
    std::size_t page_size = PAGE_SIZE_DEFAULT;   // 8 KB par défaut
};

class Database;

class PreparedStatement {
public:
    std::size_t parameter_count() const { return parameter_count_; }
    std::size_t bound_count() const;
    bool is_fully_bound() const { return bound_count() == parameter_count_; }

    void bind_null(std::size_t index);
    void bind_int(std::size_t index, std::int64_t value);
    void bind_double(std::size_t index, double value);
    void bind_bool(std::size_t index, bool value);
    void bind_string(std::size_t index, std::string value);
    void bind_value(std::size_t index, Value value);

    void reset();

    QueryResult execute(Database& db);
    QueryResult execute_with(Database& db, const std::vector<Value>& values);

private:
    friend class Database;

    PreparedStatement() = default;

    Statement statement_;
    std::size_t parameter_count_ = 0;
    std::vector<std::optional<Value>> params_;
};

class Database {
public:
    enum class TxState { None, Active, Aborted };

    struct UndoEntry {
        std::string table;
        std::vector<Row> rows_before;
    };

    explicit Database(const std::filesystem::path& directory,
                      DatabaseOptions opts = {});

    QueryResult execute(const std::string& sql);
    QueryResult execute_statement(const Statement& statement);
    QueryResult execute_top_level(const TopLevelStatement& stmt);

    PreparedStatement prepare(const std::string& sql);

    // Transactions
    void begin();
    void commit();
    void rollback();
    bool in_transaction() const { return tx_state_ == TxState::Active; }
    TxState tx_state() const { return tx_state_; }

    // Accès en lecture au catalogue
    std::vector<std::string> table_names() const;
    std::vector<TableSchema> all_schemas() const;
    bool has_table(const std::string& name) const;
    const TableSchema& schema_of(const std::string& name) const;
    std::filesystem::path data_directory() const;

    const DatabaseOptions& options() const { return options_; }
    // J5.4-E : accès à l'IndexManager (pour le REPL)
    const IndexManager& index_manager() const { return index_manager_; }
private:
    // J3.1 : verrou exclusif (mono-connexion)
    DatabaseLock lock_;
    // J3.2 : journal d'annulation persistant
    Journal journal_;
    Catalog catalog_;
    StorageManager storage_;
    IndexManager   index_manager_;   // J5.4-D
    std::filesystem::path directory_;
    DatabaseOptions options_;

    std::unordered_map<std::string, PreparedStatement> named_prepared_;

    TxState tx_state_ = TxState::None;
    std::vector<UndoEntry> undo_log_;

    void check_executable(bool is_rollback) const;
 // J3.2 : capture l'état d'une table avant modification (mémoire + journal)
    void capture_before(const std::string& table_name);
};

} // namespace kerneldb