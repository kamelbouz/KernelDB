#include <catch2/catch_test_macros.hpp>
#include "kerneldb/page_manager.hpp"
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>

using namespace kerneldb;

namespace {

struct TempFile {
    std::filesystem::path dir;
    std::filesystem::path file;

    TempFile()
        : dir(std::filesystem::temp_directory_path() /
              ("kerneldb_pm_" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()))),
          file(dir / "t.tbl") {
        std::filesystem::create_directories(dir);
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// J4.2 — PageManager
// ---------------------------------------------------------------------------

TEST_CASE("J4.2 - create initialise un header valide") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();

    REQUIRE(std::filesystem::exists(t.file));
    REQUIRE(pm.page_count() == 0);
    // Taille : 24 (header) = 24
    REQUIRE(std::filesystem::file_size(t.file) == 24);
}

TEST_CASE("J4.2 - allocate_page ajoute une page") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();

    std::uint32_t id = pm.allocate_page();
    REQUIRE(id == 0);
    REQUIRE(pm.page_count() == 1);
    // Taille : 24 (header) + 4096 (page 0) = 4120
    REQUIRE(std::filesystem::file_size(t.file) == 24 + 4096);
}

TEST_CASE("J4.2 - allocate_page incrementale") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();

    REQUIRE(pm.allocate_page() == 0);
    REQUIRE(pm.allocate_page() == 1);
    REQUIRE(pm.allocate_page() == 2);
    REQUIRE(pm.page_count() == 3);
}

TEST_CASE("J4.2 - read_page relit la page") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();
    pm.allocate_page();

    Page p = pm.read_page(0);
    REQUIRE(p.page_id == 0);
    REQUIRE(p.page_size == 4096);
    REQUIRE(p.data.size() == 4096);
}

TEST_CASE("J4.2 - write_page puis read_page") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();
    pm.allocate_page();

    Page p = pm.read_page(0);
    p.data[0] = std::byte{42};
    pm.write_page(p);

    Page p2 = pm.read_page(0);
    REQUIRE(p2.data[0] == std::byte{42});
}

TEST_CASE("J4.2 - read_page hors bornes") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();
    pm.allocate_page();

    REQUIRE_THROWS_AS(pm.read_page(1), std::runtime_error);
    REQUIRE_THROWS_AS(pm.read_page(999), std::runtime_error);
}

TEST_CASE("J4.2 - write_page hors bornes") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();
    pm.allocate_page();

    Page p(5, 4096);   // page 5 n'existe pas
    REQUIRE_THROWS_AS(pm.write_page(p), std::runtime_error);
}

TEST_CASE("J4.2 - fopen apres reouverture") {
    TempFile t;

    {
        PageManager pm(t.file, 4096);
        pm.create();
        pm.allocate_page();
        pm.allocate_page();
        Page p = pm.read_page(0);
        p.data[0] = std::byte{77};
        pm.write_page(p);
    }

    // Réouvrir
    {
        PageManager pm(t.file, 4096);
        REQUIRE(pm.page_count() == 2);
        Page p = pm.read_page(0);
        REQUIRE(p.data[0] == std::byte{77});
    }
}

TEST_CASE("J4.2 - version invalide rejetee") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();

    // Modifier la version (offset 4)
    {
        std::fstream f(t.file, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(4);
        std::uint32_t bad = 99;
        f.write(reinterpret_cast<const char*>(&bad), 4);
    }

    REQUIRE_THROWS_AS(pm.page_count(), std::runtime_error);
}

TEST_CASE("J4.2 - page_size invalide rejete") {
    TempFile t;
    PageManager pm(t.file, 4096);
    pm.create();

    // Modifier le page_size (offset 8)
    {
        std::fstream f(t.file, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(8);
        std::uint32_t bad = 8192;   // different de 4096
        f.write(reinterpret_cast<const char*>(&bad), 4);
    }

    REQUIRE_THROWS_AS(pm.page_count(), std::runtime_error);
}