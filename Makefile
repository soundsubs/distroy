# DISTROY -- Schwung audio_fx module
# Native build (for testing DSP + plugin loading on your dev machine) and
# the target shared-library name Schwung's chain host expects.
#
# Move's Cortex-A72 is aarch64 -- for the actual on-device binary you
# need an ARM64 cross-compile, not this native build. See scripts/build.sh.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -fPIC
LDLIBS  := -lm

SRC := src/distroy_dsp.c src/distroy_audio_fx.c
OBJ := $(SRC:.c=.o)
LIB := DISTROY.so

.PHONY: all clean test

all: $(LIB)

$(LIB): $(OBJ)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJ) $(LDLIBS)

%.o: %.c src/distroy_dsp.h src/audio_fx_api_v2.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Headless DSP correctness check -- no plugin-loading dependency.
test: src/distroy_dsp.c src/distroy_dsp.h test/test_dsp.c
	$(CC) -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -o test_dsp src/distroy_dsp.c test/test_dsp.c -lm
	./test_dsp

clean:
	rm -f $(OBJ) $(LIB) test_dsp
