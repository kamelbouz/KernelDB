#pragma once
#include "kerneldb/types.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <cstddef>
namespace kerneldb {

// J5.2 : clé d'index (INTEGER ou VARCHAR)
struct IndexKey {
    enum class Type { Integer, Varchar };
    Type type = Type::Integer;
    std::int64_t int_val = 0;
    std::string str_val;

    bool operator<(const IndexKey& other) const;
    bool operator==(const IndexKey& other) const;

    // Conversion Value <-> IndexKey
    static IndexKey from_value(const Value& v);
    Value to_value() const;

    // Taille sérialisée (en octets)
    std::size_t serialized_size() const;
};


// J5.2 : en-tête d'un nœud B+tree (16 octets)
//
// Layout d'une page :
//   offset 0  : page_id (u32)
//   offset 4  : is_leaf (u32)      — 1 = feuille, 0 = interne
//   offset 8  : num_keys (u32)
//   offset 12 : next_leaf (u32)    — feuille suivante (0 = dernière)
//   offset 16 : ... données
//
struct BTreeNodeHeader {
    std::uint32_t page_id = 0;
    std::uint32_t is_leaf = 0;
    std::uint32_t num_keys = 0;
    std::uint32_t next_leaf = 0;
};

constexpr std::size_t BP_NODE_HEADER_SIZE = 16;
constexpr std::size_t BP_MAX_KEY_SIZE     = 128;

// J5.3b : capacité du slot array (u16 offsets) = nombre max de clés
// APRÈS stabilisation. On dimensionne le slot array à BP_NODE_SLOTS.
constexpr std::uint32_t BP_NODE_SLOTS = 256;
constexpr std::uint32_t BP_MAX_KEYS   = 64;   // marge pour VARCHAR(64)

// J5.3b : offset du root_page_id dans le header de fichier (champ reserved)
constexpr std::size_t BP_FILE_ROOT_PAGE_OFFSET = 16;

struct SplitResult {
    IndexKey      median_key;
    std::uint32_t new_page_id;
};

// J5.3 : helpers de layout
struct NodeLayout {
    std::uint32_t max_keys;
    std::size_t keys_offset;
    std::size_t children_offset;   // interne : children (u32)
    std::size_t rids_offset;       // feuille : rowids (6 octets)
};

NodeLayout compute_layout(std::size_t page_size, bool is_leaf);
// Sérialisation du header
void write_node_header(std::vector<std::byte>& data,
                        const BTreeNodeHeader& h);
BTreeNodeHeader read_node_header(const std::vector<std::byte>& data);

// J5.2 : B+tree (structure de base)
//
// Pour J5.2, on définit l'API et le layout des pages.
// Les opérations (insert, search) seront implémentées en J5.3/J5.4.
//
class BPlusTree {
public:
    BPlusTree(const std::filesystem::path& idx_path,
              std::size_t page_size);

    // Chemin et taille
    const std::filesystem::path& path() const { return idx_path_; }
    std::size_t page_size() const { return page_size_; }

    // J5.2 : crée le fichier d'index avec une page racine vide
    void create();

    // J5.2 : nombre de pages dans le fichier
    std::uint32_t page_count() const;

    // J5.2 : lit / écrit une page brute
    std::vector<std::byte> read_page(std::uint32_t page_id) const;
    void write_page(std::uint32_t page_id,
                    const std::vector<std::byte>& data);
    // J5.3a : recherche (retourne RowId si trouvé)
    std::optional<RowId> search(const IndexKey& key) const;
    // J5.3b : insertion (avec split automatique)
    void insert(const IndexKey& key, const RowId& rid);
    bool remove(const IndexKey& key);
    // J5.3a : sérialisation d'une clé (format variable)
    static std::size_t write_key(std::vector<std::byte>& data,
                                  std::size_t offset,
                                  const IndexKey& key);
    static IndexKey read_key(const std::vector<std::byte>& data,
                              std::size_t offset);
    static std::size_t key_size(const IndexKey& key);
    std::uint32_t root_page_id() const { return root_page_id_; }

private:
    std::filesystem::path idx_path_;
    std::size_t page_size_;
    std::uint32_t root_page_id_ = 0;
    // J5.2 : header de fichier (24 octets, format MDBP)
    std::uint32_t read_page_count_from_file() const;
    void write_page_count_to_file(std::uint32_t count);

    // J5.3a : accès aux clés / rowids / children d'un nœud
    IndexKey get_key(const std::vector<std::byte>& data,
                      std::uint32_t idx) const;
    RowId get_rid(const std::vector<std::byte>& data,
                   std::uint32_t idx) const;
    std::uint32_t get_child(const std::vector<std::byte>& data,
                             std::uint32_t idx) const;

    // J5.3a : recherche récursive
    std::optional<RowId> search_in_node(std::uint32_t page_id,
                                         const IndexKey& key) const;

    // J5.3b : helpers d'insertion
    std::optional<SplitResult> insert_recursive(std::uint32_t page_id,
                                                 const IndexKey& key,
                                                 const RowId& rid);
    SplitResult split_leaf(std::vector<std::byte>& data);
    SplitResult split_internal(std::vector<std::byte>& data);
    void insert_in_leaf(std::vector<std::byte>& data,
                         const IndexKey& key,
                         const RowId& rid);
    void insert_in_internal(std::vector<std::byte>& data,
                             const IndexKey& key,
                             std::uint32_t right_child);
    std::uint32_t allocate_page();
    void set_key(std::vector<std::byte>& data,
                  std::uint32_t idx,
                  const IndexKey& key);
    void set_rid(std::vector<std::byte>& data,
                  std::uint32_t idx,
                  const RowId& rid);
    void set_child(std::vector<std::byte>& data,
                    std::uint32_t idx,
                    std::uint32_t child);
    void set_num_keys(std::vector<std::byte>& data, std::uint32_t n);
    static constexpr std::uint32_t BP_MIN_KEYS = BP_MAX_KEYS / 2;

    bool remove_recursive(std::uint32_t page_id, const IndexKey& key);
    bool rebalance(std::uint32_t parent_id, std::uint32_t node_idx);
};

} // namespace kerneldb