param([string]$Compiler = 'D:/Program Files/Webots/msys64/mingw64/bin/g++.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Force artifacts/tree-model | Out-Null
    $compilerDirectory = Split-Path -Parent (Get-Command $Compiler -ErrorAction Stop).Source
    $env:PATH = "$compilerDirectory;$compilerDirectory/cpp;" + $env:PATH
    & $Compiler -std=c++14 -Wall -Wextra -Werror -I tests/stubs tests/orchard_test.cpp -o artifacts/tree-model/tree_model_test.exe -static
    if ($LASTEXITCODE -ne 0) { throw 'Tree model target compilation failed' }
    & ./artifacts/tree-model/tree_model_test.exe --tree-model
    if ($LASTEXITCODE -ne 0) { throw 'Tree model target tests failed' }
} finally { Pop-Location }

