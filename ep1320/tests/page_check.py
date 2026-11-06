#!/usr/bin/env python3
"""The T1 page with many random keys and values, through the host mock (tools/preview/mock.c): every shape inside the
drawing and inside the OP-1's window (y 2-146), every text slot in one place. Run by tests/run.sh."""
import random, subprocess, sys
mock = sys.argv[1]
random.seed(1320)
places, bad, n = {}, 0, 0
for t in range(300):
    key, sh = random.randrange(24), random.randrange(2)
    nums = [random.randint(-24320, 24832)] + [random.choice([0, 32767, random.randint(0, 32767)]) for _ in range(7)]
    voices = [f'{random.randrange(24)}:{random.randrange(101)}' for _ in range(random.randrange(13))]
    out = subprocess.run([mock, str(key), str(sh)] + [str(v) for v in nums] + voices, capture_output=True, text=True,
                         check=True).stdout
    for ln in out.splitlines():
        p = ln.split()
        n += 1
        if p[0] == 'text':
            slot, al, x, y, size = p[1], p[2], int(p[3]), int(p[4]), int(p[5])
            ys = [y, y + size]
            if places.setdefault(slot, (al, x, y, size)) != (al, x, y, size):
                bad += 1; print('slot moved:', ln)
        elif p[0] == 'dot':
            x, y, r = int(p[1]), int(p[2]), int(p[3]); ys = [y - r, y + r]
        else:
            ys = [int(p[2]), int(p[4])]
        if min(ys) < 2 or max(ys) > 146:
            bad += 1; print('outside the window:', ln)
print(f'page: 300 random pages, {n} shapes and texts, {bad} outside the window or moved')
sys.exit(1 if bad else 0)
