#include "kerneldb/page_manager.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace kerneldb {

namespace {

template<typename T>
void write_raw(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template<typename T>
T read_raw(std::istream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in) {
        throw std::runtime_error(
            "Fichier de table corrompu : lecture incomplète");
    }
    return value;
}

} // namespace

PageManager::PageManager(const std::filesystem::path& table_path,
                          std::size_t page_size)
    : path_(table_path), page_size_(page_size) {
    validate_page_size(page_size);
}

void PageManager::create() {
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "Impossible de créer le fichier de table: " + path_.string());
    }
    out.write(FILE_MAGIC, 4);
    write_raw(out, std::uint32_t{1});                            // version
    write_raw(out, static_cast<std::uint32_t>(page_size_));      // page_size
    write_raw(out, std::uint32_t{0});                            // page_count
    write_raw(out, std::uint64_t{0});                            // reserved
    if (!out) {
        throw std::runtime_error(
            "Écriture du header échouée: " + path_.string());
    }
}

PageManager::FileHeader PageManager::read_header() const {
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        throw std::runtime_error(
            "Impossible d'ouvrir la table: " + path_.string());
    }

    char magic[4];
    in.read(magic, 4);
    if (in.gcount() != 4 || std::memcmp(magic, FILE_MAGIC, 4) != 0) {
        throw std::runtime_error("Fichier de table invalide: " + path_.string());
    }

    FileHeader h;
    h.version = read_raw<std::uint32_t>(in);
    h.page_size = read_raw<std::uint32_t>(in);
    h.page_count = read_raw<std::uint32_t>(in);
    h.reserved = read_raw<std::uint64_t>(in);

    // C09 : validation
    if (h.version != 1) {
        throw std::runtime_error(
            "Fichier de table : version non supportée (" +
            std::to_string(h.version) + "): " + path_.string());
    }
    if (h.page_size != page_size_) {
        throw std::runtime_error(
            "Fichier de table : page_size incorrect (" +
            std::to_string(h.page_size) + " vs " +
            std::to_string(page_size_) + "): " + path_.string());
    }
    return h;
}

void PageManager::write_header(const FileHeader& h) {
    std::fstream io(path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!io) {
        throw std::runtime_error(
            "Impossible d'ouvrir la table: " + path_.string());
    }
    io.seekp(4);   // après le magic
    write_raw(io, h.version);
    write_raw(io, h.page_size);
    write_raw(io, h.page_count);
    write_raw(io, h.reserved);
    if (!io) {
        throw std::runtime_error(
            "Écriture du header échouée: " + path_.string());
    }
}

std::uint32_t PageManager::page_count() const {
    return read_header().page_count;
}

Page PageManager::read_page(std::uint32_t page_id) const {
    FileHeader h = read_header();
    if (page_id >= h.page_count) {
        throw std::runtime_error(
            "Page hors bornes (" + std::to_string(page_id) +
            " / " + std::to_string(h.page_count) + "): " + path_.string());
    }

    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        throw std::runtime_error(
            "Impossible d'ouvrir la table: " + path_.string());
    }

    std::size_t offset = FILE_HEADER_SIZE + page_id * page_size_;
    in.seekg(static_cast<std::streamoff>(offset));

    Page page(page_id, static_cast<std::uint32_t>(page_size_));
    in.read(reinterpret_cast<char*>(page.data.data()), page_size_);
    if (!in) {
        throw std::runtime_error(
            "Lecture de page échouée (page " + std::to_string(page_id) + ")");
    }
    return page;
}

void PageManager::write_page(const Page& page) {
    if (page.page_size != page_size_) {
        throw std::runtime_error("Taille de page incorrecte");
    }
    FileHeader h = read_header();
    if (page.page_id >= h.page_count) {
        throw std::runtime_error(
            "Page hors bornes (" + std::to_string(page.page_id) +
            " / " + std::to_string(h.page_count) + "): " + path_.string());
    }

    std::fstream io(path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!io) {
        throw std::runtime_error(
            "Impossible d'ouvrir la table: " + path_.string());
    }

    std::size_t offset = FILE_HEADER_SIZE + page.page_id * page_size_;
    io.seekp(static_cast<std::streamoff>(offset));
    io.write(reinterpret_cast<const char*>(page.data.data()), page_size_);
    if (!io) {
        throw std::runtime_error("Écriture de page échouée");
    }
}

std::uint32_t PageManager::allocate_page() {
    FileHeader h = read_header();

    // Nouvelle page : ID = page_count, données vides
    std::uint32_t new_id = h.page_count;
    Page page(new_id, static_cast<std::uint32_t>(page_size_));
    page_init(page.data, new_id);   // J4.1 : initialiser le slot directory

    // Écrire la page à la fin
    std::ofstream out(path_, std::ios::binary | std::ios::app);
    if (!out) {
        throw std::runtime_error(
            "Impossible d'ouvrir la table en append: " + path_.string());
    }
    out.write(reinterpret_cast<const char*>(page.data.data()), page_size_);
    if (!out) {
        throw std::runtime_error(
            "Écriture de la nouvelle page échouée: " + path_.string());
    }
    out.close();

    // Mettre à jour le header
    h.page_count = new_id + 1;
    write_header(h);

    return new_id;
}

} // namespace kerneldb