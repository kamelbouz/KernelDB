#pragma once
#include "kerneldb/page.hpp"
#include <filesystem>
#include <cstdint>

namespace kerneldb {

// J4.2 : gestion bas niveau des pages d'un fichier .tbl.
//
// Structure du fichier :
//   [FileHeader 24 octets] [Page 0] [Page 1] ... [Page N-1]
//
// FileHeader :
//   magic "MDBP" (4) + version (4) + page_size (4) + page_count (4) + reserved (8)
//
class PageManager {
public:
    PageManager(const std::filesystem::path& table_path,
                std::size_t page_size);

    // Crée le fichier avec un header vierge (truncate si existant)
    void create();

    // Nombre de pages allouées
    std::uint32_t page_count() const;

    // Lit une page par son ID
    Page read_page(std::uint32_t page_id) const;

    // Écrit une page (doit exister)
    void write_page(const Page& page);

    // Alloue une nouvelle page vierge, retourne son ID
    std::uint32_t allocate_page();

    // Chemin et taille
    const std::filesystem::path& path() const { return path_; }
    std::size_t page_size() const { return page_size_; }

private:
    std::filesystem::path path_;
    std::size_t page_size_;

    struct FileHeader {
        std::uint32_t version = 1;
        std::uint32_t page_size = 0;
        std::uint32_t page_count = 0;
        std::uint64_t reserved = 0;
    };

    FileHeader read_header() const;
    void write_header(const FileHeader& h);
};

} // namespace kerneldb