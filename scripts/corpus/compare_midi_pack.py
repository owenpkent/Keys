"""ldrolez/free-midi-chords (MIT) -> Keys' numeral spelling, and a comparison with ChordLibrary.h.

THE FINDING THIS EXISTS TO HANDLE. The pack and Keys use two different roman-numeral conventions
and they disagree on exactly the degrees that matter in a minor key.

  * Keys (and MarkovData.h before it) uses a fixed MAJOR-scale degree table for every mode, so
    natural minor's flat degrees must be spelled bIII / bVI / bVII. VII is always 11 semitones.
  * The pack spells a minor-key progression against the MINOR scale, so its VI, VII and III are
    already flat and it writes them unadorned.

So the pack's "i VII VI V" and Keys' "i bVII bVI V" are the SAME progression - the Andalusian
cadence - written two ways. Comparing the strings without translating first said they had nothing
in common, which is how a 19/140 overlap came out of two collections that are both mostly canon.

Get this wrong in the other direction - importing a pack row verbatim - and it parses cleanly and
plays the wrong chords. That is the failure mode MarkovData.h's header warns about at length.
"""
import os
import re
import sys
from collections import Counter

ROOT = r"E:\Ableton\free-midi-progressions-20231004"
LIB = "src/ChordLibrary.h"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)))

# In the pack's MINOR folder these numerals are already the flat degree.
MINOR_FLATTEN = {"III": "bIII", "VI": "bVI", "VII": "bVII", "II": "bII"}


def token_to_keys(tok: str, minor_ctx: bool) -> str:
    """One of the pack's tokens in Keys' spelling, or '' when it cannot be translated."""
    m = re.fullmatch(r"([b#]?)([IViv]+)(.*)", tok)
    if not m:
        return ""
    acc, num, suf = m.groups()

    # A trailing capital M on a plain numeral means "major triad"; Keys says that with case alone.
    # It must not eat the M of M7, and M-5 is diminished, so only a bare trailing M is dropped.
    # Captured before the suffix branch below rewrites the numeral. It used to be read after,
    # so a minor-quality token ("VIm") had already been lowercased to "vi" and `upper` came out
    # False - which meant the minor-context flattening could never fire for exactly the tokens
    # that most need it, and those rows landed in the "only in pack" bucket as Keys rows under
    # the wrong spelling. This file's own header is about that failure being silent: it parses
    # cleanly and names the wrong chords.
    upper = num.isupper()

    if suf == "M":
        suf = ""
    elif suf == "m":
        suf = ""
        num = num.lower()

    if minor_ctx and not acc and upper and num in MINOR_FLATTEN:
        acc, num = "b", MINOR_FLATTEN[num][1:]

    return f"{acc}{num}{suf}"


def progression_of(filename: str, minor_ctx: bool):
    body = filename.rsplit(".mid", 1)[0]
    if " - " not in body:
        return None
    toks = [token_to_keys(t, minor_ctx) for t in body.split(" - ", 1)[1].split()]
    return " ".join(toks) if all(toks) else None


def keys_rows(path):
    """Every (name, numerals) in the table. Tolerates the entry being wrapped over two lines."""
    src = open(path, encoding="utf-8", errors="replace").read()
    src = re.sub(r"\s+", " ", src)
    return dict((m.group(2), m.group(1))
                for m in re.finditer(r'\{ "([^"]+)", "([^"]+)", Function::', src))


def main():
    if not os.path.isdir(ROOT):
        print("pack not found:", ROOT)
        return 1

    seen = Counter()
    for folder, minor in (("Major", False), ("Minor", True)):
        d = os.path.join(ROOT, folder)
        if not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            if fn.lower().endswith(".mid"):
                p = progression_of(fn, minor)
                if p and len(p.split()) >= 2:
                    seen[p] += 1

    mine = keys_rows(LIB)
    theirs = set(seen)
    mine_set = set(mine)

    both = sorted(theirs & mine_set)
    only_theirs = sorted(theirs - mine_set)

    print(f"pack: {len(theirs)} unique progressions (MIT, ldrolez/free-midi-chords)")
    print(f"Keys: {len(mine_set)} rows in ChordLibrary.h")
    print()
    print(f"IN BOTH ........ {len(both)}   <- independent corroboration of Keys' rows")
    print(f"only in pack ... {len(only_theirs)}")
    print(f"only in Keys ... {len(mine_set - theirs)}")
    print()
    print("--- corroborated (the pack independently has these) ---")
    for p in both:
        print(f"   {p:<34} {mine[p]}")

    with open(os.path.join(OUT, "pack_only.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(only_theirs))
    print()
    print("pack-only written to pack_only.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
