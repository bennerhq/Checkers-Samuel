#include "movegen.h"
#include <cassert>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Samuel's move generation uses bitwise shifts on the 32-square board.
// Active side's men move "forward" (increasing square numbers for Black).
// Kings can move in all four diagonal directions.
//
// The board uses a staggered layout: rows alternate between left-shifted
// and right-shifted dark squares. The two diagonal shifts are 4 and 5 bits,
// alternating by row parity.
//
// We encode this with two masks:
//   MASK_A rows (squares 1-4, 9-12, 17-20, 25-28): shifts are +4 and +5
//   MASK_B rows (squares 5-8, 13-16, 21-24, 29-32): shifts are +3 and +4
// (Samuel uses a slightly different internal numbering for speed; we keep
// the standard 1-32 numbering and handle the two row parities explicitly.)

static constexpr uint32_t ROWS_A = 0x0F0F0F0Fu;  // squares in odd rows (1-4, 9-12, 17-20, 25-28)
static constexpr uint32_t ROWS_B = 0xF0F0F0F0u;  // squares in even rows (5-8, 13-16, 21-24, 29-32)

// Forward moves for Black (or backward moves for White kings):
//   From an A-row square: right-forward -> +4, left-forward -> +5
//   From a B-row square: right-forward -> +3, left-forward -> +4
inline uint32_t black_forward_moves(uint32_t pieces, uint32_t empty) {
    uint32_t rf = ((pieces & ROWS_A) << 4) | ((pieces & ROWS_B) << 3);
    uint32_t lf = ((pieces & ROWS_A) << 5) | ((pieces & ROWS_B) << 4);
    return (rf | lf) & empty;
}

// RF and LF separately (needed for jump generation)
inline uint32_t black_rf(uint32_t pieces) { return ((pieces & ROWS_A) << 4) | ((pieces & ROWS_B) << 3); }
inline uint32_t black_lf(uint32_t pieces) { return ((pieces & ROWS_A) << 5) | ((pieces & ROWS_B) << 4); }
inline uint32_t black_rb(uint32_t pieces) { return ((pieces & ROWS_A) >> 5) | ((pieces & ROWS_B) >> 4); }  // backward
inline uint32_t black_lb(uint32_t pieces) { return ((pieces & ROWS_A) >> 4) | ((pieces & ROWS_B) >> 3); }

// Backward moves for Black (kings only), i.e., the reverse of White's forward
inline uint32_t black_backward_moves(uint32_t pieces, uint32_t empty) {
    uint32_t rb = ((pieces & ROWS_A) >> 5) | ((pieces & ROWS_B) >> 4);
    uint32_t lb = ((pieces & ROWS_A) >> 4) | ((pieces & ROWS_B) >> 3);
    return (rb | lb) & empty;
}

// White men move "backward" from Black's frame = decreasing square numbers
inline uint32_t white_forward_moves(uint32_t pieces, uint32_t empty) {
    return black_backward_moves(pieces, empty);
}
inline uint32_t white_backward_moves(uint32_t pieces, uint32_t empty) {
    return black_forward_moves(pieces, empty);
}

// Compute the square a jump lands on, given the attacker square and the
// captured square (intermediate). Also returns the captured square mask.
// Jump: attacker -> captured -> landing (all in the same diagonal direction).

// Landing square when jumping from 'from' over 'over' in each direction.
// Each direction function returns the landing square bitboard if the
// landing is on an empty square, else 0.
static uint32_t jump_landing_from(uint32_t from_bit, uint32_t over_bit,
                                   uint32_t empty, bool forward, bool right) {
    // We re-derive: from -> over -> landing
    // The "over" bit was computed as the appropriate shift of "from".
    // The "landing" is the same shift applied to "over".
    uint32_t land;
    if (forward) {
        if (right) land = black_rf(over_bit);
        else       land = black_lf(over_bit);
    } else {
        if (right) land = black_rb(over_bit);
        else       land = black_lb(over_bit);
    }
    (void)from_bit;
    return land & empty;
}

// ---------------------------------------------------------------------------
// Recursive jump expansion
// ---------------------------------------------------------------------------

static void expand_jumps(int from_sq, uint32_t from_bit,
                          uint32_t movers_remaining,   // original piece type flags (for king)
                          bool is_king,
                          uint32_t passive,
                          uint32_t empty,
                          uint32_t captured_so_far,
                          const Board& original,
                          std::vector<Move>& out) {

    bool found = false;

    // Try all 4 directions; kings try all 4, men only 2 (forward)
    struct Dir { bool fwd; bool right; };
    Dir dirs[4] = {{true,true},{true,false},{false,true},{false,false}};
    int ndirs = is_king ? 4 : 2;

    for (int d = 0; d < ndirs; d++) {
        uint32_t over_bit;
        if (dirs[d].fwd) {
            over_bit = dirs[d].right ? black_rf(from_bit) : black_lf(from_bit);
        } else {
            over_bit = dirs[d].right ? black_rb(from_bit) : black_lb(from_bit);
        }
        // The square being jumped must have a passive piece not already captured
        if (!(over_bit & passive & ~captured_so_far)) continue;

        uint32_t land_bit = jump_landing_from(from_bit, over_bit, empty | from_bit, dirs[d].fwd, dirs[d].right);
        if (!land_bit) continue;

        found = true;
        int land_sq = __builtin_ctz(land_bit) + 1;

        bool becomes_king = is_king;
        if (!is_king && original.side == Color::Black && land_sq >= 29) becomes_king = true;
        if (!is_king && original.side == Color::White && land_sq <= 4)  becomes_king = true;

        expand_jumps(land_sq, land_bit, movers_remaining,
                     becomes_king,
                     passive, empty | from_bit,
                     captured_so_far | over_bit,
                     original, out);
    }

    if (!found) {
        // Terminal jump: record the move (caller sets m.from and rebuilds result)
        Move m;
        m.from     = from_sq;
        m.to       = from_sq;   // current landing square; chain start set by caller
        m.captured = captured_so_far;
        m.is_jump  = true;
        m.result   = original;
        (void)movers_remaining;
        out.push_back(m);
    }
}

// ---------------------------------------------------------------------------
// apply_move
// ---------------------------------------------------------------------------

Board apply_move(const Board& b, const Move& m) {
    Board res = b;
    uint32_t from_bit = sq(m.from);
    uint32_t to_bit   = sq(m.to);

    bool piece_is_king = (b.kings & from_bit) != 0;

    // Move the piece
    if (b.side == Color::Black) {
        res.black = (b.black & ~from_bit) | to_bit;
    } else {
        res.white = (b.white & ~from_bit) | to_bit;
    }

    // Move king status
    res.kings = b.kings & ~from_bit;
    if (piece_is_king) res.kings |= to_bit;

    // Remove captured pieces
    if (m.captured) {
        if (b.side == Color::Black) {
            res.white &= ~m.captured;
        } else {
            res.black &= ~m.captured;
        }
        res.kings &= ~m.captured;
    }

    // Promotion
    if (!piece_is_king) {
        if (b.side == Color::Black && (to_bit & 0xF0000000u)) res.kings |= to_bit; // squares 29-32
        if (b.side == Color::White && (to_bit & 0x0000000Fu)) res.kings |= to_bit; // squares 1-4
    }

    res.side = (b.side == Color::Black) ? Color::White : Color::Black;
    return res;
}

// ---------------------------------------------------------------------------
// generate_moves
// ---------------------------------------------------------------------------

std::vector<Move> generate_moves(const Board& b) {
    std::vector<Move> moves;
    moves.reserve(20);

    uint32_t active  = b.active();
    uint32_t passive = b.passive();
    uint32_t empty   = b.empty();
    uint32_t aking   = b.active_kings();
    uint32_t amen    = b.active_men();

    // --- Jump moves (mandatory) ---
    std::vector<Move> jumps;

    // Iterate over all active pieces to find jumpers
    uint32_t all_active = active;
    while (all_active) {
        uint32_t piece = all_active & (-all_active);  // lowest set bit
        all_active &= all_active - 1;
        int sq_num = __builtin_ctz(piece) + 1;
        bool is_king = (aking & piece) != 0;

        // Check if this piece can make at least one jump
        bool can_jump = false;
        {
            struct Dir { bool fwd; bool right; };
            Dir dirs[4] = {{true,true},{true,false},{false,true},{false,false}};
            int ndirs = is_king ? 4 : 2;
            for (int d = 0; d < ndirs && !can_jump; d++) {
                uint32_t over;
                if (dirs[d].fwd)  over = dirs[d].right ? black_rf(piece) : black_lf(piece);
                else              over = dirs[d].right ? black_rb(piece) : black_lb(piece);
                if (!(over & passive)) continue;
                uint32_t land;
                if (dirs[d].fwd)  land = dirs[d].right ? black_rf(over) : black_lf(over);
                else              land = dirs[d].right ? black_rb(over) : black_lb(over);
                if (land & empty) can_jump = true;
            }
        }
        if (!can_jump) continue;

        // Expand all jump chains from this piece
        std::vector<Move> chain;
        expand_jumps(sq_num, piece, piece, is_king, passive, empty, 0u, b, chain);

        for (auto& mv : chain) {
            mv.from   = sq_num;
            mv.result = apply_move(b, {sq_num, mv.to, mv.captured, true, b});
            jumps.push_back(mv);
        }
    }

    if (!jumps.empty()) {
        return jumps;  // jumps are mandatory
    }

    // --- Normal moves ---
    // Convert destination masks to Move list
    // We need to find which piece moved to each destination.
    // For each destination bit, walk backwards to find source.

    // For men forward moves (two shifts: +4/+3 and +5/+4)
    auto add_normal_moves = [&](uint32_t src_pieces, bool is_king) {
        uint32_t tmp = src_pieces;
        while (tmp) {
            uint32_t piece = tmp & (-tmp);
            tmp &= tmp - 1;
            int from_sq = __builtin_ctz(piece) + 1;

            // compute all reachable empty squares for this piece
            auto add_dir = [&](uint32_t land_bit) {
                if (!land_bit) return;
                uint32_t lands = land_bit & empty;
                while (lands) {
                    uint32_t l = lands & (-lands);
                    lands &= lands - 1;
                    int to_sq = __builtin_ctz(l) + 1;
                    Move mv;
                    mv.from     = from_sq;
                    mv.to       = to_sq;
                    mv.captured = 0;
                    mv.is_jump  = false;
                    mv.result   = apply_move(b, mv);
                    moves.push_back(mv);
                }
            };

            if (b.side == Color::Black) {
                add_dir(black_rf(piece) & empty);
                add_dir(black_lf(piece) & empty);
                if (is_king) {
                    add_dir(black_rb(piece) & empty);
                    add_dir(black_lb(piece) & empty);
                }
            } else {
                add_dir(black_rb(piece) & empty);
                add_dir(black_lb(piece) & empty);
                if (is_king) {
                    add_dir(black_rf(piece) & empty);
                    add_dir(black_lf(piece) & empty);
                }
            }
        }
    };

    add_normal_moves(amen,  false);
    add_normal_moves(aking, true);

    return moves;
}

bool is_terminal(const Board& b) {
    return generate_moves(b).empty();
}

// ---------------------------------------------------------------------------
// generate_quiet_moves
// Non-jump moves only, ignoring mandatory-capture rule.
// ---------------------------------------------------------------------------
std::vector<Move> generate_quiet_moves(const Board& b) {
    std::vector<Move> moves;
    moves.reserve(20);

    uint32_t empty = b.empty();
    uint32_t aking = b.active_kings();
    uint32_t amen  = b.active_men();

    auto add_moves = [&](uint32_t src_pieces, bool is_king) {
        uint32_t tmp = src_pieces;
        while (tmp) {
            uint32_t piece = tmp & (-tmp);
            tmp &= tmp - 1;
            int from_sq = __builtin_ctz(piece) + 1;

            auto add_dir = [&](uint32_t lands) {
                lands &= empty;
                while (lands) {
                    uint32_t l = lands & (-lands);
                    lands &= lands - 1;
                    Move mv;
                    mv.from     = from_sq;
                    mv.to       = __builtin_ctz(l) + 1;
                    mv.captured = 0;
                    mv.is_jump  = false;
                    mv.result   = apply_move(b, mv);
                    moves.push_back(mv);
                }
            };

            if (b.side == Color::Black) {
                add_dir(black_rf(piece));
                add_dir(black_lf(piece));
                if (is_king) { add_dir(black_rb(piece)); add_dir(black_lb(piece)); }
            } else {
                add_dir(black_rb(piece));
                add_dir(black_lb(piece));
                if (is_king) { add_dir(black_rf(piece)); add_dir(black_lf(piece)); }
            }
        }
    };

    add_moves(amen,  false);
    add_moves(aking, true);
    return moves;
}
