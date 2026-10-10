CC ?= gcc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -Werror -O2
SANFLAGS = -std=c89 -pedantic -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS += -DROGUELIKE
CORE = ../src/roguelike/rl_core.c ../src/roguelike/rl_map.c \
       ../src/roguelike/rl_enemy.c ../src/roguelike/rl_party.c \
       ../src/roguelike/rl_rewards.c ../src/roguelike/rl_relics.c \
       ../src/roguelike/rl_save_format.c
INCLUDES = -I../src/roguelike
UI_TEST_BIN := ../build/host_tests/rl_ui_model_test
UI_TEST_SOURCES := ../src/roguelike/rl_ui_model.c \
                   ../src/roguelike/rl_text.c rl_ui_model_test.c

.PHONY: test core-test ui-test clean

test: core-test ui-test native-import-test

native-import-test:
	mkdir -p ../build/host_tests
	$(CC) $(CFLAGS) test_native_imports.c ../src/roguelike/rl_native_imports.c -o ../build/host_tests/native-imports
	../build/host_tests/native-imports
	cd .. && python3 -m unittest host_tests.test_archive_import_generator

core-test:
	@set -e; trap 'rm -f test_core test_core_san' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) test_core.c $(CORE) -o test_core; \
	./test_core; \
	$(CC) $(CPPFLAGS) $(SANFLAGS) $(INCLUDES) test_core.c $(CORE) -o test_core_san; \
	ASAN_OPTIONS=detect_leaks=1 ./test_core_san

ui-test: $(UI_TEST_BIN)
	$(UI_TEST_BIN)

$(UI_TEST_BIN): $(UI_TEST_SOURCES) ../src/roguelike/rl_ui_model.h ../src/roguelike/rl_text.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) $(UI_TEST_SOURCES) -o $@

clean:
	rm -f test_core test_core_san $(UI_TEST_BIN)
