#pragma once
#include "kerneldb/types.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace kerneldb {

class Catalog {
public:
    explicit Catalog(std::filesystem::path directory);

    void load();
    void save() const;

    void add_table(const TableSchema& schema);
    void remove_table(const std::string& name);
    const TableSchema& table(const std::string& name) const;
    bool contains(const std::string& name) const;

    // NOUVEAU : liste triée alphabétiquement des noms de tables
    std::vector<std::string> table_names() const;

    // NOUVEAU : liste triée par nom des schémas complets
    std::vector<TableSchema> all_tables() const;

    // J5.1 : gestion des index
    void add_index(const std::string& table, const IndexDef& index);
    void remove_index(const std::string& table, const std::string& index_name);
    const std::vector<IndexDef>& indexes_of(const std::string& table) const;
private:
    std::filesystem::path directory_;
    std::unordered_map<std::string, TableSchema> tables_;
};

} // namespace kerneldb