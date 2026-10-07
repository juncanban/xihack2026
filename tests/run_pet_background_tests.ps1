param([string]$Compiler = 'D:/Program Files/Webots/msys64/mingw64/bin/g++.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    $compilerDirectory = Split-Path -Parent (Get-Command $Compiler -ErrorAction Stop).Source
    $env:PATH = "$compilerDirectory;$compilerDirectory/cpp;" + $env:PATH
    & $Compiler -std=c++14 -Wall -Wextra -Werror -I tests/stubs tests/orchard_test.cpp -o artifacts/pet-backgrounds/target_test.exe -static
    if ($LASTEXITCODE -ne 0) { throw 'Pet background target compilation failed' }
    & ./artifacts/pet-backgrounds/target_test.exe --pet-backgrounds
    if ($LASTEXITCODE -ne 0) { throw 'Pet background target tests failed' }
} finally { Pop-Location }
