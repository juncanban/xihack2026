$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$taskPython = 'C:/Users/15659/.platformio/penv/Scripts/python.exe'
$taskPio = 'C:/Users/15659/.platformio/penv/Scripts/platformio.exe'
$hostScript = Join-Path $PSScriptRoot 'vibe_pet_host.py'
$out = Join-Path $projectRoot 'artifacts/tree-model/hardware'
New-Item -ItemType Directory -Force $out | Out-Null
$hostProcesses = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('python.exe','pythonw.exe') -and $_.CommandLine -match '[D-d]:[\\/]vibe_pet[\\/]host[\\/]vibe_pet_host\.py'
})
$hostProcesses | Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $out 'host_before.json')
try {
    foreach ($item in $hostProcesses) { Stop-Process -Id $item.ProcessId -ErrorAction SilentlyContinue }
    & $taskPython (Join-Path $PSScriptRoot 'verify_tree_model.py') --baseline
    if ($LASTEXITCODE -ne 0) { throw 'Pre-upload read failed' }
    & $taskPio run -d (Join-Path $projectRoot 'firmware') -e vibe_pet -t upload --upload-port COM8 2>&1 | Tee-Object -FilePath (Join-Path $out 'upload.log')
    if ($LASTEXITCODE -ne 0) { throw 'Firmware upload failed' }
    & $taskPython (Join-Path $PSScriptRoot 'verify_tree_model.py')
    if ($LASTEXITCODE -ne 0) { throw 'Tree model target verification failed' }
} finally {
    if ($hostProcesses.Count -gt 0) {
        Start-Process -FilePath $taskPython -ArgumentList @('-u', $hostScript) -WorkingDirectory $projectRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $out 'host_stdout.log') -RedirectStandardError (Join-Path $out 'host_stderr.log')
    }
}
