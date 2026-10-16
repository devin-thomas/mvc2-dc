PYTHON ?= python3
CDI ?=
BIN ?=
CONFIG := config/mvc2_na_dc.yaml
TARGET := build-inputs/1ST_READ_descrambled.bin

.PHONY: all setup descramble scramble split build match check progress test clean help

all: check

help:
	@echo "Marvel vs. Capcom 2 (Dreamcast NA) Decompilation"
	@echo "================================================"
	@echo "  make setup CDI=/path/to/game.cdi  : Extract disc assets & stage descrambled binary"
	@echo "  make split                         : Split binary into assembly and data segments"
	@echo "  make descramble                    : Descramble on-disc 1ST_READ.BIN to linear RAM image"
	@echo "  make scramble                      : Scramble linear RAM image to on-disc format"
	@echo "  make progress                      : Calculate code/data matching percentages"
	@echo "  make check                         : Verify built output against reference binary"
	@echo "  make test                          : Run tooling verification tests"
	@echo "  make clean                         : Clean build directories"

setup:
	@if [ -n "$(CDI)" ]; then \
		$(PYTHON) tools/setup_disc.py --cdi "$(CDI)"; \
	elif [ -n "$(BIN)" ]; then \
		$(PYTHON) tools/setup_disc.py --bin "$(BIN)"; \
	else \
		echo "Usage: make setup CDI=/path/to/game.cdi" >&2; \
		exit 1; \
	fi

descramble:
	$(PYTHON) tools/descramble.py descramble build-inputs/extracted/1ST_READ.BIN $(TARGET)

scramble:
	$(PYTHON) tools/descramble.py scramble $(TARGET) build/1ST_READ_scrambled.bin

split:
	$(PYTHON) tools/split.py $(CONFIG)

progress:
	$(PYTHON) tools/progress.py --badge

test:
	$(PYTHON) tools/descramble.py verify

check:
	@if [ -f "$(TARGET)" ]; then \
		$(PYTHON) tools/inspect_dc.py "$(TARGET)"; \
	else \
		echo "Target binary $(TARGET) not staged. Run 'make setup CDI=...' first." >&2; \
	fi

clean:
	rm -rf build assets/*.bin asm/*.s
