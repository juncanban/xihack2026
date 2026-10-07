param([string]$Compiler = 'D:/Program Files/Webots/msys64/mingw64/bin/g++.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Force artifacts/requested-flow | Out-Null
    $compilerDirectory = Split-Path -Parent (Get-Command $Compiler -ErrorAction Stop).Source
    $env:PATH = "$compilerDirectory;$compilerDirectory/cpp;" + $env:PATH
    & $Compiler -std=c++14 -Wall -Wextra -Werror -Wno-return-type -I tests/stubs tests/requested_flow_test.cpp -o artifacts/requested-flow/flow_test.exe -static
    if ($LASTEXITCODE -ne 0) { throw 'Requested flow compilation failed' }
    & ./artifacts/requested-flow/flow_test.exe | Tee-Object artifacts/requested-flow/result.log
    if ($LASTEXITCODE -ne 0) { throw 'Requested flow failed' }
} finally { Pop-Location }
