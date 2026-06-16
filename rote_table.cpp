#include "rote_table.h"
#include <random>
#include <algorithm>
#include <fstream>

// ---------------------------------------------------------------------------
// Zobrist keys
// ---------------------------------------------------------------------------
ZobristKeys::ZobristKeys() {
    std::mt19937_64 rng(0xDEADBEEFCAFEBABEull);
    auto rand64 = [&]() { return rng(); };
    for (int i = 0; i < 32; i++) {
        black_man[i]  = rand64();
        black_king[i] = rand64();
        white_man[i]  = rand64();
        white_king[i] = rand64();
    }
    side_to_move = rand64();
}

uint64_t zobrist_hash(const Board& b, const ZobristKeys& k) {
    // Standardize: always hash from Black's perspective
    Board std = b.standardized();
    uint64_t h = 0;
    uint32_t bm = std.black & ~std.kings;
    uint32_t bk = std.black &  std.kings;
    uint32_t wm = std.white & ~std.kings;
    uint32_t wk = std.white &  std.kings;
    while (bm) { int i = __builtin_ctz(bm); h ^= k.black_man[i];  bm &= bm-1; }
    while (bk) { int i = __builtin_ctz(bk); h ^= k.black_king[i]; bk &= bk-1; }
    while (wm) { int i = __builtin_ctz(wm); h ^= k.white_man[i];  wm &= wm-1; }
    while (wk) { int i = __builtin_ctz(wk); h ^= k.white_king[i]; wk &= wk-1; }
    // No side-to-move factor after standardization (Black always active)
    return h;
}

// ---------------------------------------------------------------------------
// RoteTable
// ---------------------------------------------------------------------------
RoteTable::RoteTable(int max_entries_, int max_age_)
    : max_entries(max_entries_), max_age(max_age_) {
    table.reserve(max_entries_);
}

RoteEntry* RoteTable::lookup(const Board& b) {
    uint64_t h = zobrist_hash(b, keys);
    auto it = table.find(h);
    if (it == table.end()) return nullptr;
    // Refresh: halve the age
    it->second.age /= 2;
    return &it->second;
}

void RoteTable::store(const Board& b, int score, int ply) {
    uint64_t h = zobrist_hash(b, keys);
    auto it = table.find(h);
    if (it != table.end()) {
        // Update only if new score is from a higher effective ply
        if (ply > it->second.ply) {
            it->second.score = score;
            it->second.ply   = ply;
        }
        it->second.age /= 2;  // refresh
    } else {
        cull_if_full();
        table[h] = {score, ply, 0};
    }
}

void RoteTable::age_all() {
    for (auto& [key, entry] : table) {
        entry.age++;
    }
}

int RoteTable::forget() {
    int removed = 0;
    for (auto it = table.begin(); it != table.end(); ) {
        if (it->second.age >= max_age) {
            it = table.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    return removed;
}

void RoteTable::cull_if_full() {
    if ((int)table.size() < max_entries) return;
    // Remove 10% of lowest-ply entries
    int target_remove = max_entries / 10;
    // Collect all entries sorted by ply ascending
    std::vector<std::pair<int, uint64_t>> entries;
    entries.reserve(table.size());
    for (auto& [key, entry] : table)
        entries.push_back({entry.ply, key});
    std::partial_sort(entries.begin(),
                      entries.begin() + target_remove,
                      entries.end());
    for (int i = 0; i < target_remove; i++)
        table.erase(entries[i].second);
}

bool RoteTable::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int sz = (int)table.size();
    f.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    for (auto& [key, entry] : table) {
        f.write(reinterpret_cast<const char*>(&key),   sizeof(key));
        f.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }
    return f.good();
}

bool RoteTable::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    int sz = 0;
    f.read(reinterpret_cast<char*>(&sz), sizeof(sz));
    table.clear();
    table.reserve(sz);
    for (int i = 0; i < sz; i++) {
        uint64_t key;
        RoteEntry entry;
        f.read(reinterpret_cast<char*>(&key),   sizeof(key));
        f.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (f) table[key] = entry;
    }
    return f.good();
}
