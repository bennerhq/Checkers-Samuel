# Samuel's Checkers — Machine Learning Implementation

A C++17 implementation of the machine-learning checkers player described in
Arthur Samuel's landmark 1959 paper, included here as `Samuel.pdf`.

---

## The Paper

**"Some Studies in Machine Learning Using the Game of Checkers"**
Arthur L. Samuel, *IBM Journal of Research and Development*, Vol. 3, No. 3, July 1959.

### Why it matters

This paper is one of the most important documents in the history of artificial
intelligence. Samuel coined the term **"machine learning"** — the idea that a
computer program could improve its own performance through experience without
being explicitly programmed to do so. Written in 1959, when computers filled
rooms and had kilobytes of memory, it demonstrated that a machine could teach
itself to play checkers well enough to defeat strong human players, including a
former checkers champion.

It introduced or independently developed several ideas that are now fundamental
to AI and game-playing programs:

- **Alpha-beta pruning** — Samuel described the minimax tree search with
  cutoffs that eliminate branches that cannot affect the final decision,
  drastically reducing the number of positions that must be evaluated. This
  technique is still used in every serious game-playing engine today.
- **Rote learning** — memorising positions and their backed-up values so that
  previously searched parts of the game tree can be recalled instantly in
  future games.
- **Generalization learning** — adjusting the weights of a scoring polynomial
  based on whether each term actually predicted improvements over the course of
  a game, allowing the program to discover which features of a position matter
  and which do not.
- **Self-play** — pitting two versions of the program (Alpha and Beta) against
  each other so that learning signal comes from the game itself, not from a
  human teacher.

The paper predates deep learning, neural networks as we know them, and modern
reinforcement learning by decades, yet the core ideas — a learned value
function, self-play, and backed-up scores as a training signal — are exactly
what powers systems like AlphaGo and AlphaZero sixty years later.

### Summary of the paper's approach

Samuel's program maintains a **scoring polynomial**: a weighted linear
combination of up to 38 board features (mobility, piece advancement, center
control, back-row bridge formations, and many more). The program searches ahead
using minimax, and the score it assigns to a position is the backed-up value
propagated from the leaf nodes of the search tree.

**Learning by generalization** works as follows. Two evaluators — Alpha and
Beta — play against each other. After each of Alpha's moves, the program
computes *delta*: the difference between the backed-up score at the current
position and the polynomial score that was saved at the previous Alpha
position. If delta is large enough, each term's **correlation coefficient** is
updated based on whether that term's contribution agreed with the direction of
improvement. Terms with high correlation get higher coefficients (more weight);
terms with low or negative correlation get lower weight or have their sign
reversed. Terms that consistently fail to correlate are periodically swapped
out from a reserve list of candidate features.

When Alpha wins a majority of recent games against Beta, Beta adopts Alpha's
polynomial — competitive pressure that ensures the learning ratchets upward.
If Alpha loses repeatedly, the leading term's coefficient is reset to allow
escape from local optima.

**Rote learning** is an independent mechanism: every position evaluated during
search is stored in a hash table (keyed by a standardized board representation)
with its backed-up score and the ply depth at which it was computed. On future
encounters with the same position, the stored result substitutes for a full
re-search, effectively giving the program a perfect memory of games it has
played.

---

## This Implementation

The code is a faithful C++17 re-creation of Samuel's system, module by module.

### Board representation (`board.h`, `board.cpp`)

The standard 32-square checkers board is encoded as three `uint32_t` bitboards:
`black`, `white`, and `kings`. Square numbering follows Samuel's internal
convention (squares 1–32, bit position = square − 1). A `flipped()` method
mirrors the board so that the side to move is always treated as "active",
allowing all move-generation and evaluation code to be written from a single
perspective.

### Move generation (`movegen.h`, `movegen.cpp`)

Moves are generated with bitwise shift operations on the two row-parity groups
of the staggered checkers layout (shifts of 3/4 for even rows, 4/5 for odd
rows). Mandatory-capture is enforced: if any jump is available, only jumps are
returned. Multi-jump chains are expanded recursively. Each `Move` struct
carries the pre-computed result board so the search tree avoids redundant
`apply_move` calls.

### Evaluation (`evaluator.h`, `evaluator.cpp`)

All 38 of Samuel's named parameters are implemented:

- **Simple terms (26):** ADV, APEX, BACK, CENT, CNTR, CORN, CRAMP, DENY, DIA,
  DIAV, DYKE, EXCH, EXPOS, FORK, GAP, GUARD, HOLE, KCENT, MOB, MOBIL, MOVE,
  NODE, OREO, POLE, RECAP, THRET.
- **Binary connective terms (12):** DEMO, DEMMO, DDEMO, DDMM (DENY × MOB
  products), MODE1–4 (MOBIL × DENY), MOC1–4 (MOBIL × CENT).

Piece advantage is computed separately and scaled to dominate the polynomial,
so the machine never sacrifices a piece for a positional gain.

16 terms are active at any time; the rest sit in a reserve queue waiting to be
rotated in when the weakest active term has had a low-correlation tally for
long enough.

### Search (`search.h`, `search.cpp`)

Negamax with alpha-beta pruning. Samuel's variable-ply termination rules are
implemented exactly:

- Minimum ply 3 (configurable).
- At min\_ply: extend if there is a jump, the last move was a jump, or an
  exchange is available.
- At min\_ply + 1: extend if there is a jump or an exchange.
- Beyond that: extend only if the next move is a jump.
- Hard cap at ply 20; early termination at ply 11+ if one side is ahead by
  more than 2 kings.

### Rote table (`rote_table.h`, `rote_table.cpp`)

Zobrist hashing on the standardized board (Black always active). Entries store
the backed-up score and the effective ply. Access refreshes an entry's age
(halves it); a periodic aging pass increments all ages and purges entries that
exceed the maximum age. When the table is full, the lowest-ply entries are
culled. The table is saved to `rote.bin` and reloaded on the next run.

### Learning (`learner.h`, `learner.cpp`)

Implements Samuel's Alpha/Beta generalization scheme in full:

- Correlation update formula: `C_N = C_{N-1} − (C_{N-1} ± 1) / N` where N is
  rounded to the nearest power of two and capped at 256.
- Coefficient assignment: the highest-correlation term gets weight 2^18; others
  are scaled proportionally as powers of two.
- Term sign reversal when correlation goes negative.
- Term replacement every 32 moves (configurable).
- Black-mark tracking: 3 consecutive losses trigger a reset of the
  leading-term coefficient to escape local optima.
- Beta adoption when Alpha wins 2 of the last 3 games.
- Polynomial state persisted to `poly.bin`.

### Game loop (`game.h`, `game.cpp`, `main.cpp`)

Two modes:

- `--human`: you play Black (moves first), the computer plays White using
  Alpha's current polynomial.
- `--self N`: N games of self-play. Alpha and Beta alternate colors (White for
  the first 14 games per Samuel, then alternating). Stats are printed every 7
  games.

---

## Building

Requires a C++17 compiler (GCC 8+ or Clang 5+) and `make`. No third-party
libraries or other build tools needed.

```bash
make              # builds with g++ by default
make CXX=clang++  # use Clang instead
make clean        # remove object files and binary
```

The Makefile uses `-MMD -MP` to generate header dependency files (`.d`) so
that changing a header triggers a minimal recompile of only the affected
translation units.

**macOS** — Xcode command-line tools provide `make` and `clang++`:
```bash
xcode-select --install   # one-time setup if not already installed
make CXX=clang++
```

**Linux (Debian/Ubuntu)**:
```bash
sudo apt install build-essential   # provides g++ and make
make
```

**Linux (Fedora/RHEL)**:
```bash
sudo dnf install gcc-c++ make
make
```

## Usage

```bash
# Train for 50 self-play games and save the learned polynomial
./checkers --self 50 --rote

# Play against the trained engine (reloads poly.bin and rote.bin)
./checkers --human --rote

# Print the final polynomial after training
./checkers --self 100 --rote --report
```

Moves are entered as `from-to` in standard checkers square notation, e.g.
`11-15`. Type `quit` to exit.

---

## Reference

Samuel, A. L. (1959). Some studies in machine learning using the game of
checkers. *IBM Journal of Research and Development*, 3(3), 210–229.
