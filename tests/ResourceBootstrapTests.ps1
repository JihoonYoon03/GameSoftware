$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$wrapper = Join-Path $root 'tools/prepare-resources.ps1'
$shell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$node = (Get-Command node.exe -CommandType Application).Source
$savedPath = $env:Path
try {
    # Simulate the Visual Studio process that inherited no Node entry in PATH.
    $nodeDirectory = Split-Path $node
    $env:Path = (($savedPath -split ';') | Where-Object {
        $_.TrimEnd('\', '/') -ine $nodeDirectory.TrimEnd('\', '/')
    }) -join ';'
    & $shell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $wrapper -Check
    if ($LASTEXITCODE -ne 0) { throw 'Node discovery without the inherited PATH entry failed' }
    & $shell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $wrapper -NodeExecutable $node -Check
    if ($LASTEXITCODE -ne 0) { throw 'Explicit NodeExecutable failed' }
    $ErrorActionPreference = 'Continue'
    $failure = & $shell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $wrapper -NodeExecutable (Join-Path $root 'x64/RevisionChecks/nonexistent node/node.exe') -Check 2>&1
    $failureCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($failureCode -eq 0 -or ($failure | Out-String) -notmatch 'Node.js 18\+ was not found') {
        throw 'Missing Node must fail with installation/path guidance'
    }
} finally {
    $env:Path = $savedPath
}
Write-Output 'Resource bootstrap checks passed: stale PATH, explicit NodeExecutable and missing runtime guidance.'
