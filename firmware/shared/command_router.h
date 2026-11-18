#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Product-level command registration. A feature registers one handler per
// command; the event dispatcher consults this table instead of growing a
// switch. Header-only and allocation-free so both cores can use it.
namespace Crabit {

using CommandHandler = bool (*)(void* ctx, const char* command,
                                const char* argument, uint16_t request_id);

namespace detail {
constexpr size_t kMaxCommands = 12;
constexpr size_t kMaxCommandLength = 47;

struct CommandEntry {
    const char* command;  // must outlive the registration (string literal)
    CommandHandler handler;
    void* ctx;
    bool used;
};

struct CommandTable {
    CommandEntry entries[kMaxCommands];
};

inline CommandTable& commandTable() {
    static CommandTable table{};
    return table;
}

inline bool validCommand(const char* command) {
    if (command == nullptr || command[0] == '\0') return false;
    const size_t length = strlen(command);
    return length <= kMaxCommandLength;
}
}  // namespace detail

// Returns false for duplicates, empty/oversized names or a full table.
inline bool registerCommand(const char* command, CommandHandler handler, void* ctx) {
    if (!detail::validCommand(command) || handler == nullptr) return false;
    detail::CommandTable& table = detail::commandTable();
    for (size_t i = 0; i < detail::kMaxCommands; ++i) {
        if (table.entries[i].used && strcmp(table.entries[i].command, command) == 0)
            return false;
    }
    for (size_t i = 0; i < detail::kMaxCommands; ++i) {
        detail::CommandEntry& entry = table.entries[i];
        if (entry.used) continue;
        entry.command = command;
        entry.handler = handler;
        entry.ctx = ctx;
        entry.used = true;
        return true;
    }
    return false;
}

// Removes the entry only when both the command and the owner match.
inline bool unregisterCommand(const char* command, void* ctx) {
    if (!detail::validCommand(command)) return false;
    detail::CommandTable& table = detail::commandTable();
    for (size_t i = 0; i < detail::kMaxCommands; ++i) {
        detail::CommandEntry& entry = table.entries[i];
        if (!entry.used || entry.ctx != ctx) continue;
        if (strcmp(entry.command, command) != 0) continue;
        entry.used = false;
        entry.handler = nullptr;
        entry.ctx = nullptr;
        return true;
    }
    return false;
}

// Exact-match dispatch. Returns true when a handler consumed the command.
inline bool dispatchCommand(const char* command, const char* argument,
                            uint16_t request_id) {
    if (!detail::validCommand(command)) return false;
    detail::CommandTable& table = detail::commandTable();
    for (size_t i = 0; i < detail::kMaxCommands; ++i) {
        detail::CommandEntry& entry = table.entries[i];
        if (!entry.used || strcmp(entry.command, command) != 0) continue;
        return entry.handler(entry.ctx, command, argument, request_id);
    }
    return false;
}

inline size_t commandCount() {
    detail::CommandTable& table = detail::commandTable();
    size_t count = 0;
    for (size_t i = 0; i < detail::kMaxCommands; ++i)
        if (table.entries[i].used) ++count;
    return count;
}

}  // namespace Crabit
