$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$venvPython = Join-Path $root ".venv\Scripts\python.exe"
$srcRoot = Join-Path $root "src"

if (-not (Test-Path $venvPython)) {
    Write-Host "Virtual environment not found. Creating .venv ..."
    python -m venv (Join-Path $root ".venv")
}

& $venvPython -m pip install -r (Join-Path $root "requirements.txt")
$env:PYTHONPATH = $srcRoot
& $venvPython -m tcp_bootload_host.main
