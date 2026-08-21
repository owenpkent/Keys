# Keys Release & Publishing

Distribution mirrors the alpha-osk pipeline, and so does this document: the
installer is built and EV-signed **locally**, then published as a GitHub Release
on a separate public repo. CI builds are unsigned smoke tests only, because the
EV certificate lives on a hardware token, so a release binary can only be
produced on the dev machine with the eToken plugged in.

## The contract, and why every clause of it is load-bearing

Releases live in the separate public repo **`okstudio1/keys-releases`**, and the
in-plugin updater is hard-pinned to it (`updaterConfig` in
`src/PluginEditor.cpp`, which the kit's `okstudio::updater` reads). The source
repo is private; the releases repo is public, which is what keeps auto-update
working for anyone regardless of who can see the source.

Four hard contracts. **Every one of them fails silently**, which is the whole
reason this file exists rather than a shorter one:

- **Never move or rename the releases repo.** Every shipped copy of Keys pins
  its API URL. A rename orphans every installed plugin, and they will report
  "up to date" for ever rather than erroring.
- **The asset must be named exactly `KeysSetup-<version>.exe`.** The updater
  matches on `assetPrefix = "KeysSetup-"` and ignores anything else in the
  release without complaint, so a release carrying a misnamed asset looks
  published and is invisible.
- **The tag must be `v<version>`**, matching the version in the asset name. The
  updater compares the tag against `KEYS_VERSION`.
- **`gh release create` must always pass `--repo okstudio1/keys-releases`.**
  Forget it and `gh` cheerfully creates the release on the *source* repo, where
  no updater will ever look. Tag the source repo for history; publish the
  binaries in the releases repo.

The updater's verification is **fail-closed** at every gate (see the kit's
`docs/AUTO_UPDATE.md`), so a release published unsigned is not merely
unpolished: every client rejects it outright.

## Version source of truth

`project(Keys VERSION x.y.z)` in `CMakeLists.txt`. Nothing else hardcodes a
version. Three consumers read it, and all three derive rather than repeat:

| Consumer | How it gets there |
| --- | --- |
| The plugin's own reported version | `KEYS_VERSION="${PROJECT_VERSION}"` compile definition, read by `updaterConfig.currentVersion` and the About box |
| The installer | `build.ps1` parses `project(Keys VERSION ...)` out of `CMakeLists.txt` and passes `/DVersion=` to `installer/keys.iss` |
| The asset name | `keys.iss` sets `OutputBaseFilename=KeysSetup-{#Version}` |

So bumping the one line in `CMakeLists.txt` is the entire version bump. If a
release ever ships with a mismatched tag and asset, that line and the tag
disagreed; nothing else can cause it.

## Release checklist

### 1. Bump the version

Edit `project(Keys VERSION x.y.z)` in `CMakeLists.txt`. Semver.

**Then delete the stale version resources**, or the shipped binary reports the
*previous* version in its Windows file properties:

```powershell
Remove-Item build\*_artefacts\JuceLibraryCode\*_resources.rc
```

JUCE writes `<Target>_resources.rc` (the `VS_VERSION_INFO` block) through juceaide
at **build** time and then never rewrites it, so a version bump alone leaves a
file dated from the last release still saying `FILEVERSION 0,1,0,0`. Re-running
`cmake -B build` does not fix it; only regenerating the file does. Caught on
2026-08-20 while cutting 0.2.0, on a binary that had already been signed.

What makes it easy to miss is that **three of the four version surfaces were
right**: `KEYS_VERSION` (which the updater and the About box read), the
`moduleinfo.json` a VST3 host actually reads, and the installer's own asset name
all came out 0.2.0. Only Explorer's Details tab disagreed. Check it explicitly:

```powershell
(Get-Item "build\Keys_artefacts\Release\VST3\Keys.vst3\Contents\x86_64-win\Keys.vst3").VersionInfo.FileVersion
```

### 2. Stamp the changelog

Move the `[Unreleased]` contents in `CHANGELOG.md` under a new
`## [x.y.z] - YYYY-MM-DD` heading and leave a fresh empty `[Unreleased]` above
it. That section is also the release notes, so it is worth reading once as a
stranger would.

### 3. Commit

```powershell
git add CMakeLists.txt CHANGELOG.md
git commit -m "chore: release x.y.z"
```

### 4. Build and sign

Plug in the EV token. **From a non-elevated shell**: SafeNet exposes the
certificate to the user session only, so an elevated shell reports "OK Studio EV
cert not available" even with the token in.

```powershell
.\build.ps1 -Installer
```

`-Installer` implies signing (`-NoSign` overrides for an unsigned test build).
The pipeline is a Release build of `Keys_VST3` and `KeysHost_VST3`, sign both
binaries, copy them to the Ableton scan folder, compile the Inno Setup
installer, then sign and verify that. Output: `release\KeysSetup-x.y.z.exe`.
The token prompts for its PIN once, part way through.

### 5. Validate before publishing

Two gates, and the second is not optional even when the first is green:

- **pluginval**: `pluginval --strictness-level 5 --validate <path to Keys.vst3>`
  (https://github.com/Tracktion/pluginval). Not currently on PATH here.
- **A real Ableton Live load test.** Live enforces things pluginval does not,
  and the MIDI-input-bus requirement in `CLAUDE.md` was caught only in Live: a
  plugin missing it fails with a silent-looking "This VST3 plug-in could not be
  opened" while pluginval passes. Run the installer, confirm the VST3 lands
  where Live scans, rescan, load Keys, play a chord into an instrument. Test an
  **upgrade over an existing version** too.

### 6. Tag and push

```powershell
git tag vX.Y.Z
git push origin main
git push origin vX.Y.Z
```

### 7. Publish, on the releases repo

```powershell
gh release create vX.Y.Z release\KeysSetup-X.Y.Z.exe --repo okstudio1/keys-releases --title "Keys X.Y.Z" --notes-file <notes>
```

The `--repo` flag is mandatory. Release notes are that version's `CHANGELOG.md`
section; keep a copy in `releases/vX.Y.Z.md` if they outgrow the changelog text.

**Prepend the SmartScreen note to every release body.** Downloaders land on the
release page rather than the README, so the explanation for the install warning
belongs where they actually are:

```markdown
> **Installing on Windows: about the SmartScreen prompt.**
> On first launch you may see a blue "Windows protected your PC" screen. This is normal for newer apps and does **not** mean anything is wrong. Keys is digitally signed by **OK Studio Inc.** Click **More info** and you will see that publisher name, then click **Run anyway** to install. The prompt stops appearing on its own as more people install. (Microsoft removed the SmartScreen fast-pass for code-signed apps in 2024, so reputation now builds from download volume, not from the certificate.)

---
```

To add it to an already-published release:

```powershell
gh release view vX.Y.Z --repo okstudio1/keys-releases --json body --jq .body > body.md
# prepend the block, then:
gh release edit vX.Y.Z --repo okstudio1/keys-releases --notes-file body.md
```

A SmartScreen warning is reputation, not a signing failure. Do not go looking
for a signing bug when one appears.

### 8. End-to-end update test, from the second release onward

With the *previous* signed version installed, open Keys and walk **Settings gear
-> Check for updates** through download, signature verification and installer.
Unsigned dev builds fail verification by design, so a real published release is
the only place the positive path is ever proven.

## Tracking downloads

```powershell
.\scripts\downloads.ps1
```

Wraps `gh api repos/okstudio1/keys-releases/releases --paginate` and sums each
release's asset download counts. The count includes auto-updater fetches as well
as manual clicks, and GitHub does not distinguish them, so treat it as
directional: downloads, not unique installs.

## Prerequisites (one-time)

- **Inno Setup 6**: `winget install JRSoftware.InnoSetup` (installed here at
  `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`)
- **Windows SDK** for `signtool.exe`; `build.ps1` finds the newest one itself
- **SafeNet drivers + the EV token**, the same certificate Octavium and
  alpha-osk use (thumbprint `FC22B5221318F3F3F6B3EB2D969D7F99091557BF`, pinned
  in `build.ps1`, and pinned again by every client's updater)
- **`gh`**, authenticated against an account with write access to
  `okstudio1/keys-releases`

## CI

`.github/workflows/ci.yml` builds on `windows-latest` for every push and PR and
uploads the unsigned VST3 as an artifact. It cannot sign and it cannot publish;
it is a smoke test. Docs and assets-only pushes skip it via `paths-ignore`.

## Future work

- **SBOM.** alpha-osk ships a CycloneDX SBOM and a requirements lockfile beside
  every installer. Keys' dependency surface is exactly JUCE plus
  `okstudio-juce-kit`, both pinned, so the honest equivalent is a small build
  manifest recording those two commits. Worth adding when distribution needs a
  build record, rather than before.
- **macOS**: VST3 + AU from the same CMakeLists, then codesign and notarize with
  an Apple Developer certificate. The Windows EV token has nothing to do with it.
- **Installer branding**: icon and wizard bitmaps, as Octavium does with
  `generate_wizard_images.py`.
