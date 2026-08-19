"""Measure `src/ChordLibrary.h` against the Chordonomicon corpus.

Answers three questions, and the third is the one worth the download:

  1. **Is each row real?** How often does its exact chord sequence turn up as a window in a real
     song. A row nobody plays is not wrong, but it should not be in the first page of results.
  2. **What is missing?** The most common sequences in the corpus that Keys has no row for.
  3. **Which section is a row?** Chordonomicon annotates verse / chorus / bridge, which is the one
     axis `docs/CHORD_LIBRARY.md` §3 wanted and deliberately left unbuilt rather than invent.

LICENCE. Chordonomicon is CC-BY-NC-4.0. Owen's call (2026-08-18) is that Keys is personal use,
which that licence permits squarely. **Nothing this script prints may be pasted into a shipped
table if Keys ever becomes commercial** - see `datasets/README.md`.

The corpus stores absolute chords ("C", "Am", "F#m7"), so everything here is transposed to a
common tonic before it is counted. Keys' table is in roman numerals, which are already relative,
so the two meet in the middle: pitch-class intervals from the first chord of the window.
"""
import csv
import os
import re
import sys
from collections import Counter, defaultdict

CSV = "datasets/chordonomicon_v2.csv"
LIB = "src/ChordLibrary.h"

PC = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "Fb": 4, "F": 5,
      "E#": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10,
      "B": 11, "Cb": 11}

# Keys' own numeral -> semitones, mirroring ChordMarkov.h's numeralDegrees(): a fixed MAJOR-scale
# table for every mode, which is why minor rows spell bIII / bVI / bVII. See datasets/README.md.
DEG = {"I": 0, "II": 2, "III": 4, "IV": 5, "V": 7, "VI": 9, "VII": 11}


# Minor-making suffixes in Keys' grammar. A lowercase numeral is minor already; an uppercase one
# with one of these is minor too ("Im7" would be, though nothing writes it that way).
MINOR_SUFFIX = ("m7b5", "m7", "m9", "m6", "mM7", "madd9", "m", "dim7", "dim")


def numeral_parts(tok):
    """(semitone, is_minor) for one of Keys' numerals, or None."""
    m = re.fullmatch(r"([b#]?)([IViv]+)(.*)", tok)
    if not m:
        return None
    acc, num, suf = m.groups()
    base = DEG.get(num.upper())
    if base is None:
        return None
    semi = (base + (-1 if acc == "b" else 1 if acc == "#" else 0)) % 12
    minor = num.islower() or any(suf.startswith(x) for x in MINOR_SUFFIX)
    return semi, minor


# The corpus writes a sharp as "s" and a minor as "min": "Fs7" is F#7, "Amin" is A minor,
# "A/Cs" is A over C#. Worth stating because a regex written for "F#7" silently matches "F" and
# counts the wrong root, which would not fail - it would just quietly measure the wrong thing.
ACCIDENTAL = {"s": 1, "#": 1, "b": -1, "f": -1}


def chord_parts(sym):
    """(root pitch class, is_minor) for a corpus chord symbol, or None.

    **Quality matters and leaving it out made the measure lie.** Matching on root motion alone,
    every two-chord row a fifth apart collapsed onto one shape, so "Gospel amen (IV-I)", "Salsa
    two-chord (im7-V7)" and "Harmonic minor vamp (i-V)" all came back with the identical count -
    a number about the interval, presented as though it were about the progression.
    """
    m = re.match(r"^([A-G])([sbf#]?)(.*)", sym)
    if not m:
        return None
    base = PC.get(m.group(1))
    if base is None:
        return None
    rest = m.group(3)
    # The corpus writes minor as "min" and diminished as "dim"; a bare letter is major.
    # "maj" must be excluded before the bare "m", or every maj7/maj9 in the corpus is counted
    # as minor: rest is "maj7" for "Cmaj7", and "maj7".startswith("m") is true. That silently
    # mislabelled every major-seventh chord, so rows written with M7 never joined the corpus
    # windows they actually match, and the major/minor valence the whole mood table is gated on
    # was computed over bad data.
    minor = not rest.startswith("maj") and (
        rest.startswith("min") or rest.startswith("dim") or rest.startswith("m")
    )
    return (base + ACCIDENTAL.get(m.group(2), 0)) % 12, minor


def keys_rows(path):
    src = re.sub(r"\s+", " ", open(path, encoding="utf-8", errors="replace").read())
    rows = {}
    for m in re.finditer(r'\{ "([^"]+)", "([^"]+)", Function::', src):
        name, numerals = m.group(1), m.group(2)
        parts = [numeral_parts(t) for t in numerals.split()]
        if parts and all(p is not None for p in parts):
            first = parts[0][0]
            # Relative to the row's own first chord, so it can be matched anywhere in a song, and
            # carrying major/minor so two rows that differ only in quality do not collapse.
            rows[name] = (numerals, tuple(((p[0] - first) % 12, p[1]) for p in parts))
    return rows


def main():
    if not os.path.exists(CSV):
        print("corpus not found:", CSV)
        return 1

    # A full pass over 680k songs at every window length Keys uses is tens of minutes of pure
    # Python. Sampled by default and it says so in the output: this is a ranking, and a ranking
    # over 150k songs and one over 680k do not disagree about which progressions are common.
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 150000

    rows = keys_rows(LIB)
    by_shape = defaultdict(list)
    for name, (numerals, shape) in rows.items():
        by_shape[shape].append(name)
    lengths = sorted({len(s) for s in by_shape})
    print(f"Keys rows parsed: {len(rows)}   window lengths to scan: {lengths[:12]}")

    hits = Counter()
    section_hits = defaultdict(Counter)
    window_counts = Counter()      # every shape seen, for "what is missing"
    songs = 0

    csv.field_size_limit(1 << 30)
    with open(CSV, encoding="utf-8", errors="replace", newline="") as f:
        r = csv.DictReader(f)
        chord_col = None
        for row in r:
            if chord_col is None:
                for cand in ("chords", "progression", "chord_progression"):
                    if cand in row:
                        chord_col = cand
                        break
                if chord_col is None:
                    print("columns:", list(row)[:12])
                    return 2
            songs += 1
            if songs > limit:
                break
            if songs % 25000 == 0:
                print(f"  ... {songs} songs", flush=True)

            raw = row.get(chord_col) or ""
            # The chord field carries <section> markers inline; keep the current one.
            section = ""
            seq = []           # (pc, section)
            for tok in raw.split():
                if tok.startswith("<") and tok.endswith(">"):
                    section = tok.strip("<>")
                    section = re.sub(r"_\d+$", "", section)
                    continue
                cp = chord_parts(tok)
                if cp is not None:
                    seq.append((cp[0], cp[1], section))

            for n in lengths:
                if len(seq) < n:
                    continue
                for i in range(len(seq) - n + 1):
                    win = seq[i:i + n]
                    base = win[0][0]
                    shape = tuple(((pc - base) % 12, mn) for pc, mn, _ in win)
                    if n == 4:
                        window_counts[shape] += 1  # only length 4, or this Counter is unbounded
                    if shape in by_shape:
                        for name in by_shape[shape]:
                            hits[name] += 1
                            section_hits[name][win[0][2]] += 1

    print(f"\nsongs scanned: {songs}\n")

    print("=" * 78)
    print("HOW OFTEN EACH KEYS ROW TURNS UP IN REAL SONGS  (top 30)")
    print("=" * 78)
    for name, c in hits.most_common(30):
        sec = section_hits[name].most_common(1)
        where = f"  mostly {sec[0][0]}" if sec and sec[0][0] else ""
        print(f"{c:>9,}  {name}{where}")

    print()
    print("=" * 78)
    print("KEYS ROWS THE CORPUS NEVER PLAYS  (candidates for demotion, not deletion)")
    print("=" * 78)
    never = sorted(n for n in rows if hits[n] == 0)
    print(f"{len(never)} of {len(rows)}")
    for n in never[:40]:
        print("   ", n, "  ", rows[n][0])

    print()
    print("=" * 78)
    print("COMMON IN THE CORPUS, ABSENT FROM KEYS  (4-chord windows, top 30)")
    print("=" * 78)
    known = set(by_shape)
    # `s[0] == 0` used to be here to mean "starts on the tonic". It was correct while a shape was
    # a tuple of ints and silently wrong the moment quality went in and each element became
    # (interval, is_minor): the comparison could never be true, so this whole section reported
    # nothing missing - a filter that had quietly become "print an empty list". Shapes are relative
    # to their own first chord by construction, so the check was redundant anyway.
    missing = [(c, s) for s, c in window_counts.items()
               if len(s) == 4 and s not in known]
    missing.sort(reverse=True)
    for c, s in missing[:30]:
        spelled = " ".join(f"{iv}{'m' if mn else ''}" for iv, mn in s)
        print(f"{c:>9,}  semitones from the first chord (m = minor): {spelled}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
