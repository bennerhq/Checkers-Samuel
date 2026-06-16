#include "evaluator.h"
#include "movegen.h"
#include <cmath>
#include <sstream>
#include <iomanip>

// Portable popcount
static inline int pc(uint32_t x) { return __builtin_popcount(x); }

// ---------------------------------------------------------------------------
// Parameter names
// ---------------------------------------------------------------------------
static const char* PARAM_NAMES[] = {
    "ADV","APEX","BACK","CENT","CNTR","CORN","CRAMP","DENY",
    "DIA","DIAV","DYKE","EXCH","EXPOS","FORK","GAP","GUARD",
    "HOLE","KCENT","MOB","MOBIL","MOVE","NODE","OREO","POLE",
    "RECAP","THRET",
    "DEMO","DEMMO","DDEMO","DDMM",
    "MODE1","MODE2","MODE3","MODE4",
    "MOC1","MOC2","MOC3","MOC4"
};

const char* param_name(ParamID id) { return PARAM_NAMES[static_cast<int>(id)]; }

// ---------------------------------------------------------------------------
// Square bitmask constants (1-based, bit = sq-1)
// ---------------------------------------------------------------------------
static constexpr uint32_t SQ(int n) { return 1u << (n-1); }

// Center squares 11,12,15,16,20,21,24,25
static constexpr uint32_t CENTER_MASK =
    SQ(11)|SQ(12)|SQ(15)|SQ(16)|SQ(20)|SQ(21)|SQ(24)|SQ(25);

// Back row Black = squares 29-32, White = squares 1-4 (used by OREO/BACK checks)
static constexpr uint32_t BLACK_BACK [[maybe_unused]] = SQ(29)|SQ(30)|SQ(31)|SQ(32);
static constexpr uint32_t WHITE_BACK [[maybe_unused]] = SQ(1)|SQ(2)|SQ(3)|SQ(4);

// Back-row bridge squares — paper (Appendix C): "squares 1 and 3, or 30 and 32"
// When passive=White the bridge is {1,3}; when passive=Black the bridge is {30,32}.
static constexpr uint32_t WHITE_BRIDGE = SQ(1)|SQ(3);
static constexpr uint32_t BLACK_BRIDGE = SQ(30)|SQ(32);

// Triangle of Oreo: Black = 2,3,7; White = 26,30,31
static constexpr uint32_t BLACK_OREO = SQ(2)|SQ(3)|SQ(7);
static constexpr uint32_t WHITE_OREO = SQ(26)|SQ(30)|SQ(31);

// Double-corner diagonal files
// Left double corner diagonal for Black: 1,5,9,14,18,23,27,32
// Right double corner diagonal for Black: 4,8,12,16,21,25,29
// For simplicity we use all double-corner squares as a mask
static constexpr uint32_t DBL_CORNER_DIAG =
    SQ(1)|SQ(5)|SQ(9)|SQ(14)|SQ(18)|SQ(23)|SQ(27)|SQ(32)|
    SQ(4)|SQ(8)|SQ(12)|SQ(16)|SQ(21)|SQ(25)|SQ(29);

// Cramping squares: Black cramps at 13, nearby 9,14; control squares 17,21,22,25
static constexpr uint32_t BLACK_CRAMP_SQ = SQ(13);
static constexpr uint32_t BLACK_CRAMP_NEARBY = SQ(9)|SQ(14);
static constexpr uint32_t BLACK_CRAMP_CTRL = SQ(17)|SQ(21)|SQ(22)|SQ(25);
static constexpr uint32_t WHITE_CRAMP_SQ = SQ(20);
static constexpr uint32_t WHITE_CRAMP_NEARBY = SQ(19)|SQ(24);
static constexpr uint32_t WHITE_CRAMP_CTRL = SQ(8)|SQ(11)|SQ(12)|SQ(16);

// Rows 5 and 6 from Black's perspective (advanced rows for ADV)
// Rows 5-6: squares 17-20 and 21-24  (5th and 6th from Black's side)
static constexpr uint32_t BLACK_ADV_HIGH = SQ(17)|SQ(18)|SQ(19)|SQ(20)|SQ(21)|SQ(22)|SQ(23)|SQ(24);
// Rows 3-4: squares 9-12 and 13-16
static constexpr uint32_t BLACK_ADV_LOW  = SQ(9)|SQ(10)|SQ(11)|SQ(12)|SQ(13)|SQ(14)|SQ(15)|SQ(16);
// Same for White (mirror)
static constexpr uint32_t WHITE_ADV_HIGH = SQ(9)|SQ(10)|SQ(11)|SQ(12)|SQ(13)|SQ(14)|SQ(15)|SQ(16);
static constexpr uint32_t WHITE_ADV_LOW  = SQ(17)|SQ(18)|SQ(19)|SQ(20)|SQ(21)|SQ(22)|SQ(23)|SQ(24);

// Apex squares: 7 and 26
static constexpr uint32_t APEX_SQ = SQ(7)|SQ(26);

// Double-corner squares
static constexpr uint32_t DBL_CORNER = SQ(1)|SQ(4)|SQ(29)|SQ(32);

// ---------------------------------------------------------------------------
// Evaluator constructor — set up default polynomial
// ---------------------------------------------------------------------------
Evaluator::Evaluator() {
    // Assign the first NUM_ACTIVE params as active with equal initial weights
    for (int i = 0; i < NUM_ACTIVE; i++) {
        active_terms[i].id          = static_cast<ParamID>(i);
        active_terms[i].sign        = +1;
        active_terms[i].coefficient = 4;    // 2^4 = 16 initial weight
        active_terms[i].correlation = 0.0;
        active_terms[i].times_used  = 0;
        active_terms[i].low_tally   = 0;
    }
    // Reserve: remaining params
    for (int i = 0; i < NUM_RESERVE; i++) {
        reserve[i] = static_cast<ParamID>(NUM_ACTIVE + i);
    }
    reserve_head = 0;
}

// ---------------------------------------------------------------------------
// Piece advantage (always evaluated separately from polynomial)
// ---------------------------------------------------------------------------
int Evaluator::piece_advantage(const Board& b) const {
    int active_score  = pc(b.active_men())  * 2
                      + pc(b.active_kings()) * 3;
    int passive_score = pc(b.passive_men())  * 2
                      + pc(b.passive_kings()) * 3;
    int diff = active_score - passive_score;

    // Scale to 2^18 per raw unit so a piece up (~2 units) is always worth more
    // than any single polynomial term at max coefficient.  Without this, the
    // polynomial (which can reach 2^18 × term_value) dwarfs the 100-centipawn
    // scale and the machine sacrifices pieces for positional gains.
    int total = active_score + passive_score;
    if (diff > 0) {
        return diff * (1 << 18) + (24 - total);   // trade bonus: smaller total is better
    } else if (diff < 0) {
        return diff * (1 << 18) - (24 - total);   // avoid-trade bonus
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Main evaluate
// ---------------------------------------------------------------------------
int Evaluator::evaluate(const Board& b) {
    int score = piece_advantage(b);
    for (int i = 0; i < NUM_ACTIVE; i++) {
        int v = compute_param(active_terms[i].id, b);
        last_term_values[i] = v;
        if (v != 0) {
            score += active_terms[i].sign
                   * (1 << active_terms[i].coefficient)
                   * v;
        }
    }
    return score;
}

// ---------------------------------------------------------------------------
// Dispatch to per-parameter functions
// ---------------------------------------------------------------------------
int Evaluator::compute_param(ParamID id, const Board& b) const {
    using P = ParamID;
    switch (id) {
    case P::ADV:   return param_ADV(b);
    case P::APEX:  return param_APEX(b);
    case P::BACK:  return param_BACK(b);
    case P::CENT:  return param_CENT(b);
    case P::CNTR:  return param_CNTR(b);
    case P::CORN:  return param_CORN(b);
    case P::CRAMP: return param_CRAMP(b);
    case P::DENY:  return param_DENY(b);
    case P::DIA:   return param_DIA(b);
    case P::DIAV:  return param_DIAV(b);
    case P::DYKE:  return param_DYKE(b);
    case P::EXCH:  return param_EXCH(b);
    case P::EXPOS: return param_EXPOS(b);
    case P::FORK:  return param_FORK(b);
    case P::GAP:   return param_GAP(b);
    case P::GUARD: return param_GUARD(b);
    case P::HOLE:  return param_HOLE(b);
    case P::KCENT: return param_KCENT(b);
    case P::MOB:   return param_MOB(b);
    case P::MOBIL: return param_MOBIL(b);
    case P::MOVE:  return param_MOVE(b);
    case P::NODE:  return param_NODE(b);
    case P::OREO:  return param_OREO(b);
    case P::POLE:  return param_POLE(b);
    case P::RECAP: return param_RECAP(b);
    case P::THRET: return param_THRET(b);
    case P::DEMO:  return param_DEMO(b);
    case P::DEMMO: return param_DEMMO(b);
    case P::DDEMO: return param_DDEMO(b);
    case P::DDMM:  return param_DDMM(b);
    case P::MODE1: return param_MODE(b,1);
    case P::MODE2: return param_MODE(b,2);
    case P::MODE3: return param_MODE(b,3);
    case P::MODE4: return param_MODE(b,4);
    case P::MOC1:  return param_MOC(b,1);
    case P::MOC2:  return param_MOC(b,2);
    case P::MOC3:  return param_MOC(b,3);
    case P::MOC4:  return param_MOC(b,4);
    default: return 0;
    }
}

// ---------------------------------------------------------------------------
// Parameter implementations
// All parameters return (active_value - passive_value) from active side's POV.
// ---------------------------------------------------------------------------

// ADV: +1 for each passive man in rows 5-6, -1 for rows 3-4 (passive's direction)
int Evaluator::param_ADV(const Board& b) const {
    uint32_t pm = b.passive_men();
    // "passive" moves opposite to active. If active=Black, passive=White.
    // Rows 5-6 from passive's perspective = ADV_HIGH for White = squares 9-16
    int val;
    if (b.side == Color::Black) {
        val  = pc(pm & WHITE_ADV_HIGH);  // passive=White advanced
        val -= pc(pm & WHITE_ADV_LOW);
    } else {
        val  = pc(pm & BLACK_ADV_HIGH);
        val -= pc(pm & BLACK_ADV_LOW);
    }
    return val;
}

// APEX: debited 1 if no kings, square 7 or 26 is occupied by active man,
// and neither is occupied by passive man.
int Evaluator::param_APEX(const Board& b) const {
    if (b.kings) return 0;
    uint32_t am = b.active_men();
    uint32_t pm = b.passive_men();
    bool active_has  = (am & APEX_SQ) != 0;
    bool passive_has = (pm & APEX_SQ) != 0;
    if (active_has && !passive_has) return -1;
    return 0;
}

// BACK: +1 if no active kings and passive holds the bridge (1&3 for White, 30&32 for Black)
int Evaluator::param_BACK(const Board& b) const {
    if (b.active_kings()) return 0;
    uint32_t pp = b.passive();
    if (b.side == Color::Black)
        return ((pp & WHITE_BRIDGE) == WHITE_BRIDGE) ? 1 : 0;
    else
        return ((pp & BLACK_BRIDGE) == BLACK_BRIDGE) ? 1 : 0;
}

// CENT: +1 for each CENTER square occupied by passive man
int Evaluator::param_CENT(const Board& b) const {
    return pc(b.passive_men() & CENTER_MASK);
}

// CNTR: +1 for each CENTER square occupied by active piece or to which active can move
int Evaluator::param_CNTR(const Board& b) const {
    int val = pc(b.active() & CENTER_MASK);
    // Also count CENTER squares reachable by active pieces
    auto moves = generate_moves(b);
    uint32_t reachable = 0;
    for (auto& m : moves) {
        if (!m.is_jump) reachable |= SQ(m.to);
    }
    val += pc(reachable & CENTER_MASK & ~b.active());
    return val;
}

// CORN: +1 if active material <= 6, passive is ahead, active can move to double corner
int Evaluator::param_CORN(const Board& b) const {
    int am_val = pc(b.active_men())*2 + pc(b.active_kings())*3;
    int pm_val = pc(b.passive_men())*2 + pc(b.passive_kings())*3;
    if (am_val > 6 || pm_val <= am_val) return 0;
    auto moves = generate_moves(b);
    for (auto& m : moves) {
        if (SQ(m.to) & DBL_CORNER) return 1;
    }
    return 0;
}

// CRAMP: +2 if passive occupies cramping square and nearby, active fills control squares
int Evaluator::param_CRAMP(const Board& b) const {
    uint32_t pp = b.passive();
    uint32_t ap = b.active();
    if (b.side == Color::Black) {
        // passive=White cramping at square 20
        if ((pp & WHITE_CRAMP_SQ) && (pp & WHITE_CRAMP_NEARBY) &&
            (ap & WHITE_CRAMP_CTRL) == WHITE_CRAMP_CTRL)
            return 2;
    } else {
        if ((pp & BLACK_CRAMP_SQ) && (pp & BLACK_CRAMP_NEARBY) &&
            (ap & BLACK_CRAMP_CTRL) == BLACK_CRAMP_CTRL)
            return 2;
    }
    return 0;
}

// MOB helper — paper: "disregarding the fact that jump moves may or may not be available"
int Evaluator::count_mob(const Board& b) const {
    auto moves = generate_quiet_moves(b);
    uint32_t dests = 0;
    for (auto& m : moves) dests |= SQ(m.to);
    return pc(dests);
}

// DENY helper — paper: "captured without an exchange"
// A square in MOB is denied only if the piece placed there could be captured
// with no recapture available for the active side.
int Evaluator::count_deny(const Board& b) const {
    auto quiet = generate_quiet_moves(b);
    uint32_t denied = 0;

    for (auto& m : quiet) {
        uint32_t to_bit = SQ(m.to);
        if (denied & to_bit) continue; // already counted this square

        // m.result has passive to move; check if passive can capture to_bit
        auto resp = generate_moves(m.result);
        for (auto& r : resp) {
            if (!r.is_jump || !(r.captured & to_bit)) continue;
            // Passive captures our piece. r.result has active to move again.
            // If active can recapture it is an exchange — not DENY.
            bool exchange = false;
            auto recaps = generate_moves(r.result);
            for (auto& rc : recaps) {
                if (rc.is_jump) { exchange = true; break; }
            }
            if (!exchange) { denied |= to_bit; break; }
        }
    }
    return pc(denied);
}

// DENY: +1 for each MOB square where moving there allows capture
int Evaluator::param_DENY(const Board& b) const {
    return count_deny(b);
}

// DIA: +1 for each passive piece on double-corner diagonal files
int Evaluator::param_DIA(const Board& b) const {
    return pc(b.passive() & DBL_CORNER_DIAG);
}

// DIAV: weighted diagonal moment (0.5 for 2 away, 1 for 1 away, 1.5 for in file)
// We approximate with integer: 1, 2, 3 for 2-away, 1-away, in-file
int Evaluator::param_DIAV(const Board& b) const {
    // Simplified: passive pieces in/near double-corner diagonal
    // In-file = 3, 1-away = 2, 2-away = 1
    uint32_t pp = b.passive();
    // In-file squares (same as DIA)
    int val = pc(pp & DBL_CORNER_DIAG) * 3;
    // Approximate adjacents -- pieces adjacent to double-corner files
    // (squares 2,6,10,13,17,22,26,31 and 3,7,11,15,20,24,28)
    static constexpr uint32_t NEAR_DBL =
        SQ(2)|SQ(6)|SQ(10)|SQ(13)|SQ(17)|SQ(22)|SQ(26)|SQ(31)|
        SQ(3)|SQ(7)|SQ(11)|SQ(15)|SQ(20)|SQ(24)|SQ(28);
    val += pc(pp & NEAR_DBL & ~DBL_CORNER_DIAG) * 2;
    return val / 2; // scale back (original uses 0.5 increments)
}

// DYKE: +1 for each string of passive pieces occupying 3 adjacent diagonal squares
int Evaluator::param_DYKE(const Board& b) const {
    // Check all diagonal chains of length >= 3 in passive pieces
    uint32_t pp = b.passive();
    int count = 0;
    // Check each possible diagonal chain of 3
    // Diagonals go in +4/+5 or +3/+4 steps (same as move generation).
    // For simplicity, enumerate all triples on the two diagonals.
    static const int diag_chains[][3] = {
        // Left diagonal (step +5 from odd rows, +4 from even rows)
        {1,5,9},{5,9,14},{9,14,18},{14,18,23},{18,23,27},{23,27,32},
        {4,8,12},{8,12,16},{12,16,21},{16,21,25},{21,25,29},
        // Right diagonal
        {1,6,10},{6,10,15},{10,15,19},{15,19,24},{19,24,28},
        {4,7,11},{7,11,16},{11,16,20},{16,20,25},{20,25,30},{25,30,0},
        {5,9,13},{9,13,17},{13,17,22},{17,22,26},{22,26,31},
        {2,6,11},{6,11,15},{11,15,20},{15,20,24},{20,24,29},
        {3,7,12},{7,12,16},{12,16,21},{16,21,25},{21,25,30},
    };
    for (auto& chain : diag_chains) {
        if (chain[2] == 0) continue;
        if ((pp & SQ(chain[0])) && (pp & SQ(chain[1])) && (pp & SQ(chain[2])))
            count++;
    }
    return count;
}

// EXCH: +1 for each square where active can advance and force an exchange
int Evaluator::param_EXCH(const Board& b) const {
    auto moves = generate_moves(b);
    int count = 0;
    for (auto& m : moves) {
        if (m.is_jump) continue;
        // After moving here, can passive immediately capture? -> exchange offered
        Board after = m.result;
        Board flipped_after = after.flipped();
        auto resp = generate_moves(flipped_after);
        for (auto& r : resp) {
            if (r.is_jump) { count++; break; }
        }
    }
    return count;
}

// EXPOS: +1 for each passive piece flanked by two empty squares on one diagonal
int Evaluator::param_EXPOS(const Board& b) const {
    uint32_t pp = b.passive();
    uint32_t empty = b.empty();
    int count = 0;
    for (int n = 1; n <= 32; n++) {
        if (!(pp & SQ(n))) continue;
        uint32_t bit = SQ(n);
        // Check left and right diagonal flanks
        // Use the same shift functions from movegen
        // Left-forward and right-forward neighbors
        // We check if both neighbors in either diagonal are empty
        // (simplified: check all 4 diagonal neighbors)
        auto rf = [](uint32_t b_) -> uint32_t {
            return ((b_ & 0x0F0F0F0Fu) << 4) | ((b_ & 0xF0F0F0F0u) << 3); };
        auto lf = [](uint32_t b_) -> uint32_t {
            return ((b_ & 0x0F0F0F0Fu) << 5) | ((b_ & 0xF0F0F0F0u) << 4); };
        auto rb = [](uint32_t b_) -> uint32_t {
            return ((b_ & 0x0F0F0F0Fu) >> 5) | ((b_ & 0xF0F0F0F0u) >> 4); };
        auto lb = [](uint32_t b_) -> uint32_t {
            return ((b_ & 0x0F0F0F0Fu) >> 4) | ((b_ & 0xF0F0F0F0u) >> 3); };

        bool exposed = false;
        if ((rf(bit) & empty) && (lb(bit) & empty)) exposed = true;
        if ((lf(bit) & empty) && (rb(bit) & empty)) exposed = true;
        if (exposed) count++;
    }
    return count;
}

// FORK: +1 for each threat of fork (active can occupy a square threatening capture of 2 passive)
int Evaluator::param_FORK(const Board& b) const {
    uint32_t pp = b.passive();
    uint32_t empty = b.empty();
    int count = 0;
    // Simplified: find adjacent passive pairs in same row, with 3 nearby empties
    for (int n = 1; n <= 31; n++) {
        if (!(pp & SQ(n))) continue;
        // Check if n and n+1 are in same row (adjacent in same rank)
        int row_n   = (n-1)/4;
        int row_np1 = n/4;
        if (row_n != row_np1) continue;
        if (!(pp & SQ(n+1))) continue;
        // Count empty squares where active could fork
        // (simplified: at least one empty square diagonally adjacent to both)
        uint32_t fork_sq = empty & 0xFFFFFFFFu;
        (void)fork_sq;
        // Full implementation would check 3 specific empty squares
        count++;  // approximate
    }
    return count / 2;  // reduce over-counting
}

// GAP: +1 for each single empty square separating two passive pieces or piece from edge
int Evaluator::param_GAP(const Board& b) const {
    uint32_t pp = b.passive();
    int count = 0;

    using DirFn = uint32_t(*)(uint32_t);
    static const DirFn dirs[4] = {
        [](uint32_t x) -> uint32_t { return ((x & 0x0F0F0F0Fu) << 4) | ((x & 0xF0F0F0F0u) << 3); },  // rf
        [](uint32_t x) -> uint32_t { return ((x & 0x0F0F0F0Fu) << 5) | ((x & 0xF0F0F0F0u) << 4); },  // lf
        [](uint32_t x) -> uint32_t { return ((x & 0x0F0F0F0Fu) >> 5) | ((x & 0xF0F0F0F0u) >> 4); },  // rb
        [](uint32_t x) -> uint32_t { return ((x & 0x0F0F0F0Fu) >> 4) | ((x & 0xF0F0F0F0u) >> 3); },  // lb
    };

    // Count passive pieces with a one-square gap to another passive piece or to the board edge
    // in any of the 4 diagonal directions (Samuel Appendix C).
    for (int n = 1; n <= 32; n++) {
        if (!(pp & SQ(n))) continue;
        uint32_t bit = SQ(n);
        bool gap = false;
        for (auto dir : dirs) {
            uint32_t next = dir(bit);
            if (!next) continue;          // piece is at board edge in this direction
            if (pp & next) continue;      // adjacent piece — no gap
            // next is empty; count if skip hits another passive OR goes off-board
            uint32_t skip = dir(next);
            if (!skip || (skip & pp)) { gap = true; break; }
        }
        if (gap) count++;
    }
    return count;
}

// GUARD: +1 if no active kings and passive has bridge or triangle of oreo
int Evaluator::param_GUARD(const Board& b) const {
    if (b.active_kings()) return 0;
    uint32_t pp = b.passive();
    if (b.side == Color::Black) {
        bool bridge = (pp & WHITE_BRIDGE) == WHITE_BRIDGE;
        bool oreo   = (pp & WHITE_OREO) == WHITE_OREO;
        return (bridge || oreo) ? 1 : 0;
    } else {
        bool bridge = (pp & BLACK_BRIDGE) == BLACK_BRIDGE;
        bool oreo   = (pp & BLACK_OREO) == BLACK_OREO;
        return (bridge || oreo) ? 1 : 0;
    }
}

// HOLE: +1 for each empty square surrounded by 3+ passive pieces
int Evaluator::param_HOLE(const Board& b) const {
    uint32_t pp = b.passive();
    uint32_t empty = b.empty();
    int count = 0;
    auto rf = [](uint32_t x){return ((x&0x0F0F0F0Fu)<<4)|((x&0xF0F0F0F0u)<<3);};
    auto lf = [](uint32_t x){return ((x&0x0F0F0F0Fu)<<5)|((x&0xF0F0F0F0u)<<4);};
    auto rb = [](uint32_t x){return ((x&0x0F0F0F0Fu)>>5)|((x&0xF0F0F0F0u)>>4);};
    auto lb = [](uint32_t x){return ((x&0x0F0F0F0Fu)>>4)|((x&0xF0F0F0F0u)>>3);};

    uint32_t tmp = empty;
    while (tmp) {
        uint32_t bit = tmp & (-tmp);
        tmp &= tmp - 1;
        int n = 0;
        if (rf(bit) & pp) n++;
        if (lf(bit) & pp) n++;
        if (rb(bit) & pp) n++;
        if (lb(bit) & pp) n++;
        if (n >= 3) count++;
    }
    return count;
}

// KCENT: +1 for each CENTER square occupied by passive king
int Evaluator::param_KCENT(const Board& b) const {
    return pc(b.passive_kings() & CENTER_MASK);
}

// MOB: total mobility for active side
int Evaluator::param_MOB(const Board& b) const {
    return count_mob(b);
}

// MOBIL: undenied mobility = MOB - DENY
int Evaluator::param_MOBIL(const Board& b) const {
    return count_mob(b) - count_deny(b);
}

// MOVE: +1 if pieces even, total < 24, odd number of pieces in move system
int Evaluator::param_MOVE(const Board& b) const {
    int am = pc(b.active_men())*2 + pc(b.active_kings())*3;
    int pm = pc(b.passive_men())*2 + pc(b.passive_kings())*3;
    int total = am + pm;
    if (am != pm || total >= 24) return 0;
    // Move system: vertical files starting with squares 1,2,3,4
    static constexpr uint32_t MOVE_SYS =
        SQ(1)|SQ(5)|SQ(9)|SQ(13)|SQ(17)|SQ(21)|SQ(25)|SQ(29)|
        SQ(2)|SQ(6)|SQ(10)|SQ(14)|SQ(18)|SQ(22)|SQ(26)|SQ(30)|
        SQ(3)|SQ(7)|SQ(11)|SQ(15)|SQ(19)|SQ(23)|SQ(27)|SQ(31)|
        SQ(4)|SQ(8)|SQ(12)|SQ(16)|SQ(20)|SQ(24)|SQ(28)|SQ(32);
    (void)MOVE_SYS;
    // Simplified: count pieces in files 1-4 (leftmost column)
    static constexpr uint32_t FILE1 = SQ(4)|SQ(12)|SQ(20)|SQ(28);
    static constexpr uint32_t FILE2 = SQ(3)|SQ(11)|SQ(19)|SQ(27);
    static constexpr uint32_t FILE3 = SQ(2)|SQ(10)|SQ(18)|SQ(26);
    static constexpr uint32_t FILE4 = SQ(1)|SQ(9)|SQ(17)|SQ(25);
    uint32_t all_pieces = b.occupied();
    int in_sys = pc(all_pieces & (FILE1|FILE2|FILE3|FILE4));
    return (in_sys % 2 == 1) ? 1 : 0;
}

// NODE: +1 for each passive piece surrounded by at least 3 empty squares
int Evaluator::param_NODE(const Board& b) const {
    uint32_t pp = b.passive();
    uint32_t empty = b.empty();
    int count = 0;
    auto rf = [](uint32_t x){return ((x&0x0F0F0F0Fu)<<4)|((x&0xF0F0F0F0u)<<3);};
    auto lf = [](uint32_t x){return ((x&0x0F0F0F0Fu)<<5)|((x&0xF0F0F0F0u)<<4);};
    auto rb = [](uint32_t x){return ((x&0x0F0F0F0Fu)>>5)|((x&0xF0F0F0F0u)>>4);};
    auto lb = [](uint32_t x){return ((x&0x0F0F0F0Fu)>>4)|((x&0xF0F0F0F0u)>>3);};

    uint32_t tmp = pp;
    while (tmp) {
        uint32_t bit = tmp & (-tmp);
        tmp &= tmp - 1;
        int n = 0;
        if (rf(bit) & empty) n++;
        if (lf(bit) & empty) n++;
        if (rb(bit) & empty) n++;
        if (lb(bit) & empty) n++;
        if (n >= 3) count++;
    }
    return count;
}

// OREO: +1 if no passive kings and triangle of oreo is occupied by passive
int Evaluator::param_OREO(const Board& b) const {
    if (b.passive_kings()) return 0;
    uint32_t pp = b.passive();
    if (b.side == Color::Black) {
        return ((pp & WHITE_OREO) == WHITE_OREO) ? 1 : 0;
    } else {
        return ((pp & BLACK_OREO) == BLACK_OREO) ? 1 : 0;
    }
}

// POLE: +1 for each passive man completely surrounded by empty squares (all 4 diagonals)
int Evaluator::param_POLE(const Board& b) const {
    uint32_t pm = b.passive_men();
    uint32_t empty = b.empty();
    int count = 0;
    auto rf = [](uint32_t x){return ((x&0x0F0F0F0Fu)<<4)|((x&0xF0F0F0F0u)<<3);};
    auto lf = [](uint32_t x){return ((x&0x0F0F0F0Fu)<<5)|((x&0xF0F0F0F0u)<<4);};
    auto rb = [](uint32_t x){return ((x&0x0F0F0F0Fu)>>5)|((x&0xF0F0F0F0u)>>4);};
    auto lb = [](uint32_t x){return ((x&0x0F0F0F0Fu)>>4)|((x&0xF0F0F0F0u)>>3);};

    uint32_t tmp = pm;
    while (tmp) {
        uint32_t bit = tmp & (-tmp);
        tmp &= tmp - 1;
        uint32_t neighbors = rf(bit)|lf(bit)|rb(bit)|lb(bit);
        if (neighbors && (neighbors & ~empty) == 0) count++;
    }
    return count;
}

// RECAP: identical to EXCH (per paper — introduced to test term introduction effects)
int Evaluator::param_RECAP(const Board& b) const {
    return param_EXCH(b);
}

// THRET: +1 for each square active can move to and threaten capture of passive on next move
int Evaluator::param_THRET(const Board& b) const {
    auto moves = generate_moves(b);
    int count = 0;
    for (auto& m : moves) {
        if (m.is_jump) continue;
        // After this move, does active threaten a jump?
        auto resp = generate_moves(m.result);
        bool threatens = false;
        for (auto& r : resp) {
            if (r.is_jump) { threatens = true; break; }
        }
        if (threatens) count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Binary connective terms
// A·B, A·B̄, Ā·B, Ā·B̄ (four connectives per pair)
// DENY vs MOB pair; MOBIL vs DENY pair; MOBIL vs CENT pair
// Values are 0 or 1 (binary split on sign)
// ---------------------------------------------------------------------------

// Sign split: positive or zero -> 1, negative -> 0
static int bsign(int v) { return v >= 0 ? 1 : 0; }

int Evaluator::param_DEMO(const Board& b) const {
    // A=DENY, B=MOB  => A·B
    int d = bsign(param_DENY(b)), m = bsign(param_MOB(b));
    return d & m;
}
int Evaluator::param_DEMMO(const Board& b) const {
    // A·B̄
    int d = bsign(param_DENY(b)), m = bsign(param_MOB(b));
    return d & (1-m);
}
int Evaluator::param_DDEMO(const Board& b) const {
    // Ā·B
    int d = bsign(param_DENY(b)), m = bsign(param_MOB(b));
    return (1-d) & m;
}
int Evaluator::param_DDMM(const Board& b) const {
    // Ā·B̄
    int d = bsign(param_DENY(b)), m = bsign(param_MOB(b));
    return (1-d) & (1-m);
}
int Evaluator::param_MODE(const Board& b, int variant) const {
    // A=MOBIL, B=DENY
    int a = bsign(param_MOBIL(b)), bv = bsign(param_DENY(b));
    switch (variant) {
    case 1: return a & bv;
    case 2: return a & (1-bv);
    case 3: return (1-a) & bv;
    case 4: return (1-a) & (1-bv);
    }
    return 0;
}
int Evaluator::param_MOC(const Board& b, int variant) const {
    // A=MOBIL, B=CENT
    int a = bsign(param_MOBIL(b)), bv = bsign(param_CENT(b));
    switch (variant) {
    case 1: return a & bv;
    case 2: return a & (1-bv);
    case 3: return (1-a) & bv;
    case 4: return (1-a) & (1-bv);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Polynomial report
// ---------------------------------------------------------------------------
std::string Evaluator::polynomial_report() const {
    std::ostringstream oss;
    oss << "Term       Corr    Sign  2^Coeff  Used\n";
    oss << "---------- ------- ----- -------- ----\n";
    for (int i = 0; i < NUM_ACTIVE; i++) {
        const auto& t = active_terms[i];
        oss << std::left  << std::setw(10) << param_name(t.id)
            << std::right << std::setw(7)  << std::fixed << std::setprecision(3)
            << t.correlation
            << "  " << (t.sign > 0 ? '+' : '-')
            << "   2^" << std::setw(2) << t.coefficient
            << "  " << t.times_used << "\n";
    }
    return oss.str();
}
