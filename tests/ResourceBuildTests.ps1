param(
    [string] $MSBuild = 'C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe',
    [string] $Toolset = 'v143'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$base = [IO.Path]::GetFullPath((Join-Path $root 'x64/RevisionChecks'))
$fixture = Join-Path $base ('resource-incremental-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $fixture 'SimpleGame'),(Join-Path $fixture 'tools'),(Join-Path $fixture 'Resource/Export'),(Join-Path $fixture 'Resource/Documentation') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'SimpleGame/SimpleGame.vcxproj') -Destination (Join-Path $fixture 'SimpleGame')
[IO.File]::WriteAllText((Join-Path $fixture 'SimpleGame/RoadPlanCatalog.h'), '// test catalogue')
[IO.File]::WriteAllText((Join-Path $fixture 'tools/ensure-resources.mjs'), '// test resource entry')
$source = Join-Path $fixture 'Resource/Export/test.glb'
[IO.File]::WriteAllText($source, 'tracked source')
$runtime = Join-Path $fixture 'Resource/Runtime'
$counter = Join-Path $runtime 'runs.txt'
# Exercise the actual MSBuild dependency target without converting art or requiring Node.
$stub = @'
param([string] $NodeExecutable)
$folder = Join-Path (Split-Path $PSScriptRoot -Parent) 'Resource/Runtime'
$counter = Join-Path $folder 'runs.txt'
$count = if (Test-Path -LiteralPath $counter) { [int](Get-Content -LiteralPath $counter) } else { 0 }
[IO.File]::WriteAllText($counter, [string]($count + 1))
[IO.File]::WriteAllBytes((Join-Path $folder 'game-assets.bin'), [byte[]](0,1,2,3))
[IO.File]::WriteAllText((Join-Path $folder 'manifest.json'), '{}')
$inputs = @(Get-ChildItem (Join-Path (Split-Path $PSScriptRoot -Parent) 'Resource/Export') -Filter *.glb | ForEach-Object { 'Resource/Export/' + $_.Name })
$inputs += 'SimpleGame/RoadPlanCatalog.h'
[IO.File]::WriteAllLines((Join-Path $folder 'build-sources.list'), $inputs)
'@
[IO.File]::WriteAllText((Join-Path $fixture 'tools/prepare-resources.ps1'), $stub)
function Build-Resources([int] $expectedRuns, [switch] $Force) {
    $args = @((Join-Path $fixture 'SimpleGame/SimpleGame.vcxproj'), '/t:PrepareGameResources', '/p:Configuration=Release', '/p:Platform=x64', "/p:PlatformToolset=$Toolset", '/nologo', '/verbosity:minimal')
    if ($Force) { $args += '/p:ForceResourceValidation=true' }
    & $MSBuild @args
    if ($LASTEXITCODE -ne 0) { throw 'Resource build target failed' }
    if ([int](Get-Content -LiteralPath $counter) -ne $expectedRuns) { throw "Unexpected preparation count; expected $expectedRuns" }
}
try {
    Build-Resources 1
    Build-Resources 1 # No PowerShell/Node on an unchanged build.
    [IO.File]::AppendAllText($source, ' changed')
    Build-Resources 2
    Build-Resources 2
    Move-Item -LiteralPath $source -Destination (Join-Path $fixture 'Resource/Export/renamed.glb')
    Build-Resources 3 # The input inventory catches renames/removal too.
    Remove-Item -LiteralPath (Join-Path $fixture 'Resource/Export/renamed.glb')
    Build-Resources 4
    Remove-Item -LiteralPath (Join-Path $runtime 'game-assets.bin')
    Build-Resources 5
    Build-Resources 5
    Build-Resources 6 -Force
    Build-Resources 6
} finally {
    $absolute = [IO.Path]::GetFullPath($fixture)
    if (-not $absolute.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($absolute).StartsWith('resource-incremental-')) { throw 'Unexpected cleanup path' }
    Remove-Item -LiteralPath $absolute -Recurse -Force
}
Write-Output 'Resource build checks passed: unchanged skip, edits, renames, removals, missing output and forced validation.'
