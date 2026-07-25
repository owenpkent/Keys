param(
    [switch]$Keys,      # run plain Keys (MIDI only, silent) instead of Keys Host
    [switch]$NoBuild,   # skip the build and just relaunch what is already there
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# The fast prototyping loop: build ONE standalone target and run it. No VST3, no
# signing, no installer, no DAW rescan. Keys Host standalone runs a real instrument
# VST3 in-process, so clicking a key makes sound with Ableton out of the picture
# entirely. Reach for build.ps1 only when a change needs a real Live load test
# (bus layout, plugin classification, installer, updater).

if ($Keys) {
    $target    = "Keys_Standalone"
    $artefacts = "Keys_artefacts"
    $exeName   = "Keys.exe"
    $procName  = "Keys"
} else {
    $target    = "KeysHost_Standalone"
    $artefacts = "KeysHost_artefacts"
    $exeName   = "Keys Host.exe"
    $procName  = "Keys Host"
}

$exe = "$PSScriptRoot\build\$artefacts\$Config\Standalone\$exeName"

# Keys Host owns two top-level windows (the keyboard and the hosted instrument's GUI),
# and Windows picks MainWindowHandle between them heuristically - often landing on the
# instrument window, whose close button is wired to just hide it. Process.CloseMainWindow
# would then be swallowed, we would fall through to a force-kill, JUCE would skip its
# settings write, and the loaded synth would be gone next launch. So target WM_CLOSE at
# the window actually titled after the product; closing it quits the app.
if (-not ([System.Management.Automation.PSTypeName]'KeysWin').Type) {
    Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class KeysWin {
  delegate bool Proc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(Proc p, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static int CloseTitled(uint pid, string title) {
    int n = 0;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid && IsWindowVisible(h)) {
        var t = new StringBuilder(256); GetWindowTextW(h, t, 256);
        if (t.ToString() == title) { PostMessageW(h, 0x0010, IntPtr.Zero, IntPtr.Zero); n++; }
      }
      return true;
    }, IntPtr.Zero);
    return n;
  }
}
'@
}

if (Get-Process -Name $procName -ErrorAction SilentlyContinue) {
    Write-Host "Closing running $procName..." -ForegroundColor DarkGray
    $windowTitle = $exeName.Replace(".exe", "")

    # Two attempts: an app still restoring a big hosted instrument is not pumping
    # messages yet and will sit on the first WM_CLOSE. Once settled it exits in well
    # under a second, so this costs nothing in the normal case.
    foreach ($attempt in 1..2) {
        foreach ($proc in Get-Process -Name $procName -ErrorAction SilentlyContinue) {
            if ([KeysWin]::CloseTitled([uint32]$proc.Id, $windowTitle) -eq 0) {
                $proc.CloseMainWindow() | Out-Null # no window by that name; fall back
            }
        }
        Wait-Process -Name $procName -Timeout 6 -ErrorAction SilentlyContinue
        if (-not (Get-Process -Name $procName -ErrorAction SilentlyContinue)) { break }
    }

    $stubborn = Get-Process -Name $procName -ErrorAction SilentlyContinue
    if ($stubborn) {
        Write-Host "  ...it ignored the close, forcing it (settings may not persist)." -ForegroundColor Yellow
        $stubborn | Stop-Process -Force
        Wait-Process -Name $procName -Timeout 5 -ErrorAction SilentlyContinue
    }
}

if (-not $NoBuild) {
    # Only configure on a cold build tree. The VS generator re-runs CMake by itself
    # when CMakeLists.txt changes, so an explicit configure every launch is dead time.
    if (-not (Test-Path "$PSScriptRoot\build\CMakeCache.txt")) {
        cmake -B build -G "Visual Studio 17 2022" -A x64 "-DKEYS_COPY_PLUGIN=OFF"
        if ($LASTEXITCODE -ne 0) { exit 1 }
    }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    cmake --build build --config $Config --target $target
    if ($LASTEXITCODE -ne 0) { exit 1 }
    $sw.Stop()
    Write-Host ("Built $target in {0:N1}s" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
}

if (-not (Test-Path $exe)) {
    Write-Error "$exeName not found at $exe - run without -NoBuild first."
    exit 1
}

# Smart App Control is enforced on this machine and dev builds are unsigned, so it
# blocks the first launch of a freshly linked exe while its reputation check runs, then
# lets the same file through a moment later (CodeIntegrity event 3118). Signing every
# iteration would need the EV eToken and a PIN, which defeats a 5-second loop, so
# absorb the transient here rather than failing the run or weakening the machine.
$launched = $false
for ($attempt = 1; $attempt -le 5; $attempt++) {
    try {
        Start-Process -FilePath $exe -ErrorAction Stop
        $launched = $true
        break
    } catch {
        if ($attempt -eq 1) {
            Write-Host "Smart App Control blocked the new build; retrying..." -ForegroundColor DarkGray
        }
        Start-Sleep -Milliseconds 700
    }
}

if (-not $launched) {
    Write-Host "Could not launch $exeName - Smart App Control blocked it 5 times." -ForegroundColor Yellow
    Write-Host "  Retry with: .\run.ps1 -NoBuild" -ForegroundColor Yellow
    Write-Host "  Or build a signed one: .\build.ps1 -Standalone -Sign (needs the eToken plugged in)." -ForegroundColor Yellow
    exit 1
}

Write-Host "Launched $exeName ($Config)" -ForegroundColor Green

if (-not $Keys) {
    Write-Host "  Silent? Load a synth VST3 into the instrument slot - Keys Host reloads it next launch." -ForegroundColor DarkGray
}
