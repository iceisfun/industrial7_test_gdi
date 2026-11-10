# Each directory under tests/ builds to build/<dir>.exe (all .c in it + common/t.c).
# The DLL under test is deliberately NOT linked; everything resolves via GetProcAddress.
CC      = x86_64-w64-mingw32-gcc
CFLAGS  ?= -O1 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format
AREAS   := $(notdir $(wildcard tests/*))
EXES    := $(addprefix build/,$(addsuffix .exe,$(AREAS)))
all: $(EXES)
.SECONDEXPANSION:
build/%.exe: $$(wildcard tests/$$*/*.c) common/t.c common/t.h common/modules.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Icommon -o $@ $(wildcard tests/$*/*.c) common/t.c -static -lkernel32 -ladvapi32 -lntdll
clean: ; rm -rf build
# Validation under Wine in a headless sandbox (see tools/wine_run.sh). Windows is the authority.
wine-run: all
	@for e in $(EXES); do echo "== $$e"; tools/wine_run.sh $$e | grep -E '^(FAIL|SKIP|SUMMARY)|^  (FAIL|NOTE)'; done
.PHONY: all clean wine-run
