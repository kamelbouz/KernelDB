#include "kerneldb/bplus_tree.hpp"
#include "kerneldb/page.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <variant>
#include <algorithm>
namespace kerneldb {

// ---------- IndexKey ----------

bool IndexKey::operator<(const IndexKey& other) const {
    if (type != other.type) {
        return static_cast<int>(type) < static_cast<int>(other.type);
    }
    if (type == Type::Integer) {
        return int_val < other.int_val;
    }
    return str_val < other.str_val;
}

bool IndexKey::operator==(const IndexKey& other) const {
    if (type != other.type) return false;
    if (type == Type::Integer) return int_val == other.int_val;
    return str_val == other.str_val;
}

IndexKey IndexKey::from_value(const Value& v) {
    IndexKey k;
    if (std::holds_alternative<std::int64_t>(v)) {
        k.type = Type::Integer;
        k.int_val = std::get<std::int64_t>(v);
    } else if (std::holds_alternative<std::string>(v)) {
        k.type = Type::Varchar;
        k.str_val = std::get<std::string>(v);
    } else {
        throw std::runtime_error(
            "IndexKey: type non supporté (INTEGER ou VARCHAR attendu)");
    }
    return k;
}

Value IndexKey::to_value() const {
    if (type == Type::Integer) return int_val;
    return str_val;
}

std::size_t IndexKey::serialized_size() const {
    if (type == Type::Integer) return 1 + 8;
    return 1 + 4 + str_val.size();
}

// ---------- BTreeNodeHeader ----------

void write_node_header(std::vector<std::byte>& data,
                        const BTreeNodeHeader& h) {
    if (data.size() < BP_NODE_HEADER_SIZE) {
        throw std::runtime_error("Data trop petite pour un BTreeNodeHeader");
    }
    std::uint32_t fields[4] = {h.page_id, h.is_leaf, h.num_keys, h.next_leaf};
    for (std::size_t i = 0; i < 4; ++i) {
        std::memcpy(data.data() + i * 4, &fields[i], 4);
    }
}

BTreeNodeHeader read_node_header(const std::vector<std::byte>& data) {
    if (data.size() < BP_NODE_HEADER_SIZE) {
        throw std::runtime_error("Data trop petite pour un BTreeNodeHeader");
    }
    BTreeNodeHeader h;
    std::memcpy(&h.page_id,   data.data(),      4);
    std::memcpy(&h.is_leaf,   data.data() + 4,  4);
    std::memcpy(&h.num_keys,  data.data() + 8,  4);
    std::memcpy(&h.next_leaf, data.data() + 12, 4);
    return h;
}

// ---------- Sérialisation des clés ----------

std::size_t BPlusTree::key_size(const IndexKey& key) {
    if (key.type == IndexKey::Type::Integer) return 1 + 2 + 8;
    return 1 + 2 + key.str_val.size();
}

std::size_t BPlusTree::write_key(std::vector<std::byte>& data,
                                  std::size_t offset,
                                  const IndexKey& key) {
    std::uint8_t  type = (key.type == IndexKey::Type::Integer) ? 0 : 1;
    std::uint16_t size = 0;
    if (key.type == IndexKey::Type::Integer) {
        size = 8;
    } else {
        if (key.str_val.size() > BP_MAX_KEY_SIZE)
            throw std::runtime_error("Clé VARCHAR trop grande");
        size = static_cast<std::uint16_t>(key.str_val.size());
    }
    std::memcpy(data.data() + offset,     &type, 1);
    std::memcpy(data.data() + offset + 1, &size, 2);
    if (key.type == IndexKey::Type::Integer) {
        std::memcpy(data.data() + offset + 3, &key.int_val, 8);
    } else {
        std::memcpy(data.data() + offset + 3, key.str_val.data(), size);
    }
    return 1 + 2 + size;
}

IndexKey BPlusTree::read_key(const std::vector<std::byte>& data,
                              std::size_t offset) {
    std::uint8_t  type;
    std::uint16_t size;
    std::memcpy(&type, data.data() + offset,     1);
    std::memcpy(&size, data.data() + offset + 1, 2);
    IndexKey key;
    if (type == 0) {
        key.type = IndexKey::Type::Integer;
        std::memcpy(&key.int_val, data.data() + offset + 3, 8);
    } else {
        key.type = IndexKey::Type::Varchar;
        key.str_val.resize(size);
        std::memcpy(key.str_val.data(), data.data() + offset + 3, size);
    }
    return key;
}

// ---------- Helpers de layout ----------

namespace {

    inline std::size_t slot_array_offset() {
        return BP_NODE_HEADER_SIZE;
    }
    inline std::size_t data_array_offset() {
        return BP_NODE_HEADER_SIZE + BP_NODE_SLOTS * 2;
    }
    inline std::size_t leaf_keys_offset() {
        return BP_NODE_HEADER_SIZE + BP_NODE_SLOTS * 2 + BP_NODE_SLOTS * 6;
    }
    inline std::size_t internal_keys_offset() {
        return BP_NODE_HEADER_SIZE + BP_NODE_SLOTS * 2 + BP_NODE_SLOTS * 4;
    }

    std::size_t node_bytes(const std::vector<IndexKey>& keys,
                            std::size_t keys_zone_offset) {
        std::size_t off = keys_zone_offset;
        for (const auto& k : keys) {
            off += BPlusTree::key_size(k);
        }
        return off;
    }

    void rebuild_keys(std::vector<std::byte>& data,
                       const std::vector<IndexKey>& keys,
                       std::size_t keys_zone_offset) {
        std::size_t off = keys_zone_offset;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            std::size_t need = BPlusTree::key_size(keys[i]);
            if (off + need > data.size()) {
                throw std::runtime_error(
                    "BPlusTree: nœud trop grand pour la page");
            }
            std::uint16_t slot_off = static_cast<std::uint16_t>(off);
            std::memcpy(data.data() + slot_array_offset() + i * 2,
                        &slot_off, 2);
            off += BPlusTree::write_key(data, off, keys[i]);
        }
    }

    void update_root_page_id_on_disk(const std::filesystem::path& path,
                                      std::uint32_t root_id) {
        std::fstream io(path,
                        std::ios::binary | std::ios::in | std::ios::out);
        if (!io) {
            throw std::runtime_error(
                "Impossible d'ouvrir l'index pour mise à jour du root");
        }
        std::uint64_t v = root_id;
        io.seekp(static_cast<std::streamoff>(BP_FILE_ROOT_PAGE_OFFSET));
        io.write(reinterpret_cast<const char*>(&v), 8);
    }

} // namespace

// ---------- BPlusTree ----------

BPlusTree::BPlusTree(const std::filesystem::path& idx_path,
                      std::size_t page_size)
    : idx_path_(idx_path), page_size_(page_size) {
    validate_page_size(page_size);

    if (!std::filesystem::exists(idx_path_)) return;

    std::ifstream in(idx_path_, std::ios::binary);
    if (!in) return;

    char magic[4];
    in.read(magic, 4);
    if (in.gcount() != 4 || std::memcmp(magic, FILE_MAGIC, 4) != 0) return;

    std::uint32_t version = 0, ps = 0, count = 0;
    std::uint64_t root = 0;
    in.read(reinterpret_cast<char*>(&version), 4);
    in.read(reinterpret_cast<char*>(&ps), 4);
    in.read(reinterpret_cast<char*>(&count), 4);
    in.read(reinterpret_cast<char*>(&root), 8);
    if (!in) return;

    root_page_id_ = static_cast<std::uint32_t>(root);
}

void BPlusTree::create() {
    std::ofstream out(idx_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Impossible de créer le fichier d'index");
    }

    out.write(FILE_MAGIC, 4);
    std::uint32_t version    = 1;
    std::uint32_t page_size  = static_cast<std::uint32_t>(page_size_);
    std::uint32_t page_count = 1;
    std::uint64_t root_id    = 0;
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&page_size), 4);
    out.write(reinterpret_cast<const char*>(&page_count), 4);
    out.write(reinterpret_cast<const char*>(&root_id), 8);

    std::vector<std::byte> root(page_size_, std::byte{0});
    BTreeNodeHeader h;
    h.page_id   = 0;
    h.is_leaf   = 1;
    h.num_keys  = 0;
    h.next_leaf = 0;
    write_node_header(root, h);
    out.write(reinterpret_cast<const char*>(root.data()), page_size_);

    root_page_id_ = 0;
}

std::uint32_t BPlusTree::read_page_count_from_file() const {
    std::ifstream in(idx_path_, std::ios::binary);
    if (!in) throw std::runtime_error("Impossible d'ouvrir l'index");
    char magic[4];
    in.read(magic, 4);
    if (in.gcount() != 4 || std::memcmp(magic, FILE_MAGIC, 4) != 0) {
        throw std::runtime_error("Fichier d'index invalide");
    }
    std::uint32_t version = 0, ps = 0, count = 0;
    in.read(reinterpret_cast<char*>(&version), 4);
    in.read(reinterpret_cast<char*>(&ps), 4);
    in.read(reinterpret_cast<char*>(&count), 4);
    return count;
}

void BPlusTree::write_page_count_to_file(std::uint32_t count) {
    std::fstream io(idx_path_,
                    std::ios::binary | std::ios::in | std::ios::out);
    if (!io) throw std::runtime_error("Impossible d'ouvrir l'index");
    io.seekp(12);
    io.write(reinterpret_cast<const char*>(&count), 4);
}

std::uint32_t BPlusTree::page_count() const {
    return read_page_count_from_file();
}

std::vector<std::byte> BPlusTree::read_page(std::uint32_t page_id) const {
    std::ifstream in(idx_path_, std::ios::binary);
    if (!in) throw std::runtime_error("Impossible d'ouvrir l'index");
    std::size_t offset = FILE_HEADER_SIZE + page_id * page_size_;
    in.seekg(static_cast<std::streamoff>(offset));

    std::vector<std::byte> data(page_size_, std::byte{0});
    in.read(reinterpret_cast<char*>(data.data()), page_size_);
    if (!in) throw std::runtime_error("Lecture de page échouée");
    return data;
}

void BPlusTree::write_page(std::uint32_t page_id,
                            const std::vector<std::byte>& data) {
    if (data.size() != page_size_)
        throw std::runtime_error("Taille de page incorrecte");
    std::fstream io(idx_path_,
                    std::ios::binary | std::ios::in | std::ios::out);
    if (!io) throw std::runtime_error("Impossible d'ouvrir l'index");
    std::size_t offset = FILE_HEADER_SIZE + page_id * page_size_;
    io.seekp(static_cast<std::streamoff>(offset));
    io.write(reinterpret_cast<const char*>(data.data()), page_size_);
    if (!io) throw std::runtime_error("Écriture de page échouée");
}

// ---------- Accès ----------

IndexKey BPlusTree::get_key(const std::vector<std::byte>& data,
                             std::uint32_t idx) const {
    std::uint16_t key_off;
    std::memcpy(&key_off, data.data() + slot_array_offset() + idx * 2, 2);
    return read_key(data, key_off);
}

RowId BPlusTree::get_rid(const std::vector<std::byte>& data,
                          std::uint32_t idx) const {
    std::size_t base = data_array_offset() + idx * 6;
    RowId rid;
    std::memcpy(&rid.page_id, data.data() + base,     4);
    std::memcpy(&rid.slot_id, data.data() + base + 4, 2);
    return rid;
}

std::uint32_t BPlusTree::get_child(const std::vector<std::byte>& data,
                                     std::uint32_t idx) const {
    std::size_t base = data_array_offset() + idx * 4;
    std::uint32_t child;
    std::memcpy(&child, data.data() + base, 4);
    return child;
}

// ---------- Setters ----------

void BPlusTree::set_num_keys(std::vector<std::byte>& data, std::uint32_t n) {
    std::memcpy(data.data() + 8, &n, 4);
}

void BPlusTree::set_rid(std::vector<std::byte>& data,
                         std::uint32_t idx, const RowId& rid) {
    std::size_t base = data_array_offset() + idx * 6;
    std::memcpy(data.data() + base,     &rid.page_id, 4);
    std::memcpy(data.data() + base + 4, &rid.slot_id, 2);
}

void BPlusTree::set_child(std::vector<std::byte>& data,
                           std::uint32_t idx, std::uint32_t child) {
    std::size_t base = data_array_offset() + idx * 4;
    std::memcpy(data.data() + base, &child, 4);
}

void BPlusTree::set_key(std::vector<std::byte>&, std::uint32_t,
                         const IndexKey&) {}

// ---------- Allocation ----------

std::uint32_t BPlusTree::allocate_page() {
    std::uint32_t new_id = read_page_count_from_file();

    {
        std::ofstream out(idx_path_, std::ios::binary | std::ios::app);
        if (!out) throw std::runtime_error("Impossible d'étendre l'index");
        std::vector<std::byte> blank(page_size_, std::byte{0});
        out.write(reinterpret_cast<const char*>(blank.data()), page_size_);
    }

    write_page_count_to_file(new_id + 1);
    return new_id;
}

// ---------- Insertion dans un nœud ----------

void BPlusTree::insert_in_leaf(std::vector<std::byte>& data,
                                const IndexKey& key, const RowId& rid) {
    auto h = read_node_header(data);

    std::vector<IndexKey> keys;
    std::vector<RowId>    rids;
    for (std::uint32_t i = 0; i < h.num_keys; ++i) {
        keys.push_back(get_key(data, i));
        rids.push_back(get_rid(data, i));
    }

    auto it = std::lower_bound(keys.begin(), keys.end(), key,
        [](const IndexKey& a, const IndexKey& b){ return a < b; });
    std::size_t pos = static_cast<std::size_t>(it - keys.begin());
    keys.insert(keys.begin() + pos, key);
    rids.insert(rids.begin() + pos, rid);

    if (node_bytes(keys, leaf_keys_offset()) > page_size_) {
        throw std::runtime_error("BPlusTree: feuille pleine (bytes)");
    }

    for (std::size_t i = 0; i < rids.size(); ++i) set_rid(data, i, rids[i]);
    rebuild_keys(data, keys, leaf_keys_offset());
    set_num_keys(data, static_cast<std::uint32_t>(keys.size()));
}

void BPlusTree::insert_in_internal(std::vector<std::byte>& data,
                                     const IndexKey& key,
                                     std::uint32_t right_child) {
    auto h = read_node_header(data);

    std::vector<IndexKey>      keys;
    std::vector<std::uint32_t> children;
    for (std::uint32_t i = 0; i < h.num_keys; ++i) {
        keys.push_back(get_key(data, i));
        children.push_back(get_child(data, i));
    }
    children.push_back(get_child(data, h.num_keys));

    auto it = std::lower_bound(keys.begin(), keys.end(), key,
        [](const IndexKey& a, const IndexKey& b){ return a < b; });
    std::size_t pos = static_cast<std::size_t>(it - keys.begin());
    keys.insert(keys.begin() + pos, key);
    children.insert(children.begin() + pos + 1, right_child);

    if (node_bytes(keys, internal_keys_offset()) > page_size_) {
        throw std::runtime_error("BPlusTree: interne plein (bytes)");
    }

    for (std::size_t i = 0; i < children.size(); ++i)
        set_child(data, i, children[i]);
    rebuild_keys(data, keys, internal_keys_offset());
    set_num_keys(data, static_cast<std::uint32_t>(keys.size()));
}

// ---------- Split ----------

SplitResult BPlusTree::split_leaf(std::vector<std::byte>& data) {
    auto h = read_node_header(data);
    std::uint32_t total = h.num_keys;
    std::uint32_t mid   = total / 2;

    std::vector<IndexKey> keys;
    std::vector<RowId>    rids;
    for (std::uint32_t i = 0; i < total; ++i) {
        keys.push_back(get_key(data, i));
        rids.push_back(get_rid(data, i));
    }

    std::uint32_t new_id = allocate_page();
    std::vector<std::byte> nd(page_size_, std::byte{0});

    BTreeNodeHeader nh;
    nh.page_id   = new_id;
    nh.is_leaf   = 1;
    nh.num_keys  = total - mid;
    nh.next_leaf = h.next_leaf;
    write_node_header(nd, nh);

    std::vector<IndexKey> nkeys(keys.begin() + mid, keys.end());
    std::vector<RowId>    nrids(rids.begin() + mid, rids.end());
    for (std::size_t i = 0; i < nrids.size(); ++i) set_rid(nd, i, nrids[i]);
    rebuild_keys(nd, nkeys, leaf_keys_offset());

    std::vector<IndexKey> lkeys(keys.begin(), keys.begin() + mid);
    std::vector<RowId>    lrids(rids.begin(), rids.begin() + mid);
    for (std::size_t i = 0; i < lrids.size(); ++i) set_rid(data, i, lrids[i]);
    rebuild_keys(data, lkeys, leaf_keys_offset());
    set_num_keys(data, mid);

    h.num_keys = mid;
    h.next_leaf = new_id;
    write_node_header(data, h);

    write_page(new_id, nd);
    return SplitResult{ nkeys.front(), new_id };
}

SplitResult BPlusTree::split_internal(std::vector<std::byte>& data) {
    auto h = read_node_header(data);
    std::uint32_t total = h.num_keys;
    std::uint32_t mid   = total / 2;

    std::vector<IndexKey>      keys;
    std::vector<std::uint32_t> children;
    for (std::uint32_t i = 0; i < total; ++i) {
        keys.push_back(get_key(data, i));
        children.push_back(get_child(data, i));
    }
    children.push_back(get_child(data, total));

    IndexKey median = keys[mid];

    std::uint32_t new_id = allocate_page();
    std::vector<std::byte> nd(page_size_, std::byte{0});

    BTreeNodeHeader nh;
    nh.page_id   = new_id;
    nh.is_leaf   = 0;
    nh.num_keys  = total - mid - 1;
    nh.next_leaf = 0;
    write_node_header(nd, nh);

    std::vector<IndexKey>      nkeys(keys.begin() + mid + 1, keys.end());
    std::vector<std::uint32_t> nchildren(children.begin() + mid + 1,
                                          children.end());
    for (std::size_t i = 0; i < nchildren.size(); ++i)
        set_child(nd, i, nchildren[i]);
    rebuild_keys(nd, nkeys, internal_keys_offset());

    std::vector<IndexKey>      lkeys(keys.begin(), keys.begin() + mid);
    std::vector<std::uint32_t> lchildren(children.begin(),
                                          children.begin() + mid + 1);
    for (std::size_t i = 0; i < lchildren.size(); ++i)
        set_child(data, i, lchildren[i]);
    rebuild_keys(data, lkeys, internal_keys_offset());
    set_num_keys(data, mid);

    write_page(new_id, nd);
    return SplitResult{ median, new_id };
}

// ---------- Recherche ----------

std::optional<RowId> BPlusTree::search_in_node(std::uint32_t page_id,
                                                const IndexKey& key) const {
    auto data = read_page(page_id);
    auto h = read_node_header(data);

    if (h.is_leaf) {
        for (std::uint32_t i = 0; i < h.num_keys; ++i) {
            auto k = get_key(data, i);
            if (k == key) return get_rid(data, i);
        }
        return std::nullopt;
    }

    std::uint32_t child_idx = h.num_keys;
    for (std::uint32_t i = 0; i < h.num_keys; ++i) {
        auto k = get_key(data, i);
        if (key < k) { child_idx = i; break; }
    }
    return search_in_node(get_child(data, child_idx), key);
}

std::optional<RowId> BPlusTree::search(const IndexKey& key) const {
    return search_in_node(root_page_id_, key);
}

// ---------- Insertion récursive + racine ----------

std::optional<SplitResult>
BPlusTree::insert_recursive(std::uint32_t page_id,
                             const IndexKey& key, const RowId& rid) {
    auto data = read_page(page_id);
    auto h = read_node_header(data);

    if (h.is_leaf) {
        if (h.num_keys >= BP_MAX_KEYS) {
            auto res = split_leaf(data);
            write_page(page_id, data);

            if (key < res.median_key) {
                auto left = read_page(page_id);
                insert_in_leaf(left, key, rid);
                write_page(page_id, left);
            } else {
                auto right = read_page(res.new_page_id);
                insert_in_leaf(right, key, rid);
                write_page(res.new_page_id, right);
            }
            return res;
        }

        insert_in_leaf(data, key, rid);
        write_page(page_id, data);
        return std::nullopt;
    }

    std::uint32_t child_idx = h.num_keys;
    for (std::uint32_t i = 0; i < h.num_keys; ++i) {
        if (key < get_key(data, i)) { child_idx = i; break; }
    }
    std::uint32_t child_id = get_child(data, child_idx);

    auto child_split = insert_recursive(child_id, key, rid);
    if (!child_split) return std::nullopt;

    try {
        insert_in_internal(data, child_split->median_key,
                            child_split->new_page_id);
    } catch (const std::runtime_error&) {
        auto res = split_internal(data);
        write_page(page_id, data);

        if (child_split->median_key < res.median_key) {
            auto left = read_page(page_id);
            insert_in_internal(left, child_split->median_key,
                                child_split->new_page_id);
            write_page(page_id, left);
        } else {
            auto right = read_page(res.new_page_id);
            insert_in_internal(right, child_split->median_key,
                                child_split->new_page_id);
            write_page(res.new_page_id, right);
        }
        return res;
    }
    write_page(page_id, data);
    return std::nullopt;
}

void BPlusTree::insert(const IndexKey& key, const RowId& rid) {
    auto split = insert_recursive(root_page_id_, key, rid);
    if (!split) return;

    std::uint32_t new_root_id = allocate_page();
    std::vector<std::byte> root(page_size_, std::byte{0});

    BTreeNodeHeader rh;
    rh.page_id   = new_root_id;
    rh.is_leaf   = 0;
    rh.num_keys  = 1;
    rh.next_leaf = 0;
    write_node_header(root, rh);

    set_child(root, 0, root_page_id_);
    set_child(root, 1, split->new_page_id);

    std::vector<IndexKey> k{ split->median_key };
    rebuild_keys(root, k, internal_keys_offset());

    write_page(new_root_id, root);
    root_page_id_ = new_root_id;
    update_root_page_id_on_disk(idx_path_, new_root_id);
}

// ============================================================================
// J5.3c : Suppression (choix B — rebalance à la remontée)
// ============================================================================

namespace {

// Extrait la clé séparatrice entre child[i] et child[i+1] dans un nœud interne
IndexKey separator_at(const std::vector<std::byte>& pdata, std::uint32_t i) {
    std::uint16_t off;
    std::memcpy(&off, pdata.data() + BP_NODE_HEADER_SIZE + i * 2, 2);
    return BPlusTree::read_key(pdata, off);
}

} // namespace

bool BPlusTree::remove(const IndexKey& key) {
    bool found = remove_recursive(root_page_id_, key);

    // Collapse de racine : racine interne vide → son unique enfant devient racine
    auto root_data = read_page(root_page_id_);
    auto rh = read_node_header(root_data);
    if (!rh.is_leaf && rh.num_keys == 0) {
        std::uint32_t new_root = get_child(root_data, 0);
        root_page_id_ = new_root;
        update_root_page_id_on_disk(idx_path_, new_root);
    }
    return found;
}

// Retourne true si la clé a été trouvée et supprimée.
// Rebalance l'enfant après suppression si nécessaire.
bool BPlusTree::remove_recursive(std::uint32_t page_id, const IndexKey& key) {
    auto data = read_page(page_id);
    auto h = read_node_header(data);

    // ---------- Feuille ----------
    if (h.is_leaf) {
        std::vector<IndexKey> keys;
        std::vector<RowId>    rids;
        for (std::uint32_t i = 0; i < h.num_keys; ++i) {
            keys.push_back(get_key(data, i));
            rids.push_back(get_rid(data, i));
        }

        auto it = std::lower_bound(keys.begin(), keys.end(), key,
            [](const IndexKey& a, const IndexKey& b){ return a < b; });
        if (it == keys.end() || !(*it == key)) {
            return false;
        }

        std::size_t pos = static_cast<std::size_t>(it - keys.begin());
        keys.erase(keys.begin() + pos);
        rids.erase(rids.begin() + pos);

        for (std::size_t i = 0; i < rids.size(); ++i) set_rid(data, i, rids[i]);
        rebuild_keys(data, keys, leaf_keys_offset());
        set_num_keys(data, static_cast<std::uint32_t>(keys.size()));
        write_page(page_id, data);
        return true;
    }

    // ---------- Interne ----------
    std::uint32_t child_idx = h.num_keys;
    for (std::uint32_t i = 0; i < h.num_keys; ++i) {
        if (key < get_key(data, i)) { child_idx = i; break; }
    }
    std::uint32_t child_id = get_child(data, child_idx);

    bool found = remove_recursive(child_id, key);
    if (!found) return false;

    // Rebalance l'enfant si sous-occupé
    auto child_data = read_page(child_id);
    auto ch = read_node_header(child_data);
    if (ch.num_keys < BP_MIN_KEYS && page_id != root_page_id_) {
        rebalance(page_id, child_idx);
    } else if (ch.num_keys < BP_MIN_KEYS && page_id == root_page_id_) {
        // Cas racine : rebalance quand même, sauf si l'enfant est l'unique
        auto pdata = read_page(page_id);
        auto ph = read_node_header(pdata);
        if (ph.num_keys > 0) {
            rebalance(page_id, child_idx);
        }
    }
    return true;
}

// Rééquilibre l'enfant à l'index node_idx du parent.
// Emprunt si le frère a plus de BP_MIN_KEYS clés, sinon fusion.
// Retourne true si le parent a besoin de rebalance à son tour (fusion).
bool BPlusTree::rebalance(std::uint32_t parent_id, std::uint32_t node_idx) {
    auto pdata = read_page(parent_id);
    auto ph = read_node_header(pdata);
    // Charger clés et enfants du parent
    std::vector<IndexKey>      pkeys;
    std::vector<std::uint32_t> pchildren;
    for (std::uint32_t i = 0; i < ph.num_keys; ++i) {
        pkeys.push_back(get_key(pdata, i));
        pchildren.push_back(get_child(pdata, i));
    }
    pchildren.push_back(get_child(pdata, ph.num_keys));

    std::uint32_t node_id = pchildren[node_idx];

    // Choisir un frère
    std::uint32_t sib_idx;
    bool sibling_is_left;
    if (node_idx > 0) {
        sib_idx = node_idx - 1;
        sibling_is_left = true;
    } else {
        sib_idx = node_idx + 1;
        sibling_is_left = false;
    }
    std::uint32_t sib_id = pchildren[sib_idx];

    // Charger node et sib
    auto ndata = read_page(node_id);
    auto nh = read_node_header(ndata);
    auto sdata = read_page(sib_id);
    auto sh = read_node_header(sdata);

    bool is_leaf = nh.is_leaf != 0;

    std::vector<IndexKey> nkeys, skeys;
    for (std::uint32_t i = 0; i < nh.num_keys; ++i) nkeys.push_back(get_key(ndata, i));
    for (std::uint32_t i = 0; i < sh.num_keys; ++i) skeys.push_back(get_key(sdata, i));

    if (is_leaf) {
        std::vector<RowId> nrids, srids;
        for (std::uint32_t i = 0; i < nh.num_keys; ++i) nrids.push_back(get_rid(ndata, i));
        for (std::uint32_t i = 0; i < sh.num_keys; ++i) srids.push_back(get_rid(sdata, i));

        // Emprunt possible ?
        if (sh.num_keys > BP_MIN_KEYS) {
            if (sibling_is_left) {
                // Prendre la dernière clé du frère gauche
                IndexKey bk = skeys.back();
                RowId    br = srids.back();
                skeys.pop_back();
                srids.pop_back();

                nkeys.insert(nkeys.begin(), bk);
                nrids.insert(nrids.begin(), br);

                // Mettre à jour la clé séparatrice
                pkeys[sib_idx] = nkeys.front();
            } else {
                // Prendre la première clé du frère droit
                IndexKey bk = skeys.front();
                RowId    br = srids.front();
                skeys.erase(skeys.begin());
                srids.erase(srids.begin());

                nkeys.push_back(bk);
                nrids.push_back(br);

                // Mettre à jour la clé séparatrice
                pkeys[node_idx] = skeys.front();
            }

            // Réécrire node
            for (std::size_t i = 0; i < nrids.size(); ++i) set_rid(ndata, i, nrids[i]);
            rebuild_keys(ndata, nkeys, leaf_keys_offset());
            set_num_keys(ndata, static_cast<std::uint32_t>(nkeys.size()));
            write_page(node_id, ndata);

            // Réécrire sib
            for (std::size_t i = 0; i < srids.size(); ++i) set_rid(sdata, i, srids[i]);
            rebuild_keys(sdata, skeys, leaf_keys_offset());
            set_num_keys(sdata, static_cast<std::uint32_t>(skeys.size()));
            write_page(sib_id, sdata);
            // Réécrire parent
            rebuild_keys(pdata, pkeys, internal_keys_offset());
            write_page(parent_id, pdata);
            return false;
        }

        // Fusion
        if (sibling_is_left) {
            // sib absorbe node
            skeys.insert(skeys.end(), nkeys.begin(), nkeys.end());
            srids.insert(srids.end(), nrids.begin(), nrids.end());

            sh.num_keys = static_cast<std::uint32_t>(skeys.size());
            sh.next_leaf = nh.next_leaf;

            for (std::size_t i = 0; i < srids.size(); ++i) set_rid(sdata, i, srids[i]);
            rebuild_keys(sdata, skeys, leaf_keys_offset());
            set_num_keys(sdata, static_cast<std::uint32_t>(skeys.size()));
            write_node_header(sdata, sh);
            write_page(sib_id, sdata);

            // Retirer node du parent (clé à sib_idx, enfant à node_idx)
            pkeys.erase(pkeys.begin() + sib_idx);
            pchildren.erase(pchildren.begin() + node_idx);
        } else {
            // node absorbe sib
            nkeys.insert(nkeys.end(), skeys.begin(), skeys.end());
            nrids.insert(nrids.end(), srids.begin(), srids.end());

            nh.num_keys = static_cast<std::uint32_t>(nkeys.size());
            nh.next_leaf = sh.next_leaf;

            for (std::size_t i = 0; i < nrids.size(); ++i) set_rid(ndata, i, nrids[i]);
            rebuild_keys(ndata, nkeys, leaf_keys_offset());
            set_num_keys(ndata, static_cast<std::uint32_t>(nkeys.size()));
            write_node_header(ndata, nh);
            write_page(node_id, ndata);

            // Retirer sib du parent (clé à node_idx, enfant à sib_idx)
            pkeys.erase(pkeys.begin() + node_idx);
            pchildren.erase(pchildren.begin() + sib_idx);
        }
    } else {
        // ---------- Interne ----------
        std::vector<std::uint32_t> nchildren, schildren;
        for (std::uint32_t i = 0; i <= nh.num_keys; ++i)
            nchildren.push_back(get_child(ndata, i));
        for (std::uint32_t i = 0; i <= sh.num_keys; ++i)
            schildren.push_back(get_child(sdata, i));

        if (sh.num_keys > BP_MIN_KEYS) {
            // Emprunt
            if (sibling_is_left) {
                IndexKey bk = skeys.back();
                std::uint32_t bc = schildren.back();
                skeys.pop_back();
                schildren.pop_back();

                IndexKey sep = pkeys[sib_idx];
                pkeys[sib_idx] = bk;

                nkeys.insert(nkeys.begin(), sep);
                nchildren.insert(nchildren.begin(), bc);
            } else {
                IndexKey bk = skeys.front();
                std::uint32_t bc = schildren.front();
                skeys.erase(skeys.begin());
                schildren.erase(schildren.begin());

                IndexKey sep = pkeys[node_idx];
                pkeys[node_idx] = bk;

                nkeys.push_back(sep);
                nchildren.push_back(bc);
            }

            for (std::size_t i = 0; i < nchildren.size(); ++i)
                set_child(ndata, i, nchildren[i]);
            rebuild_keys(ndata, nkeys, internal_keys_offset());
            set_num_keys(ndata, static_cast<std::uint32_t>(nkeys.size()));
            write_page(node_id, ndata);

            for (std::size_t i = 0; i < schildren.size(); ++i)
                set_child(sdata, i, schildren[i]);
            rebuild_keys(sdata, skeys, internal_keys_offset());
            set_num_keys(sdata, static_cast<std::uint32_t>(skeys.size()));
            write_page(sib_id, sdata);

            rebuild_keys(pdata, pkeys, internal_keys_offset());
            write_page(parent_id, pdata);
            return false;
        }

        // Fusion
        if (sibling_is_left) {
            IndexKey sep = pkeys[sib_idx];
            skeys.push_back(sep);
            skeys.insert(skeys.end(), nkeys.begin(), nkeys.end());
            schildren.insert(schildren.end(), nchildren.begin(), nchildren.end());

            for (std::size_t i = 0; i < schildren.size(); ++i)
                set_child(sdata, i, schildren[i]);
            rebuild_keys(sdata, skeys, internal_keys_offset());
            set_num_keys(sdata, static_cast<std::uint32_t>(skeys.size()));
            write_page(sib_id, sdata);

            pkeys.erase(pkeys.begin() + sib_idx);
            pchildren.erase(pchildren.begin() + node_idx);
        } else {
            IndexKey sep = pkeys[node_idx];
            nkeys.push_back(sep);
            nkeys.insert(nkeys.end(), skeys.begin(), skeys.end());
            nchildren.insert(nchildren.end(), schildren.begin(), schildren.end());

            for (std::size_t i = 0; i < nchildren.size(); ++i)
                set_child(ndata, i, nchildren[i]);
            rebuild_keys(ndata, nkeys, internal_keys_offset());
            set_num_keys(ndata, static_cast<std::uint32_t>(nkeys.size()));
            write_page(node_id, ndata);

            pkeys.erase(pkeys.begin() + node_idx);
            pchildren.erase(pchildren.begin() + sib_idx);
        }
    }

    // Réécrire le parent
    for (std::size_t i = 0; i < pchildren.size(); ++i)
        set_child(pdata, i, pchildren[i]);
    rebuild_keys(pdata, pkeys, internal_keys_offset());
    set_num_keys(pdata, static_cast<std::uint32_t>(pkeys.size()));
    write_page(parent_id, pdata);
    return true;
}

} // namespace kerneldb
