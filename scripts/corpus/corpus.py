"""The corpus analysis, in DuckDB. Replaces the hand-rolled Python passes beside it.

Owen, 2026-08-18: "how about duckdb". Correct, and it changes what is affordable rather than just
what is tidy:

  * `join_features.py` streamed a 4 GB / 56M-row parquet in Python to pull out 376k rows. That is
    one SQL statement here, and DuckDB never materialises the rest.
  * `rank_against_chordonomicon.py` and `validate_moods.py` both **sampled** - 40k or 150k songs of
    680k - because a pure-Python pass over the whole corpus is tens of minutes. DuckDB does the
    tokenising, the pitch-class mapping and the aggregation in C++ over the whole thing.

**The split of labour is the design.** Python owns the *small* work that needs Keys' own grammar:
parsing `ChordLibrary.h`, translating roman numerals through the same fixed major-scale degree table
`ChordMarkov.h` uses. That is 355 rows and it is where all the domain knowledge lives. DuckDB owns
the *large* work: 680,000 songs, ten million chord windows, the join to audio features. Neither is
good at the other's half.

**Checked against the Python original before it was believed.** A rewrite that silently changes an
answer is worse than no rewrite. On the same 200,000-song sample the two agree to three decimal
places - control gap +0.028 either way, major 0.479 / minor 0.451, Lighthearted 0.517, Triumphant
0.493, Happy 0.490 - with the song counts differing by 0.3% from CSV-quoting edge cases. The old
scripts are kept beside this one as that check rather than as dead weight; delete them only when
something else can play the role.
"""
import argparse
import os
import re
import sys

import duckdb

CHORDS = "datasets/chordonomicon_v2.csv"
BIG = "datasets/spotify_features_huge.parquet"
JOINED = "datasets/valence_for_chordonomicon.csv"
LIB = "src/ChordLibrary.h"

# Keys' own numeral table: a fixed MAJOR-scale degree for every mode, which is why minor rows spell
# bIII / bVI / bVII. Mirrors ChordMarkov.h::numeralDegrees. See datasets/README.md for the trap.
DEG = {"I": 0, "II": 2, "III": 4, "IV": 5, "V": 7, "VI": 9, "VII": 11}
MINOR_SUFFIX = ("m7b5", "m7", "m9", "m6", "mM7", "madd9", "m", "dim7", "dim")
MINOR_MODES = {"kAeolian", "kDorian", "kPhrygian", "kLocrian", "kHarmMinor", "kMelMinor"}


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


def library_rows(path=LIB):
    """[(name, shape_key, moods, is_minor_mode)] from ChordLibrary.h.

    `shape_key` is the progression relative to its own first chord, as a string DuckDB can join on:
    "0M,7M,9m,5M" - semitones from the first chord, M or m per chord. Absolute pitch is gone, which
    is what lets a row match a song in any key.
    """
    src = re.sub(r"\s+", " ", open(path, encoding="utf-8", errors="replace").read())
    out = []
    pat = r'\{ "([^"]+)", "([^"]+)", Function::\w+, (\w+), \{ ([^}]*) \}'
    for m in re.finditer(pat, src):
        name, numerals, mode, moods = m.groups()
        parts = [numeral_parts(t) for t in numerals.split()]
        if not parts or any(p is None for p in parts):
            continue
        first = parts[0][0]
        key = ",".join(f"{(p[0] - first) % 12}{'m' if p[1] else 'M'}" for p in parts)
        out.append((name, key, re.findall(r'"([^"]+)"', moods), mode in MINOR_MODES))
    return out


def connect():
    con = duckdb.connect()
    con.execute("PRAGMA threads=8")
    return con


def register_library(con):
    rows = library_rows()
    con.execute("CREATE OR REPLACE TABLE lib (name VARCHAR, shape VARCHAR, minor_mode BOOLEAN)")
    con.executemany("INSERT INTO lib VALUES (?, ?, ?)",
                    [(n, k, mm) for n, k, _m, mm in rows])
    con.execute("CREATE OR REPLACE TABLE lib_mood (name VARCHAR, mood VARCHAR)")
    con.executemany("INSERT INTO lib_mood VALUES (?, ?)",
                    [(n, mood) for n, _k, moods, _mm in rows for mood in moods])
    lens = sorted({len(k.split(",")) for _n, k, _m, _mm in rows})
    print(f"library: {len(rows)} rows, window lengths {lens}")
    return lens


# One chord symbol -> "<pitch class><M|m>", in SQL. The corpus writes a sharp as "s" and a minor as
# "min"/"m"; a bare letter is major. Section markers (<verse_1>) are dropped by the caller.
CHORD_EXPR = """
  CASE regexp_extract(tok, '^([A-G])', 1)
    WHEN 'C' THEN 0 WHEN 'D' THEN 2 WHEN 'E' THEN 4 WHEN 'F' THEN 5
    WHEN 'G' THEN 7 WHEN 'A' THEN 9 WHEN 'B' THEN 11 END
  + CASE regexp_extract(tok, '^[A-G]([sbf#]?)', 1)
      WHEN 's' THEN 1 WHEN '#' THEN 1 WHEN 'b' THEN -1 WHEN 'f' THEN -1 ELSE 0 END
"""


def build_songs(con, limit=None):
    """One row per song: an ordered list of "<pc><M|m>" tokens, plus its spotify id."""
    lim = f"LIMIT {limit}" if limit else ""
    con.execute(f"""
      CREATE OR REPLACE TABLE songs AS
      WITH raw AS (
        SELECT id, spotify_song_id, chords FROM read_csv('{CHORDS}',
               header=true, quote='"', ignore_errors=true) {lim}
      ), toks AS (
        SELECT r.id, r.spotify_song_id, t.i AS ord, t.tok
        FROM raw r, UNNEST(str_split(r.chords, ' ')) WITH ORDINALITY AS t(tok, i)
        WHERE t.tok <> '' AND NOT starts_with(t.tok, '<')
          AND regexp_matches(t.tok, '^[A-G]')
      )
      SELECT id, any_value(spotify_song_id) AS sid,
             list(((({CHORD_EXPR}) % 12 + 12) % 12)::VARCHAR ||
                  CASE WHEN regexp_matches(tok, '^[A-G][sbf#]?(min|dim|m)') THEN 'm' ELSE 'M' END
                  ORDER BY ord) AS seq
      FROM toks GROUP BY id
    """)
    n = con.execute("SELECT count(*) FROM songs").fetchone()[0]
    print(f"songs parsed: {n:,}")
    return n


def build_windows(con, lengths):
    """Every window of every needed length, as a shape key relative to its own first chord."""
    parts = []
    for n in lengths:
        # list_slice is 1-based and inclusive; generate_series gives the start of each window.
        parts.append(f"""
          SELECT s.id, s.sid, {n} AS n, g.p AS pos,
                 list_slice(s.seq, g.p, g.p + {n} - 1) AS w
          FROM songs s, generate_series(1, length(s.seq) - {n} + 1) AS g(p)
          WHERE length(s.seq) >= {n}
        """)
    con.execute(f"""
      CREATE OR REPLACE TABLE win AS
      WITH raw AS ({' UNION ALL '.join(parts)})
      SELECT id, sid,
             list_aggregate(
               list_transform(w, x ->
                 (((x[1:len(x)-1]::INTEGER - w[1][1:len(w[1])-1]::INTEGER) % 12 + 12) % 12)::VARCHAR
                 || x[len(x):]),
               'string_agg', ',') AS shape
      FROM raw
    """)
    n = con.execute("SELECT count(*) FROM win").fetchone()[0]
    print(f"windows: {n:,}")
    return n


def cmd_join_features(con):
    """The 4 GB dump reduced to the ids Chordonomicon uses. One statement."""
    if not os.path.exists(BIG):
        print("big dump not present; nothing to cut")
        return
    con.execute(f"""
      COPY (
        SELECT DISTINCT f.track_id, f.valence, f.energy
        FROM read_parquet('{BIG}') f
        WHERE f.valence IS NOT NULL AND f.energy IS NOT NULL
          AND f.track_id IN (SELECT DISTINCT spotify_song_id
                             FROM read_csv('{CHORDS}', header=true, quote='"', ignore_errors=true)
                             WHERE spotify_song_id IS NOT NULL AND spotify_song_id <> '')
      ) TO '{JOINED}' (HEADER, DELIMITER ',')
    """)
    n = con.execute(f"SELECT count(*) FROM read_csv('{JOINED}', header=true)").fetchone()[0]
    print(f"wrote {JOINED}: {n:,} tracks")


def cmd_rank(con, lengths):
    con.execute("""
      CREATE OR REPLACE TABLE hits AS
      SELECT l.name, count(*) AS n, count(DISTINCT w.id) AS songs
      FROM win w JOIN lib l ON l.shape = w.shape
      GROUP BY l.name
    """)
    print("\nHOW OFTEN EACH ROW TURNS UP  (top 25, by distinct songs)")
    for name, n, songs in con.execute(
            "SELECT name, n, songs FROM hits ORDER BY songs DESC LIMIT 25").fetchall():
        print(f"  {songs:>8,} songs  {n:>10,} windows  {name}")

    never = con.execute("""
      SELECT l.name FROM lib l LEFT JOIN hits h USING (name)
      WHERE h.name IS NULL ORDER BY l.name
    """).fetchall()
    print(f"\nROWS THE CORPUS NEVER PLAYS: {len(never)} of "
          f"{con.execute('SELECT count(*) FROM lib').fetchone()[0]}")
    for (nm,) in never:
        print("   ", nm)

    print("\nCOMMON IN THE CORPUS, ABSENT FROM THE TABLE  (4-chord, top 20)")
    for shape, c in con.execute("""
      SELECT w.shape, count(*) c FROM win w
      WHERE len(str_split(w.shape, ',')) = 4
        AND w.shape NOT IN (SELECT shape FROM lib)
      GROUP BY w.shape ORDER BY c DESC LIMIT 20
    """).fetchall():
        print(f"  {c:>10,}  {shape}")


def cmd_moods(con):
    if not os.path.exists(JOINED):
        print("no joined feature file; run join-features first")
        return
    con.execute(f"""
      CREATE OR REPLACE TABLE feat AS
      SELECT track_id, valence::DOUBLE v, energy::DOUBLE e
      FROM read_csv('{JOINED}', header=true)
    """)
    # One row per (library row, song) so a song cannot vote twice for the same progression.
    con.execute("""
      CREATE OR REPLACE TABLE scored AS
      SELECT DISTINCT l.name, w.id, f.v, f.e
      FROM win w JOIN lib l ON l.shape = w.shape JOIN feat f ON f.track_id = w.sid
    """)
    joined = con.execute("SELECT count(DISTINCT id) FROM scored").fetchone()[0]
    print(f"\nsongs joined to features: {joined:,}")

    print("\nCONTROL: does minor read sadder than major?")
    for mm, v, n in con.execute("""
      SELECT l.minor_mode, avg(s.v), count(*) FROM scored s JOIN lib l USING (name)
      GROUP BY l.minor_mode ORDER BY l.minor_mode
    """).fetchall():
        print(f"  {'minor' if mm else 'major'}-mode rows: valence {v:.3f}  (n={n:,})")
    gap = con.execute("""
      SELECT avg(CASE WHEN NOT l.minor_mode THEN s.v END)
           - avg(CASE WHEN l.minor_mode THEN s.v END)
      FROM scored s JOIN lib l USING (name)
    """).fetchone()[0]
    print(f"  gap: {gap:+.3f}" + ("   *** CONTROL FAILED ***" if gap < 0.01 else ""))

    print("\nMEAN VALENCE PER MOOD")
    for mood, v, e, n in con.execute("""
      SELECT m.mood, avg(s.v) v, avg(s.e) e, count(*) n
      FROM scored s JOIN lib_mood m USING (name)
      GROUP BY m.mood HAVING count(*) >= 200 ORDER BY v DESC
    """).fetchall():
        print(f"  {v:.3f}  energy {e:.3f}  n={n:>9,}  {mood}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["join-features", "rank", "moods", "all"])
    ap.add_argument("--limit", type=int, default=None,
                    help="songs to read; omit for the whole corpus")
    a = ap.parse_args()

    con = connect()
    if a.command == "join-features":
        cmd_join_features(con)
        return 0

    lengths = register_library(con)
    build_songs(con, a.limit)
    build_windows(con, lengths)
    if a.command in ("rank", "all"):
        cmd_rank(con, lengths)
    if a.command in ("moods", "all"):
        cmd_moods(con)
    return 0


if __name__ == "__main__":
    sys.exit(main())
