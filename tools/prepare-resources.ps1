param(
    [string] $NodeExecutable = 'node',
    [switch] $Check
)

$ErrorActionPreference = 'Stop'
$minimumMajor = 18
$candidates = [System.Collections.Generic.List[string]]::new()

function Add-NodeCandidate([string] $candidate) {
    if (-not [string]::IsNullOrWhiteSpace($candidate) -and -not $candidates.Contains($candidate)) {
        $candidates.Add($candidate)
    }
}

$automatic = [string]::IsNullOrWhiteSpace($NodeExecutable) -or $NodeExecutable -in @('node', 'node.exe')
if (-not $automatic) {
    Add-NodeCandidate $NodeExecutable
} else {
    # Visual Studio can inherit an old PATH even after Node has been installed.
    Get-Command node.exe -CommandType Application -All -ErrorAction SilentlyContinue |
        ForEach-Object { Add-NodeCandidate $_.Source }
    foreach ($scope in @('Machine', 'User')) {
        foreach ($directory in ([Environment]::GetEnvironmentVariable('Path', $scope) -split ';')) {
            if (-not [string]::IsNullOrWhiteSpace($directory)) {
                $directory = [Environment]::ExpandEnvironmentVariables($directory.Trim().Trim('"'))
                Add-NodeCandidate ([IO.Path]::Combine($directory, 'node.exe'))
            }
        }
    }
    foreach ($directory in @(
        $(if ($env:ProgramW6432) { Join-Path $env:ProgramW6432 'nodejs' }),
        $(if ($env:ProgramFiles) { Join-Path $env:ProgramFiles 'nodejs' }),
        $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} 'nodejs' }),
        $env:NVM_SYMLINK,
        $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'Programs/nodejs' }),
        $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'fnm/aliases/default' }),
        $(if ($env:USERPROFILE) { Join-Path $env:USERPROFILE 'scoop/apps/nodejs-lts/current' }),
        $(if ($env:USERPROFILE) { Join-Path $env:USERPROFILE 'scoop/apps/nodejs/current' }),
        $(if ($env:USERPROFILE) { Join-Path $env:USERPROFILE '.volta/bin' }),
        $(if ($env:USERPROFILE) {
            Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin'
        })
    )) {
        if ($directory) { Add-NodeCandidate ([IO.Path]::Combine($directory, 'node.exe')) }
    }
}

$selected = $null
$selectedVersion = $null
foreach ($candidate in $candidates) {
    try {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $versionText = (& $candidate --version 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -eq 0 -and $versionText -match '^v(\d+)\.' -and [int]$Matches[1] -ge $minimumMajor) {
            $selected = $candidate
            $selectedVersion = $versionText
            break
        }
    } catch {
        # Ignore a broken automatic candidate, then try another installed runtime.
    }
}
if (-not $selected) {
    Write-Error "Node.js 18+ was not found. Install Node.js and restart Visual Studio, or set the MSBuild NodeExecutable property to the full path of node.exe. Requested: $NodeExecutable"
    exit 1
}

Write-Output "Preparing resources with Node.js $selectedVersion ($selected)"
$arguments = @((Join-Path $PSScriptRoot 'ensure-resources.mjs'))
if ($Check) { $arguments += '--check' }
& $selected @arguments
exit $LASTEXITCODE
