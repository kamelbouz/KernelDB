#pragma once
#include <string>
#include <vector>

namespace kerneldb {

enum class TokenKind { Word, Number, String, Symbol, End };

struct Token {
    TokenKind kind;
    std::string text;
};

class Tokenizer {
public:
    explicit Tokenizer(std::string sql);
    std::vector<Token> tokenize() const;

private:
    std::string sql_;
};

} // namespace kerneldb