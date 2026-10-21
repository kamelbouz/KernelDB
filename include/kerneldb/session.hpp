#pragma once
#include "kerneldb/database.hpp"
#include "kerneldb/page.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kerneldb {

// Session = point d'entrée multi-base.
//
// Responsabilités :
//  - Gérer le répertoire des bases (KERNELDB_DATA_DIR ou ~/.kerneldb)
//  - Créer / supprimer / lister les bases
//  - Maintenir la base active (USE)
//  - Dispatcher les commandes SQL vers la bonne Database
//
class Session {
public:
    // Si data_dir est vide, résolu depuis KERNELDB_DATA_DIR ou $HOME/.kerneldb/

    explicit Session(const std::filesystem::path& data_dir = {});

    // ---------- Gestion des bases ----------

    void create_database(const std::string& name, DatabaseOptions opts = {});
    void drop_database(const std::string& name);
    void use(const std::string& name);

    // ---------- Base active ----------

    bool has_active() const { return active_ != nullptr; }
    const std::string& active_name() const { return active_name_; }
    Database& active();
    const Database& active() const;

    // ---------- Accès direct à une base ----------

    Database& database(const std::string& name);
    bool has_database(const std::string& name) const;
    std::vector<std::string> database_names() const;

    // ---------- Exécution ----------

    QueryResult execute(const std::string& sql);

    // ---------- Répertoire des bases ----------

    const std::filesystem::path& data_dir() const { return data_dir_; }

private:
    std::filesystem::path data_dir_;
    std::map<std::string, std::unique_ptr<Database>> databases_;
    // R07 : map (nom du prepared statement → nom de la base cible)
    std::map<std::string, std::string> prepared_routing_;
    Database* active_ = nullptr;
    std::string active_name_;

    // Charge (ou ouvre) une base dans `databases_`, retourne sa référence
    Database& load_or_open(const std::string& name);

    // Résout le chemin du data_dir
    static std::filesystem::path resolve_data_dir(
        const std::filesystem::path& requested);

    // Vérifie qu'un nom de base est valide (pas vide, pas de séparateur)
    static void validate_database_name(const std::string& name);
};

} // namespace kerneldb