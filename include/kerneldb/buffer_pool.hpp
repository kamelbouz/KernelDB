#pragma once
#include "kerneldb/page.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace kerneldb {

// J4.3 : clé d'une page dans un BufferPool global.
// Identifie une page par (table, page_id).
struct PageKey {
    std::string table;
    std::uint32_t page_id = 0;

    bool operator==(const PageKey& other) const {
        return table == other.table && page_id == other.page_id;
    }
};

// Hash pour unordered_map
struct PageKeyHash {
    std::size_t operator()(const PageKey& k) const {
        std::size_t h1 = std::hash<std::string>{}(k.table);
        std::size_t h2 = std::hash<std::uint32_t>{}(k.page_id);
        return h1 ^ (h2 << 1);
    }
};

// Buffer pool : cache LRU de pages en mémoire (portée globale par base).
//
// J4.4 : le writer reçoit la PageKey (table + page_id) pour savoir
// dans quel fichier écrire sur disque.
//
class BufferPool {
public:
    static constexpr std::size_t DEFAULT_CAPACITY = 64;

    // J4.4 : writer avec PageKey
    using PageWriter = std::function<void(const PageKey&, const Page&)>;

    explicit BufferPool(std::size_t capacity = DEFAULT_CAPACITY,
                        PageWriter writer = nullptr);

    // ---------- Configuration / statistiques ----------

    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return frames_.size(); }
    std::size_t hits() const { return hits_; }
    std::size_t misses() const { return misses_; }
    std::size_t evictions() const { return evictions_; }

    void reset_stats() { hits_ = misses_ = evictions_ = 0; }

    // ---------- Accès ----------

    Page& fetch(const PageKey& key,
                const std::function<Page(const PageKey&)>& loader);

    Page& create(const PageKey& key, std::uint32_t page_size);

    void mark_dirty(const PageKey& key);

    void unpin(const PageKey& key);

    bool contains(const PageKey& key) const {
        return frames_.find(key) != frames_.end();
    }

    // ---------- Flush ----------

    void flush(const PageKey& key);
    void flush_all();
    void clear();

private:
    struct Frame {
        Page page;
        bool dirty = false;
        std::size_t pin_count = 0;
        std::list<PageKey>::iterator lru_it;
    };

    std::size_t capacity_;
    PageWriter writer_;
    std::unordered_map<PageKey, Frame, PageKeyHash> frames_;
    std::list<PageKey> lru_;   // front = MRU, back = LRU

    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
    std::size_t evictions_ = 0;

    void touch(const PageKey& key);
    bool evict_one();
};

} // namespace kerneldb