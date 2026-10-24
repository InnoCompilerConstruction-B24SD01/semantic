CXX ?= c++
PYTHON ?= python3
CPPFLAGS ?=
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?=

BUILD_DIR := build
LEXER_DIR := ../lexer
SYNTAX_DIR := ../syntax
CPPFLAGS += -Iinclude -I$(SYNTAX_DIR)/include -I$(LEXER_DIR)/include

FRONTEND_OBJECTS := $(BUILD_DIR)/parser.o $(BUILD_DIR)/ast.o \
	$(BUILD_DIR)/lexer.o $(BUILD_DIR)/token.o
OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/semantic.o $(FRONTEND_OBJECTS)
TEST_OBJECTS := $(BUILD_DIR)/semantic_tests.o $(BUILD_DIR)/semantic.o \
	$(FRONTEND_OBJECTS)

.PHONY: all test clean docs
all: dsemantic

dsemantic: $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/semantic_tests: $(TEST_OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/semantic_tests.o: tests/semantic_tests.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/parser.o: $(SYNTAX_DIR)/src/parser.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/ast.o: $(SYNTAX_DIR)/src/ast.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/lexer.o: $(LEXER_DIR)/src/lexer.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/token.o: $(LEXER_DIR)/src/token.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

test: dsemantic $(BUILD_DIR)/semantic_tests
	./$(BUILD_DIR)/semantic_tests
	$(PYTHON) tests/test_integration.py ./dsemantic

docs:
	./docs/build_pdf.sh

clean:
	$(RM) -r $(BUILD_DIR) dsemantic

-include $(OBJECTS:.o=.d) $(BUILD_DIR)/semantic_tests.d
