# Keys release download counts.
#
# Wraps `gh api repos/okstudio1/keys-releases/releases --paginate` and sums each
# release's asset download counts, the alpha-osk `scripts/downloads.py` shape.
#
# The repo is hard-coded on purpose: it is the same pinned constant the updater
# and docs/RELEASE.md carry, and a parameter here would be one more place the
# four of them could drift apart.
#
# Caveat worth repeating whenever you quote a number from this: GitHub counts an
# auto-updater fetch and a manual click identically, so this is directional
# (downloads, not unique installs).

$ErrorActionPreference = "Stop"

$Repo = "okstudio1/keys-releases"

$json = gh api "repos/$Repo/releases" --paginate 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Error "gh api failed. Is gh authenticated with read access to $Repo?`n$json"
    exit 1
}

$releases = $json | ConvertFrom-Json
if (-not $releases -or $releases.Count -eq 0) {
    Write-Host "No releases published on $Repo yet." -ForegroundColor Yellow
    exit 0
}

$total = 0
foreach ($r in $releases) {
    $sub = 0
    foreach ($a in $r.assets) { $sub += $a.download_count }
    $total += $sub

    $stamp = if ($r.published_at) { ([datetime]$r.published_at).ToString("yyyy-MM-dd") } else { "unpublished" }
    Write-Host ""
    Write-Host ("{0,-12} {1,8} downloads   published {2}" -f $r.tag_name, $sub, $stamp) -ForegroundColor Cyan
    foreach ($a in $r.assets) {
        Write-Host ("    {0,-40} {1,8}" -f $a.name, $a.download_count)
    }
    if ($r.assets.Count -eq 0) {
        Write-Host "    (no assets - a release with no installer is invisible to every updater)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host ("TOTAL {0} downloads across {1} release(s) on {2}" -f $total, $releases.Count, $Repo) -ForegroundColor Green
