SOURCES := $(shell find . -name '*.c' -o -name '*.h' | grep -v '.git/' | grep -v '.ufbt/' | grep -v 'dist/')

.PHONY: all test lint format clean

all: test lint

test:
	$(MAKE) -C tests clean run

lint:
	@echo "=== clang-format check ==="
	@clang-format --style=file --Werror --dry-run $(SOURCES) && echo "All files formatted correctly."

format:
	@echo "=== clang-format fix ==="
	@clang-format --style=file -i $(SOURCES) && echo "All files formatted."

clean:
	$(MAKE) -C tests clean
