param(
    [switch]$Keys,      # run plain Keys (MIDI only, silent) instead of Keys Host
    [switch]$NoBuild,   # skip the build and just relaunch what is already there
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# A shim over run.py, which is the actual dev loop (Owen launches it by double-clicking
# the .py; typing a command is real effort for him). Everything lives there so the two
# entry points cannot drift apart - fix behaviour in run.py, not here.

$exe = if (Get-Command py -ErrorAction SilentlyContinue) { "py" }
       elseif (Get-Command python -ErrorAction SilentlyContinue) { "python" }
       else { $null }

if (-not $exe) {
    Write-Error "run.py needs Python on PATH (install it, or call cmake --build directly)."
    exit 1
}

$argv = @("$PSScriptRoot\run.py", "--config", $Config)
if ($Keys)    { $argv += "--keys" }
if ($NoBuild) { $argv += "--no-build" }

& $exe @argv
exit $LASTEXITCODE
