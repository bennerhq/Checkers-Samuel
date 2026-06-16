cmake_minimum_required(VERSION 3.14)
project(SamuelCheckers CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

add_executable(checkers
    main.cpp
    board.cpp
    movegen.cpp
    evaluator.cpp
    search.cpp
    rote_table.cpp
    learner.cpp
    game.cpp
)
