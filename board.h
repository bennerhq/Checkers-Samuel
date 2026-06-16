#pragma once
#include <cstdint>
#include <string>

// 32 playable squares mapped to bit positions in a uint32_t.
// Samuel's internal numbering (Appendix A, Fig A-1):
// Black moves from low squares (1-4) toward high squares (29-32).
// White moves from high squares toward low squares.
// Squares are numbered 1..32 (stored as bits 0..31 in the uint32_t).
//
// Board layout (standard checkers numbering, Black at bottom):
//   Row 8 (White back): 32 31 30 29
//   Row 7:           28    27    26    25
//   Row 6:              24    23    22    21
//   Row 5:           20    19    18    17
//   Row 4:              16    15    14    13
//   Row 3:           12    11    10     9
//   Row 2:               8     7     6     5
//   Row 1 (Black back):  4     3     2     1

// Bit mask helpers
inline uint32_t sq(int n) { return 1u << (n - 1); }   // square n -> bitmask

// Direction shift amounts for move generation (per Samuel's Fig A-1 notation).
// A right-forward move for the active side increases square number by 4 or 5
// depending on the row parity. We encode both and mask with EMPTY.
// Shifts used: RF=+4, LF=+5, RB=-4(=shift right 4), LB=-5(=shift right 5)
// (These apply when Black is active; White uses the negated directions.)

static constexpr int SHIFT_RF = 4;
static constexpr int SHIFT_LF = 5;
static constexpr int SHIFT_RB = 4;   // backward = right-shift
static constexpr int SHIFT_LB = 5;

// Masks to prevent wrap-around at board edges (left and right files)
// Left edge squares (can't move left-forward): 5,13,21,29 and 4,12,20,28
// Right edge squares (can't move right-forward): 8,16,24,32 and 1,9,17,25
// We use column masks to filter illegal diagonal moves.
static constexpr uint32_t MASK_L5 = 0x0F0F0F0Fu;  // columns 1-4 of odd rows
static constexpr uint32_t MASK_L4 = 0xF0F0F0F0u;  // columns 5-8 of odd rows

// Row masks (which rows are "odd" parity for diagonal addressing)
static constexpr uint32_t MASK_ROW_ODD  = 0x0F0F0F0Fu; // rows 1,3,5,7 (squares 1-4, 9-12, 17-20, 25-28)
static constexpr uint32_t MASK_ROW_EVEN = 0xF0F0F0F0u; // rows 2,4,6,8

enum class Color { Black, White };
enum class PieceType { None, Man, King };

struct Piece {
    Color color;
    PieceType type;
};

// Board state: four bitboards as per Samuel's representation.
// Perspective: "active" = side to move, "passive" = opponent.
// FA = forward active (men + kings of active side that move forward)
// BA = backward active (kings only of active side — kings can move backward)
// FP = forward passive (men + kings of passive side that move forward from passive's perspective)
// BP = backward passive (kings only of passive)
//
// In practice we store as two bitboards per side: men and kings.
// We derive FA/BA/FP/BP on demand for move generation.
struct Board {
    uint32_t black;      // all black pieces (men + kings)
    uint32_t white;      // all white pieces (men + kings)
    uint32_t kings;      // all kings (either color)
    Color    side;       // whose turn it is

    static Board initial();
    uint32_t occupied() const { return black | white; }
    uint32_t empty()    const { return ~(black | white) & 0xFFFFFFFFu; }
    uint32_t active()   const { return side == Color::Black ? black : white; }
    uint32_t passive()  const { return side == Color::Black ? white : black; }
    uint32_t active_kings()  const { return active()  & kings; }
    uint32_t passive_kings() const { return passive() & kings; }
    uint32_t active_men()    const { return active()  & ~kings; }
    uint32_t passive_men()   const { return passive() & ~kings; }

    // Flip perspective: make the other side active.
    Board flipped() const;

    // Standardize: always return a board where Black is active
    // (flip if White to move) for use as hash keys in rote table.
    Board standardized() const;

    bool operator==(const Board& o) const {
        return black == o.black && white == o.white &&
               kings == o.kings && side == o.side;
    }

    std::string to_string() const;
};
