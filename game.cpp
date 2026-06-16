#include "game.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cassert>

Game::Game(GameMode mode_) : mode(mode_), learn(), rote_table(100'000) {
    current_board = Board::initial();
}

void Game::new_game(const Board& start) {
    current_board = start;
    move_history.clear();
    move_count = 0;
}

void Game::print_board() const {
    std::cout << current_board.to_string();
}

// ---------------------------------------------------------------------------
// Parse human move input ("11-15" or "11 15")
// ---------------------------------------------------------------------------
bool Game::apply_human_move(const std::string& move_str) {
    int from = 0, to = 0;
    char sep;
    std::istringstream ss(move_str);
    if (!(ss >> from >> sep >> to) && !(ss >> from >> to)) return false;

    auto legal = generate_moves(current_board);
    for (auto& m : legal) {
        if (m.from == from && m.to == to) {
            move_history.push_back(m);
            current_board = m.result;
            move_count++;
            maybe_age_rote();
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Computer move with optional rote-table lookup
// ---------------------------------------------------------------------------
Move Game::computer_move(bool use_rote) {
    Evaluator& eval = learn.alpha_eval();

    // Check rote table first
    if (use_rote) {
        RoteEntry* entry = rote_table.lookup(current_board);
        if (entry && entry->ply >= 6) {
            // Use the cached score to guide move selection
            // Pick the move whose resulting position has the lowest opponent score
            auto moves = generate_moves(current_board);
            if (!moves.empty()) {
                int best_score = INT_MIN;
                int best_idx   = 0;
                for (int i = 0; i < (int)moves.size(); i++) {
                    RoteEntry* re = rote_table.lookup(moves[i].result);
                    int s = re ? -re->score : 0;
                    if (s > best_score) { best_score = s; best_idx = i; }
                }
                Move chosen = moves[best_idx];
                rote_table.store(current_board, best_score, entry->ply);
                move_history.push_back(chosen);
                current_board = chosen.result;
                move_count++;
                maybe_age_rote();
                return chosen;
            }
        }
    }

    // Fall back to minimax search (rote table wired in for tree-level reuse)
    Search search(eval, &rote_table);
    auto result = search.best_move(current_board);
    if (!result) {
        // Should not happen if called when not terminal
        return move_history.empty() ? Move{} : move_history.back();
    }

    // Store result in rote table
    rote_table.store(current_board, result->score, result->effective_ply);

    Move chosen = result->best_move;
    move_history.push_back(chosen);
    current_board = chosen.result;
    move_count++;
    maybe_age_rote();
    return chosen;
}

// ---------------------------------------------------------------------------
// Self-play move (Alpha or Beta depending on whose turn it is)
// ---------------------------------------------------------------------------
Move Game::self_play_move(bool alpha_to_move) {
    Evaluator& eval = alpha_to_move ? learn.alpha_eval() : learn.beta_eval();
    Search search(eval, &rote_table);

    auto result = search.best_move(current_board);
    if (!result) return Move{};

    rote_table.store(current_board, result->score, result->effective_ply);

    if (alpha_to_move) {
        // Pass the backed-up score from the current position (before the move)
        // and the board so the learner can compare with the previously saved score.
        // Samuel: delta = backed_up(current) - saved_poly_eval(previous Alpha board)
        learn.before_alpha_move(current_board, result->score);
    }

    Move chosen = result->best_move;
    move_history.push_back(chosen);
    current_board = chosen.result;
    move_count++;
    maybe_age_rote();

    if (alpha_to_move) {
        learn.maybe_replace_term(move_count);
    }

    return chosen;
}

// ---------------------------------------------------------------------------
// Check game result
// ---------------------------------------------------------------------------
GameResult Game::check_result() const {
    if (move_count >= MAX_MOVES) return GameResult::Draw;
    auto moves = generate_moves(current_board);
    if (moves.empty()) {
        return (current_board.side == Color::Black) ? GameResult::WhiteWins
                                                    : GameResult::BlackWins;
    }
    return GameResult::InProgress;
}

// ---------------------------------------------------------------------------
// Play a full game
// ---------------------------------------------------------------------------
GameResult Game::play(bool verbose) {
    // Samuel: first 14 games Alpha=White, then alternate sides each game.
    // We track which side Alpha plays by game number.
    bool alpha_is_black;
    if (games_played < 14) {
        alpha_is_black = false;  // Alpha = White for first 14 games
    } else {
        alpha_is_black = (games_played % 2 == 0);  // alternate thereafter
    }

    while (true) {
        GameResult r = check_result();
        if (r != GameResult::InProgress) {
            games_played++;
            if (r == GameResult::BlackWins) { black_wins++; }
            else if (r == GameResult::WhiteWins) { white_wins++; }
            else { draws++; }

            if (mode == GameMode::SelfPlay) {
                bool alpha_won = (r == GameResult::BlackWins && alpha_is_black) ||
                                 (r == GameResult::WhiteWins && !alpha_is_black);
                // Draws are neutral: neither a win nor a black mark
                last_alpha_won = alpha_won;
                learn.end_of_game(alpha_won);
            }

            if (verbose) {
                print_board();
                switch (r) {
                case GameResult::BlackWins: std::cout << "Black wins!\n"; break;
                case GameResult::WhiteWins: std::cout << "White wins!\n"; break;
                case GameResult::Draw:      std::cout << "Draw (70 moves)\n"; break;
                default: break;
                }
            }
            return r;
        }

        if (verbose) print_board();

        if (mode == GameMode::HumanVsComputer) {
            bool human_turn = (current_board.side == Color::Black);
            if (human_turn) {
                std::string line;
                std::cout << "Your move (from-to, e.g. 11-15): ";
                while (std::getline(std::cin, line)) {
                    if (apply_human_move(line)) break;
                    std::cout << "Illegal move, try again: ";
                }
            } else {
                Move m = computer_move(true);
                if (verbose) std::cout << "Computer: " << m.from << "-" << m.to << "\n";
            }
        } else if (mode == GameMode::SelfPlay) {
            bool alpha_turn = (current_board.side == Color::Black) == alpha_is_black;
            Move m = self_play_move(alpha_turn);
            if (verbose) {
                std::cout << (alpha_turn ? "[Alpha] " : "[Beta]  ")
                          << m.from << "-" << m.to << "\n";
            }
        }
    }
}

void Game::print_stats() const {
    std::cout << "Games: " << games_played
              << "  Black: " << black_wins
              << "  White: " << white_wins
              << "  Draws: " << draws << "\n";
    std::cout << learn.learning_report();
}

void Game::maybe_age_rote() {
    if (move_count % 20 == 0) {
        rote_table.age_all();
        rote_table.forget();
    }
}

bool Game::load_book_game(const std::string& path, std::vector<BookMove>& book) {
    std::ifstream f(path);
    if (!f) return false;
    book.clear();
    int from, to;
    char sep;
    while (f >> from >> sep >> to) {
        book.push_back({from, to});
    }
    return !book.empty();
}
