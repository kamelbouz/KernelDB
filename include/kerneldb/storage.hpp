#pragma once
#include "kerneldb/page.hpp"
#include "kerneldb/types.hpp"
#include "kerneldb/buffer_pool.hpp"
#include <filesystem>
#include <istream>
#include <ostream>
#include <optional>
#include <vector>

namespace kerneldb {

// J3.2 : sérialisation/désérialisation d'une ligne (exposé pour le journal)
std::size_t serialize_row(std::ostream& out,
                           const TableSchema& schema,
                           const Row& row);

Row deserialize_row(std::istream& in, const TableSchema& schema);
// J4.5 : helpers pour la sérialisation en mémoire (pages)
std::vector<std::byte> serialize_row_to_bytes(const TableSchema& schema,
                                               const Row& row);
Row deserialize_row_from_bytes(const std::vector<std::byte>& bytes,
                                const TableSchema& schema);
struct RowWithId {
    RowId rid;
    Row    row;
};

class StorageManager {
public:
    explicit StorageManager(std::filesystem::path directory,
                            std::size_t default_page_size = PAGE_SIZE_DEFAULT);
    // J4.5 : flush le cache à la destruction (best-effort, no-throw)
    ~StorageManager() {
        try {
            buffer_pool_.flush_all();
        } catch (...) {
            // Ignorer les erreurs à la destruction
        }
    }
    // ---------- Création / suppression ----------

    void create_table(const TableSchema& schema,
                      std::size_t page_size = 0) const;

    void drop_table(const std::string& name) const;

    // ---------- Informations ----------

    std::size_t page_size_of(const std::string& name) const;
    std::uint32_t page_count(const std::string& name) const;

    // ---------- Bas niveau : pages ----------

    Page read_page(const std::string& table, std::uint32_t page_id) const;
    void write_page(const std::string& table, const Page& page) const;

    // ---------- Haut niveau (compatibilité) ----------

    std::vector<Row> scan(const TableSchema& schema) const;
    std::vector<RowWithId> scan_with_ids(const TableSchema& schema) const;
    // Lecture ponctuelle via le cache ; nullopt si page/slot absent.
    std::optional<Row> read_by_id(const TableSchema& schema,
                                  const RowId& rid) const;
    void delete_by_id(const TableSchema& schema, const RowId& rid) const;
    RowId update_by_id(const TableSchema& schema,
                       const RowId& old_rid,
                       const Row& new_row) const;
    RowId insert(const TableSchema& schema, const Row& row) const;
    void rewrite(const TableSchema& schema,
                 const std::vector<Row>& rows) const;

    // ---------- J4.4 : flush explicite du cache ----------
    void flush_all() const { buffer_pool_.flush_all(); }

private:
    std::filesystem::path table_path(const std::string& name) const;

    // J4.4 : helpers disque (utilisés par le BufferPool)
    Page read_page_from_disk(const std::string& table,
                              std::uint32_t page_id) const;
    void write_page_to_disk(const std::string& table,
                             const Page& page) const;

    std::filesystem::path directory_;
    std::size_t default_page_size_;

    // J4.4 : cache global (toutes tables)
    mutable BufferPool buffer_pool_;
};

} // namespace kerneldb
