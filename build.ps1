[CmdletBinding()]
param(
    [switch]$DeployWorker,
    [switch]$Clean,
    [string]$Version = "0.2.1"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Src = Join-Path $RepoRoot "src"
$Dist = Join-Path $RepoRoot "dist"
$Stage = Join-Path $Dist "DreamShare-Windows-VST3"
$Zip = Join-Path $Dist "DreamShare-Windows-VST3-$Version.zip"
$Sha = "$Zip.sha256"

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

Write-Host "=== DreamShare build $Version ===" -ForegroundColor Cyan
Require-Command "node"

if ($Clean -and (Test-Path $Dist)) {
    Remove-Item $Dist -Recurse -Force
}
New-Item -ItemType Directory -Path $Dist -Force | Out-Null
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

$Worker = Join-Path $Src "worker.js"
$ModuleInfo = Join-Path $Src "vst\DreamShare.vst3\Contents\Resources\moduleinfo.json"
$VstBinary = Join-Path $Src "vst\DreamShare.vst3\Contents\x86_64-win\DreamShare.vst3"

foreach ($p in @($Worker,$ModuleInfo,$VstBinary)) {
    if (-not (Test-Path $p)) { throw "Required source file is missing: $p" }
}

Write-Host "[1/6] Checking Worker syntax..."
& node --check $Worker
if ($LASTEXITCODE -ne 0) { throw "Worker syntax check failed." }

Write-Host "[2/6] Checking VST metadata..."
$Meta = Get-Content $ModuleInfo -Raw | ConvertFrom-Json
if ($Meta.Name -ne "DreamShare") { throw "moduleinfo.json Name is '$($Meta.Name)', expected 'DreamShare'." }
Write-Host "      Product: $($Meta.Name)  Version: $($Meta.Version)"

Write-Host "[3/6] Staging VST3..."
Copy-Item (Join-Path $Src "vst\DreamShare.vst3") (Join-Path $Stage "DreamShare.vst3") -Recurse -Force
Copy-Item $Worker (Join-Path $Stage "WORKER_DREAMSHARE_PATCHED_0.2.1.js") -Force
Copy-Item (Join-Path $Src "vst\INSTALL.txt") (Join-Path $Stage "INSTALL.txt") -Force

$Audit = @"
DreamShare build $Version

Worker: src/worker.js
Worker syntax: PASS
VST metadata: PASS (Name=$($Meta.Name))
VST binary: supplied compiled Windows x64 binary packaged unchanged

Native VST theme UI/effects are not rebuilt because the original JUCE/C++ source
was not included in the supplied release. Worker theme/session/custom-role APIs
are provided by the patched Worker.
"@
Set-Content -Path (Join-Path $Stage "BUILD_AUDIT.txt") -Value $Audit -Encoding UTF8

Write-Host "[4/6] Creating ZIP..."
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Zip -CompressionLevel Optimal -Force

Write-Host "[5/6] Creating SHA-256 checksum..."
$Hash = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  $(Split-Path $Zip -Leaf)" | Set-Content -Path $Sha -Encoding ASCII

Write-Host "[6/6] Build complete." -ForegroundColor Green
Write-Host "ZIP: $Zip"
Write-Host "SHA: $Sha"

if ($DeployWorker) {
    Write-Host "Deploying Worker with Wrangler..." -ForegroundColor Yellow
    Require-Command "wrangler"
    Push-Location $RepoRoot
    try {
        & wrangler deploy src/worker.js --config wrangler.toml
        if ($LASTEXITCODE -ne 0) { throw "Wrangler deployment failed." }
    }
    finally {
        Pop-Location
    }
    Write-Host "Worker deployment complete." -ForegroundColor Green
}
