#pragma once
#include "types.hpp"
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <string>

// ============================================================
// WRITE-AHEAD LOG (WAL)
//
// WHY WAL:
//   Matching engines must survive crashes without losing orders
//   or producing duplicate trades. WAL writes every operation
//   to durable storage BEFORE applying it to in-memory state.
//   On restart, replay the log to reconstruct exact state.
//
// FORMAT (binary, fixed-size records for O(1) seek):
//   [RecordHeader][RecordPayload]
//
// DURABILITY OPTIONS:
//   fsync()  — kernel flushes to physical disk (durable, ~1ms)
//   fdatasync() — like fsync but skips metadata (slightly faster)
//   O_DSYNC  — per-write sync flag (no explicit fsync needed)
//   mmap + msync — memory-mapped (complex, used in production)
//
//   We use fdatasync() after each batch for correctness.
//   Production: group commit — batch N records, one fdatasync.
//
// RECORD TYPES:
//   NEW_ORDER   — order submitted
//   CANCEL      — order cancellation requested
//   TRADE       — match event
//   CHECKPOINT  — snapshot marker (fast recovery start point)
// ============================================================

enum class WalRecordType : uint8_t {
    NEW_ORDER  = 1,
    CANCEL     = 2,
    TRADE      = 3,
    CHECKPOINT = 4
};

// Fixed-size header for every WAL record
// Layout (16 bytes total):
//   seq         : 8 bytes @ 0
//   payload_len : 4 bytes @ 8
//   checksum    : 2 bytes @ 12
//   type        : 1 byte  @ 14
//   _pad        : 1 byte  @ 15
struct WalHeader {
    uint64_t      seq;          // monotonically increasing sequence number
    uint32_t      payload_len;  // bytes following this header
    uint16_t      checksum;     // simple XOR checksum (16-bit)
    WalRecordType type;
    uint8_t       _pad{0};
};
static_assert(sizeof(WalHeader) == 16, "WalHeader must be 16 bytes");

struct WalNewOrder {
    OrderId   order_id;
    UserId    user_id;
    int64_t   price_ticks;
    Qty       qty;
    int64_t   timestamp;
    Side      side;
    OrderType type;
    uint8_t   _pad[6];
};

struct WalCancel {
    OrderId order_id;
    int64_t timestamp;
};

struct WalTrade {
    OrderId buy_order_id;
    OrderId sell_order_id;
    int64_t price_ticks;
    Qty     qty;
    int64_t timestamp;
};

// ============================================================
// WAL WRITER
// ============================================================

class WalWriter {
public:
    explicit WalWriter(const std::string& path) {
        fd_ = ::open(path.c_str(),
                     O_WRONLY | O_CREAT | O_APPEND,
                     0644);
        if (fd_ < 0)
            throw std::runtime_error("WAL open failed: " + path);
    }

    ~WalWriter() {
        if (fd_ >= 0) {
            sync();
            ::close(fd_);
        }
    }

    // Non-copyable
    WalWriter(const WalWriter&)            = delete;
    WalWriter& operator=(const WalWriter&) = delete;

    void log_new_order(const WalNewOrder& payload) {
        write_record(WalRecordType::NEW_ORDER,
                     &payload, sizeof(payload));
    }

    void log_cancel(const WalCancel& payload) {
        write_record(WalRecordType::CANCEL,
                     &payload, sizeof(payload));
    }

    void log_trade(const WalTrade& payload) {
        write_record(WalRecordType::TRADE,
                     &payload, sizeof(payload));
    }

    void log_checkpoint() {
        write_record(WalRecordType::CHECKPOINT, nullptr, 0);
        sync(); // always sync on checkpoint
    }

    // Flush to disk (call periodically or after critical records)
    void sync() {
        if (fd_ >= 0) ::fdatasync(fd_);
    }

    uint64_t seq() const { return seq_; }

private:
    void write_record(WalRecordType type,
                      const void* payload, uint32_t len) {
        WalHeader hdr{};
        hdr.seq         = ++seq_;
        hdr.type        = type;
        hdr.payload_len = len;
        hdr.checksum    = compute_checksum(payload, len);

        // Write header
        ssize_t w = ::write(fd_, &hdr, sizeof(hdr));
        if (w != sizeof(hdr))
            throw std::runtime_error("WAL write header failed");

        // Write payload
        if (len > 0 && payload) {
            w = ::write(fd_, payload, len);
            if (w != static_cast<ssize_t>(len))
                throw std::runtime_error("WAL write payload failed");
        }
    }

    static uint16_t compute_checksum(const void* data, uint32_t len) {
        if (!data || len == 0) return 0;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        uint16_t crc = 0;
        for (uint32_t i = 0; i < len; ++i) crc ^= p[i];
        return crc;
    }

    int      fd_  {-1};
    uint64_t seq_ {0};
};

// ============================================================
// WAL READER (for crash recovery / replay)
// ============================================================

class WalReader {
public:
    explicit WalReader(const std::string& path) {
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0)
            throw std::runtime_error("WAL open for read failed: " + path);
    }

    ~WalReader() {
        if (fd_ >= 0) ::close(fd_);
    }

    WalReader(const WalReader&)            = delete;
    WalReader& operator=(const WalReader&) = delete;

    struct Record {
        WalHeader          header;
        WalRecordType      type;
        // Payload unions (only one valid based on type)
        WalNewOrder        new_order;
        WalCancel          cancel;
        WalTrade           trade;
        bool               valid {false};
    };

    // Read next record. Returns false on EOF or corruption.
    bool read_next(Record& out) {
        WalHeader hdr{};
        ssize_t r = ::read(fd_, &hdr, sizeof(hdr));
        if (r == 0) return false; // EOF
        if (r != sizeof(hdr)) return false; // truncated

        out.header = hdr;
        out.type   = hdr.type;
        out.valid  = false;

        if (hdr.payload_len == 0) {
            out.valid = true;
            return true;
        }

        // Read payload into appropriate struct
        switch (hdr.type) {
            case WalRecordType::NEW_ORDER:
                if (hdr.payload_len != sizeof(WalNewOrder)) return false;
                r = ::read(fd_, &out.new_order, sizeof(WalNewOrder));
                break;
            case WalRecordType::CANCEL:
                if (hdr.payload_len != sizeof(WalCancel)) return false;
                r = ::read(fd_, &out.cancel, sizeof(WalCancel));
                break;
            case WalRecordType::TRADE:
                if (hdr.payload_len != sizeof(WalTrade)) return false;
                r = ::read(fd_, &out.trade, sizeof(WalTrade));
                break;
            case WalRecordType::CHECKPOINT:
                out.valid = true;
                return true;
            default:
                return false; // unknown type
        }

        if (r != static_cast<ssize_t>(hdr.payload_len)) return false;
        out.valid = true;
        return true;
    }

private:
    int fd_ {-1};
};
