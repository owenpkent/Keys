"""Build a small track_id -> (valence, energy) table for ONLY the ids Chordonomicon uses.

The big dump is 56M rows and 4 GB. Loading it whole is not on; but Chordonomicon only references a
few hundred thousand Spotify ids, so the join is: collect those ids first, then stream the parquet
in row groups and keep the handful of rows that matter. The result is a few MB.
"""
import csv
import sys

import pyarrow.parquet as pq

CHORDS = "datasets/chordonomicon_v2.csv"
BIG = "datasets/spotify_features_huge.parquet"
OUT = "datasets/valence_for_chordonomicon.csv"

pf = pq.ParquetFile(BIG)
cols = pf.schema_arrow.names
print("has valence:", "valence" in cols, "| has energy:", "energy" in cols)
if "valence" not in cols or "energy" not in cols:
    print("all columns:", cols)
    sys.exit(1)

print("collecting the ids Chordonomicon actually uses ...")
csv.field_size_limit(1 << 30)
wanted = set()
with open(CHORDS, encoding="utf-8", errors="replace", newline="") as f:
    for row in csv.DictReader(f):
        t = (row.get("spotify_song_id") or "").strip()
        if t:
            wanted.add(t)
print(f"  {len(wanted):,} unique ids")

print("streaming the parquet ...")
found = {}
groups = pf.num_row_groups
for i in range(groups):
    tbl = pf.read_row_group(i, columns=["track_id", "valence", "energy"])
    ids = tbl.column("track_id").to_pylist()
    vs = tbl.column("valence").to_pylist()
    es = tbl.column("energy").to_pylist()
    for t, v, e in zip(ids, vs, es):
        if t in wanted and t not in found and v is not None and e is not None:
            found[t] = (v, e)
    if (i + 1) % 20 == 0 or i == groups - 1:
        print(f"  group {i+1}/{groups}  matched {len(found):,}", flush=True)

with open(OUT, "w", encoding="utf-8", newline="") as f:
    w = csv.writer(f)
    w.writerow(["track_id", "valence", "energy"])
    for t, (v, e) in found.items():
        w.writerow([t, v, e])
print(f"wrote {OUT}: {len(found):,} rows "
      f"({100.0 * len(found) / max(1, len(wanted)):.0f}% of Chordonomicon's ids)")
