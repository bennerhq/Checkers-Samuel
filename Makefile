CXX      ?= g++
CXXFLAGS  = -std=c++17 -O2 -Wall -Wextra
TARGET    = checkers
BUILDDIR  = build
SRCS      = main.cpp board.cpp movegen.cpp evaluator.cpp \
            search.cpp rote_table.cpp learner.cpp game.cpp
OBJS      = $(SRCS:%.cpp=$(BUILDDIR)/%.o)
DEPS      = $(OBJS:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/%.o: %.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
