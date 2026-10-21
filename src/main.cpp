#include "kerneldb/database.hpp"
#include "kerneldb/session.hpp"
#include "kerneldb/types.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void print_result(const kerneldb::QueryResult& result) {
    for (const auto& col : result.columns)
        std::cout << std::left << std::setw(18) << col;
    if (!result.columns.empty()) std::cout << '\n';

    for (const auto& row : result.rows) {
        for (const auto& value : row.values)
            std::cout << std::left << std::setw(18) << kerneldb::value_to_string(value);
        std::cout << '\n';
    }
    std::cout << result.message << '\n';
}

// J5.4-A : affiche un schéma avec PK/UNIQUE table-level
void print_schema(const kerneldb::TableSchema& s) {
    std::cout << "CREATE TABLE " << s.name << " (";

    bool first = true;
    for (const auto& c : s.columns) {
        if (!first) std::cout << ", ";
        first = false;

        std::cout << c.name << ' ' << kerneldb::data_type_name(c.type);
        if (c.type == kerneldb::DataType::Varchar)
            std::cout << '(' << c.max_length << ')';

        if (c.is_not_null)
            std::cout << " NOT NULL";
    }

    if (!s.primary_key.empty()) {
        std::cout << ", PRIMARY KEY (";
        for (std::size_t i = 0; i < s.primary_key.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << s.primary_key[i];
        }
        std::cout << ")";
    }

    for (const auto& uc : s.unique_constraints) {
        std::cout << ", ";
        if (!uc.name.empty()) {
            std::cout << "CONSTRAINT " << uc.name << ' ';
        }
        std::cout << "UNIQUE (";
        for (std::size_t i = 0; i < uc.columns.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << uc.columns[i];
        }
        std::cout << ")";
    }

    std::cout << ");\n";
}

// Affiche le prompt (avec la base active si présente)
void print_prompt(const kerneldb::Session& session) {
    if (session.has_active()) {
        std::cout << "kerneldb (" << session.active_name() << ")> ";
    } else {
        std::cout << "kerneldb> ";
    }
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try {
        kerneldb::Session session(argc > 1 ? argv[1] : "");
        std::cout << "KernelDB 1.7.0 - multi-base shell\n";
        std::cout << "Data dir: " << session.data_dir().string() << "\n";
        std::cout << "Tapez .help ou .quit\n";

        for (std::string line;;) {
            print_prompt(session);
            if (!std::getline(std::cin, line) || line == ".quit" || line == ".exit") break;
            if (line.empty()) continue;

            // ---------- Commandes méta ----------
            if (line == ".help") {
                std::cout
                    << "Commandes SQL :\n"
                    << "  CREATE TABLE nom (\n"
                    << "      col TYPE [NOT NULL], ...\n"
                    << "      [, PRIMARY KEY (col1 [, col2]...)]\n"
                    << "      [, UNIQUE (col1 [, col2]...)]\n"
                    << "  )\n"
                    << "  CREATE DATABASE nom [WITH PAGE_SIZE 4096|8192|16384|32768|65536]\n"
                    << "  DROP TABLE nom\n"
                    << "  DROP DATABASE nom\n"
                    << "  ALTER TABLE nom ADD [COLUMN] col TYPE [NOT NULL]\n"
                    << "  INSERT INTO nom VALUES (v1, v2, ...)\n"
                    << "  UPDATE nom SET col = val [, ...] [WHERE ...]\n"
                    << "  DELETE FROM nom [WHERE ...]\n"
                    << "  SELECT [DISTINCT] * | col1, col2 FROM nom\n"
                    << "         [WHERE cond [AND|OR cond]...]\n"
                    << "         [GROUP BY col1 [, col2]...]\n"
                    << "         [HAVING cond [AND|OR cond]...]\n"
                    << "         [ORDER BY col [ASC|DESC] [, ...]] [LIMIT n]\n"
                    << "  SELECT ... FROM base.table ...  (cross-database)\n"
                    << "\n"
                    << "Syntaxe PK/UNIQUE inline (équivalent mono-colonne) :\n"
                    << "  CREATE TABLE t (id INTEGER PRIMARY KEY, name VARCHAR(50) UNIQUE)\n"
                    << "\n"
                    << "Multi-base :\n"
                    << "  SHOW DATABASES          liste les bases\n"
                    << "  USE nom                 active une base\n"
                    << "\n"
                    << "Commandes méta :\n"
                    << "  .databases              liste les bases\n"
                    << "  .current                affiche la base active\n"
                    << "  .tables                 liste les tables de la base active\n"
                    << "  .schema                 affiche les schémas de la base active\n"
                    << "  .schema <nom>           schéma d'une table\n"
                    << "  .indexes                liste les index de toutes les tables\n"
                    << "  .indexes <nom>          liste les index d'une table\n"
                    << "  .<nom>                  affiche les données d'une table\n"
                    << "  .help  .quit  .exit\n";
                continue;
            }

            if (line == ".databases") {
                auto names = session.database_names();
                if (names.empty()) {
                    std::cout << "(aucune base)\n";
                } else {
                    for (const auto& n : names) {
                        std::cout << n;
                        if (n == session.active_name()) std::cout << "  (active)";
                        std::cout << '\n';
                    }
                }
                continue;
            }

            if (line == ".current") {
                if (session.has_active()) {
                    std::cout << session.active_name() << '\n';
                } else {
                    std::cout << "(aucune base active)\n";
                }
                continue;
            }

            if (line == ".tables" || line == ".schema" ||
                line.rfind(".schema ", 0) == 0 ||
                line == ".indexes" || line.rfind(".indexes ", 0) == 0 ||
                (line[0] == '.' && line.size() > 1 &&
                 line != ".databases" && line != ".current" &&
                 line != ".help" && line != ".quit" && line != ".exit" &&
                 line != ".indexes" && line.rfind(".indexes ", 0) != 0)) {
                if (!session.has_active()) {
                    std::cerr << "Erreur: aucune base active (utilisez USE)\n";
                    continue;
                }

                auto& db = session.active();

                if (line == ".tables") {
                    auto names = db.table_names();
                    if (names.empty()) std::cout << "(aucune table)\n";
                    else for (const auto& n : names) std::cout << n << '\n';
                    continue;
                }

                if (line == ".schema") {
                    auto schemas = db.all_schemas();
                    if (schemas.empty()) std::cout << "(aucune table)\n";
                    else for (const auto& s : schemas) print_schema(s);
                    continue;
                }

                if (line.rfind(".schema ", 0) == 0) {
                    std::string table = line.substr(8);
                    while (!table.empty() && std::isspace(static_cast<unsigned char>(table.front()))) table.erase(table.begin());
                    while (!table.empty() && std::isspace(static_cast<unsigned char>(table.back()))) table.pop_back();
                    if (!db.has_table(table)) {
                        std::cerr << "Erreur: table inconnue: " << table << '\n';
                    } else {
                        print_schema(db.schema_of(table));
                    }
                    continue;
                }
                // (3bis) .indexes et .indexes <nom>
                if (line == ".indexes" || line.rfind(".indexes ", 0) == 0) {
                    auto print_indexes_of = [&](const kerneldb::TableSchema& s) {
                        const auto& im = db.index_manager();
                        auto names = im.index_names(s.name);
                        if (names.empty()) {
                            std::cout << s.name << " : (aucun index)\n";
                            return;
                        }
                        for (const auto& n : names) {
                            std::cout << s.name << " : " << n;
                            if (n == ("pk_" + s.name) && !s.primary_key.empty()) {
                                std::cout << " (PRIMARY KEY)";
                            } else {
                                std::cout << " (UNIQUE)";
                            }
                            std::cout << "\n";
                        }
                    };

                    if (line == ".indexes") {
                        auto schemas = db.all_schemas();
                        if (schemas.empty()) {
                            std::cout << "(aucune table)\n";
                        } else {
                            for (const auto& s : schemas) print_indexes_of(s);
                        }
                        continue;
                    }

                    // .indexes <nom>
                    std::string table = line.substr(9);
                    while (!table.empty() && std::isspace(static_cast<unsigned char>(table.front()))) table.erase(table.begin());
                    while (!table.empty() && std::isspace(static_cast<unsigned char>(table.back()))) table.pop_back();
                    if (!db.has_table(table)) {
                        std::cerr << "Erreur: table inconnue: " << table << '\n';
                    } else {
                        print_indexes_of(db.schema_of(table));
                    }
                    continue;
                }
                std::string table = line.substr(1);
                if (!db.has_table(table)) {
                    std::cerr << "Erreur: table inconnue: " << table << '\n';
                } else {
                    try {
                        auto result = db.execute("SELECT * FROM " + table);
                        print_result(result);
                    } catch (const std::exception& e) {
                        std::cerr << "Erreur: " << e.what() << '\n';
                    }
                }
                continue;
            }

            // ---------- SQL ----------
            try {
                auto result = session.execute(line);
                print_result(result);
            } catch (const std::exception& e) {
                std::cerr << "Erreur: " << e.what() << '\n';
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Erreur fatale: " << e.what() << '\n';
        return 1;
    }
}