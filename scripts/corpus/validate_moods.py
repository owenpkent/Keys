"""Can Keys' mood tags be validated against data? Partly. This measures the part that can.

THE JOIN. Chordonomicon carries a `spotify_song_id` on 73% of its songs. Spotify's audio-feature
dumps carry **valence** (0..1, "musical positiveness") and **energy** (0..1) per track id. Join the
two and every chord progression in the corpus gains a measured position on the two axes that music
psychology actually uses - Russell's circumplex, valence against arousal.

Then, for each of Keys' rows, take the mean valence and energy of the songs that play it, and ask
whether the rows tagged Melancholic really do sit lower on valence than the rows tagged Triumphant.

WHAT THIS CAN AND CANNOT SETTLE, because the answer is "partly" and the limits are the interesting
half:

  * **It can check the ordering.** If "Sad" rows do not come out below "Happy" rows, something is
    wrong with the tags, the data, or this script - and that is worth knowing.
  * **It cannot separate two words in the same region.** Valence and arousal are two numbers.
    "Haunting" and "Eerie", "Dreamy" and "Atmospheric" land in the same place on both, and no amount
    of this data will tell them apart. A 46-word categorical vocabulary is strictly richer than a
    2-D model, which is why it is a vocabulary and not a pair of sliders.
  * **Spotify's valence is computed from AUDIO, not harmony.** Timbre, tempo, production and vocals
    all move it. A despairing lyric over major chords reads high-valence. So a correlation here
    says "songs that use this progression tend to sound positive", never "this progression is
    positive". That is a real effect and a noisy one, and the sample size is what makes it usable.
  * **Confounds ride along.** Genre correlates with both chords and production, so some of any
    signal is "this progression is common in dance music, and dance music is energetic".

LICENCE. Chordonomicon is CC-BY-NC-4.0 and the larger Spotify dump is too; the BSD-licensed
114k-track set is the permissive fallback. Owen's call (2026-08-18) is that Keys is personal use.
See `datasets/README.md`. Nothing measured here is shipped: it validates tags that were already
authored, and a tag is not changed by this script.
"""
import csv
import os
import re
import sys
from collections import defaultdict

import pandas as pd

CHORDS = "datasets/chordonomicon_v2.csv"
FEATS_BSD = "datasets/spotify_tracks_bsd.csv"
FEATS_BIG = "datasets/spotify_features_huge.parquet"
# What `join_features.py` leaves behind: the big dump reduced to only the ids Chordonomicon uses,
# a few MB instead of 4 GB. Preferred when it exists, which is what makes a wide join affordable.
FEATS_JOINED = "datasets/valence_for_chordonomicon.csv"
LIB = "src/ChordLibrary.h"

DEG = {"I": 0, "II": 2, "III": 4, "IV": 5, "V": 7, "VI": 9, "VII": 11}
MINOR_SUFFIX = ("m7b5", "m7", "m9", "m6", "mM7", "madd9", "m", "dim7", "dim")
ACCIDENTAL = {"s": 1, "#": 1, "b": -1, "f": -1}
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def numeral_parts(tok):
    m = re.fullmatch(r"([b#]?)([IViv]+)(.*)", tok)
    if not m:
        return None
    acc, num, suf = m.groups()
    base = DEG.get(num.upper())
    if base is None:
        return None
    semi = (base + (-1 if acc == "b" else 1 if acc == "#" else 0)) % 12
    return semi, (num.islower() or any(suf.startswith(x) for x in MINOR_SUFFIX))


def chord_parts(sym):
    m = re.match(r"^([A-G])([sbf#]?)(.*)", sym)
    if not m:
        return None
    base = PC.get(m.group(1))
    if base is None:
        return None
    rest = m.group(3)
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
    """name -> (shape, [moods]). Moods come out of the same braces the table writes them in."""
    src = re.sub(r"\s+", " ", open(path, encoding="utf-8", errors="replace").read())
    rows = {}
    pat = r'\{ "([^"]+)", "([^"]+)", Function::\w+, (\w+), \{ ([^}]*) \}'
    for m in re.finditer(pat, src):
        name, numerals, _mode, moods = m.groups()
        parts = [numeral_parts(t) for t in numerals.split()]
        if not parts or any(p is None for p in parts):
            continue
        first = parts[0][0]
        shape = tuple(((p[0] - first) % 12, p[1]) for p in parts)
        rows[name] = (shape, re.findall(r'"([^"]+)"', moods), _mode)
    return rows


def load_features():
    """track id -> (valence, energy), from whichever dumps are present."""
    feats = {}
    if os.path.exists(FEATS_JOINED):
        with open(FEATS_JOINED, encoding="utf-8", errors="replace", newline="") as f:
            for row in csv.DictReader(f):
                try:
                    feats[row["track_id"]] = (float(row["valence"]), float(row["energy"]))
                except (KeyError, ValueError):
                    pass
        print(f"  pre-joined set: {len(feats):,} tracks")
    if not feats and os.path.exists(FEATS_BIG) and os.environ.get("SKIP_BIG") != "1":
        df = pd.read_parquet(FEATS_BIG)
        cols = {c.lower(): c for c in df.columns}
        idc = cols.get("id") or cols.get("track_id") or cols.get("spotify_id")
        if idc and "valence" in cols and "energy" in cols:
            for i, v, e in zip(df[idc], df[cols["valence"]], df[cols["energy"]]):
                feats[str(i)] = (float(v), float(e))
            print(f"  big dump: {len(feats):,} tracks  (cols {idc}/valence/energy)")
        else:
            print("  big dump: unexpected columns:", list(df.columns)[:14])
    if os.path.exists(FEATS_BSD):
        with open(FEATS_BSD, encoding="utf-8", errors="replace", newline="") as f:
            for row in csv.DictReader(f):
                tid = (row.get("track_id") or row.get("id") or "").strip()
                try:
                    if tid and tid not in feats:
                        feats[tid] = (float(row["valence"]), float(row["energy"]))
                except (KeyError, ValueError):
                    pass
        print(f"  after BSD set: {len(feats):,} tracks")
    return feats


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 200000

    print("loading audio features ...")
    feats = load_features()
    if not feats:
        print("no features loaded")
        return 1

    rows = keys_rows(LIB)
    by_shape = defaultdict(list)
    for name, (shape, _m, _mode) in rows.items():
        by_shape[shape].append(name)
    lengths = sorted({len(s) for s in by_shape})
    print(f"Keys rows: {len(rows)}")

    # valence/energy samples per row name
    vals = defaultdict(list)
    matched_songs = 0
    songs = 0

    csv.field_size_limit(1 << 30)
    with open(CHORDS, encoding="utf-8", errors="replace", newline="") as f:
        for row in csv.DictReader(f):
            songs += 1
            if songs > limit:
                break
            tid = (row.get("spotify_song_id") or "").strip()
            fv = feats.get(tid)
            if fv is None:
                continue
            matched_songs += 1
            seq = []
            for tok in (row.get("chords") or "").split():
                if tok.startswith("<"):
                    continue
                cp = chord_parts(tok)
                if cp:
                    seq.append(cp)
            hit = set()
            for n in lengths:
                for i in range(len(seq) - n + 1):
                    base = seq[i][0]
                    shape = tuple(((pc - base) % 12, mn) for pc, mn in seq[i:i + n])
                    for name in by_shape.get(shape, ()):
                        hit.add(name)
            for name in hit:          # once per song, not once per occurrence
                vals[name].append(fv)

    print(f"songs read {songs:,}, joined to features {matched_songs:,}\n")

    # Mood-level aggregate: every row carrying the tag, weighted by how many songs backed it.
    mood_v, mood_e, mood_n = defaultdict(float), defaultdict(float), defaultdict(int)
    for name, (_shape, moods, _mode) in rows.items():
        s = vals.get(name)
        if not s or len(s) < 30:      # a row nobody plays says nothing about its tag
            continue
        for mood in moods:
            for v, e in s:
                mood_v[mood] += v
                mood_e[mood] += e
                mood_n[mood] += 1

    # ---- THE CONTROL, and it should be read before the mood table --------------------------
    #
    # Rows whose chords are mostly MINOR against rows whose chords are mostly MAJOR. That minor
    # sounds sadder than major is about the most replicated finding in music psychology, so it is
    # the one result this pipeline must reproduce. If it does not, nothing below means anything -
    # the join, the shape matching or the feature data is broken, and a plausible-looking mood
    # ordering would just be noise arranged alphabetically by accident.
    #
    # It is also the honest yardstick for effect size. Whatever gap minor-vs-major produces is
    # roughly the most a *harmonic* fact can move Spotify's valence, which is computed from audio.
    # A mood spread much smaller than that gap is not a signal.
    # **Split on the row's declared mode, not on how many of its chords are minor.** Counting
    # minor chords is the obvious test and it is wrong, which the first run proved by failing: in a
    # minor key most of the triads are *major* - bIII, bVI and bVII all are - so the Andalusian
    # cadence, "i bVII bVI V", counts three major against one minor and lands on the major side of
    # a test meant to identify it as minor. What makes a progression minor is its tonic, and
    # `Entry::mode` is the field that says so.
    MINOR_MODES = {"kAeolian", "kDorian", "kPhrygian", "kLocrian", "kHarmMinor", "kMelMinor"}
    maj_v = [v for name, (_s, _m, mode) in rows.items()
             if vals.get(name) and mode not in MINOR_MODES for v, _e in vals[name]]
    min_v = [v for name, (_s, _m, mode) in rows.items()
             if vals.get(name) and mode in MINOR_MODES for v, _e in vals[name]]
    print("=" * 74)
    print("CONTROL: does minor read sadder than major in this data?")
    print("=" * 74)
    if maj_v and min_v:
        mv, nv = sum(maj_v) / len(maj_v), sum(min_v) / len(min_v)
        print(f"  mostly-major rows: valence {mv:.3f}  (n={len(maj_v):,} song-row pairs)")
        print(f"  mostly-minor rows: valence {nv:.3f}  (n={len(min_v):,})")
        print(f"  gap: {mv - nv:+.3f}   <- the ceiling on what harmony alone can move here")
        if mv - nv < 0.01:
            print("  *** the control FAILED. Treat everything below as noise. ***")
    else:
        print("  not enough rows on one side to compare")
    print()

    print("=" * 74)
    print("MEAN VALENCE OF THE SONGS THAT PLAY EACH MOOD'S ROWS  (0 = sad, 1 = happy)")
    print("=" * 74)
    ranked = sorted(((mood_v[m] / mood_n[m], mood_e[m] / mood_n[m], mood_n[m], m)
                     for m in mood_n if mood_n[m] >= 200), reverse=True)
    for v, e, n, m in ranked:
        bar = "#" * int(v * 44)
        print(f"{v:.3f}  energy {e:.3f}  n={n:>7,}  {m:<14} {bar}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
