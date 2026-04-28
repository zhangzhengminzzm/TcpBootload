$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $root
$venvPython = Join-Path $root ".venv\Scripts\python.exe"
$srcRoot = Join-Path $root "src"
$releaseRoot = Join-Path $repoRoot "release"
$appName = "TcpBootloadHost"

if (-not (Test-Path $venvPython)) {
    Write-Host "Virtual environment not found. Creating .venv ..."
    python -m venv (Join-Path $root ".venv")
}

& $venvPython -m pip install -r (Join-Path $root "requirements.txt")
& $venvPython -m pip install pyinstaller

$mainFile = Join-Path $srcRoot "tcp_bootload_host\main.py"
$workPath = Join-Path $root "build\pyinstaller"
$specPath = Join-Path $root "build\spec"
$distPath = $releaseRoot

if (-not (Test-Path $releaseRoot)) {
    New-Item -ItemType Directory -Path $releaseRoot | Out-Null
}

& $venvPython -m PyInstaller `
    --noconfirm `
    --clean `
    --windowed `
    --name $appName `
    --paths $srcRoot `
    --distpath $distPath `
    --workpath $workPath `
    --specpath $specPath `
    $mainFile

$exePath = Join-Path $releaseRoot "$appName\$appName.exe"
if (-not (Test-Path $exePath)) {
    throw "Build failed: $exePath not found."
}

Write-Host "Build complete: $exePath"

