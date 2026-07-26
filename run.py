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
import time
from ctypes import wintypes

ROOT = os.path.dirname(os.path.abspath(__file__))

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


def console_is_ours() -> bool:
    """True when this console exists only for us, i.e. the script was double-clicked.

    Matters because that console vanishes the instant the script returns, taking any
    error message with it. Run from a terminal the count is at least two (the shell and
    us) and we must not pause. GetConsoleProcessList is the only reliable way to tell.
    """
    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        buf = (wintypes.DWORD * 8)()
        count = kernel32.GetConsoleProcessList(buf, len(buf))
        return count == 1
    except OSError:
        return False


def hold_window_open() -> None:
    """Keep a double-clicked console up so the failure above can actually be read."""
    if not console_is_ours():
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
        try:
            subprocess.run([cand, "--version"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True)
        except OSError as exc:
            blocked.append((cand, exc))
            continue
        except subprocess.CalledProcessError:
            continue
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
        wait_for_exit(exe_name, 5.0)


# --------------------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="Build and launch a Keys standalone.")
    parser.add_argument("--keys", action="store_true",
                        help="run plain Keys (MIDI only, silent) instead of Keys Host")
    parser.add_argument("--no-build", action="store_true",
                        help="skip the build and just relaunch what is already there")
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

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

    # Smart App Control is enforced on this machine and dev builds are unsigned, so it
    # blocks the first launch of a freshly linked exe while its reputation check runs,
    # then lets the same file through once that finishes (CodeIntegrity event 3118).
    # Signing every iteration would need the EV eToken and a PIN, which defeats a
    # 5-second loop, so absorb the transient here rather than weakening the machine.
    #
    # The wait used to be 5 tries over 3.5s, which was tuned when the check cleared almost
    # at once. It does not always: measured at up to ~3 minutes on this machine. Giving up
    # after three and a half seconds turned a slow launch into "it's broken", and the
    # advice it printed was to run the same command again. Wait properly instead, and say
    # what is happening so the pause is legible rather than a hang.
    launch_timeout = 240.0
    deadline = time.monotonic() + launch_timeout
    launched = False
    announced = False
    while True:
        try:
            # Detached, so the app outlives this console instead of dying with it.
            subprocess.Popen([exe], cwd=ROOT, close_fds=True,
                             creationflags=subprocess.DETACHED_PROCESS
                             | subprocess.CREATE_NEW_PROCESS_GROUP)
            launched = True
            break
        except OSError:
            if time.monotonic() >= deadline:
                break
            if not announced:
                announced = True
                print(f"{GREY}Smart App Control is checking the new build; waiting for it "
                      f"(up to {launch_timeout:.0f}s)...{RESET}")
            time.sleep(2.0)
    if launched and announced:
        print(f"{GREEN}Cleared.{RESET}")

    if not launched:
        print(f"{YELLOW}Could not launch {exe_name} - Smart App Control blocked it for "
              f"{launch_timeout:.0f}s.{RESET}")
        print(f"{YELLOW}  Retry with: py run.py --no-build{RESET}")
        print(f"{YELLOW}  Or build a signed one: .\\build.ps1 -Standalone -Sign "
              f"(needs the eToken plugged in).{RESET}")
        return 1

    print(f"{GREEN}Launched {exe_name} ({args.config}){RESET}")
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
    except Exception:  # noqa: BLE001 - a double-clicked window must show the traceback
        import traceback
        traceback.print_exc()
        hold_window_open()
        sys.exit(1)
    if code != 0:
        hold_window_open()
    sys.exit(code)
