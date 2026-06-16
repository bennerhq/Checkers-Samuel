#pragma once
#include "board.h"
#include "search.h"
#include "rote_table.h"
#include "learner.h"
#include <string>
#include <vector>
#include <functional>

enum class GameMode {
    HumanVsComputer,
    SelfPlay,        // Alpha vs Beta generalization learning
    BookGame,        // Follow a recorded game, correlate machine vs book moves
};

enum class GameResult {
    BlackWins,
    WhiteWins,
    Draw,
    InProgress,
};

// A recorded move (from-square, to-square) in standard checkers notation
struct BookMove {
    int from;
    int to;
};

// Game state and loop controller
class Game {
public:
    explicit Game(GameMode mode = GameMode::HumanVsComputer);

    // Start a new game (optionally with a custom starting position)
    void new_game(const Board& start = Board::initial());

    // Play one full game, return the result
    GameResult play(bool verbose = true);

    // Human input: parse "from-to" notation, e.g. "11-15"
    // Returns false if the move is illegal.
    bool apply_human_move(const std::string& move_str);

    // Let the computer make its move (uses Alpha evaluator + search)
    Move computer_move(bool use_rote = true);

    // Print the current board
    void print_board() const;

    // Print statistics
    void print_stats() const;

    // Access components
    Board&     board()    { return current_board; }
    Learner&   learner()  { return learn; }
    RoteTable& rote()     { return rote_table; }

    // Move history for end-game analysis
    const std::vector<Move>& history() const { return move_history; }

    // Load a book game from a file (list of "from-to" moves)
    bool load_book_game(const std::string& path, std::vector<BookMove>& book);

    // Statistics
    int  games_played  = 0;
    int  black_wins    = 0;
    int  white_wins    = 0;
    int  draws         = 0;
    int  move_count    = 0;
    bool last_alpha_won = false;   // result of the most recent self-play game

private:
    GameMode  mode;
    Board     current_board;
    Learner   learn;
    RoteTable rote_table;

    std::vector<Move> move_history;

    static constexpr int MAX_MOVES = 70;  // game terminated at 70 moves per Samuel

    // Self-play: alternate Alpha and Beta
    Move self_play_move(bool alpha_to_move);

    // Determine game result from current position
    GameResult check_result() const;

    // Age rote table every ~20 moves
    void maybe_age_rote();
};
