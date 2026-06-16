CXX      ?= g++
CXXFLAGS  = -std=c++17 -O2 -Wall -Wextra -Isrc
TARGET    = checkers
SRCDIR    = src
BUILDDIR  = build
SRCS      = $(SRCDIR)/main.cpp $(SRCDIR)/board.cpp $(SRCDIR)/movegen.cpp \
            $(SRCDIR)/evaluator.cpp $(SRCDIR)/search.cpp \
            $(SRCDIR)/rote_table.cpp $(SRCDIR)/learner.cpp $(SRCDIR)/game.cpp
OBJS      = $(SRCS:$(SRCDIR)/%.cpp=$(BUILDDIR)/%.o)
DEPS      = $(OBJS:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
