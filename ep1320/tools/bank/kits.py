"""The four medieval kits: which EP-1320 sound sits on which OP-1 key.

Keys are numbered 0..23 from the lowest key of the OP-1 keyboard (F) to the highest (E), black keys included:
    0 F   1 F#  2 G   3 G#  4 A   5 A#  6 B   7 C   8 C#  9 D   10 D#  11 E
    12 F  13 F# 14 G  15 G# 16 A  17 A# 18 B  19 C  20 C# 21 D  22 D#  23 E
Each entry is (EP-1320 file number, tune in semitones). The file number is the leading number of the file name in the
"ep1320 sounds" folder ("001 BATTLE KIK.wav" -> 1). Tune shifts that one key; the green knob shifts the whole kit.
Each sound appears in one kit only, so the four presets really are four different sets of sounds.
Copyright (C) 2026 Sammi (Project Medieval). GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE, NOTICE).
"""

KITS = [
    ("war drums", [            # kicks, stomps, war toms and plague drums low; clangs and bells high
        (1, 0), (6, 0), (2, 0), (7, 0), (50, 0), (19, 0), (51, 0), (52, 0), (18, 0), (58, 0), (157, 0), (60, 0),
        (61, 0), (66, 0), (62, 0), (175, 0), (63, 0), (43, 0), (10, 0), (15, 0), (156, 0), (155, 0), (171, 0), (8, 0),
    ]),
    ("tavern", [               # frame drums and toms on the white keys low, clappers and coconuts between; tambourines high
        (53, 0), (30, 0), (54, 0), (31, 0), (46, 0), (32, 0), (47, 0), (48, 0), (33, 0), (49, 0), (34, 0), (55, 0),
        (11, 0), (20, 0), (12, 0), (27, 0), (13, 0), (28, 0), (21, 0), (29, 0), (23, 0), (26, 0), (25, 0), (199, 0),
    ]),
    ("armory", [               # junk, scrap and rubble low; clanks, swords and chains high
        (3, 0), (38, 0), (9, 0), (37, 0), (41, 0), (36, 0), (40, 0), (42, 0), (44, 0), (159, 0), (161, 0), (160, 0),
        (166, 0), (170, 0), (167, 0), (163, 0), (168, 0), (169, 0), (172, 0), (174, 0), (39, 0), (187, 0), (158, 0),
        (182, 0),
    ]),
    ("royal court", [          # ceremonial kicks, deep toms and soft plague drums low; stabs, horns and strings high
        (5, 0), (64, 0), (4, 0), (14, 0), (56, 0), (67, 0), (57, 0), (68, 0), (17, 0), (69, 0), (16, 0), (59, 0),
        (130, 0), (136, 0), (131, 0), (118, 0), (134, 0), (116, 0), (83, 0), (151, 0), (124, 0), (122, 0), (144, 0),
        (138, 0),
    ]),
]

# longest a sound may be (seconds) before it is faded out; the white knob (decay) shortens it further when played
MAX_LEN = 0.8
