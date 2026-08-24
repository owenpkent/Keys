#!/usr/bin/env python3
"""The fast prototyping loop: build ONE standalone target and run it.

Double-click this file (or right-click -> Open) to build and launch Keys Host. No
VST3, no signing, no installer, no DAW rescan. Keys Host standalone runs a real
instrument VST3 in-process, so clicking a key makes sound with Ableton out of the
picture entirely. Reach for build.ps1 only when a change needs a real Live load test
(bus layout, plugin classification, installer, updater).

    py run.py                # build + launch Keys Host standalone (makes sound)
    py run.py --keys         # plain Keys instead (MIDI only, silent)
    py run.py --no-build     # just relaunch what is already built

This is the implementation; run.ps1 is a shim that forwards to it, so there is one
copy of the logic and the two entry points cannot drift apart.

Windows only, like the product. Standard library only, so it runs on a bare Python.
"""

import argparse
import ctypes
import glob
import os
import shutil
import subprocess
import sys
import sysconfig
import threading
import time
from ctypes import wintypes

ROOT = os.path.dirname(os.path.abspath(__file__))

# How long to let Smart App Control hold a freshly linked build before giving up on it.
# Twenty minutes because that is what was measured here (19m44s on 2026-07-27), not
# because anyone should have to wait that long - docs/BUILD.md has the ways out.
LAUNCH_TIMEOUT_S = 1200.0

# Our own messages interleave with cmake's, which writes straight to the console handle.
# Without this, Python's block buffering lands "Closing running Keys..." *after* the build
# output it happened before, which reads like the script did things out of order.
try:
    sys.stdout.reconfigure(line_buffering=True)
except (AttributeError, OSError):
    pass

# --------------------------------------------------------------------------------------
# Console
# --------------------------------------------------------------------------------------

def _enable_ansi() -> bool:
    """Turn on VT sequences so the colours below mean something. False if we can't."""
    if not sys.stdout.isatty():
        return False
    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
        mode = wintypes.DWORD()
        if not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            return False
        return bool(kernel32.SetConsoleMode(handle, mode.value | 0x0004))
    except OSError:
        return False


_ANSI = _enable_ansi()
GREY = "\033[90m" if _ANSI else ""
GREEN = "\033[92m" if _ANSI else ""
YELLOW = "\033[93m" if _ANSI else ""
RESET = "\033[0m" if _ANSI else ""


# Anything here means a human's shell is sharing this console, so it will still be on
# screen after we return and there is nothing to hold open.
_SHELLS = frozenset({"cmd.exe", "powershell.exe", "pwsh.exe", "bash.exe", "sh.exe",
                     "zsh.exe", "wt.exe", "windowsterminal.exe", "conemu64.exe",
                     "conemuc.exe", "code.exe"})


def console_is_ours() -> bool:
    """True when this console exists only for us, i.e. the script was double-clicked.

    Matters because that console vanishes the instant the script returns, taking any
    error message with it. Run from a terminal we must not pause.

    This used to ask GetConsoleProcessList for a count of exactly one, which is never
    true: the .py association is py.exe, and py.exe spawns python.exe and then *stays in
    the same console* to relay the exit code, so even a double-click counts two. Because
    the count never matched, hold_window_open() never held, and every failure this script
    can report - no cmake, a failed build, a blocked launch, a traceback - flashed past
    unread. That is the precise thing the script exists to prevent, so identify the
    processes rather than counting them.

    (Uses the module-level kernel32 handle and constants defined further down; everything
    here runs long after import.)
    """
    try:
        buf = (wintypes.DWORD * 64)()
        count = kernel32.GetConsoleProcessList(buf, len(buf))
        if count == 0:
            return False
        for i in range(min(count, len(buf))):
            handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, buf[i])
            if not handle:
                continue  # a process we may not open is not a shell of ours
            try:
                name = ctypes.create_unicode_buffer(32768)
                length = wintypes.DWORD(len(name))
                if kernel32.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(length)):
                    if os.path.basename(name.value).lower() in _SHELLS:
                        return False
            finally:
                kernel32.CloseHandle(handle)
        return True
    except OSError:
        return False


_FORCE_HOLD = False  # --hold: for a console that is transient but has a shell in it


def hold_window_open() -> None:
    """Keep a double-clicked console up so the failure above can actually be read."""
    if not _FORCE_HOLD and not console_is_ours():
        return
    print()
    print(f"{YELLOW}Read the error above, then close this window (or press Enter).{RESET}")
    try:
        input()
    except (EOFError, KeyboardInterrupt):
        pass


# --------------------------------------------------------------------------------------
# Finding a cmake that will actually start
# --------------------------------------------------------------------------------------

def _cmake_candidates():
    """Every cmake worth trying, best first.

    The one on PATH is *not* best. This machine installs cmake through pip, and pip's
    entry point is a small unsigned launcher at Scripts\\cmake.exe that re-execs the real
    binary. Smart App Control (enforced here) blocks unsigned executables it does not
    recognise, so CreateProcess on the launcher fails outright — while the signed binary
    it wraps, in site-packages/cmake/data/bin, starts perfectly. Prefer anything signed
    and leave the launcher as the last resort.
    """
    override = os.environ.get("CMAKE")
    if override:
        yield override

    # Visual Studio ships a Microsoft-signed cmake. Not present on every install, and the
    # directory is the VS *major version* (18 here), never the marketing year.
    for base in (os.environ.get("ProgramFiles", r"C:\Program Files"),
                 os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")):
        pattern = os.path.join(base, "Microsoft Visual Studio", "*", "*", "Common7", "IDE",
                               "CommonExtensions", "Microsoft", "CMake", "CMake", "bin", "cmake.exe")
        yield from sorted(glob.glob(pattern), reverse=True)

    # pip's real payload, behind the launcher.
    for key in ("purelib", "platlib"):
        path = sysconfig.get_paths().get(key)
        if path:
            yield os.path.join(path, "cmake", "data", "bin", "cmake.exe")

    # A normal MSI install.
    for base in (os.environ.get("ProgramFiles", r"C:\Program Files"),
                 os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")):
        yield os.path.join(base, "CMake", "bin", "cmake.exe")

    found = shutil.which("cmake")
    if found:
        yield found


_cmake_cached = None


def find_cmake():
    """The first cmake that actually launches, or None. Probed once per run."""
    global _cmake_cached
    if _cmake_cached is not None:
        return _cmake_cached

    on_path = shutil.which("cmake")
    seen = set()
    blocked = []
    for cand in _cmake_candidates():
        cand = os.path.normpath(cand)
        key = cand.lower()
        if key in seen or not os.path.exists(cand):
            continue
        seen.add(key)
        probe_started = time.monotonic()
        try:
            # A timeout, because a candidate can start and then wedge - a stale UNC path
            # under ProgramFiles, a disconnected mapped drive, an AV scanning a first-run
            # binary. Without it the probe hangs here, before a single line of output, and
            # looks exactly like the launch hang further down. (It does not cover
            # CreateProcess itself blocking, which no timeout can; the candidates are
            # ordered signed-first precisely so that stays hypothetical.)
            subprocess.run([cand, "--version"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True, timeout=20)
        except OSError as exc:
            blocked.append((cand, exc))
            continue
        except subprocess.TimeoutExpired:
            blocked.append((cand, "started but did not answer --version within 20s"))
            continue
        except subprocess.CalledProcessError:
            continue
        # Silent when it is instant, which is every normal run; if a probe was slow enough
        # to notice, say which one, so the pause has a name.
        if time.monotonic() - probe_started > 2.0:
            print(f"{GREY}cmake took {time.monotonic() - probe_started:.0f}s to answer ({cand}){RESET}")
        # Say so whenever we sidestep PATH. Comparing against `blocked` instead would never
        # fire: the signed payload is tried first, so the blocked launcher on PATH is not
        # even probed, and the notice that explains the switch would be dead code.
        if on_path and os.path.normcase(os.path.normpath(on_path)) != os.path.normcase(cand):
            print(f"{GREY}cmake on PATH ({on_path}) is not the one being used; using {cand}{RESET}")
        _cmake_cached = cand
        return cand

    _report_no_cmake(blocked)
    return None


def _report_no_cmake(blocked) -> None:
    print(f"{YELLOW}No usable cmake found.{RESET}")
    for path, exc in blocked:
        print(f"{YELLOW}  blocked: {path}{RESET}")
        print(f"{YELLOW}           {exc}{RESET}")
    if blocked:
        print(f"{YELLOW}  Smart App Control blocks unsigned executables. Either install CMake "
              f"from cmake.org (signed), or set CMAKE to a cmake.exe that runs.{RESET}")
    else:
        print(f"{YELLOW}  Install CMake, or set the CMAKE environment variable to its path.{RESET}")


def run_cmake(args) -> int:
    """Run cmake with `args`, turning a failure to even start into a readable message.

    subprocess raises OSError out of CreateProcess when the exe is blocked, and the raw
    traceback that produces is exactly the thing this script exists to avoid: Owen
    double-clicks it and has to read the console.
    """
    exe = find_cmake()
    if exe is None:
        return 1
    try:
        return subprocess.run([exe] + list(args)).returncode
    except OSError as exc:
        print(f"{YELLOW}Could not run cmake ({exe}):{RESET}")
        print(f"{YELLOW}  {exc}{RESET}")
        print(f"{YELLOW}  Set the CMAKE environment variable to a cmake.exe that runs.{RESET}")
        return 1


# --------------------------------------------------------------------------------------
# Win32: find the running app and ask it to close politely
# --------------------------------------------------------------------------------------

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PROCESS_TERMINATE = 0x0001
WM_CLOSE = 0x0010

kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
kernel32.QueryFullProcessImageNameW.argtypes = [
    wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)
]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def running_pids(exe_name: str) -> list:
    """PIDs of every running process whose image is exactly `exe_name`."""
    size = 1024
    while True:
        arr = (wintypes.DWORD * size)()
        needed = wintypes.DWORD()
        if not psapi.EnumProcesses(ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(needed)):
            return []
        if needed.value < ctypes.sizeof(arr):
            break
        size *= 2  # the list filled the buffer exactly; it may have been truncated

    found = []
    for i in range(needed.value // ctypes.sizeof(wintypes.DWORD)):
        pid = arr[i]
        if pid == 0:
            continue
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            continue  # system processes we may not open; never ours
        try:
            buf = ctypes.create_unicode_buffer(32768)
            length = wintypes.DWORD(len(buf))
            if kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(length)):
                if os.path.basename(buf.value).lower() == exe_name.lower():
                    found.append(pid)
        finally:
            kernel32.CloseHandle(handle)
    return found


def post_close(pids: list, title: str) -> int:
    """WM_CLOSE every visible top-level window of `pids` titled exactly `title`.

    Keys Host owns two top-level windows (the keyboard and the hosted instrument's GUI),
    and Windows picks the "main" one between them heuristically - often landing on the
    instrument window, whose close button is wired to just hide it. A close aimed there
    is swallowed, we fall through to a force-kill, JUCE skips its settings write, and the
    loaded synth is gone next launch. So target the window actually titled after the
    product; closing that one quits the app.

    Returns how many windows were asked to close, so the caller can fall back when the
    app has no window by that name.
    """
    closed = 0
    wanted = set(pids)

    def callback(hwnd, _lparam):
        nonlocal closed
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in wanted and user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, buf, 256)
            if buf.value == title:
                user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
                closed += 1
        return True

    proc = WNDENUMPROC(callback)  # must outlive the EnumWindows call
    user32.EnumWindows(proc, 0)
    return closed


def post_close_any_window(pids: list) -> int:
    """Fallback for a process with no window by the expected name: close any it has."""
    closed = 0
    wanted = set(pids)

    def callback(hwnd, _lparam):
        nonlocal closed
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in wanted and user32.IsWindowVisible(hwnd):
            user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
            closed += 1
        return True

    proc = WNDENUMPROC(callback)
    user32.EnumWindows(proc, 0)
    return closed


def wait_for_exit(exe_name: str, timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if not running_pids(exe_name):
            return True
        time.sleep(0.15)
    return not running_pids(exe_name)


def launch_detached(exe: str, timeout_s: float) -> tuple:
    """Start `exe` detached, absorbing whatever Smart App Control does to a new build.

    SAC gets in the way two different ways and this has to survive both. With a verdict
    already cached it *fails* the call outright (OSError, error 4551). While the reputation
    lookup is still in flight it *blocks* the call instead, for as long as the lookup takes.
    Only the first form was ever handled, and the second is the one that reads as a hang:
    the script sat inside CreateProcess with nothing on screen - the "waiting" message lived
    in the OSError branch and so was never reached - until Owen pressed Ctrl+C.

    So the launch runs on a worker thread and this one does the talking: a live counter, a
    deadline that is actually enforced, and a Ctrl+C that is heard rather than swallowed by
    a blocking syscall. Measured here on 2026-07-27, one unsigned build stayed blocked for
    19m44s across 175 CodeIntegrity 3118 events, so the wait is long on purpose and says so
    out loud rather than looking broken.
    """
    outcome = {}

    def attempt() -> None:
        deadline = time.monotonic() + timeout_s
        while True:
            try:
                # Detached, so the app outlives this console instead of dying with it.
                subprocess.Popen([exe], cwd=ROOT, close_fds=True,
                                 creationflags=subprocess.DETACHED_PROCESS
                                 | subprocess.CREATE_NEW_PROCESS_GROUP)
                outcome["ok"] = True
                return
            except OSError as exc:
                outcome["error"] = exc
                if time.monotonic() >= deadline:
                    outcome["ok"] = False
                    return
                time.sleep(2.0)

    worker = threading.Thread(target=attempt, name="launch", daemon=True)
    worker.start()

    started = time.monotonic()
    announced = False
    while True:
        worker.join(0.25)  # interruptible, unlike the CreateProcess it is waiting on
        if not worker.is_alive():
            break
        waited = time.monotonic() - started
        if waited >= timeout_s:
            break
        if waited < 2.0:
            continue  # a normal launch is instant and should print nothing at all
        if not announced:
            announced = True
            print(f"{GREY}Smart App Control is vetting this build before it will run it. "
                  f"That is normal for an unsigned dev build.{RESET}")
            print(f"{GREY}  It has been measured at twenty minutes on this machine. "
                  f"Ctrl+C to stop waiting - the build itself is already done.{RESET}")
        # The counter overwrites itself, so it is a console-only trick: redirected to a file
        # it would be one enormous line. The announcement above is enough there.
        if sys.stdout.isatty():
            print(f"\r{GREY}  waiting {waited:.0f}s of {timeout_s:.0f}s{RESET}", end="", flush=True)

    if announced and sys.stdout.isatty():
        print()  # close off the counter line

    launched = bool(outcome.get("ok"))
    if launched and announced:
        print(f"{GREEN}Cleared.{RESET}")
    # Hand back the last error too. Blaming Smart App Control for whatever went wrong was a
    # guess the old code made without looking: an exe still held by the linker, a missing
    # dependent DLL and a file deleted under us all raise here, and all used to be reported
    # as a reputation check.
    return launched, outcome.get("error")


def close_running(exe_name: str, window_title: str) -> None:
    pids = running_pids(exe_name)
    if not pids:
        return

    print(f"{GREY}Closing running {window_title}...{RESET}")

    # Two attempts: an app still restoring a big hosted instrument is not pumping
    # messages yet and will sit on the first WM_CLOSE. Once settled it exits in well
    # under a second, so this costs nothing in the normal case.
    for _ in range(2):
        pids = running_pids(exe_name)
        if not pids:
            return
        if post_close(pids, window_title) == 0:
            post_close_any_window(pids)
        if wait_for_exit(exe_name, 6.0):
            return

    if running_pids(exe_name):
        print(f"{YELLOW}  ...it ignored the close, forcing it (settings may not persist).{RESET}")
        for pid in running_pids(exe_name):
            handle = kernel32.OpenProcess(PROCESS_TERMINATE, False, pid)
            if handle:
                try:
                    kernel32.TerminateProcess(handle, 1)
                finally:
                    kernel32.CloseHandle(handle)
        # Say so when even that fails, rather than walking into the build and letting the
        # linker report it as LNK1104 several thousand lines of output later. A process we
        # cannot terminate is usually elevated, or mid-crash-dump, or held by a driver.
        if not wait_for_exit(exe_name, 5.0):
            stuck = ", ".join(str(pid) for pid in running_pids(exe_name))
            print(f"{YELLOW}  ...and it is still running (pid {stuck}). The build is about "
                  f"to fail with LNK1104 because the linker cannot overwrite a running "
                  f"exe.{RESET}")
            print(f"{YELLOW}  Close {window_title} by hand, then run this again.{RESET}")


# --------------------------------------------------------------------------------------

def build_stamp() -> str:
    """The commit the working tree is on, for the launch line.

    Not what the *binary* was built from - nothing records that, and a stamp baked into the
    exe would mean a relink on every commit. It is the honest thing to print beside a build
    that just happened, and `stale_sources` below is what covers the case where one did not.
    """
    def git(*args) -> str:
        try:
            out = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                                 timeout=5)
            return out.stdout.strip() if out.returncode == 0 else ""
        except (OSError, subprocess.SubprocessError):
            return ""  # no git, no repo, no matter: the stamp is a nicety

    def git_dirty() -> str:
        """The stamp's dirty marker: "" clean, " +changes" dirty, " +?" if git could not say.

        **Three states, because two of them are not the same.** A git that cannot answer must
        not read as a clean tree, and it has now done exactly that twice by two different
        routes: first a stdout-only check, then `diff --quiet` read as `returncode == 1`, which
        quietly files exit 128 - no HEAD, not a work tree, a corrupt index - under "no
        differences". Whatever cannot be established has to be said out loud, since the whole
        job of this stamp is to answer "is the thing in front of me my change?".

        **`status --porcelain` rather than `diff --quiet HEAD`, and the untracked files are the
        reason.** `diff` compares tracked paths against HEAD and cannot see a new file at all,
        and a source file added but not yet staged is the ordinary state halfway through a
        feature - precisely the change most worth warning about. One process either way, and
        both stat the whole tree, so the saving that bought the blind spot was a subprocess.
        """
        try:
            out = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                                 capture_output=True, text=True, timeout=5)
        except (OSError, subprocess.SubprocessError):
            return " +?"
        if out.returncode != 0:
            return " +?"  # git is there and still could not answer: say so, do not guess
        return " +changes" if out.stdout.strip() else ""

    # One call for the sha and the subject, one for the branch, one for the dirty flag. It was
    # four, on a loop this project measures in seconds and advertises at about one for a no-op:
    # process creation is not free on Windows and none of this is worth a frame of it. The
    # `--format=%h%n%s` is where the saving actually came from; the dirty check stayed on
    # `status --porcelain` because it is the only one of the two that sees an untracked file.
    head = git("log", "-1", "--format=%h%n%s")
    if not head:
        return ""
    sha, _, subject = head.partition("\n")
    branch = git("rev-parse", "--abbrev-ref", "HEAD") or "detached"
    # Unstaged, staged and untracked all count: any of them means the tree is not the commit
    # named beside it.
    dirty = git_dirty()
    stamp = f"{branch} @ {sha}{dirty}"
    return f'{stamp}\n  "{subject}"' if subject else stamp


def stale_sources(exe: str, host: bool) -> list:
    """Source files newer than the binary about to be launched, newest first.

    The whole reason this exists (2026-08-23): a day-old Keys Host was mistaken for a current
    one for most of an afternoon, and nothing on screen or in this script's output said
    otherwise. A stale binary is the normal state after `--no-build`, and it is indistinguishable
    from a fresh one by looking at it.

    **Only what this target compiles.** It walked `tests/` and all of `src/` whatever was being
    launched, and neither target builds the test suite while plain Keys does not build
    `src/host/` - so editing a test file and rebuilding relinked nothing, left the exe older
    than the file, and printed a warning that *rebuilding could not clear*. A warning you
    cannot act on is worse than none: it teaches you to ignore the one that matters.
    """
    try:
        built = os.path.getmtime(exe)
    except OSError:
        return []
    # KEYS_SOURCES in CMakeLists.txt, plus src/host for the Keys Host target alone. Headers
    # count: they are what the compiler reads, whatever the target lists.
    skip = set() if host else {os.path.join(ROOT, "src", "host")}
    newer = []
    for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, "src")):
        dirnames[:] = [d for d in dirnames if os.path.join(dirpath, d) not in skip]
        for name in filenames:
            if not name.endswith((".cpp", ".h", ".hpp")):
                continue
            path = os.path.join(dirpath, name)
            try:
                when = os.path.getmtime(path)
            except OSError:
                continue
            # Filtered here rather than after the sort: the common answer is "nothing is
            # stale", and collecting the whole tree first meant sorting every source file in
            # the repo on every launch to hand back an empty list.
            if when > built:
                newer.append((when, path))
    return [path for _, path in sorted(newer, reverse=True)]


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and launch a Keys standalone.")
    parser.add_argument("--keys", action="store_true",
                        help="run plain Keys (MIDI only, silent) instead of Keys Host")
    parser.add_argument("--no-build", action="store_true",
                        help="skip the build and just relaunch what is already there")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--hold", action="store_true",
                        help="pause on failure even when a shell shares the console "
                             "(for run.ps1 started from a console that will close)")
    args = parser.parse_args()

    global _FORCE_HOLD
    _FORCE_HOLD = args.hold

    os.chdir(ROOT)

    if args.keys:
        target, artefacts, exe_name = "Keys_Standalone", "Keys_artefacts", "Keys.exe"
    else:
        target, artefacts, exe_name = "KeysHost_Standalone", "KeysHost_artefacts", "Keys Host.exe"

    exe = os.path.join(ROOT, "build", artefacts, args.config, "Standalone", exe_name)
    window_title = exe_name[: -len(".exe")]

    close_running(exe_name, window_title)

    if not args.no_build:
        # Only configure on a cold build tree. The VS generator re-runs CMake by itself
        # when CMakeLists.txt changes, so an explicit configure every launch is dead time.
        if not os.path.exists(os.path.join(ROOT, "build", "CMakeCache.txt")):
            if run_cmake(["-B", "build", "-G", "Visual Studio 17 2022",
                          "-A", "x64", "-DKEYS_COPY_PLUGIN=OFF"]) != 0:
                return 1

        started = time.monotonic()
        if run_cmake(["--build", "build", "--config", args.config, "--target", target]) != 0:
            return 1
        print(f"{GREEN}Built {target} in {time.monotonic() - started:.1f}s{RESET}")

    if not os.path.exists(exe):
        print(f"{YELLOW}{exe_name} not found at {exe} - run without --no-build first.{RESET}")
        return 1

    # Smart App Control is enforced on this machine (VerifiedAndReputablePolicyState = 1)
    # and dev builds are unsigned, so it gates the first launch of every freshly linked exe
    # while its reputation check runs, then lets the same file through once that finishes
    # (CodeIntegrity event 3118). Signing every iteration would need the EV eToken and a
    # PIN, which defeats a 5-second loop, so absorb the wait here rather than weakening the
    # machine behind Owen's back. See docs/BUILD.md for the ways out of the wait itself.
    launched, why = launch_detached(exe, LAUNCH_TIMEOUT_S)
    if not launched:
        print(f"{YELLOW}Could not launch {exe_name} after "
              f"{LAUNCH_TIMEOUT_S / 60:.0f} minutes.{RESET}")
        if why is not None:
            # Say what Windows actually said. Not every failure here is Smart App Control:
            # an exe still held by the linker, or a missing dependent DLL, lands in exactly
            # the same place and used to be reported as a reputation check regardless.
            print(f"{YELLOW}  Windows said: {why}{RESET}")
        print(f"{YELLOW}  If that was Smart App Control it may still clear and open on its "
              f"own. To try again without rebuilding: py run.py --no-build{RESET}")
        print(f"{YELLOW}  To stop it happening at all, see \"Smart App Control\" in "
              f"docs/BUILD.md.{RESET}")
        return 1

    print(f"{GREEN}Launched {exe_name} ({args.config}){RESET}")

    # What you are actually looking at. The build time first, because that is the fact that
    # settles "is this my change?", then the commit, so a screenshot can be placed later.
    # Guarded, unlike the identical call inside stale_sources, and for a sharper reason: the
    # launch has already succeeded and said so by the time this runs, so an exe that has gone
    # unreadable in between (a quarantine, a concurrent build swapping it out) would raise here,
    # hit the top-level handler, and report a failure with the app open on screen.
    try:
        built_at = time.strftime("%H:%M", time.localtime(os.path.getmtime(exe)))
    except OSError:
        built_at = "?"
    stamp = build_stamp()
    print(f"{GREY}  built {built_at}" + (f", {stamp}" if stamp else "") + f"{RESET}")

    # And the warning that is the point of all this. Only reachable with --no-build: everything
    # scanned is compiled into this target, so a build that just ran relinked past all of it.
    stale = stale_sources(exe, host=not args.keys)
    if stale:
        newest = os.path.relpath(stale[0], ROOT)
        print(f"{YELLOW}  This binary is older than {len(stale)} source "
              f"file{'s' if len(stale) != 1 else ''} - it is NOT what you just changed.{RESET}")
        print(f"{YELLOW}  Newest: {newest}. Run it again without --no-build to rebuild.{RESET}")

    if not args.keys:
        print(f"{GREY}  Silent? Load a synth VST3 into the instrument slot - "
              f"Keys Host reloads it next launch.{RESET}")
    return 0


if __name__ == "__main__":
    if sys.platform != "win32":
        print("run.py is Windows-only, like the product.")
        sys.exit(1)
    try:
        code = main()
    except KeyboardInterrupt:
        # Not an error, and not a traceback: Ctrl+C is the documented way out of the Smart
        # App Control wait. `except Exception` below does not catch it (BaseException), so
        # this used to dump a stack trace and, when double-clicked, close on it.
        print(f"\n{YELLOW}Cancelled.{RESET}")
        sys.exit(130)
    except SystemExit as exc:
        # argparse's exit path, which is also where a file *dropped onto run.py in
        # Explorer* ends up: it becomes an unrecognised argument, argparse prints usage to
        # stderr and raises SystemExit(2). SystemExit is a BaseException too, so this used
        # to skip the hold and close on the usage message.
        code = exc.code if isinstance(exc.code, int) else 0
        if code:
            hold_window_open()
        sys.exit(code)
    except Exception:  # noqa: BLE001 - a double-clicked window must show the traceback
        import traceback
        traceback.print_exc()
        hold_window_open()
        sys.exit(1)
    if code != 0:
        hold_window_open()
    sys.exit(code)
