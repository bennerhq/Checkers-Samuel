#pragma once
#include "board.h"
#include <array>
#include <string>
#include <cstdint>

// ---------------------------------------------------------------------------
// Evaluation parameter IDs
// ---------------------------------------------------------------------------
// 27 simple terms + 11 binary connective terms = 38 total.
// The piece-advantage term is always present and is not in this list.

enum class ParamID : int {
    // Simple terms (Appendix C)
    ADV = 0, APEX, BACK, CENT, CNTR, CORN, CRAMP, DENY,
    DIA, DIAV, DYKE, EXCH, EXPOS, FORK, GAP, GUARD,
    HOLE, KCENT, MOB, MOBIL, MOVE, NODE, OREO, POLE,
    RECAP, THRET,
    // Binary connective terms
    DEMO, DEMMO, DDEMO, DDMM,
    MODE1, MODE2, MODE3, MODE4,
    MOC1, MOC2, MOC3, MOC4,
    COUNT  // sentinel — must be last
};

static constexpr int NUM_PARAMS = static_cast<int>(ParamID::COUNT);
static constexpr int NUM_ACTIVE = 16;   // active terms in polynomial at any time
static constexpr int NUM_RESERVE = NUM_PARAMS - NUM_ACTIVE;

const char* param_name(ParamID id);

// ---------------------------------------------------------------------------
// A single term entry in the polynomial
// ---------------------------------------------------------------------------
struct TermEntry {
    ParamID  id;
    int      sign;           // +1 or -1
    int      coefficient;    // power of 2: actual weight = 2^coefficient
    double   correlation;    // running correlation coefficient C_x
    int      times_used;     // number of moves where term was non-zero
    int      low_tally;      // consecutive moves as lowest-correlation term
};

// ---------------------------------------------------------------------------
// Evaluator
// ---------------------------------------------------------------------------
// Computes the board score as a linear polynomial:
//   score = piece_advantage_score + sum_i(sign_i * 2^coeff_i * param_i)
// All terms are from the active side's perspective (positive = good for active).
// ---------------------------------------------------------------------------
class Evaluator {
public:
    Evaluator();

    // Returns board score in centipawns (positive = good for active side).
    // Stores individual term values in last_term_values for learning feedback.
    int evaluate(const Board& b);

    // The last computed per-term contributions (for correlation updates).
    // Index matches active_terms[i].
    std::array<int, NUM_ACTIVE> last_term_values{};

    // Active polynomial terms (16 of them)
    std::array<TermEntry, NUM_ACTIVE> active_terms;

    // Reserve list (remaining terms, head = next to be introduced)
    // Stored as a circular queue.
    std::array<ParamID, NUM_RESERVE> reserve;
    int reserve_head = 0;

    // Compute piece advantage score only (always included).
    int piece_advantage(const Board& b) const;

    // Print current polynomial state
    std::string polynomial_report() const;

private:
    // Compute a single parameter value for the board (active vs passive difference)
    int compute_param(ParamID id, const Board& b) const;

    // Individual parameter computations
    int param_ADV   (const Board& b) const;
    int param_APEX  (const Board& b) const;
    int param_BACK  (const Board& b) const;
    int param_CENT  (const Board& b) const;
    int param_CNTR  (const Board& b) const;
    int param_CORN  (const Board& b) const;
    int param_CRAMP (const Board& b) const;
    int param_DENY  (const Board& b) const;
    int param_DIA   (const Board& b) const;
    int param_DIAV  (const Board& b) const;
    int param_DYKE  (const Board& b) const;
    int param_EXCH  (const Board& b) const;
    int param_EXPOS (const Board& b) const;
    int param_FORK  (const Board& b) const;
    int param_GAP   (const Board& b) const;
    int param_GUARD (const Board& b) const;
    int param_HOLE  (const Board& b) const;
    int param_KCENT (const Board& b) const;
    int param_MOB   (const Board& b) const;
    int param_MOBIL (const Board& b) const;
    int param_MOVE  (const Board& b) const;
    int param_NODE  (const Board& b) const;
    int param_OREO  (const Board& b) const;
    int param_POLE  (const Board& b) const;
    int param_RECAP (const Board& b) const;
    int param_THRET (const Board& b) const;

    // Binary connective terms
    int param_DEMO  (const Board& b) const;
    int param_DEMMO (const Board& b) const;
    int param_DDEMO (const Board& b) const;
    int param_DDMM  (const Board& b) const;
    int param_MODE  (const Board& b, int variant) const;
    int param_MOC   (const Board& b, int variant) const;

    // Helpers used by multiple parameters
    int count_mob(const Board& b) const;   // MOB raw count
    int count_deny(const Board& b) const;  // DENY raw count
};
