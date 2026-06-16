#pragma once
#include "board.h"
#include <vector>
#include <cstdint>

// A single move: from-square and to-square (1-based), plus captured squares.
// For multi-jumps, captures lists all captured pieces in order.
struct Move {
    int        from;
    int        to;
    uint32_t   captured;      // bitmask of captured squares
    bool       is_jump;
    Board      result;        // board state after move is applied
};

// Generate all legal moves from a given board position.
// If any jump is available, only jump moves are returned (mandatory capture).
// Multi-jumps are expanded fully.
std::vector<Move> generate_moves(const Board& b);

// Generate only non-jump moves, regardless of whether jumps are available.
// Samuel's MOB and DENY parameters require this (paper: "disregarding the fact
// that jump moves may or may not be available").
std::vector<Move> generate_quiet_moves(const Board& b);

// Apply a move to a board and return the resulting position.
Board apply_move(const Board& b, const Move& m);

// Check if the game is over (active side has no moves).
bool is_terminal(const Board& b);
