param(
    [string]$Config = "Release",
    [switch]$Standalone,
    [switch]$Installer,
    [switch]$Sign,
    [switch]$NoSign,
    [switch]$PrintVersion
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# project(Keys VERSION ...) in CMakeLists.txt is the one place the version lives
# (docs/RELEASE.md "Version source of truth"). Read once here, reused below for
# the installer's /DVersion=, and exposed via -PrintVersion so release.py can
# ask this script for it instead of keeping a second copy of the regex.
$Version = (Select-String -Path "$PSScriptRoot\CMakeLists.txt" -Pattern 'project\(Keys VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if ($PrintVersion) {
    Write-Output $Version
    exit 0
}

# Dev builds drop the VST3 where the DAW scans. Ableton is set to scan its custom
# VST3 folder (Ableton\vst3), NOT %USERPROFILE%\VST3. build.ps1 owns the copy so a
# locked destination (DAW has Keys loaded) warns instead of failing the build.
$VstCopyDir = "$env:USERPROFILE\Ableton\vst3"
if (-not (Test-Path $VstCopyDir)) { New-Item -ItemType Directory -Path $VstCopyDir -Force | Out-Null }

# The auto-updater pins this exact thumbprint (OK Studio Inc. EV cert on the eToken),
# so a release signed with anything else is rejected by every client. One file,
# installer/signing-thumbprint.txt, so release.py reads the same value rather than
# keeping its own copy.
$SignThumbprint = (Get-Content "$PSScriptRoot\installer\signing-thumbprint.txt" -Raw).Trim()
$TimestampUrl   = "http://timestamp.sectigo.com"
$doSign = ($Sign -or $Installer) -and -not $NoSign

function Get-SignTool {
    $candidates = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object { [version]($_.Directory.Parent.Name) } -Descending
    if (-not $candidates) { Write-Error "signtool.exe not found (install the Windows 10/11 SDK)"; exit 1 }
    $candidates[0].FullName
}

function Invoke-Sign {
    param([string[]]$Files)
    $signtool = Get-SignTool
    $cert = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
        Where-Object { $_.Thumbprint -eq $SignThumbprint -and $_.HasPrivateKey }
    if (-not $cert) {
        Write-Error "OK Studio EV cert ($SignThumbprint) not available. Plug in the eToken (SafeNet client running), then rebuild. Use -NoSign for an unsigned dev build."
        exit 1
    }
    Write-Host "Signing $($Files.Count) file(s) with $($cert.Subject.Split(',')[0])..." -ForegroundColor Cyan
    & $signtool sign /sha1 $SignThumbprint /fd sha256 /tr $TimestampUrl /td sha256 /d "Keys" @Files
    if ($LASTEXITCODE -ne 0) { Write-Error "Signing failed (eToken PIN cancelled or token removed?)"; exit 1 }
    & $signtool verify /pa /v @Files | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Error "Signature verification failed"; exit 1 }
}

function Copy-Vst3ToDaw {
    param([string]$Artefacts, [string]$Bundle)
    $src = "$PSScriptRoot\build\$Artefacts\$Config\VST3\$Bundle"
    $dst = "$VstCopyDir\$Bundle"
    try {
        Copy-Item $src $VstCopyDir -Recurse -Force -ErrorAction Stop
        Write-Host "Installed VST3 -> $dst" -ForegroundColor Green
        return $true
    } catch {
        Write-Host "WARNING: could not update $dst" -ForegroundColor Yellow
        Write-Host "  The DAW has it loaded (file locked). Unload it (or close the DAW), then rerun." -ForegroundColor Yellow
        return $false
    }
}

# build.ps1 owns the copy, so disable JUCE's post-build copy (it hard-fails on a
# locked destination when the DAW has the plugin open).
cmake -B build -G "Visual Studio 17 2022" -A x64 "-DKEYS_COPY_PLUGIN=OFF"
if ($LASTEXITCODE -ne 0) { exit 1 }

cmake --build build --config $Config --target Keys_VST3 KeysHost_VST3
if ($LASTEXITCODE -ne 0) { exit 1 }

if ($Standalone) {
    cmake --build build --config $Config --target Keys_Standalone KeysHost_Standalone
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

if ($doSign) {
    $toSign = @("$PSScriptRoot\build\Keys_artefacts\$Config\VST3\Keys.vst3\Contents\x86_64-win\Keys.vst3",
                "$PSScriptRoot\build\KeysHost_artefacts\$Config\VST3\Keys Host.vst3\Contents\x86_64-win\Keys Host.vst3")
    if ($Standalone) {
        $toSign += "$PSScriptRoot\build\Keys_artefacts\$Config\Standalone\Keys.exe"
        $toSign += "$PSScriptRoot\build\KeysHost_artefacts\$Config\Standalone\Keys Host.exe"
    }
    Invoke-Sign -Files $toSign
}

$copied = Copy-Vst3ToDaw -Artefacts "Keys_artefacts" -Bundle "Keys.vst3"
$copied = (Copy-Vst3ToDaw -Artefacts "KeysHost_artefacts" -Bundle "Keys Host.vst3") -and $copied

if ($Installer) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) {
        Write-Error "Inno Setup 6 not found. Install with: winget install JRSoftware.InnoSetup"
        exit 1
    }
    & $iscc "/DVersion=$Version" "installer\keys.iss"
    if ($LASTEXITCODE -ne 0) { exit 1 }

    $setupExe = "$PSScriptRoot\release\KeysSetup-$Version.exe"
    if ($doSign) {
        Invoke-Sign -Files @($setupExe)
        Write-Host "Installer signed: release\KeysSetup-$Version.exe" -ForegroundColor Green
    } else {
        Write-Host "Installer: release\KeysSetup-$Version.exe (UNSIGNED - clients will reject it!)" -ForegroundColor Yellow
    }
}

Write-Host ""
$state = if ($doSign) { "signed" } else { "unsigned" }
if ($copied) {
    Write-Host "Done ($state). Keys.vst3 and Keys Host.vst3 installed to $VstCopyDir" -ForegroundColor Green
} else {
    Write-Host "Done ($state), but at least one VST3 NOT installed - unload it in the DAW and rerun." -ForegroundColor Yellow
}
