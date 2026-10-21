#include "kerneldb/journal.hpp"
#include "kerneldb/catalog.hpp"
#include "kerneldb/storage.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace kerneldb {

namespace {

constexpr const char* JOURNAL_MAGIC = "MDBJ";
constexpr std::uint32_t JOURNAL_VERSION = 1;

void write_u32(std::ostream& out, std::uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

std::uint32_t read_u32(std::istream& in) {
    std::uint32_t v = 0;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (!in) {
        // C06 : signaler une fin de fichier tolérée
        if (in.eof()) throw std::runtime_error("__EOF__");
        throw std::runtime_error("Journal corrompu : lecture incomplète");
    }
    return v;
}

void write_u64(std::ostream& out, std::uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

std::uint64_t read_u64(std::istream& in) {
    std::uint64_t v = 0;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (!in) {
        if (in.eof()) throw std::runtime_error("__EOF__");
        throw std::runtime_error("Journal corrompu : lecture incomplète");
    }
    return v;
}

// J3.3 (C05) : synchronise un fichier sur disque (fsync portable).
// Windows : FlushFileBuffers via CreateFileW.
// POSIX   : fsync via open/close.
void sync_file(const std::filesystem::path& path) {
#if defined(_WIN32)
    HANDLE h = CreateFileW(
        path.wstring().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(
            "sync_file: impossible d'ouvrir " + path.string());
    }
    BOOL ok = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        throw std::runtime_error(
            "sync_file: FlushFileBuffers a échoué pour " + path.string());
    }
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error(
            "sync_file: impossible d'ouvrir " + path.string());
    }
    int rc = ::fsync(fd);
    ::close(fd);
    if (rc != 0) {
        throw std::runtime_error(
            "sync_file: fsync a échoué pour " + path.string());
    }
#endif
}

} // namespace

// ---------- Journal ----------

Journal::Journal(const std::filesystem::path& dir)
    : dir_(dir), journal_path_(dir / "undo.log") {
    std::filesystem::create_directories(dir_);
}

void Journal::begin() {
    if (active_) {
        throw std::runtime_error("Journal : transaction déjà active");
    }

    // Crée (ou écrase) le fichier journal
    std::ofstream out(journal_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "Impossible de créer le journal: " + journal_path_.string());
    }
    out.write(JOURNAL_MAGIC, 4);
    write_u32(out, JOURNAL_VERSION);
    out.flush();
    out.close();
    sync_file(journal_path_);   // C05 : garantir l'en-tête sur disque
    active_ = true;
}

void Journal::record_table(const std::string& table,
                            const TableSchema& schema,
                            const std::vector<Row>& rows_before) {
    if (!active_) {
        throw std::runtime_error(
            "Journal::record_table appelé sans transaction active");
    }

    // Ouvre en append
    std::ofstream out(journal_path_, std::ios::binary | std::ios::app);
    if (!out) {
        throw std::runtime_error(
            "Impossible d'ouvrir le journal en écriture: "
            + journal_path_.string());
    }

    // Écrit [table_name_length u32] [table_name bytes]
    std::uint32_t name_len = static_cast<std::uint32_t>(table.size());
    write_u32(out, name_len);
    out.write(table.data(), name_len);

    // Écrit [row_count u32]
    std::uint32_t row_count = static_cast<std::uint32_t>(rows_before.size());
    write_u32(out, row_count);

    // Écrit chaque ligne sérialisée
    for (const auto& row : rows_before) {
        serialize_row(out, schema, row);
    }

    if (!out) {
        throw std::runtime_error(
            "Erreur d'écriture dans le journal: " + journal_path_.string());
    }

    // J3.3 (C05) : flush + fsync réel
    out.flush();
    out.close();
    sync_file(journal_path_);
}
void Journal::commit() {
    if (!active_) return;

    // C04 : propager l'erreur de suppression (ne pas faire de faux succès)
    std::error_code ec;
    std::filesystem::remove(journal_path_, ec);
    if (ec) {
        throw std::runtime_error(
            "Journal::commit : impossible de supprimer le journal: "
            + ec.message());
    }
    active_ = false;
}

void Journal::clear() {
    std::error_code ec;
    std::filesystem::remove(journal_path_, ec);
    active_ = false;
}

std::vector<Journal::PendingEntry> Journal::read_pending(
        const Catalog& catalog) const {
    std::vector<PendingEntry> result;

    if (!std::filesystem::exists(journal_path_)) {
        return result;   // pas de journal = rien à annuler
    }

    std::ifstream in(journal_path_, std::ios::binary);
    if (!in) {
        throw std::runtime_error(
            "Impossible de lire le journal: " + journal_path_.string());
    }

    // Vérifie MAGIC
    char magic[4];
    in.read(magic, 4);
    if (in.gcount() != 4 || std::memcmp(magic, JOURNAL_MAGIC, 4) != 0) {
        throw std::runtime_error("Journal corrompu : mauvais magic");
    }

    // Vérifie version
    std::uint32_t version = read_u32(in);
    if (version != JOURNAL_VERSION) {
        throw std::runtime_error(
            "Journal : version non supportée (" + std::to_string(version) + ")");
    }

    // Lit les entrées
    // C06 : tolérer une fin de journal incomplète (crash pendant l'écriture)
    try {
        while (in.peek() != std::char_traits<char>::eof()) {
            // [table_name_length u32] [table_name bytes]
            std::uint32_t name_len = read_u32(in);
            if (name_len == 0 || name_len > 1024) {
                throw std::runtime_error("Journal corrompu : nom de table invalide");
            }
            std::string table(name_len, '\0');
            in.read(table.data(), name_len);
            if (!in) {
                if (in.eof()) throw std::runtime_error("__EOF__");
                throw std::runtime_error("Journal corrompu : nom de table incomplet");
            }

            // [row_count u32]
            std::uint32_t row_count = read_u32(in);

            // Récupère le schéma
            const TableSchema& schema = catalog.table(table);

            // Lit chaque ligne
            std::vector<Row> rows;
            rows.reserve(row_count);
            for (std::uint32_t i = 0; i < row_count; ++i) {
                rows.push_back(deserialize_row(in, schema));
            }

            result.push_back({table, std::move(rows)});
        }
    } catch (const std::exception& e) {
        if (std::string(e.what()) == "__EOF__") {
            // C06 : fin incomplète → tolérer (garder les entrées déjà lues)
            // Pas de re-throw
        } else {
            throw;
        }
    }

    return result;
}

} // namespace kerneldb