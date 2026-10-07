param(
    [string]$Compiler = 'D:/Program Files/Webots/msys64/mingw64/bin/g++.exe',
    [string]$Python = 'D:/ProgramData/anaconda3/python.exe'
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Force artifacts/archive_firmware | Out-Null
    $compilerDirectory = Split-Path -Parent (Get-Command $Compiler -ErrorAction Stop).Source
    $env:PATH = "$compilerDirectory;$compilerDirectory/cpp;" + $env:PATH
    & $Compiler -std=c++14 -Wall -Wextra -Werror -I tests/stubs tests/orchard_test.cpp -o artifacts/archive_test.exe -static
    if ($LASTEXITCODE -ne 0) { throw 'Archive target compilation failed' }
    & ./artifacts/archive_test.exe --archive
    if ($LASTEXITCODE -ne 0) { throw 'Archive target tests failed' }
    & $Python tests/render_archive.py
    if ($LASTEXITCODE -ne 0) { throw 'Archive rendering failed' }
} finally { Pop-Location }
