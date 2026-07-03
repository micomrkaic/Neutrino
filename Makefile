# Neutrino build.
#
# Toolchain: a C23 compiler. gcc 14+ spells the flag -std=c23; gcc 13 calls the
# same thing -std=c2x. Apple Clang from Xcode 15+ accepts -std=c2x and the C23
# features used here (nullptr, [[noreturn]], enum : uint8_t). `cc` resolves to
# gcc on Linux and to Apple Clang on macOS.
#
# Common overrides:
#   make CC=clang            choose the compiler
#   make STD=c23             newer std spelling (gcc 14+, recent clang)
#   make WERROR=             drop -Werror (handy on a new compiler whose warning
#                            set differs from the one this was tuned against)
#   make READLINE=0          force the plain fgets REPL (skip libreadline)
CC      ?= cc
STD     ?= c2x
WERROR  ?= -Werror
CFLAGS   = -std=$(STD) -Wall -Wextra $(WERROR) -O2
LDFLAGS ?=
SRCS     = lexer.c arena.c ast.c parser.c value.c eval.c chunk.c compile.c vm.c repl.c main.c
HDRS     = lexer.h arena.h ast.h parser.h value.h eval.h repl.h chunk.h compile.h vm.h nrt.h
BIN      = neutrino
LIBS     = -lm

# On macOS, Homebrew's readline lives under the brew prefix rather than the
# default search path; add it so the probe and link can find it. Empty on Linux
# (or anywhere without Homebrew), so behaviour there is unchanged.
BREW := $(shell brew --prefix 2>/dev/null)
ifneq ($(BREW),)
  RL_INC := -I$(BREW)/include
  RL_LIB := -L$(BREW)/lib
endif

# Auto-detect readline (or the libedit shim that macOS's -lreadline resolves to);
# build a line-editing REPL if present, else fall back to a plain fgets REPL.
# Override with READLINE=0 to force the fallback.
READLINE ?= $(shell printf 'int main(void){char*l=readline("");return l!=0;}\n' \
              | $(CC) -xc - $(RL_INC) -include readline/readline.h $(RL_LIB) -lreadline -o /dev/null 2>/dev/null && echo 1)
ifeq ($(READLINE),1)
  CFLAGS += -DHAVE_READLINE $(RL_INC)
  LIBS   += $(RL_LIB) -lreadline
  # Real GNU readline also exposes rl_catch_signals and the signal-cleanup
  # helpers; the macOS libedit shim does not. Gate those extras separately so
  # the REPL still builds (with line editing + history) against libedit.
  GNU_READLINE := $(shell printf 'int main(void){rl_catch_signals=0;rl_cleanup_after_signal();return 0;}\n' \
              | $(CC) -xc - $(RL_INC) -include readline/readline.h $(RL_LIB) -lreadline -o /dev/null 2>/dev/null && echo 1)
  ifeq ($(GNU_READLINE),1)
    CFLAGS += -DHAVE_GNU_READLINE
  endif
endif

$(BIN): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) $(LIBS) -o $@

# Headless test driver: same VM as `neutrino`, but reads stdin line by line and
# echoes each result (no readline), so piping a script in gives one result per
# line. Handy for batch/regression testing and ASan runs.
VM_SRCS = lexer.c arena.c ast.c parser.c value.c eval.c chunk.c compile.c vm.c vmtest.c
vmtest: $(VM_SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(VM_SRCS) -lm -o $@

# ASan/UBSan build of the VM driver, for leak-checking the error path.
vmtest-asan: $(VM_SRCS)
	$(CC) -std=$(STD) -Wall -Wextra -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	   $(VM_SRCS) -lm -o $@

# Regression suite: golden-output tests in tests/*.test (see tests/run.sh),
# plus bytecode-disassembly goldens in tests/dis/ (see tests/run_dis.sh).
test: vmtest $(BIN)
	@bash tests/run.sh
	@bash tests/run_dis.sh
	@bash tests/run_manual.sh

# Same corpus, every input run under AddressSanitizer/UBSan; fails on any leak.
test-asan: vmtest-asan
	@bash tests/run.sh --asan

# Regenerate MANUAL.pdf from MANUAL.md (needs pandoc + xelatex; the styling
# header is optional). On a full TeX Live / MacTeX install the stock template
# works as-is.
manual:
	pandoc MANUAL.md -o MANUAL.pdf --pdf-engine=xelatex --toc --toc-depth=2 \
	  -V geometry:margin=2.4cm -V fontsize=10pt -V colorlinks=true

run:    $(BIN); ./$(BIN)
repl:   $(BIN); ./$(BIN)
sample: $(BIN); ./$(BIN) --sample
ast:    $(BIN); ./$(BIN) --ast
tokens: $(BIN); ./$(BIN) --tokens
clean:; rm -f $(BIN) vmtest vmtest-asan
.PHONY: run repl sample ast tokens clean test test-asan
