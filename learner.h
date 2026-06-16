#pragma once
#include "evaluator.h"
#include "search.h"
#include "board.h"
#include <vector>
#include <array>
#include <string>

// ---------------------------------------------------------------------------
// Generalization learner — implements Samuel's Alpha/Beta scheme
// ---------------------------------------------------------------------------
// Two evaluators:
//   Alpha: learns after each move by adjusting coefficients and swapping terms
//   Beta:  holds a stable polynomial; adopts Alpha's polynomial when Alpha
//          wins a majority of self-play games
//
// Learning signal: delta = backed_up_score(current) - saved_score(previous)
// Correlation coefficients track how well each term predicts improvement.
// ---------------------------------------------------------------------------

struct CorrelationRecord {
    double   C;           // running correlation coefficient
    int      N;           // effective usage count (capped at 256)
    int      low_tally;   // consecutive moves with lowest correlation
};

class Learner {
public:
    Learner();

    // Called when Alpha is about to move, with the backed-up score already
    // computed (search result). Computes delta = backed_up - saved_score_from_last_time,
    // updates correlations, then saves the post-correction polynomial eval
    // of the current board as the new saved_score.
    // 'b' is the board Alpha is currently facing; 'backed_up' is the minimax score.
    void before_alpha_move(const Board& b, int backed_up);

    // At end of game: if Alpha won, transfer Alpha's polynomial to Beta.
    // If Alpha lost, record a black mark; at 3 marks reset Alpha's leading term.
    void end_of_game(bool alpha_won);

    // Run one step of term replacement: retire lowest-correlation term,
    // introduce head of reserve. Frequency: once every REPLACE_INTERVAL moves.
    void maybe_replace_term(int move_number);

    // Access both evaluators
    Evaluator& alpha_eval() { return alpha; }
    Evaluator& beta_eval()  { return beta;  }

    // Persist and restore the full learner state (polynomial + correlations)
    bool save(const std::string& path) const;
    bool load(const std::string& path);

    // Report learning state
    std::string learning_report() const;

    static constexpr int REPLACE_INTERVAL = 32;   // moves between term replacements
    static constexpr int BLACK_MARK_LIMIT  = 3;    // consecutive losses before reset
    static constexpr int DELTA_MIN_DIVISOR = 1;    // min delta = avg coefficient

private:
    Evaluator alpha;
    Evaluator beta;

    std::array<CorrelationRecord, NUM_ACTIVE> corr;

    int  saved_score = 0;       // poly eval of previous Alpha board (post-correction)
    std::array<int, NUM_ACTIVE> saved_term_values{};  // term values at that board
    bool saved_valid = false;   // whether saved_score is populated

    int  black_marks = 0;
    int  total_games_played = 0;

    // Rolling window for majority-wins Beta adoption (Samuel: "majority" criterion)
    static constexpr int WIN_WINDOW   = 3;
    static constexpr int WIN_REQUIRED = 2;
    std::array<bool, WIN_WINDOW> recent_results{};
    int recent_idx   = 0;
    int recent_count = 0;

    // Compute the minimum delta threshold (average of current coefficients)
    int delta_min() const;

    // Core correlation update (Samuel's formula: C_N = C_{N-1} - (C_{N-1}±1)/N)
    void update_correlation(int term_idx, int sign_of_contribution, int sign_of_delta);

    // Set polynomial coefficients from correlation values
    void recompute_coefficients();

    // Reset the leading term's coefficient (escape from local maximum)
    void reset_leading_term();
};
