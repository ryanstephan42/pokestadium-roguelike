CC ?= gcc
CFLAGS ?= -std=c89 -Wall -Wextra -Werror -O2
CPPFLAGS += -DROGUELIKE -I../src/roguelike

TEST_BIN := ../build/host_tests/rl_ui_model_test
TEST_SOURCES := ../src/roguelike/rl_ui_model.c ../src/roguelike/rl_text.c rl_ui_model_test.c

.PHONY: test clean

test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): $(TEST_SOURCES) ../src/roguelike/rl_ui_model.h ../src/roguelike/rl_text.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) -o $@

clean:
	$(RM) $(TEST_BIN)
