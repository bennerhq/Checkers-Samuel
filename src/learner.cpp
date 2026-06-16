#include "learner.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>

Learner::Learner() {
    // Initialize correlation records
    for (int i = 0; i < NUM_ACTIVE; i++) {
        corr[i].C         = 0.0;
        // Start new terms as if already used 16 times with 0 correlation
        // to prevent violent initial fluctuations (Samuel's stabilization)
        corr[i].N         = 16;
        corr[i].low_tally = 0;
    }
}

// ---------------------------------------------------------------------------
// Delta minimum threshold: average value of active term coefficients
// ---------------------------------------------------------------------------
int Learner::delta_min() const {
    int sum = 0;
    for (int i = 0; i < NUM_ACTIVE; i++)
        sum += (1 << alpha.active_terms[i].coefficient);
    return sum / NUM_ACTIVE;
}

// ---------------------------------------------------------------------------
// Alpha's move: compute delta against previously saved score, update
// correlations, then save the post-correction poly-eval for next time.
// Samuel: delta = backed_up(current) - saved_poly_eval(previous Alpha board)
// ---------------------------------------------------------------------------
void Learner::before_alpha_move(const Board& b, int backed_up) {
    if (!saved_valid) {
        // First Alpha move of a game: save static poly-eval of this board.
        // Samuel: "recompute the scoring polynomial for the initial board position
        // after corrections and save this score for future comparisons."
        saved_score = alpha.evaluate(b);
        saved_term_values = alpha.last_term_values;
        saved_valid = true;
        return;
    }

    // delta = backed_up(current) - saved_poly_eval(previous Alpha board)
    int delta = backed_up - saved_score;
    int min_d = delta_min();

    if (std::abs(delta) >= min_d) {
        int sign_delta = (delta > 0) ? +1 : -1;

        // Bonus multipliers (Samuel):
        //   piece ratio changed (~1 man = 2 × 2^18 = 524K): double effect
        //   near win/loss (several pieces ≈ 3M+): quadruple effect
        int multiplier = 1;
        if      (std::abs(delta) > 3'000'000) multiplier = 4;
        else if (std::abs(delta) > 400'000)   multiplier = 2;

        // Correlate term signs from the PREVIOUS position (saved_term_values)
        // against sign_of_delta.  This is Samuel's "initial scoring polynomial."
        for (int i = 0; i < NUM_ACTIVE; i++) {
            int v = saved_term_values[i];
            if (v == 0) continue;

            int sign_contribution = (v * alpha.active_terms[i].sign > 0) ? +1 : -1;

            // Samuel's asymmetric update (second series, Appendix C):
            //   delta > 0 (improved): skip terms that agree with sign of polynomial
            //   delta < 0 (worsened): update all non-zero contributing terms
            if (sign_delta > 0) {
                bool poly_positive = (saved_score >= 0);
                if ( poly_positive && sign_contribution > 0) continue;
                if (!poly_positive && sign_contribution < 0) continue;
            }

            for (int m = 0; m < multiplier; m++)
                update_correlation(i, sign_contribution, sign_delta);
        }

        recompute_coefficients();

        // Start fresh span from current position (post-correction eval)
        saved_score = alpha.evaluate(b);
        saved_term_values = alpha.last_term_values;
    }
    // If |delta| < min_d: EXTEND the span — keep old saved values.
    // Samuel: "the position evaluation is extended for another play in
    // order to obtain a larger delta value."
}

// ---------------------------------------------------------------------------
// Core correlation update: C_N = C_{N-1} - (C_{N-1} ± 1) / N
// where ± matches sign_of_contribution * sign_of_delta
// ---------------------------------------------------------------------------
void Learner::update_correlation(int idx, int sign_contribution, int sign_delta) {
    auto& r = corr[idx];

    // Effective N: use powers of 2, capped at 256 (Samuel's approximation)
    int N_used = r.N;
    // Round up to next power of 2, cap at 256
    if (N_used < 32)       N_used = 16;
    else if (N_used < 64)  N_used = 32;
    else if (N_used < 128) N_used = 64;
    else if (N_used < 256) N_used = 128;
    else                   N_used = 256;

    int concordant = (sign_contribution * sign_delta > 0) ? +1 : -1;
    r.C = r.C - (r.C - concordant) / (double)N_used;

    // Advance N (capped at 256)
    if (r.N < 256) r.N++;
    alpha.active_terms[idx].times_used++;

    // Reverse sign of term if correlation goes negative
    if (r.C < 0) {
        alpha.active_terms[idx].sign *= -1;
        r.C = -r.C;
    }
}

// ---------------------------------------------------------------------------
// Set coefficients: the term with the highest correlation gets the max
// coefficient; others are set proportionally as powers of 2.
// ---------------------------------------------------------------------------
void Learner::recompute_coefficients() {
    // Find maximum correlation
    double max_corr = 0.0;
    int max_idx = 0;
    for (int i = 0; i < NUM_ACTIVE; i++) {
        if (corr[i].C > max_corr) {
            max_corr = corr[i].C;
            max_idx = i;
        }
    }
    if (max_corr == 0.0) return;

    // Set max term to coefficient 18 (2^18), others proportionally
    static constexpr int MAX_COEFF = 18;
    alpha.active_terms[max_idx].coefficient  = MAX_COEFF;
    alpha.active_terms[max_idx].correlation  = corr[max_idx].C;

    for (int i = 0; i < NUM_ACTIVE; i++) {
        if (i == max_idx) continue;
        // Sync display field
        alpha.active_terms[i].correlation = corr[i].C;
        if (max_corr == 0.0) {
            alpha.active_terms[i].coefficient = 0;
            continue;
        }
        double ratio = corr[i].C / max_corr;
        // ratio in [1/2^n, 1/2^{n-1}) -> coefficient = MAX_COEFF - n
        int n = 0;
        double r = ratio;
        while (r < 1.0 && n < MAX_COEFF) {
            r *= 2.0;
            n++;
        }
        alpha.active_terms[i].coefficient = std::max(0, MAX_COEFF - n);
    }
}

// ---------------------------------------------------------------------------
// Term replacement: retire weakest term, introduce from reserve
// ---------------------------------------------------------------------------
void Learner::maybe_replace_term(int move_number) {
    if (move_number % REPLACE_INTERVAL != 0) return;

    // Find term with lowest correlation and increment its low_tally
    int worst_idx = 0;
    double worst_corr = corr[0].C;
    for (int i = 1; i < NUM_ACTIVE; i++) {
        if (corr[i].C < worst_corr) {
            worst_corr = corr[i].C;
            worst_idx = i;
        }
    }
    alpha.active_terms[worst_idx].low_tally++;

    if (alpha.active_terms[worst_idx].low_tally >= 8) {
        // Retire this term to the back of the reserve, introduce from front
        ParamID retiring = alpha.active_terms[worst_idx].id;

        // Push retiring term to end of reserve (circular)
        int tail = (alpha.reserve_head + NUM_RESERVE - 1) % NUM_RESERVE;
        alpha.reserve[tail] = retiring;

        // Bring in new term from reserve head
        ParamID incoming = alpha.reserve[alpha.reserve_head];
        alpha.reserve_head = (alpha.reserve_head + 1) % NUM_RESERVE;

        // Reset the slot
        alpha.active_terms[worst_idx].id          = incoming;
        alpha.active_terms[worst_idx].sign        = +1;
        alpha.active_terms[worst_idx].coefficient = 4;
        alpha.active_terms[worst_idx].low_tally   = 0;

        // New term gets bootstrapped correlation (as if used 16 times at 0)
        corr[worst_idx].C = 0.0;
        corr[worst_idx].N = 16;
    }
}

// ---------------------------------------------------------------------------
// End of game: Alpha vs Beta outcome
// ---------------------------------------------------------------------------
void Learner::end_of_game(bool alpha_won) {
    total_games_played++;

    // Record outcome in rolling window
    recent_results[recent_idx] = alpha_won;
    recent_idx = (recent_idx + 1) % WIN_WINDOW;
    if (recent_count < WIN_WINDOW) recent_count++;

    if (!alpha_won) {
        black_marks++;
        if (black_marks >= BLACK_MARK_LIMIT) {
            reset_leading_term();
            black_marks = 0;
        }
    }

    // Samuel: Beta adopts Alpha's polynomial only after Alpha wins a MAJORITY
    // of recent games — creates genuine competitive pressure.
    if (recent_count >= WIN_WINDOW) {
        int wins = 0;
        for (int i = 0; i < WIN_WINDOW; i++) wins += recent_results[i] ? 1 : 0;
        if (wins >= WIN_REQUIRED) {
            beta.active_terms = alpha.active_terms;
            black_marks = 0;
        }
    }

    saved_valid = false;
}

// ---------------------------------------------------------------------------
// Escape from local maximum: set leading term coefficient to 0
// ---------------------------------------------------------------------------
void Learner::reset_leading_term() {
    // Find the highest-coefficient term and reduce it
    int top_idx = 0;
    for (int i = 1; i < NUM_ACTIVE; i++) {
        if (alpha.active_terms[i].coefficient > alpha.active_terms[top_idx].coefficient)
            top_idx = i;
    }
    alpha.active_terms[top_idx].coefficient = 0;
    corr[top_idx].C = 0.0;
    corr[top_idx].N = 16;
}

// ---------------------------------------------------------------------------
// Persist / restore
// ---------------------------------------------------------------------------

// Binary layout (all little-endian native):
//   int32  version = 1
//   int32  NUM_ACTIVE
//   for each active term:
//     int32  id, int32 sign, int32 coefficient, double correlation, int32 times_used
//     double corr_C, int32 corr_N
//   int32  reserve_head
//   for each reserve slot: int32 id
//   int32  black_marks, int32 total_games_played

bool Learner::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    auto wi  = [&](int v)    { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
    auto wd  = [&](double v) { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); };

    int version = 1;
    wi(version);
    wi(NUM_ACTIVE);

    for (int i = 0; i < NUM_ACTIVE; i++) {
        const auto& t = alpha.active_terms[i];
        wi(static_cast<int>(t.id));
        wi(t.sign);
        wi(t.coefficient);
        wd(corr[i].C);
        wi(corr[i].N);
        wi(t.times_used);
    }

    wi(alpha.reserve_head);
    wi(NUM_RESERVE);
    for (int i = 0; i < NUM_RESERVE; i++)
        wi(static_cast<int>(alpha.reserve[i]));

    wi(black_marks);
    wi(total_games_played);

    return f.good();
}

bool Learner::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    auto ri  = [&]() -> int    { int v;    f.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; };
    auto rd  = [&]() -> double { double v; f.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; };

    int version = ri();
    if (version != 1) return false;
    int n = ri();
    if (n != NUM_ACTIVE) return false;

    for (int i = 0; i < NUM_ACTIVE; i++) {
        alpha.active_terms[i].id          = static_cast<ParamID>(ri());
        alpha.active_terms[i].sign        = ri();
        alpha.active_terms[i].coefficient = ri();
        corr[i].C                         = rd();
        corr[i].N                         = ri();
        alpha.active_terms[i].times_used  = ri();
        alpha.active_terms[i].correlation = corr[i].C;  // sync display field
    }

    alpha.reserve_head = ri();
    int nr = ri();
    if (nr != NUM_RESERVE) return false;
    for (int i = 0; i < NUM_RESERVE; i++)
        alpha.reserve[i] = static_cast<ParamID>(ri());

    black_marks        = ri();
    total_games_played = ri();

    // Give Beta the same polynomial that Alpha has
    beta.active_terms = alpha.active_terms;

    return f.good();
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------
std::string Learner::learning_report() const {
    std::ostringstream oss;
    oss << "=== Alpha Polynomial ===\n" << alpha.polynomial_report();
    oss << "Black marks: " << black_marks
        << "  Games played: " << total_games_played << "\n";
    return oss.str();
}
