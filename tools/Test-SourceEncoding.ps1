param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
$extensions = @('.cpp', '.h', '.hpp', '.ps1', '.mjs', '.md', '.json', '.vcxproj', '.filters')
$failures = [System.Collections.Generic.List[string]]::new()
$files = @(
    foreach ($directory in @('SimpleGame', 'tests', 'tools')) {
        Get-ChildItem -LiteralPath (Join-Path $Root $directory) -Recurse -File |
            Where-Object { $_.Extension -in $extensions -and $_.FullName -notmatch '[\\/]Dependencies[\\/]' }
    }
    Get-Item -LiteralPath (Join-Path $Root 'GAME_DOCUMENTATION.md')
)
foreach ($file in $files) {
    try {
        $source = $strictUtf8.GetString([System.IO.File]::ReadAllBytes($file.FullName))
        if ($source.Contains([string][char]0xfffd)) {
            $failures.Add($file.FullName + ': contains a replacement character (U+FFFD).')
        }
    } catch [System.Text.DecoderFallbackException] {
        $failures.Add($file.FullName + ': invalid UTF-8; do not save source as CP949/ANSI.')
    }
}
if ($failures.Count -gt 0) {
    throw ($failures -join [Environment]::NewLine)
}
Write-Output ('UTF-8 source check passed: {0} files.' -f $files.Count)
