param([switch]$Run)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Visual Studio의 x64 Native Tools 콘솔에서 실행해 주세요.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$checkDirectory = Join-Path $projectRoot 'x64/WorldStreamingChecks'
New-Item -ItemType Directory -Path $checkDirectory -Force | Out-Null
$outputPrefix = $checkDirectory.Replace('\', '/') + '/'
$executable = Join-Path $checkDirectory 'WorldStreamingTests.exe'
$buildLog = Join-Path $checkDirectory 'build.log'
$testLog = Join-Path $checkDirectory 'tests.log'
$sources = @('SimpleGame/World.cpp', 'SimpleGame/WorldGeneration.cpp', 'SimpleGame/WorldContent.cpp', 'SimpleGame/WorldStreaming.cpp', 'tests/WorldStreamingTests.cpp') |
    ForEach-Object { Join-Path $projectRoot $_ }
& cl.exe /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /Y- "/I$projectRoot/SimpleGame" "/Fo:$outputPrefix" "/Fe:$executable" @sources *> $buildLog
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $buildLog -Tail 20
    throw "지형 검사 빌드 실패. 전체 로그: $buildLog"
}
Write-Output "지형 CPU 검사 빌드 완료. 로그: $buildLog"
if ($Run) {
    & $executable *> $testLog
    $testExit = $LASTEXITCODE
    Get-Content -LiteralPath $testLog -Tail 5
    if ($testExit -ne 0) { throw "지형 검사 실패. 전체 로그: $testLog" }
}
