# Each directory under tests/ builds to build/<dir>.exe (all .c in it + common/t.c).
# The DLL under test is deliberately NOT linked; everything resolves via GetProcAddress.
CC        = x86_64-w64-mingw32-gcc
CC32      = i686-w64-mingw32-gcc
WINDRES   = x86_64-w64-mingw32-windres
CFLAGS   ?= -O1 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format
AREAS    := $(notdir $(wildcard tests/*))
AREAS    := $(filter-out compare_fixtures,$(AREAS))
EXES     := $(addprefix build/,$(addsuffix .exe,$(AREAS)))
all: $(EXES) probes
.SECONDEXPANSION:
build/%.exe: $$(wildcard tests/$$*/*.c) common/t.c common/t.h common/modules.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Icommon -o $@ $(wildcard tests/$*/*.c) common/t.c -static -lkernel32 -ladvapi32 -lntdll

# ---------------------------------------------------------------- probes (recorders; docs/PROBE_FORMAT.md)
# probes/<name>/*.c (*.cpp) + common/rec.c -> build/probes/<name>.exe   (no manifest: DPI-unaware, comctl32 5.82)
# Manifest variants (common/manifests/<v>.manifest embedded as RT_MANIFEST #1 via windres):
#   build/probes/<name>.<v>.exe for v in: u (dpiAware=false), v6 (comctl32 6 + unaware), sys (system-aware),
#   pm2 (PerMonitorV2, PerMonitor fallback), v6sys, v6pm2.      build/probes/<name>.x86.exe = 32-bit, no manifest.
# Extra link flags for one probe: probes/<name>/LIBS (one line).  `make probe-variants` builds every variant.
PROBES     := $(sort $(notdir $(patsubst %/,%,$(dir $(wildcard probes/*/*.c probes/*/*.cpp)))))
VARIANTS   := u v6 sys pm2 v6sys v6pm2
PROBE_LIBS := -lgdi32 -luser32 -lcomctl32 -ldwrite -lusp10 -luxtheme -ladvapi32 -lole32 -loleaut32 -luuid -lshell32 -lkernel32
REC_DEPS   := common/rec.c common/rec.h
probe_src   = $(wildcard probes/$(1)/*.c probes/$(1)/*.cpp)
probe_extra = $(if $(wildcard probes/$(1)/LIBS),$(shell cat probes/$(1)/LIBS)) $(if $(filter %.cpp,$(call probe_src,$(1))),-lstdc++)

probes: $(PROBES:%=build/probes/%.exe) build/vmrunner.exe
probe-variants: $(foreach v,$(VARIANTS),$(PROBES:%=build/probes/%.$(v).exe))
probes32: $(PROBES:%=build/probes/%.x86.exe)

build/res/%.o: common/manifests/%.manifest
	@mkdir -p build/res
	printf '1 24 "%s"\n' $< > build/res/$*.rc
	$(WINDRES) -O coff -o $@ build/res/$*.rc

# explicit rules per probe (and per probe x variant): no pattern ambiguity between hello.exe and hello.v6.exe
define PROBE_RULES
build/probes/$(1).exe: $(call probe_src,$(1)) $(REC_DEPS)
	@mkdir -p build/probes
	$$(CC) $$(CFLAGS) -Icommon -Iprobes/$(1) -o $$@ $(call probe_src,$(1)) common/rec.c -static $(call probe_extra,$(1)) $$(PROBE_LIBS)
build/probes/$(1).x86.exe: $(call probe_src,$(1)) $(REC_DEPS)
	@mkdir -p build/probes
	$$(CC32) $$(CFLAGS) -Icommon -Iprobes/$(1) -o $$@ $(call probe_src,$(1)) common/rec.c -static $(call probe_extra,$(1)) $$(PROBE_LIBS)
endef
define VARIANT_RULE
build/probes/$(1).$(2).exe: $(call probe_src,$(1)) $(REC_DEPS) build/res/$(2).o
	@mkdir -p build/probes
	$$(CC) $$(CFLAGS) -Icommon -Iprobes/$(1) -o $$@ $(call probe_src,$(1)) common/rec.c build/res/$(2).o -static $(call probe_extra,$(1)) $$(PROBE_LIBS)
endef
$(foreach p,$(PROBES),$(eval $(call PROBE_RULES,$(p))))
$(foreach p,$(PROBES),$(foreach v,$(VARIANTS),$(eval $(call VARIANT_RULE,$(p),$(v)))))

# in-guest plan runner used by tools/vm_run.sh (per-step timeout, markers, stderr relay, screenshots, display mode)
build/vmrunner.exe: tools/vmrunner/vmrunner.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< -static -luser32 -lgdi32 -ladvapi32

# record-format self test: C writer (run under headless Wine) vs tools/rec.py parser
rec-selftest: build/probes/hello.exe
	python3 tools/rec.py selftest

clean: ; rm -rf build
# Validation under Wine in a headless sandbox (see tools/wine_run.sh). Windows is the authority.
wine-run: all
	@for e in $(EXES); do echo "== $$e"; tools/wine_run.sh $$e | grep -E '^(FAIL|SKIP|SUMMARY)|^  (FAIL|NOTE)'; done
.PHONY: all clean wine-run probes probe-variants probes32 rec-selftest
