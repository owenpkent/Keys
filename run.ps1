param(
    [switch]$Keys,      # run plain Keys (MIDI only, silent) instead of Keys Host
    [switch]$NoBuild,   # skip the build and just relaunch what is already there
    [switch]$Hold,      # pause on failure (for a console that closes when this returns)
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
    # Write-Host, not Write-Error: $ErrorActionPreference = "Stop" above turns Write-Error
    # into a terminating error, so the `exit 1` under it was unreachable and the caller's
    # $LASTEXITCODE was left reading as whatever succeeded last.
    Write-Host "run.py needs Python on PATH (install it, or call cmake --build directly)." `
        -ForegroundColor Yellow
    exit 1
}

$argv = @("$PSScriptRoot\run.py", "--config", $Config)
if ($Keys)    { $argv += "--keys" }
if ($NoBuild) { $argv += "--no-build" }
if ($Hold)    { $argv += "--hold" }

& $exe @argv

# $LASTEXITCODE is only set by a native command that actually ran. If py.exe never started
# it is $null (or stale from earlier in the session), and `exit $null` exits 0 - the shim
# reporting success for a run that never happened.
$code = $LASTEXITCODE
if ($null -eq $code) { $code = 1 }
exit $code
