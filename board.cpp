#include "board.h"
#include <sstream>
#include <stdexcept>

// Standard starting position.
// Black pieces on squares 1-12, White on 21-32.
Board Board::initial() {
    Board b;
    b.black = 0x00000FFFu;  // bits 0-11 = squares 1-12
    b.white = 0xFFF00000u;  // bits 20-31 = squares 21-32
    b.kings = 0u;
    b.side  = Color::Black;
    return b;
}

// Flip which side is active. Mirrors the board so that the previous passive
// side's pieces now occupy the "forward" positions.
// Samuel standardizes by reversing pieces and positions when White is to move.
Board Board::flipped() const {
    Board b;
    // Reverse the bit order (square 1 <-> 32, etc.) so the opponent's
    // forward direction is correctly oriented.
    auto rev = [](uint32_t x) -> uint32_t {
        x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
        x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
        x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
        x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
        x = ( x >> 16)               | ( x                << 16);
        return x;
    };
    b.black = rev(white);
    b.white = rev(black);
    b.kings = rev(kings);
    b.side  = (side == Color::Black) ? Color::White : Color::Black;
    return b;
}

Board Board::standardized() const {
    if (side == Color::Black) return *this;
    return flipped();
}

// Print an 8x8 board to a string.
// b = black man, B = black king, w = white man, W = white king, . = empty dark square
std::string Board::to_string() const {
    // Map square number (1-32) to (row, col) where row 0 = top (White's back row)
    // and col 0 = leftmost column.
    // Standard checkers: row 8 has squares 29-32, row 7 has 25-28, etc.
    // Dark squares only; light squares are always empty.
    std::string grid(64, ' ');

    auto place = [&](int sq_num, char ch) {
        // sq_num is 1-based
        int idx = sq_num - 1;      // 0-based
        int row = 7 - (idx / 4);   // row 0 = top
        int col_in_row = idx % 4;
        // In even rows (from bottom: rows 1,3,5,7 = top indices 0,2,4,6)
        // dark squares are at columns 0,2,4,6; in odd rows at 1,3,5,7.
        int col;
        if ((7 - row) % 2 == 0) {  // bottom-based even row
            col = col_in_row * 2 + 1;
        } else {
            col = col_in_row * 2;
        }
        grid[row * 8 + col] = ch;
    };

    for (int n = 1; n <= 32; n++) {
        uint32_t bit = sq(n);
        char ch = '.';
        if (black & bit) {
            ch = (kings & bit) ? 'B' : 'b';
        } else if (white & bit) {
            ch = (kings & bit) ? 'W' : 'w';
        }
        place(n, ch);
    }

    std::ostringstream oss;
    oss << "  +----------------+\n";
    for (int r = 0; r < 8; r++) {
        oss << (8 - r) << " |";
        for (int c = 0; c < 8; c++) {
            char ch = grid[r * 8 + c];
            oss << (ch == ' ' ? "  " : std::string(1, ch) + " ");
        }
        oss << "|\n";
    }
    oss << "  +----------------+\n";
    oss << "   a b c d e f g h\n";
    oss << (side == Color::Black ? "  Black to move\n" : "  White to move\n");
    return oss.str();
}
