#include "kerneldb/tokenizer.hpp"
#include <cctype>
#include <stdexcept>

namespace kerneldb {

Tokenizer::Tokenizer(std::string sql) : sql_(std::move(sql)) {}

std::vector<Token> Tokenizer::tokenize() const {
    std::vector<Token> out;
    for (std::size_t i = 0; i < sql_.size();) {
        if (std::isspace(static_cast<unsigned char>(sql_[i]))) { ++i; continue; }
        if (std::isalpha(static_cast<unsigned char>(sql_[i])) || sql_[i] == '_') {
            auto start = i++;
            while (i < sql_.size() && (std::isalnum(static_cast<unsigned char>(sql_[i])) || sql_[i] == '_')) ++i;
            out.push_back({TokenKind::Word, sql_.substr(start, i - start)});
        } else if (std::isdigit(static_cast<unsigned char>(sql_[i])) || (sql_[i] == '-' && i + 1 < sql_.size() && std::isdigit(static_cast<unsigned char>(sql_[i + 1])))) {
            auto start = i++;
            while (i < sql_.size() && (std::isdigit(static_cast<unsigned char>(sql_[i])) || sql_[i] == '.')) ++i;
            // R05-bis : notation scientifique (1e10, 1.5e-3, 2E+5)
            if (i < sql_.size() && (sql_[i] == 'e' || sql_[i] == 'E')) {
                std::size_t save = i;
                ++i;
                if (i < sql_.size() && (sql_[i] == '+' || sql_[i] == '-')) ++i;
                if (i < sql_.size() && std::isdigit(static_cast<unsigned char>(sql_[i]))) {
                    while (i < sql_.size() && std::isdigit(static_cast<unsigned char>(sql_[i]))) ++i;
                } else {
                    i = save;
                }
            }
            out.push_back({TokenKind::Number, sql_.substr(start, i - start)});
                } else if (sql_[i] == '\'') {
            ++i;
                        std::string value;
            while (i < sql_.size()) {
                // C12 : apostrophe doublée '' = apostrophe littérale (SQL standard)
                if (sql_[i] == '\'' && i + 1 < sql_.size() && sql_[i + 1] == '\'') {
                    value += '\'';
                    i += 2;
                    continue;
                }
                 // C12 : seuls \' et \\ sont des échappements.
                // Tout autre \x est un backslash littéral suivi de x.
                if (sql_[i] == '\\' && i + 1 < sql_.size()) {
                    char next = sql_[i + 1];
                    if (next == '\'' || next == '\\') {
                        value += next;
                        i += 2;
                        continue;
                    }
                    // Backslash littéral
                    value += '\\';
                    ++i;
                    continue;
                }
                // Fin de chaîne
                if (sql_[i] == '\'') break;
                value += sql_[i];
                ++i;
            }
            if (i == sql_.size()) throw std::runtime_error("Chaîne SQL non terminée");
            ++i;   // consomme la quote fermante
            out.push_back({TokenKind::String, value});
        } else {
            std::string symbol(1, sql_[i++]);
            if (i < sql_.size() && (symbol == "<" || symbol == ">" || symbol == "!") && sql_[i] == '=') symbol += sql_[i++];
            out.push_back({TokenKind::Symbol, symbol});
        }
    }
    out.push_back({TokenKind::End, ""});
    return out;
}

} // namespace kerneldb