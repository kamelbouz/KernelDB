#include <catch2/catch_test_macros.hpp>
#include "kerneldb/page.hpp"
#include <cstddef>

using namespace kerneldb;

namespace {

std::vector<std::byte> make_bytes(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<std::byte>(c));
    return out;
}

std::string to_string(const std::vector<std::byte>& data) {
    std::string out;
    out.reserve(data.size());
    for (auto b : data) out += static_cast<char>(b);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// J4.1 — Slotted page
// ---------------------------------------------------------------------------

TEST_CASE("J4.1 - page_init initialise une page vierge") {
    std::vector<std::byte> data(8192, std::byte{0});
    page_init(data, 42);

    PageHeader h = read_page_header(data);
    REQUIRE(h.page_id == 42);
    REQUIRE(h.slot_count == 0);
    REQUIRE(h.free_space_offset == 8192);
    REQUIRE(page_slot_count(data) == 0);
    // Espace libre = page_size - header = 8192 - 16 = 8176
    REQUIRE(page_free_space(data) == 8192 - PAGE_HEADER_SIZE);
}

TEST_CASE("J4.1 - insert une ligne") {
    std::vector<std::byte> data(8192, std::byte{0});
    page_init(data, 1);

    auto row = make_bytes("hello");
    int slot = page_insert_row(data, row);
    REQUIRE(slot == 0);
    REQUIRE(page_slot_count(data) == 1);

    std::vector<std::byte> out;
    REQUIRE(page_read_row(data, 0, out));
    REQUIRE(to_string(out) == "hello");
}

TEST_CASE("J4.1 - insert plusieurs lignes") {
    std::vector<std::byte> data(8192, std::byte{0});
    page_init(data, 1);

    REQUIRE(page_insert_row(data, make_bytes("a")) == 0);
    REQUIRE(page_insert_row(data, make_bytes("bb")) == 1);
    REQUIRE(page_insert_row(data, make_bytes("ccc")) == 2);
    REQUIRE(page_slot_count(data) == 3);

    std::vector<std::byte> out;
    REQUIRE(page_read_row(data, 0, out));
    REQUIRE(to_string(out) == "a");
    REQUIRE(page_read_row(data, 1, out));
    REQUIRE(to_string(out) == "bb");
    REQUIRE(page_read_row(data, 2, out));
    REQUIRE(to_string(out) == "ccc");
}

TEST_CASE("J4.1 - remove marque le slot vide") {
    std::vector<std::byte> data(8192, std::byte{0});
    page_init(data, 1);

    page_insert_row(data, make_bytes("hello"));
    REQUIRE(page_remove_row(data, 0));

    std::vector<std::byte> out;
    REQUIRE_FALSE(page_read_row(data, 0, out));
    // Le slot existe toujours mais est vide
    REQUIRE(page_slot_count(data) == 1);
}

TEST_CASE("J4.1 - remove double retourne false") {
    std::vector<std::byte> data(8192, std::byte{0});
    page_init(data, 1);

    page_insert_row(data, make_bytes("hello"));
    REQUIRE(page_remove_row(data, 0));
    REQUIRE_FALSE(page_remove_row(data, 0));
}

TEST_CASE("J4.1 - read slot inexistant retourne false") {
    std::vector<std::byte> data(8192, std::byte{0});
    page_init(data, 1);

    std::vector<std::byte> out;
    REQUIRE_FALSE(page_read_row(data, 0, out));
    REQUIRE_FALSE(page_read_row(data, 999, out));
}

TEST_CASE("J4.1 - insert retourne -1 si pas de place") {
    std::vector<std::byte> data(4096, std::byte{0});   // page minimale
    page_init(data, 1);

    // Remplir jusqu'à saturation
    std::vector<std::byte> big(2000, std::byte{'x'});
    REQUIRE(page_insert_row(data, big) == 0);   // 1 ligne de 2000 octets
    // 2e ligne de 2000 ne devrait pas tenir (4096 - 16 - 4*2 - 2000 = ~2072)
    // 2000 + 4 = 2004 > 2072 ? Non → OK
    REQUIRE(page_insert_row(data, big) == 1);
    // 3e : reste ~68 octets → pas de place
    REQUIRE(page_insert_row(data, big) == -1);
}

TEST_CASE("J4.1 - compact récupère l'espace des slots vides") {
    std::vector<std::byte> data(4096, std::byte{0});
    page_init(data, 1);

    std::vector<std::byte> big(1000, std::byte{'x'});
    page_insert_row(data, big);   // slot 0
    page_insert_row(data, big);   // slot 1
    page_insert_row(data, big);   // slot 2

    std::uint32_t free_before = page_free_space(data);
    REQUIRE(page_remove_row(data, 1));   // supprime slot 1

    // L'espace n'est pas récupéré automatiquement
    REQUIRE(page_free_space(data) == free_before);

    // Compaction
    page_compact(data);

    // Maintenant l'espace est récupéré
    std::uint32_t free_after = page_free_space(data);
    REQUIRE(free_after > free_before);

    // Les slots 0 et 2 restent lisibles
    std::vector<std::byte> out;
    REQUIRE(page_read_row(data, 0, out));
    REQUIRE(to_string(out) == std::string(1000, 'x'));
    REQUIRE(page_read_row(data, 2, out));
    REQUIRE(to_string(out) == std::string(1000, 'x'));
    // Slot 1 est vide
    REQUIRE_FALSE(page_read_row(data, 1, out));
}
