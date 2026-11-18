#pragma once

#include "diag.hpp"
#include "token.hpp"

#include <string>

namespace okaycc {

// C lexer. Written from ISO/IEC 9899:2018 clauses 5.1.1.2 and 6.4.
// Choices the standard leaves open are listed in docs/own_compiler_plan.md.
class Lexer {
public:
    Lexer(std::string file, std::string source, Diagnostics& diags);

    Token next();

    // Header-name (C17 6.4.7), only used while reading #include.
    // The next character must be '"' or '<'. `name` is the raw contents.
    bool readHeaderName(std::string& name, bool& angle, int& line, int& column);

    const std::string& file() const { return file_; }

private:
    std::string file_;
    std::string src_;
    Diagnostics& diags_;
    std::size_t i_ = 0;
    int line_ = 1;
    int col_ = 1;

    bool eof() const { return i_ >= src_.size(); }
    char peekPhys() const;
    bool isNlAt(std::size_t pos) const;
    char getPhys();
    void skipSplices();
    char get();
    char peekLogical();
    bool startsWith(const char* s);

    void errAt(int line, int col, const std::string& msg);
    Token make(Tok kind, int line, int col, bool lead) const;

    bool scanBlockComment(int startLine, int startCol);
    // Skips spaces and comments. Returns true when the next character is a newline.
    bool skipSpaceAndComments(bool& lead);
    void lexIdent(Token& t);
    void lexNumber(Token& t);
    void lexHex(std::string& spelling, Token& t);
    void lexChar(Token& t, const std::string& prefix, int width);
    void lexString(Token& t, const std::string& prefix, int width);
    bool tryPunct(Token& t);

    struct Esc {
        bool ok = false;
        bool hasCp = false;
        bool isUcn = false;
        uint32_t cp = 0;
        std::string bytes;
        std::string raw;
    };
    Esc readEscape();
    void readIntSuffix(std::string& spelling, Token& t);
    void readFloatSuffix(std::string& spelling, Token& t);
    bool readDecExponent(std::string& spelling);
    bool readBinExponent(std::string& spelling);
    void finishInt(Token& t, int base);
    void finishFloat(Token& t);
    void appendUtf8(std::string& out, uint32_t cp) const;
    uint32_t packBytes(const std::string& bytes, int line, int col);
};

}  // namespace okaycc
