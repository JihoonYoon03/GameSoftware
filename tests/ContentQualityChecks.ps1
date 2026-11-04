param(
    [switch]$Build,
    [switch]$Run,
    [ValidateSet('WorldTests','WorldGenerationTests','WorldStreamingTests','GameplayTests','CivicTests','MapHudTests','MovementTests','SceneGraphTests','RoadPlanTests','ConsoleOutputTests','RenderingTests')]
    [string[]]$Test = @('WorldTests','WorldGenerationTests','WorldStreamingTests','GameplayTests','CivicTests','MapHudTests','MovementTests','SceneGraphTests','RoadPlanTests','ConsoleOutputTests'),
    [ValidateRange(10, 3600)][int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$Test = @($Test | Sort-Object -Unique)
$projectRoot = Split-Path -Parent $PSScriptRoot
$checkDirectory = Join-Path $projectRoot 'x64/H3Checks'
$binaryDirectory = Join-Path $checkDirectory 'bin'
$objectDirectory = Join-Path $checkDirectory 'obj'
$utf8 = [Text.UTF8Encoding]::new($false)
if (-not $Build -and -not $Run) {
    Write-Output 'H3 검사 준비: 기본 실행은 빌드/네이티브 검사를 수행하지 않습니다.'
    Write-Output 'x64 Native Tools 콘솔에서 .\tests\ContentQualityChecks.ps1 -Build -Run'
    Write-Output ('선택한 검사: ' + ($Test -join ', '))
    return
}
& (Join-Path $projectRoot 'tools/Test-SourceEncoding.ps1') -Root $projectRoot
[xml]$project = Get-Content -LiteralPath (Join-Path $projectRoot 'SimpleGame/SimpleGame.vcxproj') -Raw
$ns = [Xml.XmlNamespaceManager]::new($project.NameTable)
$ns.AddNamespace('p', $project.DocumentElement.NamespaceURI)
$sources = @($project.SelectNodes('//p:ClCompile[@Include]', $ns) | ForEach-Object { $_.Include } |
    Where-Object { $_ -ne 'SimpleGame.cpp' -and $_ -ne 'stdafx.cpp' } |
    ForEach-Object { Join-Path $projectRoot ('SimpleGame/' + $_.Replace('\','/')) })
if (-not $sources.Count) { throw '프로젝트의 코어 소스 목록이 비어 있습니다.' }
if (@($Test | Where-Object { $_ -ne 'WorldStreamingTests' }).Count -and -not (Test-Path -LiteralPath (Join-Path $projectRoot 'Resource/Runtime/game-assets.bin'))) {
    throw '런타임 리소스가 없습니다. 일반 게임 프로젝트를 한 번 빌드해 리소스를 준비한 뒤 실행해 주세요.'
}
$duplicates = @($sources | Group-Object { [IO.Path]::GetFileNameWithoutExtension($_) } | Where-Object Count -gt 1)
if ($duplicates.Count) { throw '오브젝트 파일명이 겹칩니다. 검사 빌드의 출력 파일명 규칙을 먼저 갱신해 주세요.' }
$headers = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'SimpleGame') -Filter *.h -File -Recurse |
    Where-Object { $_.FullName -notmatch '[\\/]Dependencies[\\/]' } | ForEach-Object FullName)
$testSources = @($Test | Sort-Object -Unique | ForEach-Object { Join-Path $PSScriptRoot ($_ + '.cpp') })
$graphicsInputs = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'SimpleGame/Dependencies') -Recurse -File | Where-Object { $_.Extension -in '.c','.h','.vcxproj' } | ForEach-Object FullName)
$inputFiles = @($sources + $headers + $testSources + $graphicsInputs + $PSCommandPath + (Join-Path $projectRoot 'SimpleGame/SimpleGame.vcxproj') | Sort-Object -Unique)
$inputLines = @($inputFiles | ForEach-Object { $_ + ':' + (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
$manifest = Join-Path $projectRoot 'Resource/Runtime/manifest.json'
if (Test-Path -LiteralPath $manifest) { $inputLines += 'resource-manifest:' + (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash }
$sha = [Security.Cryptography.SHA256]::Create()
try { $fingerprint = [BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($inputLines -join "`n"))).Replace('-','') } finally { $sha.Dispose() }
$stampFile = Join-Path $checkDirectory 'build-state.json'
New-Item -ItemType Directory -Path $checkDirectory, $binaryDirectory, $objectDirectory -Force | Out-Null
$compileOptions = @('/nologo','/O2','/EHsc','/std:c++17','/utf-8','/we4819','/we4828','/Y-','/UNDEBUG','/DNOMINMAX','/DGLEW_STATIC','/DGLEW_NO_GLU','/DGLFW_INCLUDE_NONE','/MT','/D_CRT_SECURE_NO_WARNINGS',('/I' + (Join-Path $projectRoot 'SimpleGame')))
if ($Build) {
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or -not (Get-Command lib.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
        throw 'Visual Studio의 x64 Native Tools 콘솔에서 실행해 주세요.'
    }
    $assertionHeader = Join-Path $objectDirectory 'assertions-required.h'
    [IO.File]::WriteAllText($assertionHeader, "#ifdef NDEBUG`r`n#error Native regression checks require assertions.`r`n#endif`r`n", $utf8)
    $compileOptions += '/FI' + $assertionHeader
    $graphicsProject = Join-Path $projectRoot 'SimpleGame/Dependencies/GraphicsDependencies.vcxproj'
    & msbuild.exe $graphicsProject /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Source-built graphics dependencies failed.' }
    # The engine is compiled once, outside the normal game output. Assertions stay enabled with /O2.
    $coreLog = Join-Path $checkDirectory 'core-build.log'
    & cl.exe @compileOptions /c ('/Fo:' + $objectDirectory.Replace('\','/') + '/') @sources *> $coreLog
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $coreLog -Tail 20; throw ('코어 검사 빌드 실패: ' + $coreLog) }
    $objects = @($sources | ForEach-Object { Join-Path $objectDirectory ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj') })
    $library = Join-Path $objectDirectory 'game-core.lib'
    & lib.exe /nologo ('/OUT:' + $library) @objects *> (Join-Path $checkDirectory 'library-build.log')
    if ($LASTEXITCODE -ne 0) { throw '코어 검사 라이브러리를 만들지 못했습니다.' }
    foreach ($name in $Test) {
        $log = Join-Path $checkDirectory ($name + '-build.log')
        $binary = Join-Path $binaryDirectory ($name + '.exe')
        & cl.exe @compileOptions (Join-Path $PSScriptRoot ($name + '.cpp')) $library ('/Fo:' + (Join-Path $objectDirectory ($name + '.obj'))) ('/Fe:' + $binary) /link (Join-Path $projectRoot 'build/graphics/x64/Release/GameGraphics.lib') opengl32.lib psapi.lib ws2_32.lib user32.lib gdi32.lib shell32.lib winmm.lib *> $log
        if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $log -Tail 20; throw ('검사 빌드 실패: ' + $log) }
    }
    $binaries = @($Test | ForEach-Object { [pscustomobject]@{ name = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $binaryDirectory ($_ + '.exe'))).Hash } })
    $stamp = [pscustomobject]@{ schema = 'cq01_h3_build_v1'; fingerprint = $fingerprint; tests = @($Test | Sort-Object -Unique); compiler = (Get-Command cl.exe).Source; toolset = $env:VCToolsVersion; created_utc = [DateTime]::UtcNow.ToString('o'); optimized = $true; assertions = $true; binaries = $binaries }
    [IO.File]::WriteAllText($stampFile, ($stamp | ConvertTo-Json -Depth 5), $utf8)
    Write-Output ('검사 빌드 완료. 일반 게임 실행 파일은 변경하지 않았습니다: ' + $binaryDirectory)
}
if (-not $Run) { return }
if (-not (Test-Path -LiteralPath $stampFile)) { throw '-Build로 검사 실행 파일을 먼저 준비해 주세요.' }
$stamp = Get-Content -LiteralPath $stampFile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($stamp.fingerprint -ne $fingerprint -or (Compare-Object @($stamp.tests) @($Test | Sort-Object -Unique))) {
    throw '현재 소스/검사 선택과 검사 빌드가 다릅니다. 같은 -Test 목록으로 -Build를 먼저 실행해 주세요.'
}
foreach ($binary in $stamp.binaries) {
    if ((Get-FileHash -LiteralPath (Join-Path $binaryDirectory ($binary.name + '.exe'))).Hash -ne $binary.sha256) { throw '검사 바이너리가 빌드 기록과 다릅니다. 다시 -Build해 주세요.' }
}
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'x64/RevisionChecks'), (Join-Path $checkDirectory 'UserData') -Force | Out-Null
$results = [Collections.Generic.List[object]]::new()
function Invoke-Check([string]$Name, [string]$Executable, [string[]]$Arguments = @()) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.Arguments = (($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    $start.WorkingDirectory = $projectRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    # Tests must never overwrite the player's real LocalAppData save files.
    $start.EnvironmentVariables['LOCALAPPDATA'] = Join-Path $checkDirectory 'UserData'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $timedOut = $false
    try {
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { $timedOut = $true; $process.Kill(); $process.WaitForExit() }
        $exitCode = $process.ExitCode
        $text = $stdout.GetAwaiter().GetResult() + "`r`n" + $stderr.GetAwaiter().GetResult()
        $log = Join-Path $checkDirectory ($Name + '.log')
        [IO.File]::WriteAllText($log, $text, $utf8)
        $results.Add([pscustomobject]@{ name = $Name; exit_code = $exitCode; timed_out = $timedOut; passed = $exitCode -eq 0 -and -not $timedOut; seconds = $timer.Elapsed.TotalSeconds; log = $log })
        Write-Output ('{0}: exit {1}, {2:N1}s' -f $Name, $exitCode, $timer.Elapsed.TotalSeconds)
        if ($exitCode -ne 0 -or $timedOut) { Get-Content -LiteralPath $log -Tail 12 }
    } finally { $process.Dispose() }
}
foreach ($name in $Test) { if ($name -eq 'RenderingTests') { Invoke-Check $name (Join-Path $binaryDirectory ($name + '.exe')) @((Join-Path $checkDirectory 'renders')) } else { Invoke-Check $name (Join-Path $binaryDirectory ($name + '.exe')) } }
if ('GameplayTests' -in $Test -and ($results | Where-Object name -eq 'GameplayTests').passed) {
    $checkpoint = Join-Path $checkDirectory ('checkpoint-' + [Guid]::NewGuid().ToString('N') + '.sav')
    Invoke-Check 'checkpoint-write' (Join-Path $binaryDirectory 'GameplayTests.exe') @('--checkpoint-write', $checkpoint)
    if (($results | Where-Object name -eq 'checkpoint-write').passed) { Invoke-Check 'checkpoint-read' (Join-Path $binaryDirectory 'GameplayTests.exe') @('--checkpoint-read', $checkpoint) }
}
$report = [pscustomobject]@{ schema = 'cq01_h3_checks_v1'; created_utc = [DateTime]::UtcNow.ToString('o'); fingerprint = $fingerprint; build = $stamp; tests = @($results.ToArray()); gpu_fps_verified = $false }
[IO.File]::WriteAllText((Join-Path $checkDirectory 'results.json'), ($report | ConvertTo-Json -Depth 8), $utf8)
if (@($results | Where-Object { -not $_.passed }).Count) { throw ('실패한 검사가 있습니다. 결과: ' + (Join-Path $checkDirectory 'results.json')) }
Write-Output ('선택한 회귀 검사 통과. 실제 게임의 장거리 플레이/FPS 판정은 별도로 수행합니다: ' + (Join-Path $checkDirectory 'results.json'))
