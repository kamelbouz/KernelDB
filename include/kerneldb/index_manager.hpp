#pragma once
#include "kerneldb/bplus_tree.hpp"
#include "kerneldb/types.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kerneldb {

class StorageManager;

// Distingue le repli vers le scan d'une recherche indexée sans résultat.
struct IndexLookupResult {
    bool used_index = false;
    std::optional<RowId> rid;
};

// ---------------------------------------------------------------------------
// J5.4-C/D : IndexManager
//
// Gère les index B+ tree associés aux contraintes d'une table :
//   - 1 PRIMARY KEY  → 1 index sur ses colonnes
//   - N UNIQUE       → N index, un par contrainte
//
// Fichiers :
//   <table>.idx               → PK (si elle existe)
//   <table>.uq_<nom>.idx      → chaque UNIQUE
//
// Sémantique Oracle :
//   - PK  : toutes les colonnes NOT NULL, toutes les lignes indexées
//   - UNIQUE : si TOUTES les colonnes de la contrainte sont NULL
//              → la ligne n'est pas indexée
//              sinon, on indexe (avec NULL dans la clé si partiel)
//              → l'unicité est vérifiée par can_insert_unique / can_update_unique
//
// Encodage des clés composites (Décision 3 = C) :
//   [type:u8][len:u16][data] × N, concaténés
//   Pour Integer : len=8, data = int64 big-endian
//   Pour Varchar : len=str.size(), data = str
//   NULL : [0xFF][0x00]
// ---------------------------------------------------------------------------

class IndexManager {
public:
    IndexManager(std::filesystem::path directory,
                 std::size_t page_size);

    // ---------- Gestion du cycle de vie ----------

    void ensure_indexes(const TableSchema& schema,
                        const StorageManager& storage);

    void drop_indexes(const std::string& table);

    void rebuild(const TableSchema& schema,
                 const StorageManager& storage);

    // ---------- Accès ----------

    BPlusTree* pk_index(const std::string& table);
    const BPlusTree* pk_index(const std::string& table) const;

    BPlusTree* unique_index(const std::string& table,
                            const std::string& constraint_name);
    const BPlusTree* unique_index(const std::string& table,
                                  const std::string& constraint_name) const;

    bool has_indexes(const std::string& table) const;

    std::vector<std::string> index_names(const std::string& table) const;

    // J5.5 : égalités non NULL sur une PK/UNIQUE complète, sans OR.
    // Les autres prédicats seront réévalués par Database après lecture.
    IndexLookupResult lookup_equal(const TableSchema& schema,
                                    const WhereExpr& where) const;

    // ---------- Encodage ----------

    static IndexKey encode_key(const TableSchema& schema,
                                const std::vector<std::string>& columns,
                                const Row& row);

    static bool row_all_null(const TableSchema& schema,
                              const std::vector<std::string>& columns,
                              const Row& row);
    // J5.4-D (Oracle strict) : vrai si AU MOINS une des colonnes est NULL.
    // Les contraintes UNIQUE ignorent les lignes avec au moins une NULL.
    static bool row_any_null(const TableSchema& schema,
                              const std::vector<std::string>& columns,
                              const Row& row);   
    // ---------- Vérifications (avant insert/update) ----------

    // Vérifie qu'une ligne est insérable sans violer PK ni UNIQUE.
    // Retourne true si OK (pas de conflit).
    bool can_insert_pk(const TableSchema& schema, const Row& row) const;
    bool can_insert_unique(const TableSchema& schema, const Row& row) const;

    // Vérifie qu'un UPDATE ne crée pas de conflit.
    // `exclude_rid` = RowId de la ligne qu'on modifie (ignoré dans la recherche).
    bool can_update_pk(const TableSchema& schema,
                        const Row& new_row,
                        const RowId& exclude_rid) const;
    bool can_update_unique(const TableSchema& schema,
                            const Row& new_row,
                            const RowId& exclude_rid) const;

    // ---------- Maintenance unitaire ----------

    // Insère (pk_value, rid) pour la PK et (uq_value, rid) pour chaque UNIQUE.
    // Ne vérifie PAS l'unicité — c'est au caller de le faire via can_*.
    void on_insert(const TableSchema& schema, const Row& row, const RowId& rid);

    // Supprime (pk_value, rid) pour la PK et (uq_value, rid) pour chaque UNIQUE.
    // No-op silencieux si la clé n'est pas dans l'index.
    void on_delete(const TableSchema& schema, const Row& old_row);

    // Patche l'index si la PK/UNIQUE ou le RowId a changé.
    void on_update(const TableSchema& schema,
                   const Row& old_row, const RowId& old_rid,
                   const Row& new_row, const RowId& new_rid);

private:
    std::filesystem::path dir_;
    std::size_t page_size_;

    // Clé : "<table>" pour la PK, "<table>.<constraint_name>" pour UNIQUE
    std::unordered_map<std::string, std::unique_ptr<BPlusTree>> indexes_;

    std::filesystem::path pk_path(const std::string& table) const;
    std::filesystem::path unique_path(const std::string& table,
                                       const std::string& constraint_name) const;

    std::unique_ptr<BPlusTree>
    load_or_create(const std::filesystem::path& path,
                   const TableSchema& schema,
                   const std::vector<std::string>& columns,
                   const StorageManager& storage,
                   bool is_pk);
};

} // namespace kerneldb
