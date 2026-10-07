param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force artifacts/ui | Out-Null
    # Webots bundled MinGW keeps its runtime DLLs in the sibling cpp directory.
    $compilerCommand = Get-Command $Compiler -ErrorAction Stop
    $compilerDirectory = Split-Path -Parent $compilerCommand.Source
    $env:PATH = "$compilerDirectory;$compilerDirectory/cpp;" + $env:PATH
    & $Compiler -std=c++14 -Wall -Wextra -Werror -I tests/stubs tests/clock_test.cpp -o artifacts/clock_test.exe -static
    if ($LASTEXITCODE -ne 0) { throw 'Clock test compilation failed' }
    & ./artifacts/clock_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Clock calendar regression failed' }
    & $Compiler -std=c++14 -Wall -Wextra -Werror -I tests/stubs tests/fridge_logic_test.cpp -o artifacts/fridge_logic_test.exe -static
    if ($LASTEXITCODE -ne 0) { throw 'Fridge logic test compilation failed' }
    & ./artifacts/fridge_logic_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Fridge data and storage regression failed' }
    & $Compiler -std=c++14 -Wall -Wextra -Werror -I tests/stubs tests/orchard_test.cpp -o artifacts/orchard_test.exe -static
    if ($LASTEXITCODE -ne 0) { throw 'Native test compilation failed' }
    & ./artifacts/orchard_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native UI regression failed' }
    python tests/check_ui_contract.py
    if ($LASTEXITCODE -ne 0) { throw 'Pet preservation or glyph coverage failed' }
    python tests/render_ui.py
    if ($LASTEXITCODE -ne 0) { throw 'UI rendering failed' }
} finally { Pop-Location }
