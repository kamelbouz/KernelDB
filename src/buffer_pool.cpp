#include "kerneldb/buffer_pool.hpp"
#include <stdexcept>
#include <string>

namespace kerneldb {

BufferPool::BufferPool(std::size_t capacity, PageWriter writer)
    : capacity_(capacity), writer_(std::move(writer)) {
    if (capacity_ == 0) {
        throw std::runtime_error("BufferPool: capacity must be > 0");
    }
}

// ---------- Accès ----------

Page& BufferPool::fetch(const PageKey& key,
                        const std::function<Page(const PageKey&)>& loader) {
    auto it = frames_.find(key);
    if (it != frames_.end()) {
        ++hits_;
        it->second.pin_count++;
        touch(key);
        return it->second.page;
    }

    ++misses_;

    while (frames_.size() >= capacity_) {
        if (!evict_one()) {
            throw std::runtime_error(
                "BufferPool: all frames are pinned, cannot evict");
        }
    }

    Page page = loader(key);

    lru_.push_front(key);
    Frame frame;
    frame.page = std::move(page);
    frame.dirty = false;
    frame.pin_count = 1;
    frame.lru_it = lru_.begin();

    auto [inserted_it, ok] = frames_.emplace(key, std::move(frame));
    (void)ok;
    return inserted_it->second.page;
}

Page& BufferPool::create(const PageKey& key, std::uint32_t page_size) {
    auto it = frames_.find(key);
    if (it != frames_.end()) {
        it->second.pin_count++;
        it->second.dirty = true;
        touch(key);
        return it->second.page;
    }

    while (frames_.size() >= capacity_) {
        if (!evict_one()) {
            throw std::runtime_error(
                "BufferPool: all frames are pinned, cannot evict");
        }
    }

    lru_.push_front(key);
    Frame frame;
    frame.page = Page(key.page_id, page_size);
    frame.dirty = true;
    frame.pin_count = 1;
    frame.lru_it = lru_.begin();

    auto [inserted_it, ok] = frames_.emplace(key, std::move(frame));
    (void)ok;
    return inserted_it->second.page;
}

void BufferPool::mark_dirty(const PageKey& key) {
    auto it = frames_.find(key);
    if (it == frames_.end()) {
        throw std::runtime_error(
            "BufferPool::mark_dirty: page " + std::to_string(key.page_id) +
            " (table " + key.table + ") not in cache");
    }
    it->second.dirty = true;
}

void BufferPool::unpin(const PageKey& key) {
    auto it = frames_.find(key);
    if (it == frames_.end()) {
        return;
    }
    if (it->second.pin_count > 0) {
        it->second.pin_count--;
    }
}

// ---------- Flush ----------

void BufferPool::flush(const PageKey& key) {
    auto it = frames_.find(key);
    if (it == frames_.end()) {
        return;
    }
    if (it->second.dirty && writer_) {
        writer_(key, it->second.page);
        it->second.dirty = false;
    }
}

void BufferPool::flush_all() {
    for (auto& [key, frame] : frames_) {
        if (frame.dirty && writer_) {
            writer_(key, frame.page);
            frame.dirty = false;
        }
    }
}

void BufferPool::clear() {
    flush_all();

    for (auto it = frames_.begin(); it != frames_.end(); ) {
        if (it->second.pin_count == 0) {
            lru_.erase(it->second.lru_it);
            it = frames_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------- Interne ----------

void BufferPool::touch(const PageKey& key) {
    auto it = frames_.find(key);
    if (it == frames_.end()) return;

    lru_.erase(it->second.lru_it);
    lru_.push_front(key);
    it->second.lru_it = lru_.begin();
}

bool BufferPool::evict_one() {
    for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
        const PageKey& key = *it;
        auto fit = frames_.find(key);
        if (fit == frames_.end()) continue;

        if (fit->second.pin_count == 0) {
            if (fit->second.dirty && writer_) {
                writer_(key, fit->second.page);
            }

            lru_.erase(fit->second.lru_it);
            frames_.erase(fit);
            ++evictions_;
            return true;
        }
    }
    return false;
}

} // namespace kerneldb