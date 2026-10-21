#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace kerneldb {

// ---------- Tailles de page supportées ----------

constexpr std::size_t PAGE_SIZE_MIN     = 4096;    // 4 KB
constexpr std::size_t PAGE_SIZE_DEFAULT = 8192;    // 8 KB
constexpr std::size_t PAGE_SIZE_MAX     = 65536;   // 64 KB

// Vérifie qu'une taille est valide (puissance de 2 entre MIN et MAX)
inline bool is_valid_page_size(std::size_t size) {
    if (size < PAGE_SIZE_MIN || size > PAGE_SIZE_MAX) return false;
    // Doit être une puissance de 2
    return (size & (size - 1)) == 0;
}

inline void validate_page_size(std::size_t size) {
    if (!is_valid_page_size(size)) {
        throw std::runtime_error(
            "Taille de page invalide: " + std::to_string(size) +
            " (doit être une puissance de 2 entre 4096 et 65536)");
    }
}

// ---------- En-tête de fichier ----------

constexpr std::size_t FILE_HEADER_SIZE = 24;
constexpr const char* FILE_MAGIC = "MDBP";   // KernelDB Pages

// ---------- Structure d'une page ----------

struct Page {
    std::uint32_t page_id = 0;
    std::uint32_t page_size = 0;              // taille de cette page
    std::vector<std::byte> data;              // contenu (taille = page_size)

    // Constructeurs
    Page() = default;
    explicit Page(std::uint32_t id, std::uint32_t size)
        : page_id(id), page_size(size), data(size, std::byte{0}) {}
};

// ---------- En-tête d'une page (dans les 16 premiers bytes) ----------

// Layout d'une page :
//   offset 0  : page_id (uint32)
//   offset 4  : slot_count (uint32)
//   offset 8  : free_space_offset (uint32)
//   offset 12 : flags (uint32) — réservé
//   offset 16 : ... données

constexpr std::size_t PAGE_HEADER_SIZE = 16;

struct PageHeader {
    std::uint32_t page_id = 0;
    std::uint32_t slot_count = 0;
    std::uint32_t free_space_offset = PAGE_HEADER_SIZE;
    std::uint32_t flags = 0;
};

// Sérialise un PageHeader dans les 16 premiers bytes de `data`
void write_page_header(std::vector<std::byte>& data, const PageHeader& header);

// Lit un PageHeader depuis les 16 premiers bytes de `data`
PageHeader read_page_header(const std::vector<std::byte>& data);


// ---------- J4.1 : slotted page ----------

// Entrée du slot directory (4 octets) :
//   offset (u16) : position absolue de la ligne dans la page
//   length (u16) : longueur de la ligne (0 = slot vide)
constexpr std::size_t SLOT_SIZE = 4;

struct Slot {
    std::uint16_t offset = 0;
    std::uint16_t length = 0;   // 0 = slot vide
};

// Lit un Slot depuis `data` à l'index `slot_idx`.
Slot read_slot(const std::vector<std::byte>& data, std::uint32_t slot_idx);

// Écrit un Slot dans `data` à l'index `slot_idx`.
void write_slot(std::vector<std::byte>& data,
                std::uint32_t slot_idx,
                const Slot& slot);

// ---------- API slotted page ----------

// Initialise une page vierge (header + slot directory vide).
void page_init(std::vector<std::byte>& data, std::uint32_t page_id);

// Nombre de slots (y compris les slots vides).
std::uint32_t page_slot_count(const std::vector<std::byte>& data);

// Espace libre contigu (octets).
std::uint32_t page_free_space(const std::vector<std::byte>& data);

// Insère une ligne. Retourne l'index du slot, ou -1 si pas de place.
int page_insert_row(std::vector<std::byte>& data,
                    const std::vector<std::byte>& row_bytes);

// Lit la ligne du slot `slot_idx`. Retourne false si slot vide.
bool page_read_row(const std::vector<std::byte>& data,
                   std::uint32_t slot_idx,
                   std::vector<std::byte>& out);

// Marque le slot `slot_idx` comme vide. Retourne false si déjà vide.
bool page_remove_row(std::vector<std::byte>& data, std::uint32_t slot_idx);

// Compacte les lignes (défragmentation).
void page_compact(std::vector<std::byte>& data);
} // namespace kerneldb