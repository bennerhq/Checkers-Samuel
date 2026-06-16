#include "search.h"
#include <algorithm>
#include <cassert>

// ---------------------------------------------------------------------------
// Ply termination logic (Samuel's rules)
// ---------------------------------------------------------------------------
// At min_ply: stop unless (1) next move is jump, (2) last was jump, (3) exchange available
// At min_ply+1: stop unless (1) or (3)
// At min_ply+2+: stop unless (1)
// At 11+: also stop if one side is ahead by more than 2 kings
// Hard stop at ply 20.
// ---------------------------------------------------------------------------

bool Search::has_exchange(const Board& b) {
    // Exchange available if active can move to a square that lets passive immediately capture
    auto moves = generate_moves(b);
    if (!moves.empty() && moves[0].is_jump) return false; // jump situation, not exchange
    for (auto& m : moves) {
        if (m.is_jump) continue;
        auto resp = generate_moves(m.result);
        for (auto& r : resp) {
            if (r.is_jump) return true;
        }
    }
    return false;
}

bool Search::should_stop(const Board& b, int depth, int current_ply, int min_ply,
                          bool last_was_jump, bool next_is_jump) const {
    if (current_ply >= 20) return true;

    // Kings-ahead early termination (ply 11+)
    if (current_ply >= 11) {
        int ak = __builtin_popcount(b.active_kings());
        int pk = __builtin_popcount(b.passive_kings());
        if (std::abs(ak - pk) > 2) return true;
    }

    bool exchange = has_exchange(b);

    if (depth <= 0) {
        // Below min_ply: use progressive rules
        int excess = current_ply - min_ply;
        if (excess == 0) {
            // At min_ply: stop only if none of the 3 conditions
            return !next_is_jump && !last_was_jump && !exchange;
        } else if (excess == 1) {
            return !next_is_jump && !exchange;
        } else {
            return !next_is_jump;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Minimax with alpha-beta
// depth = remaining plies to search (starts at min_ply, decremented each call)
// current_ply = how deep we are from root (for direction sense and ply rules)
// ---------------------------------------------------------------------------
int Search::minimax(const Board& b, int depth, int alpha, int beta,
                    int current_ply, int min_ply) {
    nodes_searched++;

    // Rote table probe: if we have a deep-enough stored result, use it directly.
    // Samuel (Fig. 3): a rote entry stored at ply P is treated as if the search
    // had been extended by an additional P plies beyond the current position.
    if (rote_table) {
        RoteEntry* entry = rote_table->lookup(b);
        if (entry && entry->ply >= min_ply) {
            return entry->score;
        }
    }

    auto moves = generate_moves(b);

    if (moves.empty()) {
        // Active side cannot move: loss for active side
        // Direction sense: prefer later losses (high ply) so subtract ply decay
        return SCORE_LOSS + current_ply * SCORE_PLY_DECAY;
    }

    bool next_is_jump = !moves.empty() && moves[0].is_jump;

    if (should_stop(b, depth, current_ply, min_ply, false, next_is_jump)) {
        return evaluator.evaluate(b);
    }

    int best = alpha;

    for (auto& m : moves) {
        // The result board is already pre-computed in the Move struct.
        // We need to search from the opponent's perspective (minimizing).
        // Flip the board so the next player is "active".
        Board next = m.result;

        // Recurse: opponent minimizes, so we negate the score.
        bool this_is_jump = m.is_jump;
        int child_depth = depth - 1;
        // Extend search if we just jumped
        if (this_is_jump && depth <= 1) child_depth = 1;

        int score = -minimax(next, child_depth, -beta, -best,
                              current_ply + 1, min_ply);

        // Apply direction sense decay: reduce score magnitude slightly per ply
        // so equal-scoring positions prefer shorter wins / longer losses
        if (score > 0) score -= SCORE_PLY_DECAY;
        else if (score < 0) score += SCORE_PLY_DECAY;

        if (score > best) {
            best = score;
            if (best >= beta) break;  // beta cutoff
        }
    }

    // Store backed-up result in rote table so future searches can reuse it
    if (rote_table) {
        rote_table->store(b, best, current_ply);
    }

    return best;
}

// ---------------------------------------------------------------------------
// Root search: find best move
// ---------------------------------------------------------------------------
std::optional<SearchResult> Search::best_move(const Board& b, int min_ply) {
    auto moves = generate_moves(b);
    if (moves.empty()) return std::nullopt;

    nodes_searched = 0;

    int best_score = SCORE_LOSS - 1;
    int best_idx   = 0;

    for (int i = 0; i < (int)moves.size(); i++) {
        auto& m = moves[i];
        int score = -minimax(m.result, min_ply - 1, SCORE_LOSS, -best_score,
                              1, min_ply);
        if (score > best_score) {
            best_score = score;
            best_idx   = i;
        }
    }

    SearchResult res;
    res.best_move    = moves[best_idx];
    res.score        = best_score;
    res.effective_ply = min_ply;
    return res;
}
