#pragma once
#include "board.h"
#include <unordered_map>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Zobrist hashing for board positions
// ---------------------------------------------------------------------------
struct ZobristKeys {
    uint64_t black_man[32];
    uint64_t black_king[32];
    uint64_t white_man[32];
    uint64_t white_king[32];
    uint64_t side_to_move;  // XOR when White is to move

    ZobristKeys();
};

uint64_t zobrist_hash(const Board& b, const ZobristKeys& keys);

// ---------------------------------------------------------------------------
// Rote table entry
// ---------------------------------------------------------------------------
struct RoteEntry {
    int      score;      // backed-up score (from standardized active side)
    int      ply;        // effective ply depth of this score
    int      age;        // aging counter (start at 0, incremented at merge)
};

// ---------------------------------------------------------------------------
// Rote learning table
// ---------------------------------------------------------------------------
// Stores previously encountered board positions with their backed-up scores.
// Samuel's key features:
//   - Standardized board (Black always active) as the key
//   - Refreshing: age halved on access
//   - Aging: incremented every ~20 moves (at "merge" time)
//   - Forgetting: entries evicted when age > max_age
//   - Ply culling: when a bucket is full, drop lowest-ply entries
// ---------------------------------------------------------------------------
class RoteTable {
public:
    explicit RoteTable(int max_entries = 100'000, int max_age = 64);

    // Look up a position. Returns nullptr if not found.
    // Access refreshes the entry's age.
    RoteEntry* lookup(const Board& b);

    // Store or update a board position with its backed-up score.
    void store(const Board& b, int score, int ply);

    // Called approximately every 20 moves to age all entries.
    void age_all();

    // Remove stale entries (age >= max_age) and return count removed.
    int forget();

    // Save / load to disk
    bool save(const std::string& path) const;
    bool load(const std::string& path);

    int size() const { return (int)table.size(); }
    int max_entries;
    int max_age;

private:
    ZobristKeys keys;
    std::unordered_map<uint64_t, RoteEntry> table;

    void cull_if_full();
};
