param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force artifacts | Out-Null
    $compilerCommand = Get-Command $Compiler -ErrorAction Stop
    $compilerDirectory = Split-Path -Parent $compilerCommand.Source
    $env:PATH = "$compilerDirectory;$compilerDirectory/cpp;" + $env:PATH
    & $Compiler -std=c++14 -Wall -Wextra -Werror -I tests/stubs tests/orchard_test.cpp -o artifacts/timer_audio_test.exe -static
    if ($LASTEXITCODE -ne 0) { throw 'Timer audio test compilation failed' }
    & ./artifacts/timer_audio_test.exe --timer-audio
    if ($LASTEXITCODE -ne 0) { throw 'Timer audio test failed' }
} finally { Pop-Location }
