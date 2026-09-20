# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later

param([string]$OutDir)

. "$PSScriptRoot\Common.ps1"

$cultures = Assert-BuildOutput
$ver = Get-DeGhosterVersion
if (-not $OutDir) { $OutDir = $DistDir }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host "Staging payload ($($cultures.Count) UI languages incl. en-US)..."
$stage = New-StageDir

# Sanitize FullSemVer for a filename (e.g. metadata '+' -> '.').
$verTag = ($ver.fullSemVer -replace '[+]', '.') -replace '[^A-Za-z0-9._-]', '-'
$zipName = "DeGhoster-$verTag-win-x64.zip"
$zipPath = Join-Path $OutDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

Write-Host "Creating $zipName ..."
# Zip the *contents* of the stage dir (files at the archive root).
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal

$size = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Write-Host "ZIP ready: $zipPath ($size MB)" -ForegroundColor Green
