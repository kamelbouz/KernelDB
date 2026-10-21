#include "kerneldb/page.hpp"
#include <cstring>
#include <algorithm> 
#include <vector>
namespace kerneldb {

// ---------- Sérialisation du PageHeader ----------

void write_page_header(std::vector<std::byte>& data, const PageHeader& header) {
    if (data.size() < PAGE_HEADER_SIZE) {
        throw std::runtime_error("Data trop petite pour un PageHeader");
    }

    std::uint32_t fields[4] = {
        header.page_id,
        header.slot_count,
        header.free_space_offset,
        header.flags,
    };

    for (std::size_t i = 0; i < 4; ++i) {
        std::memcpy(data.data() + i * 4, &fields[i], 4);
    }
}

PageHeader read_page_header(const std::vector<std::byte>& data) {
    if (data.size() < PAGE_HEADER_SIZE) {
        throw std::runtime_error("Data trop petite pour un PageHeader");
    }

    PageHeader h;
    std::memcpy(&h.page_id, data.data(), 4);
    std::memcpy(&h.slot_count, data.data() + 4, 4);
    std::memcpy(&h.free_space_offset, data.data() + 8, 4);
    std::memcpy(&h.flags, data.data() + 12, 4);
    return h;
}

// ---------- J4.1 : slotted page ----------

// Initialise une page vierge
void page_init(std::vector<std::byte>& data, std::uint32_t page_id) {
    if (data.empty()) {
        throw std::runtime_error("page_init : data vide");
    }
    // Remplir de zéros
    std::fill(data.begin(), data.end(), std::byte{0});
    PageHeader h;
    h.page_id = page_id;
    h.slot_count = 0;
    h.free_space_offset = static_cast<std::uint32_t>(data.size());
    h.flags = 0;
    write_page_header(data, h);
}

std::uint32_t page_slot_count(const std::vector<std::byte>& data) {
    return read_page_header(data).slot_count;
}

std::uint32_t page_free_space(const std::vector<std::byte>& data) {
    PageHeader h = read_page_header(data);
    // Espace libre = entre la fin du slot directory et la première ligne
    std::size_t slot_dir_end = PAGE_HEADER_SIZE + h.slot_count * SLOT_SIZE;
    if (slot_dir_end > h.free_space_offset) {
        return 0;   // incohérent
    }
    return h.free_space_offset - slot_dir_end;
}

int page_insert_row(std::vector<std::byte>& data,
                    const std::vector<std::byte>& row_bytes) {
    if (row_bytes.empty()) {
        return -1;
    }
    if (row_bytes.size() > 65535) {
        return -1;   // trop grande
    }
    PageHeader h = read_page_header(data);
    std::size_t slot_dir_end = PAGE_HEADER_SIZE + h.slot_count * SLOT_SIZE;
    std::size_t free = h.free_space_offset - slot_dir_end;

    // Espace nécessaire = slot (4) + row_bytes
    if (free < SLOT_SIZE + row_bytes.size()) {
        // Tenter une compaction
        page_compact(data);
        h = read_page_header(data);
        slot_dir_end = PAGE_HEADER_SIZE + h.slot_count * SLOT_SIZE;
        free = h.free_space_offset - slot_dir_end;
        if (free < SLOT_SIZE + row_bytes.size()) {
            return -1;   // pas de place même après compaction
        }
    }

    // Nouvelle position de la ligne (depuis la fin de l'espace libre)
    std::uint16_t row_offset =
        static_cast<std::uint16_t>(h.free_space_offset - row_bytes.size());
    std::memcpy(data.data() + row_offset, row_bytes.data(), row_bytes.size());

    // Ajouter le slot
    std::uint32_t new_slot = h.slot_count;
    Slot s;
    s.offset = row_offset;
    s.length = static_cast<std::uint16_t>(row_bytes.size());
    write_slot(data, new_slot, s);

    // Mettre à jour le header
    h.slot_count = new_slot + 1;
    h.free_space_offset = row_offset;
    write_page_header(data, h);

    return static_cast<int>(new_slot);
}

bool page_read_row(const std::vector<std::byte>& data,
                   std::uint32_t slot_idx,
                   std::vector<std::byte>& out) {
    PageHeader h = read_page_header(data);
    if (slot_idx >= h.slot_count) return false;
    Slot s = read_slot(data, slot_idx);
    if (s.length == 0) return false;   // slot vide
    if (s.offset + s.length > data.size()) return false;
    out.assign(data.begin() + s.offset, data.begin() + s.offset + s.length);
    return true;
}

bool page_remove_row(std::vector<std::byte>& data, std::uint32_t slot_idx) {
    PageHeader h = read_page_header(data);
    if (slot_idx >= h.slot_count) return false;
    Slot s = read_slot(data, slot_idx);
    if (s.length == 0) return false;
    // Marquer le slot vide (l'espace n'est pas récupéré immédiatement)
    s.length = 0;
    write_slot(data, slot_idx, s);
    return true;
}

void page_compact(std::vector<std::byte>& data) {
    PageHeader h = read_page_header(data);

    // Collecter les lignes vivantes avec leur index de slot d'origine
    struct Live {
        std::uint32_t slot_idx;
        std::uint16_t offset;
        std::uint16_t length;
    };
    std::vector<Live> lives;
    lives.reserve(h.slot_count);
    for (std::uint32_t i = 0; i < h.slot_count; ++i) {
        Slot s = read_slot(data, i);
        if (s.length > 0) {
            lives.push_back({i, s.offset, s.length});
        }
    }

    // Nouveau tableau de slots : préserve les indices
    std::vector<Slot> new_slots(h.slot_count, Slot{0, 0});

    // Réécrire les lignes compactées depuis la fin
    std::vector<std::byte> new_data(data.size(), std::byte{0});
    std::size_t write_pos = data.size();
    for (const auto& live : lives) {
        write_pos -= live.length;
        std::memcpy(new_data.data() + write_pos,
                    data.data() + live.offset, live.length);
        // Slot à son index d'origine
        new_slots[live.slot_idx].offset = static_cast<std::uint16_t>(write_pos);
        new_slots[live.slot_idx].length = live.length;
    }

    // Réécrire le header + slot directory
    data = std::move(new_data);
    h.free_space_offset = static_cast<std::uint32_t>(write_pos);
    write_page_header(data, h);
    for (std::uint32_t i = 0; i < new_slots.size(); ++i) {
        write_slot(data, i, new_slots[i]);
    }
}

Slot read_slot(const std::vector<std::byte>& data, std::uint32_t slot_idx) {
    if (data.size() < PAGE_HEADER_SIZE) {
        throw std::runtime_error("Data trop petite pour un Slot");
    }
    std::size_t pos = PAGE_HEADER_SIZE + slot_idx * SLOT_SIZE;
    if (pos + SLOT_SIZE > data.size()) {
        throw std::runtime_error("Slot index hors bornes");
    }
    Slot s;
    std::memcpy(&s.offset, data.data() + pos, 2);
    std::memcpy(&s.length, data.data() + pos + 2, 2);
    return s;
}

void write_slot(std::vector<std::byte>& data,
                std::uint32_t slot_idx,
                const Slot& slot) {
    if (data.size() < PAGE_HEADER_SIZE) {
        throw std::runtime_error("Data trop petite pour un Slot");
    }
    std::size_t pos = PAGE_HEADER_SIZE + slot_idx * SLOT_SIZE;
    if (pos + SLOT_SIZE > data.size()) {
        throw std::runtime_error("Slot index hors bornes");
    }
    std::memcpy(data.data() + pos, &slot.offset, 2);
    std::memcpy(data.data() + pos + 2, &slot.length, 2);
}

} // namespace kerneldb