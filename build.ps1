# build.ps1 – Build PocketOPDS on Windows using Docker
#
# Requirements:
#   Docker Desktop (https://www.docker.com/products/docker-desktop/)
#   (WSL2 backend is recommended; Docker Desktop enables it automatically)
#
# Usage:
#   .\build.ps1            # normal build
#   .\build.ps1 -Rebuild   # force Docker image rebuild (re-downloads SDK)


param(
    [switch]$Rebuild
)


Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"


$ImageName = "pocketopds-builder"
$ProjectDir = $PSScriptRoot


# ── Check Docker ──────────────────────────────────────────────────────────────
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Error @"
Docker is not installed or not in PATH.


Install Docker Desktop from:
  https://www.docker.com/products/docker-desktop/


Then restart this terminal and run:
  .\build.ps1
"@
    exit 1
}


# Make sure Docker daemon is running. Preserve Docker's actual diagnostic:
# "daemon is not running" and "access denied" require different fixes.
$savedErrorPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$dockerInfo = docker info 2>&1
$dockerInfoExitCode = $LASTEXITCODE
$ErrorActionPreference = $savedErrorPreference
if ($dockerInfoExitCode -ne 0) {
    $dockerDetail = ($dockerInfo | Out-String).Trim()
    Write-Error @"
Docker cannot be reached.

Docker reported:
$dockerDetail

Make sure Docker Desktop says "Engine running", then try again.
If the message says "Access is denied", run this build from your normal
Windows account rather than an Administrator terminal.
"@
    exit 1
}


# ── Build Docker image (contains SDK + toolchain) ─────────────────────────────
$buildImage = $Rebuild -or
    -not (docker images -q $ImageName 2>$null | Where-Object { $_ })


if ($buildImage) {
    Write-Host "=== Building Docker image (downloads ~540 MB SDK on first run) ===" -ForegroundColor Cyan
    docker build -t $ImageName "$ProjectDir"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "=== Using cached Docker image '$ImageName' ===" -ForegroundColor Cyan
    Write-Host "    (Run with -Rebuild to force image recreation)" -ForegroundColor DarkGray
}


# ── Run the build inside the container ───────────────────────────────────────
Write-Host ""
Write-Host "=== Running cross-compilation ===" -ForegroundColor Cyan


# Convert Windows path to forward-slash format for Docker bind-mount
$mountPath = $ProjectDir -replace '\\', '/'
$mountPath = $mountPath -replace '^([A-Za-z]):', '/$1'  # e.g. D: -> /d


docker run --rm `
    -v "${mountPath}:/workspace" `
    $ImageName


if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Error "Build FAILED (exit code $LASTEXITCODE)"
    exit $LASTEXITCODE
}


# ── Done ──────────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "=== Output ===" -ForegroundColor Green
$appPath = Join-Path $ProjectDir "build\PocketOPDS.app"
if (Test-Path $appPath) {
    $size = (Get-Item $appPath).Length
    Write-Host "  $appPath" -ForegroundColor Green
    Write-Host "  Size: $([math]::Round($size/1KB, 1)) KB"
}
Write-Host ""
Write-Host "Deploy to device:" -ForegroundColor Yellow
Write-Host "  Copy build\PocketOPDS.app  →  <device>:\applications\PocketOPDS.app"
