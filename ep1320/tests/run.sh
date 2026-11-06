#!/bin/bash
# Host tests under AddressSanitizer and UndefinedBehaviorSanitizer:
#   OPUS_SDK=/path/to/opus2-plugin-kit/sdk bash tests/run.sh
set -eo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); SRC=$HERE/..; OUT=$(mktemp -d)
SDK=${OPUS_SDK:?set OPUS_SDK to the plugin kit sdk folder}
CF="-std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror -I$SRC"
${CC:-cc} $CF -o "$OUT/host_test" "$HERE/host_test.c" "$SRC/p_voice.c"
${CC:-cc} $CF -o "$OUT/decode_test" "$HERE/decode_test.c"
${CC:-cc} $CF -I"$SDK" -o "$OUT/mock" "$SRC/tools/preview/mock.c" "$SRC/p_page.c" "$SRC/p_voice.c"
${CC:-cc} $CF -I"$SDK" -o "$OUT/engine_test" "$HERE/engine_test.c" "$SRC/ep1320.c" "$SRC/p_voice.c"
"$OUT/host_test"
"$OUT/engine_test"
"$OUT/decode_test"
python3 "$HERE/page_check.py" "$OUT/mock"
rm -rf "$OUT"
