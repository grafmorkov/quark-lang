#pragma once

#include <cstddef>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace quark::memory {

struct Block {
    std::byte* data = nullptr;
    size_t capacity = 0;
    size_t offset = 0;
};

struct Stats {
    size_t used = 0;
    size_t peak = 0;
    size_t allocations = 0;
    size_t blocks = 0;
};

class Arena {
public:
    explicit Arena(size_t size = 1024 * 1024)
        : default_block_size(size)
    {
        add_block(size);
    }

    ~Arena() {
        for (auto& b : blocks) {
            std::free(b.data);
        }
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* alloc(size_t size, size_t align) {
        while (true) {
            Block* b = &blocks.back();

            std::byte* base = b->data + b->offset;
            size_t space = b->capacity - b->offset;

            void* ptr = base;
            void* aligned = std::align(align, size, ptr, space);

            if (aligned) {
                size_t new_offset =
                    (reinterpret_cast<std::byte*>(ptr) - b->data) + size;

                b->offset = new_offset;

                stats.allocations++;
                update_stats();

                return aligned;
            }

            add_block((std::max)(size + align, b->capacity * 2));
            b = &blocks.back();
        }
    }

    void* alloc_zeroed(size_t size, size_t align) {
        void* mem = alloc(size, align);
        std::memset(mem, 0, size);
        return mem;
    }

    void reset() {
        for (auto& b : blocks) {
            b.offset = 0;
        }
        stats.used = 0;
    }

    size_t used() const { return stats.used; }
    size_t peak() const { return stats.peak; }
    size_t allocations() const { return stats.allocations; }
    size_t block_count() const { return blocks.size(); }

    size_t mark() const {
        return blocks.back().offset;
    }

    void rewind(size_t m) {
        blocks.back().offset = m;
        update_stats();
    }

    void debug_fill(uint8_t byte = 0xCD) {
        for (auto& b : blocks) {
            std::memset(b.data, byte, b.capacity);
        }
    }

private:
    void add_block(size_t size) {
        Block b;
        b.capacity = size;
        b.offset = 0;
        b.data = static_cast<std::byte*>(std::malloc(size));

        if (!b.data)
            throw std::bad_alloc();

        blocks.push_back(b);
        stats.blocks++;
    }

    void update_stats() {
        size_t total = 0;

        for (auto& b : blocks)
            total += b.offset;

        stats.used = total;
        stats.peak = (std::max)(stats.peak, total);
    }

private:
    std::vector<Block> blocks;
    Stats stats;
    size_t default_block_size;
};

} // namespace quark::memoryп