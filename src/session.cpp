#include "kerneldb/session.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace kerneldb {

namespace {

// Extrait le nom de base explicite d'un statement (vide si non spécifié)
// Extrait le nom de base explicite d'un statement.
// Retourne {} si le statement ne référence pas de base explicitement.
template<typename T>
std::string extract_db(const T&) {
    // Cas par défaut : aucun champ `database`
    return {};
}

template<typename T>
std::string explicit_database(const T& s) {
    using S = std::decay_t<T>;

    if constexpr (std::is_same_v<S, InsertStatement> ||
                  std::is_same_v<S, UpdateStatement> ||
                  std::is_same_v<S, DeleteStatement> ||
                  std::is_same_v<S, DropTableStatement> ||
                  std::is_same_v<S, AlterTableStatement> ||
                  std::is_same_v<S, SelectStatement>) {
        return s.database;
    }
    // R07 : pour PREPARE, la base cible est celle du statement interne
    else if constexpr (std::is_same_v<S, PrepareStatement>) {
        if (s.inner) {
            return std::visit([](const auto& inner_stmt) -> std::string {
                return explicit_database(inner_stmt);
            }, *s.inner);
        }
        return {};
    }
    else {
        return {};
    }
}

// Lit le page_size depuis un fichier config.meta
std::size_t read_page_size(const std::filesystem::path& dir) {
    std::ifstream in(dir / "config.meta");
    if (!in) return PAGE_SIZE_DEFAULT;

    std::string key;
    std::size_t value = PAGE_SIZE_DEFAULT;
    while (in >> key >> value) {
        if (key == "page_size") return value;
    }
    return PAGE_SIZE_DEFAULT;
}

} // namespace

// ---------- Construction ----------

Session::Session(const std::filesystem::path& data_dir)
    : data_dir_(resolve_data_dir(data_dir)) {
    std::filesystem::create_directories(data_dir_);
}

std::filesystem::path Session::resolve_data_dir(
    const std::filesystem::path& requested) {
    // 1. Si un chemin explicite est donné
    if (!requested.empty()) return requested;

    // 2. Variable d'environnement KERNELDB_DATA_DIR
    if (const char* env = std::getenv("KERNELDB_DATA_DIR")) {
        return std::filesystem::path(env);
    }

    // 3. $HOME/.kerneldb (Linux/macOS) ou %USERPROFILE%\.kerneldb (Windows)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".kerneldb";
    }
#ifdef _WIN32
    if (const char* up = std::getenv("USERPROFILE")) {
        return std::filesystem::path(up) / ".kerneldb";
    }
#endif

    // 4. Fallback : ./kerneldb_data
    return std::filesystem::path("kerneldb_data");
}

// ---------- Validation du nom ----------

void Session::validate_database_name(const std::string& name) {
    if (name.empty())
        throw std::runtime_error("Nom de base vide");

    // Refuse les séparateurs de chemin et les caractères spéciaux
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            throw std::runtime_error(
                "Nom de base invalide (caractère interdit): " + name);
        }
    }

    // Refuse les noms commençant par un point (fichiers cachés)
    if (name[0] == '.') {
        throw std::runtime_error("Nom de base invalide (commence par '.'): " + name);
    }
}

// ---------- Gestion des bases ----------

void Session::create_database(const std::string& name,
                                DatabaseOptions opts) {
    validate_database_name(name);

    // Vérifie que la base n'existe pas
    if (has_database(name)) {
        throw std::runtime_error("Base déjà existante: " + name);
    }

    auto path = data_dir_ / name;

    if (std::filesystem::exists(path)) {
        // Le dossier existe mais n'est pas chargé
        if (!std::filesystem::is_empty(path)) {
            throw std::runtime_error(
                "Le dossier existe déjà et n'est pas vide: " + path.string());
        }
    } else {
        std::filesystem::create_directories(path);
    }

    // Écrit config.meta
    {
        std::ofstream out(path / "config.meta", std::ios::trunc);
        if (!out) throw std::runtime_error("Impossible d'écrire config.meta");
        out << "page_size " << opts.page_size << "\n";
    }

    // Crée un catalogue vide
    {
        std::ofstream cat(path / "catalog.meta", std::ios::trunc);
        if (!cat) throw std::runtime_error("Impossible d'écrire catalog.meta");
    }

    // Charge la base en mémoire
    load_or_open(name);
}

void Session::drop_database(const std::string& name) {
    validate_database_name(name);

    auto path = data_dir_ / name;
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Base inconnue: " + name);
    }

    // C02 : si la base est ouverte par CETTE session, on la ferme d'abord
    // (sinon son verrou bloquerait la suppression)
    auto it = databases_.find(name);
    if (it != databases_.end()) {
        if (active_ == it->second.get()) {
            active_ = nullptr;
            active_name_.clear();
        }
        databases_.erase(it);   // ← libère le verrou (destructeur de Database)
    }

    // C02 : acquérir le verrou avant suppression
    // (échoue si une AUTRE instance détient la base)
    DatabaseLock lock(path);

    // Supprimer le répertoire
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
        throw std::runtime_error(
            "Impossible de supprimer la base: " + name + " (" + ec.message() + ")");
    }
}

void Session::use(const std::string& name) {
    validate_database_name(name);

    if (!has_database(name)) {
        auto path = data_dir_ / name;
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Base inconnue: " + name);
        }
    }

    auto& db = load_or_open(name);
    active_ = &db;
    active_name_ = name;
}

// ---------- Base active ----------

Database& Session::active() {
    if (!active_) {
        throw std::runtime_error("Aucune base active (utilisez USE)");
    }
    return *active_;
}

const Database& Session::active() const {
    if (!active_) {
        throw std::runtime_error("Aucune base active (utilisez USE)");
    }
    return *active_;
}

// ---------- Accès direct ----------

Database& Session::database(const std::string& name) {
    // Déjà en mémoire ?
    auto it = databases_.find(name);
    if (it != databases_.end()) return *it->second;

    // F10 : ouvrir à la demande si existe sur disque
    if (!std::filesystem::exists(data_dir_ / name)) {
        throw std::runtime_error("Base inconnue: " + name);
    }
    return load_or_open(name);
}

bool Session::has_database(const std::string& name) const {
    // En mémoire ?
    if (databases_.count(name) > 0) return true;
    // F10 : sur disque ?
    return std::filesystem::exists(data_dir_ / name);
}

std::vector<std::string> Session::database_names() const {
    std::vector<std::string> names;
    names.reserve(databases_.size());
    for (const auto& [n, _] : databases_) names.push_back(n);

    // Ajoute aussi les bases sur disque non encore chargées
    if (std::filesystem::exists(data_dir_)) {
        for (const auto& entry : std::filesystem::directory_iterator(data_dir_)) {
            if (!entry.is_directory()) continue;
            std::string name = entry.path().filename().string();
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        }
    }

    std::sort(names.begin(), names.end());
    return names;
}

Database& Session::load_or_open(const std::string& name) {
    auto it = databases_.find(name);
    if (it != databases_.end()) return *it->second;

    auto path = data_dir_ / name;

    DatabaseOptions opts;
    opts.page_size = read_page_size(path);

    auto db = std::make_unique<Database>(path, opts);
    Database* ptr = db.get();
    databases_.emplace(name, std::move(db));
    return *ptr;
}

// ---------- Exécution SQL ----------

QueryResult Session::execute(const std::string& sql) {
    auto top = Parser{}.parse(sql);

    return std::visit([this](const auto& s) -> QueryResult {
        using T = std::decay_t<decltype(s)>;

        // ---------- CREATE DATABASE ----------
        if constexpr (std::is_same_v<T, CreateDatabaseStatement>) {
            DatabaseOptions opts;
            if (s.has_page_size) opts.page_size = s.page_size;
            create_database(s.name, opts);
            return {{}, {}, "Base " + s.name + " créée"};
        }

        // ---------- DROP DATABASE ----------
        // ---------- DROP DATABASE ----------
        else if constexpr (std::is_same_v<T, DropDatabaseStatement>) {
            drop_database(s.name);
            // A03 : nettoyer les prepared statements de cette base
            for (auto it = prepared_routing_.begin(); it != prepared_routing_.end(); ) {
                if (it->second == s.name) {
                    it = prepared_routing_.erase(it);
                } else {
                    ++it;
                }
            }
            return {{}, {}, "Base " + s.name + " supprimée"};
        }
        // ---------- USE ----------
        else if constexpr (std::is_same_v<T, UseStatement>) {
            use(s.name);
            return {{}, {}, "Base active : " + s.name};
        }

        // ---------- SHOW DATABASES ----------
        else if constexpr (std::is_same_v<T, ShowDatabasesStatement>) {
            QueryResult result;
            result.columns.push_back("name");
            for (const auto& n : database_names()) {
                Row row;
                row.values.emplace_back(n);
                result.rows.push_back(std::move(row));
            }
            result.message = std::to_string(result.rows.size()) + " base(s)";
            return result;
        }

        // ---------- Commandes SQL classiques ----------
                // ---------- PREPARE (R07) ----------
        else if constexpr (std::is_same_v<T, PrepareStatement>) {
            if (!active_) {
                throw std::runtime_error("Aucune base active (utilisez USE)");
            }
            std::string target = explicit_database(s);
            if (target.empty()) target = active_name();
            if (!has_database(target)) {
                throw std::runtime_error("Base inconnue: " + target);
            }
            // A04 : refuser un nom déjà utilisé dans cette session
            if (prepared_routing_.count(s.name)) {
                throw std::runtime_error(
                    "Prepared statement déjà existant: " + s.name
                    + " (utilisez DEALLOCATE pour le libérer)");
            }
            Database& db = database(target);
            QueryResult r = db.execute_top_level(TopLevelStatement{s});
            prepared_routing_[s.name] = target;
            return r;
        }

        // ---------- EXECUTE (R07) ----------
        else if constexpr (std::is_same_v<T, ExecuteStatement>) {
            auto it = prepared_routing_.find(s.name);
            if (it == prepared_routing_.end()) {
                // Fallback : déléguer à la base active (comportement historique)
                if (!active_) {
                    throw std::runtime_error(
                        "Aucune base active (utilisez USE)");
                }
                return active_->execute_top_level(TopLevelStatement{s});
            }
            return database(it->second)
                .execute_top_level(TopLevelStatement{s});
        }

        // ---------- DEALLOCATE (R07) ----------
        // ---------- DEALLOCATE (A03) ----------
        else if constexpr (std::is_same_v<T, DeallocateStatement>) {
            auto it = prepared_routing_.find(s.name);
            if (it == prepared_routing_.end()) {
                throw std::runtime_error(
                    "Prepared statement inconnu: " + s.name);
            }
            std::string target = it->second;
            // A03 : exécuter DEALLOCATE sur la base propriétaire
            QueryResult r = database(target)
                .execute_top_level(TopLevelStatement{s});
            // Retirer du routage seulement après succès
            prepared_routing_.erase(it);
            return r;
        }
        else {
            if (!active_) {
                throw std::runtime_error("Aucune base active (utilisez USE)");
            }

            // Si le statement référence explicitement une base, on route
            // vers cette base ; sinon, on utilise la base active.
            std::string target_db = explicit_database(s);

            if (!target_db.empty()) {
                return database(target_db).execute_top_level(TopLevelStatement{s});
            }
            return active_->execute_top_level(TopLevelStatement{s});
        }
    }, top);
}

} // namespace kerneldb