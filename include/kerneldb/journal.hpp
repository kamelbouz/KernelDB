#pragma once
#include "kerneldb/types.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace kerneldb {

class Catalog;

// J3.2 : journal d'annulation persistant.
//
// Avant chaque modification d'une table, l'état précédent est écrit
// dans `undo.log` et synchronisé sur disque (fsync). À COMMIT, le
// journal est supprimé. Au démarrage de la Database, si le journal
// existe encore, la transaction non commitée est annulée.
//
// Format du fichier `undo.log` :
//   [MAGIC "MDBJ"] [version u32]
//   Pour chaque table enregistrée :
//     [table_name_length u32] [table_name bytes]
//     [row_count u32]
//     Pour chaque ligne : [serialized row]
//
class Journal {
public:
    explicit Journal(const std::filesystem::path& dir);

    // Démarre une transaction : crée le journal et écrit l'en-tête.
    void begin();

    // Enregistre l'état d'une table avant modification (append + fsync).
    void record_table(const std::string& table,
                      const TableSchema& schema,
                      const std::vector<Row>& rows_before);

    // Marque la transaction comme commitée : supprime le journal.
    void commit();

    // Supprime le journal (après rollback réussi).
    void clear();

    // Une transaction est-elle en cours ?
    bool active() const { return active_; }

    // Résultat de la lecture du journal (pour recovery).
    struct PendingEntry {
        std::string table;
        std::vector<Row> rows_before;
    };

    // Lit le journal (s'il existe) et retourne les entrées à annuler.
    // Retourne un vecteur vide si le journal est absent ou vide.
    std::vector<PendingEntry> read_pending(const Catalog& catalog) const;

    const std::filesystem::path& path() const { return journal_path_; }

private:
    std::filesystem::path dir_;
    std::filesystem::path journal_path_;
    bool active_ = false;
};

} // namespace kerneldb