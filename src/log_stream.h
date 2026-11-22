// Internal. A std::ostream whose lines go to muninn::Logger.
//
// Muninn used to print progress with std::cout. A host that uses stdout for
// something else (Loki Studio's worker speaks JSON on it) got those lines in
// its channel, and lines from different threads could interleave. Each
// thread gets its own buffer, and every complete line becomes one Logger
// call, so lines stay whole and go wherever the host sent the log.
#pragma once

#include "muninn/logging.h"

#include <ostream>
#include <streambuf>
#include <string>

namespace muninn {

class LoggerLineBuf : public std::streambuf {
public:
    explicit LoggerLineBuf(LogLevel level) : level_(level) {}
    ~LoggerLineBuf() override { flush_line(); }

protected:
    int overflow(int c) override {
        if (c == traits_type::eof()) return traits_type::not_eof(c);
        if (c == '\n') flush_line();
        else line_.push_back(static_cast<char>(c));
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i) overflow(static_cast<unsigned char>(s[i]));
        return n;
    }

private:
    void flush_line() {
        if (line_.empty()) return;
        // Drop the old "[Muninn] " prefix: the Logger adds its own.
        std::string text = line_.rfind("[Muninn] ", 0) == 0 ? line_.substr(9) : line_;
        line_.clear();
        switch (level_) {
            case LogLevel::Warning: Logger::warn(text); break;
            case LogLevel::Error: Logger::error(text); break;
            case LogLevel::Debug: Logger::debug(text); break;
            default: Logger::info(text); break;
        }
    }
    LogLevel level_;
    std::string line_;
};

/// Use like std::cout: mlog() << "text " << value << "\n";
inline std::ostream& mlog() {
    thread_local LoggerLineBuf buf(LogLevel::Info);
    thread_local std::ostream os(&buf);
    return os;
}

/// Use like std::cerr.
inline std::ostream& mlog_warn() {
    thread_local LoggerLineBuf buf(LogLevel::Warning);
    thread_local std::ostream os(&buf);
    return os;
}

} // namespace muninn
