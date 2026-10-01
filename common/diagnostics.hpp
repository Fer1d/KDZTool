#ifndef DIAGNOSTICS_HPP
#define DIAGNOSTICS_HPP

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Shared policy object for every parser in the tool.
//
// The parsers have to deal with two very different classes of problem:
//
//   1. Structural problems - reading cannot continue at all, for example a
//      truncated file, a wrong magic number, or a compression scheme this tool
//      has no decompressor for. These abort in every mode.
//
//   2. Consistency problems - the file disagrees with a value that we *derived*
//      from it, for example a checksum, an accumulated sector count, a weekday
//      computed from the build date, or a field that is merely "expected" to be
//      zero. The value stored in the file stays authoritative, so a mismatch
//      does not stop the tool from extracting the firmware. Unless --strict was
//      requested, the finding is reported and parsing carries on.
//
// Keeping the two apart is what lets the tool cope with firmware produced by a
// slightly different tool chain without hiding real structural damage.
class Diagnostics {
public:
    enum class Level { Info, Warning, Error };

    struct Message {
        Level level;
        std::string where;
        std::string text;
    };

    explicit Diagnostics(bool strict = false) : strict_(strict) {}

    bool strict() const { return strict_; }
    void set_strict(bool strict) { strict_ = strict; }

    static const char* level_name(Level level) {
        switch (level) {
            case Level::Info: return "info";
            case Level::Warning: return "warning";
            case Level::Error: return "error";
        }
        return "unknown";
    }

    // Purely informative note. Never aborts.
    void info(const std::string& where, const std::string& text) {
        record(Level::Info, where, text);
    }

    // Worth telling the user about, but never fatal (also not in strict mode).
    void warn(const std::string& where, const std::string& text) {
        std::cerr << "[!] Warning: " << where << ": " << text << std::endl;
        record(Level::Warning, where, text);
    }

    // The file violates one of our assumptions but is still parseable. This is
    // exactly the old hard failure, downgraded unless --strict is in use.
    void unexpected(const std::string& where, const std::string& text) {
        if (strict_) {
            throw std::runtime_error(where + ": " + text);
        }
        warn(where, text);
    }

    // A value we derived disagrees with the value stored in the file. The
    // stored value wins; the difference is only reported.
    void inconsistent(const std::string& where, const std::string& what,
                      const std::string& stored, const std::string& derived) {
        std::string detail = what + " mismatch (stored " + stored + ", derived " + derived + ")";
        if (strict_) {
            throw std::runtime_error(where + ": " + detail);
        }
        std::cerr << "[!] Warning: " << where << ": " << detail
                  << ", keeping the stored value" << std::endl;
        record(Level::Warning, where, detail);
    }

    // Cannot continue: always fatal, in every mode.
    [[noreturn]] void fatal(const std::string& where, const std::string& text) const {
        throw std::runtime_error(where + ": " + text);
    }

    const std::vector<Message>& messages() const { return messages_; }

    std::size_t count(Level level) const {
        std::size_t total = 0;
        for (const auto& message : messages_) {
            if (message.level == level) ++total;
        }
        return total;
    }

    std::size_t warning_count() const { return count(Level::Warning); }

    void dump(std::ostream& out) const {
        for (const auto& message : messages_) {
            out << "[" << level_name(message.level) << "] "
                << message.where << ": " << message.text << std::endl;
        }
    }

private:
    void record(Level level, const std::string& where, const std::string& text) {
        messages_.push_back(Message{level, where, text});
    }

    bool strict_;
    std::vector<Message> messages_;
};

#endif // DIAGNOSTICS_HPP
