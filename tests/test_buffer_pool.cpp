#include <catch2/catch_test_macros.hpp>
#include "kerneldb/buffer_pool.hpp"

#include <map>
#include <vector>

using namespace kerneldb;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct FakeDisk {
    std::unordered_map<PageKey, Page, PageKeyHash> pages;
    std::vector<PageKey> writes;

    std::function<Page(const PageKey&)> loader() {
        return [this](const PageKey& key) -> Page {
            auto it = pages.find(key);
            if (it != pages.end()) {
                return it->second;
            }
            return Page(key.page_id, 8192);
        };
    }

        std::function<void(const PageKey&, const Page&)> writer() {
        return [this](const PageKey& key, const Page& p) {
            pages[key] = p;
            writes.push_back(key);
        };
    }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - construction invalide") {
    REQUIRE_THROWS_AS(BufferPool(0), std::runtime_error);
}

TEST_CASE("BufferPool - etat initial") {
    BufferPool bp(4);
    REQUIRE(bp.capacity() == 4);
    REQUIRE(bp.size() == 0);
    REQUIRE(bp.hits() == 0);
    REQUIRE(bp.misses() == 0);
    REQUIRE(bp.evictions() == 0);
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - fetch charge depuis le disque") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey key{"t", 1};
    auto& page = bp.fetch(key, disk.loader());
    REQUIRE(page.page_id == 1);
    REQUIRE(bp.size() == 1);
    REQUIRE(bp.hits() == 0);
    REQUIRE(bp.misses() == 1);

    bp.unpin(key);
}

TEST_CASE("BufferPool - fetch deux fois = hit") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey key{"t", 1};
    bp.fetch(key, disk.loader());
    bp.unpin(key);

    bp.fetch(key, disk.loader());
    bp.unpin(key);

    REQUIRE(bp.hits() == 1);
    REQUIRE(bp.misses() == 1);
    REQUIRE(bp.size() == 1);
}

TEST_CASE("BufferPool - fetch pages differentes") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};
    PageKey k3{"t", 3};

    bp.fetch(k1, disk.loader()); bp.unpin(k1);
    bp.fetch(k2, disk.loader()); bp.unpin(k2);
    bp.fetch(k3, disk.loader()); bp.unpin(k3);

    REQUIRE(bp.size() == 3);
    REQUIRE(bp.misses() == 3);
}

// ---------------------------------------------------------------------------
// Eviction LRU
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - eviction LRU quand plein") {
    FakeDisk disk;
    BufferPool bp(2, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};
    PageKey k3{"t", 3};

    bp.fetch(k1, disk.loader()); bp.unpin(k1);
    bp.fetch(k2, disk.loader()); bp.unpin(k2);
    REQUIRE(bp.size() == 2);

    bp.fetch(k3, disk.loader()); bp.unpin(k3);

    REQUIRE(bp.size() == 2);
    REQUIRE(bp.evictions() == 1);
    REQUIRE(bp.contains(k2));
    REQUIRE(bp.contains(k3));
    REQUIRE_FALSE(bp.contains(k1));
}

TEST_CASE("BufferPool - LRU respecte l'ordre d'acces") {
    FakeDisk disk;
    BufferPool bp(2, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};
    PageKey k3{"t", 3};

    bp.fetch(k1, disk.loader()); bp.unpin(k1);
    bp.fetch(k2, disk.loader()); bp.unpin(k2);
    bp.fetch(k1, disk.loader()); bp.unpin(k1);

    bp.fetch(k3, disk.loader()); bp.unpin(k3);

    REQUIRE(bp.contains(k1));
    REQUIRE(bp.contains(k3));
    REQUIRE_FALSE(bp.contains(k2));
}

// ---------------------------------------------------------------------------
// Pin / Unpin
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - page pinnee ne peut pas etre evincee") {
    FakeDisk disk;
    BufferPool bp(2, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};
    PageKey k3{"t", 3};

    bp.fetch(k1, disk.loader());
    bp.fetch(k2, disk.loader()); bp.unpin(k2);

    bp.fetch(k3, disk.loader()); bp.unpin(k3);

    REQUIRE(bp.contains(k1));
    REQUIRE(bp.contains(k3));
    REQUIRE_FALSE(bp.contains(k2));

    bp.unpin(k1);
}

TEST_CASE("BufferPool - toutes pinnees = exception") {
    FakeDisk disk;
    BufferPool bp(2, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};
    PageKey k3{"t", 3};

    bp.fetch(k1, disk.loader());
    bp.fetch(k2, disk.loader());

    REQUIRE_THROWS_AS(bp.fetch(k3, disk.loader()), std::runtime_error);

    bp.unpin(k1);
    bp.unpin(k2);
}

// ---------------------------------------------------------------------------
// Dirty / Flush
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - mark_dirty sur page absente = exception") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey key{"t", 99};
    REQUIRE_THROWS_AS(bp.mark_dirty(key), std::runtime_error);
}

TEST_CASE("BufferPool - flush ecrit sur disque") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey key{"t", 1};
    auto& page = bp.fetch(key, disk.loader());
    page.data[0] = std::byte{42};
    bp.mark_dirty(key);
    bp.flush(key);

    REQUIRE(disk.writes.size() == 1);
    REQUIRE(disk.writes[0].page_id == 1);
    REQUIRE(disk.pages.count(PageKey{"t", 1}) == 1);
    REQUIRE(disk.pages[PageKey{"t", 1}].data[0] == std::byte{42});
    bp.unpin(key);
}

TEST_CASE("BufferPool - eviction flush automatiquement les pages dirty") {
    FakeDisk disk;
    BufferPool bp(1, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};

    auto& p1 = bp.fetch(k1, disk.loader());
    p1.data[0] = std::byte{7};
    bp.mark_dirty(k1);
    bp.unpin(k1);

    bp.fetch(k2, disk.loader());
    bp.unpin(k2);

    REQUIRE(bp.evictions() == 1);
    REQUIRE(disk.writes.size() == 1);
    REQUIRE(disk.writes[0].page_id == 1);
    REQUIRE(disk.pages[PageKey{"t", 1}].data[0] == std::byte{7});
}

TEST_CASE("BufferPool - flush_all ecrit toutes les pages dirty") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};
    PageKey k3{"t", 3};

    auto& p1 = bp.fetch(k1, disk.loader());
    auto& p2 = bp.fetch(k2, disk.loader());
    auto& p3 = bp.fetch(k3, disk.loader());

    p1.data[0] = std::byte{1};
    p2.data[0] = std::byte{2};
    p3.data[0] = std::byte{3};
    bp.mark_dirty(k1);
    bp.mark_dirty(k2);
    bp.mark_dirty(k3);

    bp.flush_all();

    REQUIRE(disk.writes.size() == 3);

    disk.writes.clear();
    bp.flush_all();
    REQUIRE(disk.writes.empty());

    bp.unpin(k1);
    bp.unpin(k2);
    bp.unpin(k3);
}

// ---------------------------------------------------------------------------
// Create
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - create ajoute une page vierge") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey key{"t", 10};
    auto& page = bp.create(key, 4096);
    REQUIRE(page.page_id == 10);
    REQUIRE(page.page_size == 4096);
    REQUIRE(bp.contains(key));
    REQUIRE(bp.size() == 1);

    bp.unpin(key);
}

TEST_CASE("BufferPool - create marque la page dirty") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey key{"t", 10};
    bp.create(key, 4096);
    bp.unpin(key);

    bp.flush_all();
    REQUIRE(disk.writes.size() == 1);
    REQUIRE(disk.writes[0].page_id == 10);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - clear libere les pages non-pinnees") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};

    bp.fetch(k1, disk.loader()); bp.unpin(k1);
    bp.fetch(k2, disk.loader()); bp.unpin(k2);

    bp.clear();

    REQUIRE(bp.size() == 0);
    REQUIRE_FALSE(bp.contains(k1));
    REQUIRE_FALSE(bp.contains(k2));
}

TEST_CASE("BufferPool - clear ne libere pas les pages pinnees") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};

    bp.fetch(k1, disk.loader()); bp.unpin(k1);
    bp.fetch(k2, disk.loader());

    bp.clear();

    REQUIRE(bp.size() == 1);
    REQUIRE(bp.contains(k2));
    REQUIRE_FALSE(bp.contains(k1));

    bp.unpin(k2);
}

// ---------------------------------------------------------------------------
// Mode sans writer
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - sans writer les evictions dirty ne crashent pas") {
    BufferPool bp(1);

    auto loader = [](const PageKey& key) -> Page {
        return Page(key.page_id, 8192);
    };

    PageKey k1{"t", 1};
    PageKey k2{"t", 2};

    auto& p1 = bp.fetch(k1, loader);
    p1.data[0] = std::byte{5};
    bp.mark_dirty(k1);
    bp.unpin(k1);

    bp.fetch(k2, loader);
    bp.unpin(k2);

    REQUIRE(bp.evictions() == 1);
}

// ---------------------------------------------------------------------------
// Statistiques
// ---------------------------------------------------------------------------

TEST_CASE("BufferPool - reset_stats remet les compteurs a zero") {
    FakeDisk disk;
    BufferPool bp(4, disk.writer());

    PageKey key{"t", 1};
    bp.fetch(key, disk.loader()); bp.unpin(key);
    bp.fetch(key, disk.loader()); bp.unpin(key);

    REQUIRE(bp.hits() == 1);
    REQUIRE(bp.misses() == 1);

    bp.reset_stats();
    REQUIRE(bp.hits() == 0);
    REQUIRE(bp.misses() == 0);
    REQUIRE(bp.evictions() == 0);
}