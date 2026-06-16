#include "game.h"
#include <iostream>
#include <string>
#include <cstring>

static void print_usage() {
    std::cout <<
        "Samuel's Checkers — Machine Learning Implementation\n"
        "Usage: checkers [options]\n"
        "  --human     Human vs Computer (default, you play Black)\n"
        "  --self N    Self-play N games (Alpha learns by generalization)\n"
        "  --rote      Enable rote learning (board table saved to rote.bin)\n"
        "  --report    Print polynomial report after self-play\n"
        "  --help      Show this message\n"
        "\n"
        "Both --rote and the learned polynomial are auto-saved to rote.bin / poly.bin\n"
        "and reloaded on the next run when those flags are present.\n"
        "\n"
        "Typical workflow:\n"
        "  Train:  ./checkers --self 50 --rote\n"
        "  Play:   ./checkers --human --rote\n";
}

int main(int argc, char* argv[]) {
    GameMode mode    = GameMode::HumanVsComputer;
    int      n_games = 1;
    bool     use_rote   = false;
    bool     do_report  = false;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--human") == 0) {
            mode = GameMode::HumanVsComputer;
        } else if (std::strcmp(argv[i], "--self") == 0) {
            mode = GameMode::SelfPlay;
            if (i + 1 < argc) n_games = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--rote") == 0) {
            use_rote = true;
        } else if (std::strcmp(argv[i], "--report") == 0) {
            do_report = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage();
            return 1;
        }
    }

    Game game(mode);

    // Load saved state if --rote is set
    if (use_rote) {
        if (game.learner().load("poly.bin"))
            std::cout << "Loaded polynomial (poly.bin)\n";
        if (game.rote().load("rote.bin"))
            std::cout << "Loaded rote table (" << game.rote().size() << " entries)\n";
    }

    if (mode == GameMode::HumanVsComputer) {
        std::cout << "Samuel's Checkers — you play Black, computer plays White.\n";
        std::cout << "Enter moves as 'from-to', e.g. '11-15'. Type 'quit' to exit.\n\n";
        game.play(true);
    } else if (mode == GameMode::SelfPlay) {
        std::cout << "Running " << n_games << " self-play games...\n";
        for (int g = 0; g < n_games; g++) {
            game.new_game();
            GameResult r = game.play(false);
            bool alpha_won = game.last_alpha_won;
            std::string outcome;
            if (r == GameResult::Draw)  outcome = "Draw";
            else                        outcome = alpha_won ? "Alpha" : "Beta";
            std::cout << "Game " << (g+1) << ": " << outcome
                      << "  moves=" << game.move_count << "\n";

            if ((g+1) % 7 == 0) {
                game.print_stats();
            }
        }
        game.print_stats();
    }

    if (do_report) {
        std::cout << "\n=== Final Polynomial ===\n";
        std::cout << game.learner().alpha_eval().polynomial_report();
    }

    // Save state
    if (use_rote) {
        if (game.learner().save("poly.bin"))
            std::cout << "Saved polynomial (poly.bin)\n";
        if (game.rote().save("rote.bin"))
            std::cout << "Saved rote table (" << game.rote().size() << " entries)\n";
    }

    return 0;
}
