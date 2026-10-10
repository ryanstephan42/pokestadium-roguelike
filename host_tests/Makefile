CC ?= gcc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -Werror -O2
SANFLAGS = -std=c89 -pedantic -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS += -DROGUELIKE
CORE = ../src/roguelike/rl_core.c ../src/roguelike/rl_map.c \
       ../src/roguelike/rl_content.c \
       ../src/roguelike/rl_enemy.c ../src/roguelike/rl_party.c \
       ../src/roguelike/rl_rewards.c ../src/roguelike/rl_relics.c \
       ../src/roguelike/rl_save_format.c
INCLUDES = -I../src/roguelike
UI_TEST_BIN := ../build/host_tests/rl_ui_model_test
UI_TEST_SOURCES := ../src/roguelike/rl_ui_model.c \
                   ../src/roguelike/rl_text.c rl_ui_model_test.c

.PHONY: test core-test content-test meta-test ido-test battle-adapter-test ui-test save-test native-import-test clean

test: core-test content-test meta-test ido-test battle-adapter-test ui-test save-test native-import-test nodes-test scene-nodes-test
	cd .. && python3 -m unittest host_tests.test_roster_extract host_tests.test_balance

ido-test:
	mkdir -p ../build/host_tests
	@set -e; for source in $(CORE) ../src/roguelike/rl_meta.c; do \
		../tools/ido/linux/7.1/cc -c -O2 -mips2 -G 0 -non_shared \
		$(INCLUDES) $$source -o ../build/host_tests/ido-$$(basename $$source .c).o; \
	done
	@echo "Pure core IDO 7.1 compilation: OK"

battle-adapter-test:
	cd .. && bash host_tests/run_battle_adapter.sh

meta-test:
	@set -e; trap 'rm -f test_meta test_meta_san' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) test_meta.c $(CORE) \
	../src/roguelike/rl_meta.c -o test_meta; \
	./test_meta; \
	$(CC) $(CPPFLAGS) $(SANFLAGS) $(INCLUDES) test_meta.c $(CORE) \
	../src/roguelike/rl_meta.c -o test_meta_san; \
	ASAN_OPTIONS=detect_leaks=1 ./test_meta_san

.PHONY: scene-nodes-test
scene-nodes-test:
	@set -e; trap 'rm -f test_scene_nodes test_scene_nodes_san' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -I.. -iquote ../include -Wno-endif-labels \
	-include scene_nodes_stubs.h test_scene_nodes.c \
	../src/fragments/rl/rl_scene_nodes.c ../src/roguelike/rl_ui_model.c \
	$(CORE) ../src/roguelike/rl_meta.c -o test_scene_nodes; \
	./test_scene_nodes; \
	$(CC) $(CPPFLAGS) $(SANFLAGS) $(INCLUDES) -I.. -iquote ../include -Wno-endif-labels \
	-include scene_nodes_stubs.h test_scene_nodes.c \
	../src/fragments/rl/rl_scene_nodes.c ../src/roguelike/rl_ui_model.c \
	$(CORE) ../src/roguelike/rl_meta.c -o test_scene_nodes_san; \
	ASAN_OPTIONS=detect_leaks=1 ./test_scene_nodes_san

.PHONY: nodes-test
nodes-test:
	@set -e; trap 'rm -f test_nodes test_nodes_san' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) test_nodes.c $(CORE) ../src/roguelike/rl_meta.c -o test_nodes; \
	./test_nodes; \
	$(CC) $(CPPFLAGS) $(SANFLAGS) $(INCLUDES) test_nodes.c $(CORE) ../src/roguelike/rl_meta.c -o test_nodes_san; \
	ASAN_OPTIONS=detect_leaks=1 ./test_nodes_san
content-test:
	@set -e; trap 'rm -f test_content test_content_san' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) test_content.c $(CORE) -o test_content; \
	./test_content; \
	$(CC) $(CPPFLAGS) $(SANFLAGS) $(INCLUDES) test_content.c $(CORE) -o test_content_san; \
	ASAN_OPTIONS=detect_leaks=1 ./test_content_san

native-import-test:
	mkdir -p ../build/host_tests
	$(CC) $(CFLAGS) test_native_imports.c ../src/roguelike/rl_native_imports.c -o ../build/host_tests/native-imports
	../build/host_tests/native-imports
	cd .. && python3 -m unittest host_tests.test_archive_import_generator
save-test:
	@set -e; trap 'rm -f test_save test_save_san' EXIT; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) test_save.c $(CORE) \
	../src/roguelike/rl_save.c ../src/roguelike/rl_pak.c -o test_save; \
	./test_save; \
	$(CC) $(CPPFLAGS) $(SANFLAGS) $(INCLUDES) test_save.c $(CORE) \
	../src/roguelike/rl_save.c ../src/roguelike/rl_pak.c -o test_save_san; \
	ASAN_OPTIONS=detect_leaks=1 ./test_save_san

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
	rm -f test_core test_core_san test_save test_save_san $(UI_TEST_BIN)
