#pragma once
#include <stddef.h>
#include <stdint.h>

// The only writer of the STM32 internal Flash. Callers use a transaction so
// unlock/erase/program/lock cannot interleave between settings and BadUSB.
class FlashStore {
public:
    enum class Result : uint8_t { Ok, Busy, Unlock, Erase, Program, Lock };

    class Transaction {
    public:
        explicit Transaction(uint32_t timeout_ms = 100);
        ~Transaction();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        bool entered() const { return entered_; }
        Result status() const { return result_; }
        Result erasePages(uint32_t first_page, uint32_t count);
        Result program(uint32_t address, const void* data, size_t length);
        Result close();

    private:
        bool entered_ = false;
        bool closed_ = false;
        Result result_ = Result::Ok;
    };

    static bool init();
    static Result read(uint32_t address, void* data, size_t length);
    static void clearErrors();
    static void flushCaches();

private:
    static void* mutex_;
};
