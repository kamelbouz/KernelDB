#include <catch2/catch_test_macros.hpp>
#include "kerneldb/bplus_tree.hpp"
#include <chrono>
#include <filesystem>
#include <cstdio>   // pour std::snprintf
#include <algorithm>
using namespace kerneldb;

namespace {

struct TempIdx {
    std::filesystem::path dir;
    std::filesystem::path idx;

    TempIdx()
        : dir(std::filesystem::temp_directory_path() /
              ("kerneldb_bp_" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()))),
          idx(dir / "test.idx") {
        std::filesystem::create_directories(dir);
    }
    ~TempIdx() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// J5.2 — IndexKey
// ---------------------------------------------------------------------------

TEST_CASE("J5.2 - IndexKey depuis INTEGER") {
    auto k = IndexKey::from_value(std::int64_t{42});
    REQUIRE(k.type == IndexKey::Type::Integer);
    REQUIRE(k.int_val == 42);
}

TEST_CASE("J5.2 - IndexKey depuis VARCHAR") {
    auto k = IndexKey::from_value(std::string{"hello"});
    REQUIRE(k.type == IndexKey::Type::Varchar);
    REQUIRE(k.str_val == "hello");
}

TEST_CASE("J5.2 - IndexKey comparaison INTEGER") {
    auto a = IndexKey::from_value(std::int64_t{1});
    auto b = IndexKey::from_value(std::int64_t{2});
    REQUIRE(a < b);
    REQUIRE_FALSE(b < a);
    REQUIRE(a == a);
    REQUIRE_FALSE(a == b);
}

TEST_CASE("J5.2 - IndexKey comparaison VARCHAR") {
    auto a = IndexKey::from_value(std::string{"abc"});
    auto b = IndexKey::from_value(std::string{"abd"});
    REQUIRE(a < b);
    REQUIRE(a == a);
}

TEST_CASE("J5.2 - IndexKey to_value") {
    auto k1 = IndexKey::from_value(std::int64_t{42});
    REQUIRE(std::get<std::int64_t>(k1.to_value()) == 42);

    auto k2 = IndexKey::from_value(std::string{"abc"});
    REQUIRE(std::get<std::string>(k2.to_value()) == "abc");
}

TEST_CASE("J5.2 - IndexKey type non supporte") {
    REQUIRE_THROWS_AS(IndexKey::from_value(3.14), std::runtime_error);
    REQUIRE_THROWS_AS(IndexKey::from_value(true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// J5.2 — BTreeNodeHeader
// ---------------------------------------------------------------------------

TEST_CASE("J5.2 - write/read BTreeNodeHeader") {
    std::vector<std::byte> data(8192, std::byte{0});
    BTreeNodeHeader h;
    h.page_id = 42;
    h.is_leaf = 1;
    h.num_keys = 7;
    h.next_leaf = 99;
    write_node_header(data, h);

    auto h2 = read_node_header(data);
    REQUIRE(h2.page_id == 42);
    REQUIRE(h2.is_leaf == 1);
    REQUIRE(h2.num_keys == 7);
    REQUIRE(h2.next_leaf == 99);
}

// ---------------------------------------------------------------------------
// J5.2 — BPlusTree fichier
// ---------------------------------------------------------------------------

TEST_CASE("J5.2 - create cree un fichier avec 1 page") {
    TempIdx t;
    BPlusTree bp(t.idx, 8192);
    bp.create();

    REQUIRE(std::filesystem::exists(t.idx));
    REQUIRE(bp.page_count() == 1);
    REQUIRE(std::filesystem::file_size(t.idx) == 24 + 8192);
}

TEST_CASE("J5.2 - read_page lit la racine") {
    TempIdx t;
    BPlusTree bp(t.idx, 8192);
    bp.create();

    auto data = bp.read_page(0);
    REQUIRE(data.size() == 8192);

    auto h = read_node_header(data);
    REQUIRE(h.page_id == 0);
    REQUIRE(h.is_leaf == 1);
    REQUIRE(h.num_keys == 0);
    REQUIRE(h.next_leaf == 0);
}

TEST_CASE("J5.2 - write_page puis read_page") {
    TempIdx t;
    BPlusTree bp(t.idx, 8192);
    bp.create();

    auto data = bp.read_page(0);
    BTreeNodeHeader h;
    h.page_id = 0;
    h.is_leaf = 1;
    h.num_keys = 5;
    h.next_leaf = 0;
    write_node_header(data, h);
    bp.write_page(0, data);

    auto data2 = bp.read_page(0);
    auto h2 = read_node_header(data2);
    REQUIRE(h2.num_keys == 5);
}

TEST_CASE("J5.2 - read_page hors bornes echoue") {
    TempIdx t;
    BPlusTree bp(t.idx, 8192);
    bp.create();

    REQUIRE_THROWS_AS(bp.read_page(1), std::runtime_error);
    REQUIRE_THROWS_AS(bp.read_page(999), std::runtime_error);
}

TEST_CASE("J5.2 - write_page taille incorrecte") {
    TempIdx t;
    BPlusTree bp(t.idx, 8192);
    bp.create();

    std::vector<std::byte> small(1024, std::byte{0});
    REQUIRE_THROWS_AS(bp.write_page(0, small), std::runtime_error);
}

TEST_CASE("J5.2 - IndexKey serialized_size") {
    auto k1 = IndexKey::from_value(std::int64_t{42});
    REQUIRE(k1.serialized_size() == 1 + 8);

    auto k2 = IndexKey::from_value(std::string{"hello"});
    REQUIRE(k2.serialized_size() == 1 + 4 + 5);
}


// ---------------------------------------------------------------------------
// J5.3a — Sérialisation des clés + search
// ---------------------------------------------------------------------------

TEST_CASE("J5.3a - key_size INTEGER") {
    auto k = IndexKey::from_value(std::int64_t{42});
    REQUIRE(BPlusTree::key_size(k) == 1 + 2 + 8);
}

TEST_CASE("J5.3a - key_size VARCHAR") {
    auto k = IndexKey::from_value(std::string{"hello"});
    REQUIRE(BPlusTree::key_size(k) == 1 + 2 + 5);
}

TEST_CASE("J5.3a - write/read key INTEGER") {
    std::vector<std::byte> data(8192, std::byte{0});
    auto k = IndexKey::from_value(std::int64_t{42});
    BPlusTree::write_key(data, 100, k);
    auto k2 = BPlusTree::read_key(data, 100);
    REQUIRE(k2.type == IndexKey::Type::Integer);
    REQUIRE(k2.int_val == 42);
}

TEST_CASE("J5.3a - write/read key VARCHAR") {
    std::vector<std::byte> data(8192, std::byte{0});
    auto k = IndexKey::from_value(std::string{"hello"});
    BPlusTree::write_key(data, 100, k);
    auto k2 = BPlusTree::read_key(data, 100);
    REQUIRE(k2.type == IndexKey::Type::Varchar);
    REQUIRE(k2.str_val == "hello");
}

TEST_CASE("J5.3a - search sur arbre vide retourne nullopt") {
    TempIdx t;
    BPlusTree bp(t.idx, 8192);
    bp.create();

    auto k = IndexKey::from_value(std::int64_t{42});
    auto rid = bp.search(k);
    REQUIRE_FALSE(rid.has_value());
}


// ---------------------------------------------------------------------------
// J5.3b — Insertion avec split automatique
// ---------------------------------------------------------------------------

namespace {

IndexKey make_int_key(std::int64_t v) {
    return IndexKey::from_value(v);
}

RowId make_rid(std::uint32_t page, std::uint16_t slot) {
    RowId r;
    r.page_id = page;
    r.slot_id = slot;
    return r;
}

} // namespace

TEST_CASE("J5.3b - insert une cle dans feuille vide") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    bp.insert(make_int_key(42), make_rid(1, 0));

    auto r = bp.search(make_int_key(42));
    REQUIRE(r.has_value());
    REQUIRE(r->page_id == 1);
    REQUIRE(r->slot_id == 0);
}

TEST_CASE("J5.3b - insert plusieurs cles sans split") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    for (int i = 0; i < 10; ++i) {
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));
    }

    for (int i = 0; i < 10; ++i) {
        auto r = bp.search(make_int_key(i));
        REQUIRE(r.has_value());
        REQUIRE(r->page_id == static_cast<std::uint32_t>(i));
    }

    REQUIRE(bp.page_count() == 1);
}

TEST_CASE("J5.3b - insert en ordre inverse") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    for (int i = 9; i >= 0; --i) {
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));
    }

    for (int i = 0; i < 10; ++i) {
        auto r = bp.search(make_int_key(i));
        REQUIRE(r.has_value());
    }
}

TEST_CASE("J5.3b - insert cle en double") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    bp.insert(make_int_key(42), make_rid(1, 0));
    bp.insert(make_int_key(42), make_rid(2, 0));

    auto r = bp.search(make_int_key(42));
    REQUIRE(r.has_value());
}

TEST_CASE("J5.3b - split feuille force par 300 cles") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 300;
    for (int i = 0; i < N; ++i) {
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));
    }

    for (int i = 0; i < N; ++i) {
        auto r = bp.search(make_int_key(i));
        REQUIRE(r.has_value());
        REQUIRE(r->page_id == static_cast<std::uint32_t>(i));
    }

    REQUIRE(bp.page_count() >= 2);
}

TEST_CASE("J5.3b - split cree une nouvelle racine interne") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    for (int i = 0; i < 300; ++i) {
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));
    }

    auto root_id = bp.root_page_id();
    auto data = bp.read_page(root_id);
    auto h = read_node_header(data);
    REQUIRE(h.is_leaf == 0);

    bool found_leaf_after_root = false;
    for (std::uint32_t pid = 1; pid < bp.page_count(); ++pid) {
        auto d = bp.read_page(pid);
        auto hh = read_node_header(d);
        if (hh.is_leaf) { found_leaf_after_root = true; break; }
    }
    REQUIRE(found_leaf_after_root);
}

TEST_CASE("J5.3b - persistance apres split") {
    TempIdx t;
    constexpr int N = 300;

    {
        BPlusTree bp(t.idx, 4096);
        bp.create();
        for (int i = 0; i < N; ++i) {
            bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));
        }
        REQUIRE(bp.page_count() >= 2);
    }

    {
        BPlusTree bp(t.idx, 4096);
        for (int i = 0; i < N; ++i) {
            auto r = bp.search(make_int_key(i));
            REQUIRE(r.has_value());
            REQUIRE(r->page_id == static_cast<std::uint32_t>(i));
        }
    }
}

TEST_CASE("J5.3b - 1000 cles (multi-niveaux)") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 1000;
    for (int i = 0; i < N; ++i) {
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));
    }

    for (int i = 0; i < N; ++i) {
        auto r = bp.search(make_int_key(i));
        REQUIRE(r.has_value());
        REQUIRE(r->page_id == static_cast<std::uint32_t>(i));
    }

    REQUIRE(bp.page_count() >= 4);
}

TEST_CASE("J5.3b - cles aleatoires") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 500;
    std::vector<std::int64_t> keys;
    keys.reserve(N);
    for (int i = 0; i < N; ++i) keys.push_back(i);

    std::uint64_t seed = 12345;
    for (int i = N - 1; i > 0; --i) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        std::size_t j = static_cast<std::size_t>(seed % (i + 1));
        std::swap(keys[i], keys[j]);
    }

    for (auto k : keys) {
        bp.insert(make_int_key(k),
                  make_rid(static_cast<std::uint32_t>(k), 0));
    }

    for (std::int64_t k = 0; k < N; ++k) {
        auto r = bp.search(make_int_key(k));
        REQUIRE(r.has_value());
        REQUIRE(r->page_id == static_cast<std::uint32_t>(k));
    }
}

TEST_CASE("J5.3b - cles VARCHAR avec split") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 300;
    for (int i = 0; i < N; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "key_%03d", i);
        bp.insert(IndexKey::from_value(std::string{buf}),
                  make_rid(static_cast<std::uint32_t>(i), 0));
    }

    for (int i = 0; i < N; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "key_%03d", i);
        auto r = bp.search(IndexKey::from_value(std::string{buf}));
        REQUIRE(r.has_value());
        REQUIRE(r->page_id == static_cast<std::uint32_t>(i));
    }
}

TEST_CASE("J5.3b - cles negatives") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    for (int i = -150; i <= 150; ++i) {
        bp.insert(make_int_key(i),
                  make_rid(static_cast<std::uint32_t>(i + 1000), 0));
    }

    for (int i = -150; i <= 150; ++i) {
        auto r = bp.search(make_int_key(i));
        REQUIRE(r.has_value());
        REQUIRE(r->page_id == static_cast<std::uint32_t>(i + 1000));
    }
}

TEST_CASE("J5.3b - root_page_id persiste correctement") {
    TempIdx t;
    constexpr int N = 300;

    std::uint32_t page_count_after_first = 0;
    {
        BPlusTree bp(t.idx, 4096);
        bp.create();
        for (int i = 0; i < N; ++i) {
            bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));
        }
        page_count_after_first = bp.page_count();
    }

    {
        BPlusTree bp(t.idx, 4096);
        REQUIRE(bp.page_count() == page_count_after_first);
        for (int i = 0; i < N; ++i) {
            REQUIRE(bp.search(make_int_key(i)).has_value());
        }
    }
}

// ---------------------------------------------------------------------------
// J5.3c — Suppression
// ---------------------------------------------------------------------------

TEST_CASE("J5.3c - remove sur arbre vide retourne false") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    REQUIRE_FALSE(bp.remove(make_int_key(42)));
}

TEST_CASE("J5.3c - remove cle inexistante retourne false") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    bp.insert(make_int_key(1), make_rid(1, 0));
    REQUIRE_FALSE(bp.remove(make_int_key(99)));
    REQUIRE(bp.search(make_int_key(1)).has_value());
}

TEST_CASE("J5.3c - remove simple dans feuille racine") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    bp.insert(make_int_key(1), make_rid(1, 0));
    bp.insert(make_int_key(2), make_rid(2, 0));
    bp.insert(make_int_key(3), make_rid(3, 0));

    REQUIRE(bp.remove(make_int_key(2)));
    REQUIRE_FALSE(bp.search(make_int_key(2)).has_value());
    REQUIRE(bp.search(make_int_key(1)).has_value());
    REQUIRE(bp.search(make_int_key(3)).has_value());
}

TEST_CASE("J5.3c - remove tout dans feuille racine") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    for (int i = 0; i < 10; ++i)
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

    for (int i = 0; i < 10; ++i)
        REQUIRE(bp.remove(make_int_key(i)));

    for (int i = 0; i < 10; ++i)
        REQUIRE_FALSE(bp.search(make_int_key(i)).has_value());
}

TEST_CASE("J5.3c - remove apres split") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 200;
    for (int i = 0; i < N; ++i)
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

    for (int i = N - 1; i >= N - 20; --i)
        REQUIRE(bp.remove(make_int_key(i)));

    for (int i = 0; i < N - 20; ++i)
        REQUIRE(bp.search(make_int_key(i)).has_value());
    for (int i = N - 20; i < N; ++i)
        REQUIRE_FALSE(bp.search(make_int_key(i)).has_value());
}

TEST_CASE("J5.3c - remove avec emprunt a gauche") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 300;
    for (int i = 0; i < N; ++i)
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

    for (int i = N - 1; i >= N - 100; --i)
        REQUIRE(bp.remove(make_int_key(i)));

    for (int i = 0; i < N - 100; ++i)
        REQUIRE(bp.search(make_int_key(i)).has_value());
    for (int i = N - 100; i < N; ++i)
        REQUIRE_FALSE(bp.search(make_int_key(i)).has_value());
}

TEST_CASE("J5.3c - remove avec emprunt a droite") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 300;
    for (int i = 0; i < N; ++i)
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

    for (int i = 0; i < 100; ++i)
        REQUIRE(bp.remove(make_int_key(i)));

    for (int i = 0; i < 100; ++i)
        REQUIRE_FALSE(bp.search(make_int_key(i)).has_value());
    for (int i = 100; i < N; ++i)
        REQUIRE(bp.search(make_int_key(i)).has_value());
}

TEST_CASE("J5.3c - remove avec fusion") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 500;
    for (int i = 0; i < N; ++i)
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

    for (int i = 0; i < N; i += 2)
        REQUIRE(bp.remove(make_int_key(i)));

    for (int i = 0; i < N; ++i) {
        bool found = bp.search(make_int_key(i)).has_value();
        if (i % 2 == 0) REQUIRE_FALSE(found);
        else            REQUIRE(found);
    }
}

TEST_CASE("J5.3c - remove jusqu'a collapse de la racine") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 300;
    for (int i = 0; i < N; ++i)
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

    REQUIRE(read_node_header(bp.read_page(bp.root_page_id())).is_leaf == 0);

    for (int i = 0; i < N; ++i)
        REQUIRE(bp.remove(make_int_key(i)));

    auto rh = read_node_header(bp.read_page(bp.root_page_id()));
    REQUIRE(rh.is_leaf == 1);
    REQUIRE(rh.num_keys == 0);
}

TEST_CASE("J5.3c - remove aleatoire stress") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    constexpr int N = 500;
    std::vector<std::int64_t> keys;
    for (int i = 0; i < N; ++i) {
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));
        keys.push_back(i);
    }

    std::uint64_t seed = 42;
    for (std::size_t i = keys.size() - 1; i > 0; --i) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        std::size_t j = static_cast<std::size_t>(seed % (i + 1));
        std::swap(keys[i], keys[j]);
    }

    for (std::size_t i = 0; i < keys.size() / 2; ++i)
        REQUIRE(bp.remove(make_int_key(keys[i])));

    for (int i = 0; i < N; ++i) {
        bool found = bp.search(make_int_key(i)).has_value();
        bool should_exist =
            std::find(keys.begin(), keys.begin() + keys.size() / 2, i)
            == keys.begin() + keys.size() / 2;
        REQUIRE(found == should_exist);
    }
}

TEST_CASE("J5.3c - persistance apres remove") {
    TempIdx t;
    constexpr int N = 300;

    {
        BPlusTree bp(t.idx, 4096);
        bp.create();
        for (int i = 0; i < N; ++i)
            bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

        for (int i = 0; i < N; i += 2)
            bp.remove(make_int_key(i));
    }

    {
        BPlusTree bp(t.idx, 4096);
        for (int i = 0; i < N; ++i) {
            bool found = bp.search(make_int_key(i)).has_value();
            if (i % 2 == 0) REQUIRE_FALSE(found);
            else            REQUIRE(found);
        }
    }
}

TEST_CASE("J5.3c - insert et remove entrelaces") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    for (int i = 0; i < 200; ++i)
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

    for (int i = 0; i < 100; ++i)
        bp.remove(make_int_key(i));

    for (int i = 200; i < 300; ++i)
        bp.insert(make_int_key(i), make_rid(static_cast<std::uint32_t>(i), 0));

    for (int i = 0; i < 100; ++i)
        REQUIRE_FALSE(bp.search(make_int_key(i)).has_value());
    for (int i = 100; i < 300; ++i)
        REQUIRE(bp.search(make_int_key(i)).has_value());
}

TEST_CASE("J5.3c - remove VARCHAR") {
    TempIdx t;
    BPlusTree bp(t.idx, 4096);
    bp.create();

    for (int i = 0; i < 50; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "key_%03d", i);
        bp.insert(IndexKey::from_value(std::string{buf}),
                  make_rid(static_cast<std::uint32_t>(i), 0));
    }

    REQUIRE(bp.remove(IndexKey::from_value(std::string{"key_025"})));
    REQUIRE_FALSE(bp.search(IndexKey::from_value(std::string{"key_025"})).has_value());
    REQUIRE(bp.search(IndexKey::from_value(std::string{"key_024"})).has_value());
    REQUIRE(bp.search(IndexKey::from_value(std::string{"key_026"})).has_value());
}