#include "kerneldb/storage.hpp"
#include "kerneldb/page.hpp" 
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <sstream>

#if defined(_WIN32)
    #include <windows.h>
#endif

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

// ---------- Sérialisation ----------

std::size_t serialize_row(std::ostream& out,
                           const TableSchema& schema,
                           const Row& row) {
    std::size_t total = 0;

    for (std::size_t i = 0; i < row.values.size(); ++i) {
        const auto& col = schema.columns[i];
        const auto& value = row.values[i];

        if (is_null(value)) {
            std::uint8_t marker = 0xFF;
            write_raw(out, marker);
            total += 1;
            continue;
        }

        std::uint8_t marker = 0x00;
        write_raw(out, marker);
        total += 1;

        if (col.type == DataType::Integer) {
            auto v = std::get<std::int64_t>(value);
            write_raw(out, v);
            total += 8;
        }
        else if (col.type == DataType::Double) {
            double v = std::holds_alternative<double>(value)
                     ? std::get<double>(value)
                     : static_cast<double>(std::get<std::int64_t>(value));
            write_raw(out, v);
            total += 8;
        }
        else if (col.type == DataType::Boolean) {
            std::uint8_t v = std::get<bool>(value) ? 1 : 0;
            write_raw(out, v);
            total += 1;
        }
        else {
            const auto& s = std::get<std::string>(value);
            auto size = static_cast<std::uint32_t>(s.size());
            write_raw(out, size);
            out.write(s.data(), size);
            total += 4 + size;
        }
    }
    return total;
}

Row deserialize_row(std::istream& in, const TableSchema& schema) {
    Row row;
    row.values.reserve(schema.columns.size());

    for (const auto& col : schema.columns) {
        std::uint8_t marker = read_raw<std::uint8_t>(in);
        if (!in) throw std::runtime_error("Ligne de table incomplète");
        if (marker != 0x00 && marker != 0xFF) {
            throw std::runtime_error(
                "Fichier de table corrompu : marqueur invalide " +
                std::to_string(marker));
        }
        if (marker == 0xFF) {
            row.values.emplace_back(std::monostate{});
            continue;
        }

        if (col.type == DataType::Integer) {
            row.values.emplace_back(read_raw<std::int64_t>(in));
        }
        else if (col.type == DataType::Double) {
            row.values.emplace_back(read_raw<double>(in));
        }
        else if (col.type == DataType::Boolean) {
            row.values.emplace_back(read_raw<std::uint8_t>(in) != 0);
        }
        else {
            auto size = read_raw<std::uint32_t>(in);
            if (size > col.max_length)
                throw std::runtime_error("Ligne corrompue");
            std::string s(size, '\0');
            in.read(s.data(), size);
            row.values.emplace_back(std::move(s));
        }
        if (!in) throw std::runtime_error("Ligne de table incomplète");
    }
    return row;
}

// J4.5 : helpers en mémoire

std::vector<std::byte> serialize_row_to_bytes(const TableSchema& schema,
                                               const Row& row) {
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    serialize_row(ss, schema, row);
    std::string s = ss.str();
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<std::byte>(c));
    return out;
}

Row deserialize_row_from_bytes(const std::vector<std::byte>& bytes,
                                const TableSchema& schema) {
    std::string s;
    s.reserve(bytes.size());
    for (auto b : bytes) s += static_cast<char>(b);
    std::stringstream ss(s, std::ios::in | std::ios::binary);
    return deserialize_row(ss, schema);
}


namespace {

std::size_t row_size(const TableSchema& schema, const Row& row) {
    std::size_t total = 0;
    for (std::size_t i = 0; i < row.values.size(); ++i) {
        const auto& col = schema.columns[i];
        const auto& value = row.values[i];
        total += 1;

        if (is_null(value)) continue;

        if (col.type == DataType::Integer)      total += 8;
        else if (col.type == DataType::Double)  total += 8;
        else if (col.type == DataType::Boolean) total += 1;
        else {
            const auto& s = std::get<std::string>(value);
            total += 4 + s.size();
        }
    }
    return total;
}

void validate_row(const TableSchema& schema, const Row& row) {
    if (row.values.size() != schema.columns.size())
        throw std::runtime_error("Nombre de valeurs incorrect");
    for (std::size_t i = 0; i < row.values.size(); ++i) {
        if (is_parameter(row.values[i]))
            throw std::runtime_error("Parameter '?' non substitué (bug interne)");
        if (!value_matches_type(row.values[i], schema.columns[i]))
            throw std::runtime_error("Type incorrect pour la colonne " + schema.columns[i].name);
    }
}

} // namespace

// ---------- Constructeur ----------

StorageManager::StorageManager(std::filesystem::path directory,
                                std::size_t default_page_size)
    : directory_(std::move(directory)),
      default_page_size_(default_page_size),
      buffer_pool_(BufferPool::DEFAULT_CAPACITY,
                   [this](const PageKey& key, const Page& page) {
                       write_page_to_disk(key.table, page);
                   }) {
    validate_page_size(default_page_size_);
    std::filesystem::create_directories(directory_);
}

std::filesystem::path StorageManager::table_path(const std::string& name) const {
    return directory_ / (name + ".tbl");
}

// ---------- Création ----------

void StorageManager::create_table(const TableSchema& schema,
                                   std::size_t page_size) const {
    std::size_t ps = (page_size == 0) ? default_page_size_ : page_size;
    validate_page_size(ps);

    std::ofstream out(table_path(schema.name),
                       std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Impossible de créer le fichier de table");

    // Header
    out.write(FILE_MAGIC, 4);
    write_raw(out, std::uint32_t{1});
    write_raw(out, static_cast<std::uint32_t>(ps));
    write_raw(out, std::uint32_t{1});   // 1 page initiale
    write_raw(out, std::uint64_t{0});

    // Page 0 vierge
    std::vector<std::byte> page_data(ps, std::byte{0});
    page_init(page_data, 0);
    out.write(reinterpret_cast<const char*>(page_data.data()), ps);

    if (!out) throw std::runtime_error("Écriture du header échouée");
}

// ---------- Suppression ----------

void StorageManager::drop_table(const std::string& name) const {
    auto path = table_path(name);
    std::error_code ec;
    if (!std::filesystem::exists(path)) return;
    std::filesystem::remove(path, ec);
    if (ec) throw std::runtime_error("Impossible de supprimer la table: " + name);
}

// ---------- Informations ----------

std::size_t StorageManager::page_size_of(const std::string& name) const {
    std::ifstream in(table_path(name), std::ios::binary);
    if (!in) throw std::runtime_error("Impossible d'ouvrir la table: " + name);

    char magic[4];
    in.read(magic, 4);
    if (in.gcount() != 4 || std::memcmp(magic, FILE_MAGIC, 4) != 0)
        throw std::runtime_error("Fichier de table invalide: " + name);

    auto version = read_raw<std::uint32_t>(in);
    if (version != 1) {
        throw std::runtime_error(
            "Fichier de table : version non supportée (" +
            std::to_string(version) + "): " + name);
    }
    auto ps = read_raw<std::uint32_t>(in);
    if (ps < PAGE_SIZE_MIN || ps > PAGE_SIZE_MAX) {
        throw std::runtime_error(
            "Fichier de table : taille de page invalide (" +
            std::to_string(ps) + "): " + name);
    }
    return ps;
}

std::uint32_t StorageManager::page_count(const std::string& name) const {
    std::ifstream in(table_path(name), std::ios::binary);
    if (!in) throw std::runtime_error("Impossible d'ouvrir la table: " + name);

    char magic[4];
    in.read(magic, 4);
    if (in.gcount() != 4 || std::memcmp(magic, FILE_MAGIC, 4) != 0)
        throw std::runtime_error("Fichier de table invalide: " + name);

    auto version = read_raw<std::uint32_t>(in);
    if (version != 1) {
        throw std::runtime_error(
            "Fichier de table : version non supportée (" +
            std::to_string(version) + "): " + name);
    }
    read_raw<std::uint32_t>(in);
    auto n = read_raw<std::uint32_t>(in);
    return n;
}

// ---------- J4.4 : helpers disque ----------

Page StorageManager::read_page_from_disk(const std::string& table,
                                          std::uint32_t page_id) const {
    std::size_t ps = page_size_of(table);

    std::ifstream in(table_path(table), std::ios::binary);
    if (!in) throw std::runtime_error("Impossible d'ouvrir la table: " + table);

    std::size_t offset = FILE_HEADER_SIZE + page_id * ps;
    in.seekg(static_cast<std::streamoff>(offset));

    Page page(page_id, static_cast<std::uint32_t>(ps));
    in.read(reinterpret_cast<char*>(page.data.data()), ps);
    if (!in) throw std::runtime_error(
        "Lecture de page échouée (page " + std::to_string(page_id) + ")");
    return page;
}

void StorageManager::write_page_to_disk(const std::string& table,
                                         const Page& page) const {
    std::size_t ps = page_size_of(table);
    if (page.page_size != ps)
        throw std::runtime_error("Taille de page incorrecte");

    std::fstream io(table_path(table),
                     std::ios::binary | std::ios::in | std::ios::out);
    if (!io) throw std::runtime_error("Impossible d'ouvrir la table: " + table);

    std::size_t offset = FILE_HEADER_SIZE + page.page_id * ps;
    io.seekp(static_cast<std::streamoff>(offset));
    io.write(reinterpret_cast<const char*>(page.data.data()), ps);
    if (!io) throw std::runtime_error("Écriture de page échouée");
}

// ---------- J4.4 : read_page / write_page via cache ----------

Page StorageManager::read_page(const std::string& table,
                                std::uint32_t page_id) const {
    PageKey key{table, page_id};
    Page& cached = buffer_pool_.fetch(
        key,
        [this, &table](const PageKey& k) {
            return read_page_from_disk(table, k.page_id);
        });
    Page result = cached;   // copie
    buffer_pool_.unpin(key);
    return result;
}

void StorageManager::write_page(const std::string& table,
                                 const Page& page) const {
    PageKey key{table, page.page_id};

    // J4.5 : si la page est déjà en cache, la mettre à jour.
    // Sinon, l'ajouter directement (create) sans passer par le loader
    // (car la page peut ne pas exister sur disque).
    Page* cached = nullptr;
    if (buffer_pool_.contains(key)) {
        // Déjà en cache : fetch pour pinner + récupérer
        cached = &buffer_pool_.fetch(
            key,
            [this, &table](const PageKey& k) {
                return read_page_from_disk(table, k.page_id);
            });
    } else {
        // Pas en cache : create (page vierge)
        cached = &buffer_pool_.create(key, page.page_size);
    }

    *cached = page;
    buffer_pool_.mark_dirty(key);
    buffer_pool_.unpin(key);
}

// ---------- Scan complet ----------

std::vector<Row> StorageManager::scan(const TableSchema& schema) const {
    std::vector<Row> rows;
    std::uint32_t n = page_count(schema.name);

    for (std::uint32_t pid = 0; pid < n; ++pid) {
        Page page = read_page(schema.name, pid);
        std::uint32_t slots = page_slot_count(page.data);
        for (std::uint32_t i = 0; i < slots; ++i) {
            std::vector<std::byte> row_bytes;
            if (page_read_row(page.data, i, row_bytes)) {
                rows.push_back(deserialize_row_from_bytes(row_bytes, schema));
            }
        }
    }
    return rows;
}


RowId StorageManager::insert(const TableSchema& schema, const Row& row) const {
    validate_row(schema, row);

    // Sérialiser la ligne en bytes
    auto row_bytes = serialize_row_to_bytes(schema, row);

    std::size_t ps = page_size_of(schema.name);
    if (row_bytes.size() + SLOT_SIZE + PAGE_HEADER_SIZE > ps) {
        throw std::runtime_error(
            "Ligne trop grande pour tenir dans une page");
    }

    // Chercher une page avec espace libre
    std::uint32_t n = page_count(schema.name);
    for (std::uint32_t pid = 0; pid < n; ++pid) {
        Page page = read_page(schema.name, pid);
        int slot = page_insert_row(page.data, row_bytes);
        if (slot >= 0) {
            write_page(schema.name, page);
            RowId rid;
            rid.page_id = pid;
            rid.slot_id = static_cast<std::uint16_t>(slot);
            return rid;
        }
    }

    // Aucune page avec espace → créer une nouvelle page
    std::uint32_t new_pid = n;
    Page new_page(new_pid, static_cast<std::uint32_t>(ps));
    page_init(new_page.data, new_pid);
    int slot = page_insert_row(new_page.data, row_bytes);
    if (slot < 0) {
        throw std::runtime_error("Impossible d'insérer dans une page vierge");
    }
    write_page(schema.name, new_page);

    // Mettre à jour page_count dans le header
    {
        std::fstream io(table_path(schema.name),
                         std::ios::binary | std::ios::in | std::ios::out);
        if (!io) throw std::runtime_error("Impossible d'ouvrir la table");
        io.seekp(12);   // offset page_count
        write_raw(io, new_pid + 1);
    }
    RowId rid;
    rid.page_id = new_pid;
    rid.slot_id = static_cast<std::uint16_t>(slot);
    return rid;
}

void StorageManager::rewrite(const TableSchema& schema,
                              const std::vector<Row>& rows) const {
    auto final_path = table_path(schema.name);
    auto tmp_path = final_path;
    tmp_path += ".tmp";

    std::size_t ps = page_size_of(schema.name);

    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Impossible de créer le fichier temporaire");

        // Header (provisoire, page_count à 0)
        out.write(FILE_MAGIC, 4);
        write_raw(out, std::uint32_t{1});
        write_raw(out, static_cast<std::uint32_t>(ps));
        std::streampos page_count_pos = out.tellp();
        write_raw(out, std::uint32_t{0});
        write_raw(out, std::uint64_t{0});

        // Écrire les pages
        std::uint32_t page_id = 0;
        std::vector<std::byte> page_data(ps, std::byte{0});
        page_init(page_data, page_id);

        for (const auto& row : rows) {
            validate_row(schema, row);
            auto row_bytes = serialize_row_to_bytes(schema, row);
            int slot = page_insert_row(page_data, row_bytes);
            if (slot < 0) {
                // Page pleine → écrire et créer une nouvelle page
                out.write(reinterpret_cast<const char*>(page_data.data()), ps);
                ++page_id;
                page_data.assign(ps, std::byte{0});
                page_init(page_data, page_id);
                slot = page_insert_row(page_data, row_bytes);
                if (slot < 0) {
                    throw std::runtime_error(
                        "Ligne trop grande pour tenir dans une page");
                }
            }
        }
        // Écrire la dernière page
        out.write(reinterpret_cast<const char*>(page_data.data()), ps);
        std::uint32_t total_pages = page_id + 1;

        // Mettre à jour page_count
        out.seekp(page_count_pos);
        write_raw(out, total_pages);
        out.flush();
        if (!out) throw std::runtime_error("Écriture de la table échouée");
    }
    // J4.5 : vider le cache (les pages sur disque ont changé)
    buffer_pool_.clear();
#if defined(_WIN32)
    if (!ReplaceFileW(
            final_path.wstring().c_str(),
            tmp_path.wstring().c_str(),
            nullptr,
            REPLACEFILE_WRITE_THROUGH,
            nullptr,
            nullptr)) {
        std::error_code ec;
        std::filesystem::rename(tmp_path, final_path, ec);
        if (ec) {
            std::error_code ec2;
            std::filesystem::remove(tmp_path, ec2);
            throw std::runtime_error(
                "C08 : impossible de remplacer le fichier: " + ec.message());
        }
    }
#else
    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp_path, ec2);
        throw std::runtime_error(
            "C08 : impossible de remplacer le fichier: " + ec.message());
    }
#endif
}
std::vector<RowWithId>
StorageManager::scan_with_ids(const TableSchema& schema) const {
    std::vector<RowWithId> rows;
    std::uint32_t n = page_count(schema.name);

    for (std::uint32_t pid = 0; pid < n; ++pid) {
        Page page = read_page(schema.name, pid);
        std::uint32_t slots = page_slot_count(page.data);
        for (std::uint32_t i = 0; i < slots; ++i) {
            std::vector<std::byte> row_bytes;
            if (page_read_row(page.data, i, row_bytes)) {
                RowWithId rw;
                rw.rid.page_id = pid;
                rw.rid.slot_id = static_cast<std::uint16_t>(i);
                rw.row = deserialize_row_from_bytes(row_bytes, schema);
                rows.push_back(std::move(rw));
            }
        }
    }
    return rows;
}
std::optional<Row> StorageManager::read_by_id(const TableSchema& schema,
                                              const RowId& rid) const {
    if (rid.page_id >= page_count(schema.name)) {
        return std::nullopt;
    }
    Page page = read_page(schema.name, rid.page_id);
    std::vector<std::byte> bytes;
    if (!page_read_row(page.data, rid.slot_id, bytes)) {
        return std::nullopt;
    }
    return deserialize_row_from_bytes(bytes, schema);
}

void StorageManager::delete_by_id(const TableSchema& schema,
                                   const RowId& rid) const {
    Page page = read_page(schema.name, rid.page_id);
    if (!page_remove_row(page.data, rid.slot_id)) {
        throw std::runtime_error(
            "delete_by_id: slot déjà vide ou invalide "
            "(page " + std::to_string(rid.page_id) +
            ", slot " + std::to_string(rid.slot_id) + ")");
    }
    write_page(schema.name, page);
}
RowId StorageManager::update_by_id(const TableSchema& schema,
                                    const RowId& old_rid,
                                    const Row& new_row) const {
    validate_row(schema, new_row);
    auto new_bytes = serialize_row_to_bytes(schema, new_row);

    // Lire la page et le slot actuel
    Page page = read_page(schema.name, old_rid.page_id);
    std::vector<std::byte> old_bytes;
    if (!page_read_row(page.data, old_rid.slot_id, old_bytes)) {
        throw std::runtime_error("update_by_id: slot invalide");
    }

    // Si la nouvelle taille tient dans l'ancienne, réécriture in-place
    if (new_bytes.size() <= old_bytes.size()) {
        Slot slot = read_slot(page.data, old_rid.slot_id);
        std::memcpy(page.data.data() + slot.offset,
                    new_bytes.data(), new_bytes.size());
        slot.length = static_cast<std::uint16_t>(new_bytes.size());
        write_slot(page.data, old_rid.slot_id, slot);
        write_page(schema.name, page);
        return old_rid;
    }

    // Sinon, delete + insert
    delete_by_id(schema, old_rid);
    return insert(schema, new_row);
}
} // namespace kerneldb
