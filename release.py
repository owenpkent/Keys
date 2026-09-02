#!/usr/bin/env python3
"""Build the signed, branded installer. Double-click this file in Explorer.

This exists because signing cannot be automated from here. The EV certificate
lives on the SafeNet eToken, and once its cached PIN expires the driver puts up
a **GUI prompt**. A build launched by an agent runs non-interactively, that
prompt is never shown, and signtool returns ERROR_CANCELLED (0x800704c7)
immediately - which reads exactly like a cancelled PIN and is not one. Run from
your own session the prompt appears and signing works.

    Double-click release.py          build + sign + installer
    py release.py --no-sign          unsigned, for checking layout and branding

It wraps build.ps1 rather than repeating it, the run.ps1 / run.py rule: one copy
of the logic. Afterwards it verifies what came out, because "signature valid" on
its own has already been misleading once here - a validly signed installer from
an earlier build sat in release/ looking publishable while missing the branding
entirely.

Windows only, like the product. Standard library only.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent

_ANSI = os.name != "nt" or os.environ.get("WT_SESSION") or os.environ.get("ANSICON")
if os.name == "nt":
    try:  # Windows 10+ terminals understand ANSI once this is asked for.
        import ctypes

        ctypes.windll.kernel32.SetConsoleMode(
            ctypes.windll.kernel32.GetStdHandle(-11), 7
        )
        _ANSI = True
    except Exception:  # noqa: BLE001 - colour is a nicety, never a reason to fail
        pass

GREEN = "\033[92m" if _ANSI else ""
YELLOW = "\033[93m" if _ANSI else ""
RED = "\033[91m" if _ANSI else ""
CYAN = "\033[96m" if _ANSI else ""
DIM = "\033[2m" if _ANSI else ""
RESET = "\033[0m" if _ANSI else ""


def console_is_ours() -> bool:
    """True when double-clicked from Explorer, so the window dies with us."""
    if os.name != "nt":
        return False
    try:
        import ctypes

        pid = ctypes.c_uint()
        count = ctypes.windll.kernel32.GetConsoleProcessList(ctypes.byref(pid), 1)
        return count <= 1
    except Exception:  # noqa: BLE001
        return False


def hold_window_open() -> None:
    if not console_is_ours():
        return
    print()
    print(f"{YELLOW}Read the message above, then close this window (or press Enter).{RESET}")
    try:
        input()
    except (EOFError, KeyboardInterrupt):
        pass


def project_version() -> str:
    """Ask build.ps1 for the version rather than keeping a second copy of the
    project(Keys VERSION ...) regex - build.ps1 -PrintVersion reads CMakeLists.txt
    and exits immediately, before touching any build state."""
    out = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
         "-File", str(ROOT / "build.ps1"), "-PrintVersion"],
        capture_output=True,
        text=True,
    )
    version = out.stdout.strip()
    if out.returncode != 0 or not version:
        raise RuntimeError(
            f"build.ps1 -PrintVersion failed (exit {out.returncode}): {out.stderr.strip()}"
        )
    return version


def powershell(script: str) -> str:
    out = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
        capture_output=True,
        text=True,
    )
    return out.stdout.strip()


def thumbprint() -> str:
    """installer/signing-thumbprint.txt - the one copy of the OK Studio EV cert's
    thumbprint; build.ps1 reads the same file rather than each script pinning its
    own. A function rather than a module-level constant so a missing file raises
    inside main()'s own try/except (see __main__ below) instead of before it -
    the double-clicked-window case this file exists to keep readable."""
    return (ROOT / "installer" / "signing-thumbprint.txt").read_text(encoding="utf-8").strip()


def token_present() -> bool:
    return powershell(
        f"[bool](Get-ChildItem Cert:\\CurrentUser\\My,Cert:\\LocalMachine\\My "
        f"-EA SilentlyContinue | ? {{ $_.Thumbprint -eq '{thumbprint()}' -and $_.HasPrivateKey }})"
    ).lower().startswith("true")


def verify(exe: Path, version: str, signed: bool) -> bool:
    """Check the four things that have actually gone wrong here at least once."""
    ok = True

    if not exe.exists():
        print(f"{RED}  MISSING  {exe.name} was never produced{RESET}")
        return False

    stale = powershell(
        f"((Get-Item '{exe}').LastWriteTime -lt (Get-Date).AddMinutes(-10))"
    ).lower().startswith("true")
    if stale:
        print(f"{RED}  STALE    {exe.name} predates this run - the build did not reach Inno{RESET}")
        ok = False
    else:
        print(f"{GREEN}  fresh    {exe.name}{RESET}")

    if signed:
        status = powershell(f"(Get-AuthenticodeSignature '{exe}').Status")
        if status == "Valid":
            print(f"{GREEN}  signed   {status}{RESET}")
        else:
            print(f"{RED}  UNSIGNED signature status: {status}{RESET}")
            ok = False

    vst = (
        ROOT / "build" / "Keys_artefacts" / "Release" / "VST3" / "Keys.vst3"
        / "Contents" / "x86_64-win" / "Keys.vst3"
    )
    reported = powershell(f"(Get-Item '{vst}').VersionInfo.FileVersion")
    # Compared component by component, not as a string. Windows carries a version resource as
    # four numbers and JUCE writes three, so whether the fourth comes back as a trailing ".0"
    # is the tool's choice rather than ours - and a gate that fails on a *correct* build is one
    # that gets ignored, which is worse than not having it. Trailing zeros are the only slack
    # allowed: 0.2.1.0 matches 0.2.1, and 0.2.10 does not.
    def parts(v: str) -> list:
        out = [int(n) for n in re.findall(r"\d+", v)]
        while out and out[-1] == 0:
            out.pop()
        return out

    if reported and parts(reported) == parts(version):
        print(f"{GREEN}  version  {reported}{RESET}")
    else:
        # The JUCE resources.rc trap, see docs/RELEASE.md step 1.
        print(f"{RED}  VERSION  binary says {reported}, CMakeLists says {version}{RESET}")
        print(f"{DIM}           delete build/*_artefacts/JuceLibraryCode/*_resources.rc and rebuild{RESET}")
        ok = False

    for art in (ROOT / "assets" / "Keys.ico",
                ROOT / "installer" / "wizard_large.bmp",
                ROOT / "installer" / "wizard_small.bmp"):
        if not art.exists():
            print(f"{RED}  BRANDING {art.name} missing - run installer/generate_brand.py{RESET}")
            ok = False

    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the signed Keys installer.")
    parser.add_argument("--no-sign", action="store_true",
                        help="skip signing (layout and branding checks only)")
    args = parser.parse_args()

    version = project_version()
    print(f"{CYAN}Keys {version} installer{RESET}")
    print()

    if not args.no_sign:
        if not token_present():
            print(f"{RED}The EV token is not available.{RESET}")
            print("Plug in the eToken, wait for the SafeNet client to pick it up, and try again.")
            print(f"{DIM}Or run:  py release.py --no-sign{RESET}")
            return 1
        print(f"{YELLOW}The token will ask for its PIN part way through.{RESET}")
        print(f"{YELLOW}The dialog can open BEHIND this window - if nothing seems to be{RESET}")
        print(f"{YELLOW}happening, check the taskbar.{RESET}")
        print()

    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", str(ROOT / "build.ps1"), "-Installer"]
    if args.no_sign:
        cmd.append("-NoSign")

    code = subprocess.run(cmd).returncode
    if code != 0:
        print()
        print(f"{RED}Build failed (exit {code}).{RESET}")
        print(f"{DIM}A signing failure of 0x800704c7 is the PIN prompt going unanswered.{RESET}")
        return code

    print()
    print(f"{CYAN}Checking what came out:{RESET}")
    exe = ROOT / "release" / f"KeysSetup-{version}.exe"
    if not verify(exe, version, signed=not args.no_sign):
        print()
        print(f"{RED}The installer was built but did not pass its checks. Do not publish it.{RESET}")
        return 1

    print()
    print(f"{GREEN}Ready: release\\KeysSetup-{version}.exe{RESET}")
    print(f"{DIM}Next: docs/RELEASE.md step 5 (install it, load it in Live), then tag and publish.{RESET}")
    return 0


if __name__ == "__main__":
    try:
        rc = main()
    except KeyboardInterrupt:
        print(f"\n{YELLOW}Cancelled.{RESET}")
        sys.exit(130)
    except Exception:  # noqa: BLE001 - a double-clicked window must show the traceback
        import traceback

        traceback.print_exc()
        hold_window_open()
        sys.exit(1)
    if rc != 0:
        hold_window_open()
    else:
        hold_window_open()
    sys.exit(rc)
