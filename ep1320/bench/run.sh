#!/bin/bash
# The cycle bench in GDB's Blackfin simulator, with the shared toolchain:
#   . ~/mnt/Documents/op1-toolchain/env.sh && bash bench/run.sh
set -eo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); SRC=$HERE/..; OUT=$(mktemp -d)
GCC=$OP1_BFIN_TOOLCHAIN/bin/bfin-elf-gcc; RUN=${BFIN_SIM_RUN:-$BFIN_RUN}
CF="-O2 -mcpu=bf524-any -fno-indirect-inlining -ffreestanding -fno-builtin -nostdlib -fno-common -Wall -Wextra -Werror -I$SRC -I$HERE"
$GCC $CF -c "$SRC/p_voice.c" -o $OUT/voice.o
$GCC $CF -c "$SRC/p_run.S" -o $OUT/run.o
$GCC $CF -c "$HERE/bench.c" -o $OUT/bench.o
$GCC -mcpu=bf524-any -c "$HERE/crt0.S" -o $OUT/crt0.o
$GCC -mcpu=bf524-any -nostdlib -T "$HERE/link.ld" $OUT/crt0.o $OUT/bench.o $OUT/voice.o $OUT/run.o -lgcc -o $OUT/bench.elf
"$RUN" --model bf524 --memory-size 32m --memory-region 0xff800000,0x8000 $OUT/bench.elf
rm -rf $OUT
