#pragma once
#include "board.h"
#include "movegen.h"
#include "evaluator.h"
#include "rote_table.h"
#include <optional>
#include <cstdint>
#include <climits>

// Win/loss sentinel scores — well above any reachable evaluation.
// piece_advantage is scaled to (diff * 2^18) ≈ up to ~9.4M max.
// Polynomial terms reach ~21M. SCORE_WIN must be larger than both.
static constexpr int SCORE_WIN  = 500'000'000;
static constexpr int SCORE_LOSS = -500'000'000;
// Small epsilon subtracted per ply to encode direction sense:
// prefer shorter wins and longer losses.
static constexpr int SCORE_PLY_DECAY = 1;

// Result of a search
struct SearchResult {
    Move best_move;
    int  score;          // from active side's perspective
    int  effective_ply;  // actual ply reached
};

// Variable-ply minimax with alpha-beta pruning.
// Implements Samuel's ply rules:
//   min_ply = 3 (opening), dynamic extension on jumps/exchanges, hard cap 20.
class Search {
public:
    explicit Search(Evaluator& eval, RoteTable* rote = nullptr)
        : evaluator(eval), rote_table(rote) {}

    // Find the best move from position b. Returns nullopt if no moves exist.
    std::optional<SearchResult> best_move(const Board& b, int min_ply = 3);

    // Raw minimax for use in rote learning and generalization (exposes backed-up score)
    int minimax(const Board& b, int depth, int alpha, int beta,
                int current_ply, int min_ply);

    // Check if a position has any exchange offers (used for ply extension)
    static bool has_exchange(const Board& b);

    long long nodes_searched = 0;

private:
    Evaluator&  evaluator;
    RoteTable*  rote_table = nullptr;

    // Ply termination decision per Samuel's rules
    bool should_stop(const Board& b, int depth, int current_ply, int min_ply,
                     bool last_was_jump, bool next_is_jump) const;
};
