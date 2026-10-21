#include "kerneldb/lock.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
    #include <windows.h>
    #include <process.h>
#else
    #include <fcntl.h>
    #include <signal.h>       // pour kill()
    #include <sys/types.h>    // pour pid_t
    #include <unistd.h>
    #include <cerrno>
#endif

namespace kerneldb {

namespace {

std::string current_pid_string() {
#if defined(_WIN32)
    return std::to_string(_getpid());
#else
    return std::to_string(getpid());
#endif

}

// C03 : vérifie si un processus est vivant
#if defined(_WIN32)
bool is_pid_alive(unsigned long pid) {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}
#else
bool is_pid_alive(long pid) {
    if (pid <= 0) return false;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH;
}
#endif

// C03 : lit le PID depuis un fichier .lock
long read_pid(const std::filesystem::path& lock_path) {
    std::ifstream in(lock_path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("pid=", 0) == 0) {
            try {
                return std::stol(line.substr(4));
            } catch (...) {
                return -1;
            }
        }
    }
    return -1;
}

} // namespace

DatabaseLock::DatabaseLock(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    lock_path_ = dir / ".lock";

    // C01 : acquisition ATOMIQUE du verrou
#if defined(_WIN32)
    HANDLE h = CreateFileW(
        lock_path_.wstring().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,                           // aucun partage
        nullptr,
        CREATE_NEW,                  // ← ATOMIQUE : échoue si existe
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

        if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) {
            // C03 : détection verrou orphelin
            long pid = read_pid(lock_path_);
            if (pid > 0 && !is_pid_alive(static_cast<unsigned long>(pid))) {
                // Verrou orphelin → supprimer et retenter
                std::error_code ec;
                std::filesystem::remove(lock_path_, ec);
                h = CreateFileW(
                    lock_path_.wstring().c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h == INVALID_HANDLE_VALUE) {
                    throw std::runtime_error(
                        "Impossible de créer le fichier de verrou après nettoyage: "
                        + lock_path_.string());
                }
            } else {
                throw std::runtime_error(
                    "Base déjà verrouillée (fichier " + lock_path_.string() +
                    " présent, pid=" + std::to_string(pid) + ").");
            }
        } else {
            throw std::runtime_error(
                "Impossible de créer le fichier de verrou: " + lock_path_.string()
                + " (code " + std::to_string(err) + ")");
        }
    }


    std::string content = "pid=" + current_pid_string() + "\n";
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    content += "timestamp=" + std::to_string(secs) + "\n";
    content += "version=1\n";

        DWORD written = 0;
    if (!WriteFile(h, content.data(), (DWORD)content.size(), &written, nullptr)
        || written != content.size()) {
        CloseHandle(h);
        throw std::runtime_error(
            "Impossible d'écrire dans le fichier de verrou: " + lock_path_.string());
    }
    CloseHandle(h);
#else
    int fd = ::open(lock_path_.c_str(),
                    O_CREAT | O_EXCL | O_WRONLY,
                    0644);
        if (fd < 0) {
        if (errno == EEXIST) {
            // C03 : détection verrou orphelin
            long pid = read_pid(lock_path_);
            if (pid > 0 && !is_pid_alive(pid)) {
                std::error_code ec;
                std::filesystem::remove(lock_path_, ec);
                fd = ::open(lock_path_.c_str(),
                            O_CREAT | O_EXCL | O_WRONLY, 0644);
                if (fd < 0) {
                    throw std::runtime_error(
                        "Impossible de créer le fichier de verrou après nettoyage: "
                        + lock_path_.string());
                }
            } else {
                throw std::runtime_error(
                    "Base déjà verrouillée (fichier " + lock_path_.string() +
                    " présent, pid=" + std::to_string(pid) + ").");
            }
        } else {
            throw std::runtime_error(
                "Impossible de créer le fichier de verrou: " + lock_path_.string()
                + " (errno " + std::to_string(errno) + ")");
        }
    }


    std::string content = "pid=" + current_pid_string() + "\n";
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    content += "timestamp=" + std::to_string(secs) + "\n";
    content += "version=1\n";

    ssize_t written = ::write(fd, content.data(), content.size());
    if (written != static_cast<ssize_t>(content.size())) {
        ::close(fd);
        throw std::runtime_error(
            "Impossible d'écrire dans le fichier de verrou: " + lock_path_.string());
    }
    ::close(fd);
#endif

    acquired_ = true;
}

DatabaseLock::~DatabaseLock() {
    if (acquired_) {
        std::error_code ec;
        std::filesystem::remove(lock_path_, ec);
    }
}

} // namespace kerneldb