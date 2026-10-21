#pragma once
#include <filesystem>

namespace kerneldb {

// J3.1 : verrou exclusif d'une base (mode mono-connexion).
//
// Crée un fichier `.lock` dans le répertoire de la base à l'ouverture
// et le supprime à la fermeture. Lève std::runtime_error si le verrou
// est déjà pris (base déjà ouverte par une autre instance).
//
// Le fichier contient le PID du processus propriétaire et un timestamp,
// utiles pour le diagnostic en cas de verrou orphelin (crash).
//
// Non copiable, non déplaçable.
class DatabaseLock {
public:
    explicit DatabaseLock(const std::filesystem::path& dir);
    ~DatabaseLock();

    DatabaseLock(const DatabaseLock&) = delete;
    DatabaseLock& operator=(const DatabaseLock&) = delete;
    DatabaseLock(DatabaseLock&&) = delete;
    DatabaseLock& operator=(DatabaseLock&&) = delete;

    // Chemin du fichier .lock (utile pour les tests)
    const std::filesystem::path& path() const { return lock_path_; }

private:
    std::filesystem::path lock_path_;
    bool acquired_ = false;
};

} // namespace kerneldb