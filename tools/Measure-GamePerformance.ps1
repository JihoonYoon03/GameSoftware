param(
    [string[]]$LogPath,
    [ValidateRange(0, 3600)][double]$WarmupSeconds = 10,
    [string]$TargetGpu = 'RTX 3080',
    [switch]$IncludeInactive,
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'x64/H3Checks/performance')
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false, $true)
if (-not $LogPath) {
    $latest = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'x64/Debug/Logs'), (Join-Path $projectRoot 'x64/Release/Logs') -Filter 'performance-*.jsonl' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (-not $latest) { throw '성능 로그가 없습니다. 게임 실행 후 생성된 performance-*.jsonl 경로를 지정해 주세요.' }
    $prefix = $latest.Name -replace '-\d+\.jsonl$', ''
    $LogPath = @(Get-ChildItem -LiteralPath $latest.DirectoryName -Filter ($prefix + '-*.jsonl') -File | ForEach-Object FullName)
}
$files = @($LogPath | ForEach-Object { Get-Item -Path $_ -ErrorAction Stop } | Sort-Object FullName -Unique)
if (-not $files.Count) { throw '분석할 로그 파일이 없습니다.' }
$sessions = @{}
$invalidLines = 0
foreach ($file in $files) {
    if ($file.PSIsContainer) { throw "로그 파일을 지정해 주세요: $($file.FullName)" }
    $sessionKey = Join-Path $file.DirectoryName ($file.Name -replace '-\d+\.jsonl$', '')
    if (-not $sessions.ContainsKey($sessionKey)) { $sessions[$sessionKey] = @{} }
    $stream = [IO.FileStream]::new($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $reader = [IO.StreamReader]::new($stream, $utf8, $true)
    try { while ($null -ne ($line = $reader.ReadLine())) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        try { $record = $line | ConvertFrom-Json } catch { ++$invalidLines; continue }
        if ($record.schema -ne 'game_performance_v1' -or $record.event -ne 'performance_window') { continue }
        $seconds = [double]$record.window_seconds
        $frameCount = [double]$record.frames
        if ([double]::IsNaN($frameCount) -or [double]::IsInfinity($frameCount) -or $seconds -le 0 -or [double]::IsNaN($seconds) -or [double]::IsInfinity($seconds) -or [double]$record.frames -le 0) {
            ++$invalidLines; continue
        }
        # Rotated/copied logs can repeat a window; count that window only once.
        $identity = '{0}:{1}' -f $record.window_index, $record.frame_id
        $sessions[$sessionKey][$identity] = $record
    } } finally { $reader.Dispose() }
}
$groups = [Collections.Generic.List[object]]::new()
$warmupWindows = 0
$inactiveWindows = 0
$mixedWindows = 0
foreach ($sessionKey in @($sessions.Keys | Sort-Object)) {
    $elapsed = 0.0
    $buckets = @{}
    foreach ($record in @($sessions[$sessionKey].Values | Sort-Object { [long]$_.window_index }, { [long]$_.frame_id })) {
        $windowStart = $elapsed
        $elapsed += [double]$record.window_seconds
        if ($windowStart -lt $WarmupSeconds) { ++$warmupWindows; continue }
        if ($record.scene -eq 'mixed' -or $record.mixed_context) { ++$mixedWindows; continue }
        $context = $record.context
        if (-not $IncludeInactive -and $context -and ($context.paused -or $context.full_map -or $context.menu)) { ++$inactiveWindows; continue }
        $contextKey = if ($context) { $context | ConvertTo-Json -Compress } else { 'legacy_context_unknown' }
        $key = '{0}|{1}|{2}|{3}' -f $record.opengl.renderer, $record.build_config, $record.scene, $contextKey
        if (-not $buckets.ContainsKey($key)) { $buckets[$key] = [Collections.Generic.List[object]]::new() }
        $buckets[$key].Add($record)
    }
    foreach ($key in @($buckets.Keys | Sort-Object)) {
        $records = $buckets[$key]
        $first = $records[0]
        $frames = 0L; $seconds = 0.0; $gpuTotal = 0.0; $gpuSamples = 0L
        $p95Max = 0.0; $p99Max = 0.0; $intervalMax = 0.0; $dropped = 0L; $slowSeconds = 0.0
        $peakPrivate = 0L; $firstPrivate = $null; $lastPrivate = $null
        $cpu = @{}; $counters = @{}
        foreach ($record in $records) {
            $frames += [long]$record.frames; $seconds += [double]$record.window_seconds
            $p95Max = [Math]::Max($p95Max, [double]$record.frame_interval_ms.p95)
            $p99Max = [Math]::Max($p99Max, [double]$record.frame_interval_ms.p99)
            $intervalMax = [Math]::Max($intervalMax, [double]$record.frame_interval_ms.max)
            $dropped += [long]$record.frame_interval_ms.dropped_samples
            if ([double]$record.frames / [double]$record.window_seconds -lt 60) { $slowSeconds += [double]$record.window_seconds }
            if ($record.gpu_elapsed_ms) {
                $gpuSamples += [long]$record.gpu_elapsed_ms.samples
                $gpuTotal += [double]$record.gpu_elapsed_ms.avg * [double]$record.gpu_elapsed_ms.samples
            }
            if ($record.process_memory_bytes) {
                $memory = [long]$record.process_memory_bytes.private_commit
                $peakPrivate = [Math]::Max($peakPrivate, $memory)
                if ($null -eq $firstPrivate) { $firstPrivate = $memory }
                $lastPrivate = $memory
            }
            foreach ($scope in $record.cpu_scopes_ms.PSObject.Properties) {
                if (-not $cpu.ContainsKey($scope.Name)) { $cpu[$scope.Name] = 0.0 }
                $cpu[$scope.Name] += [double]$scope.Value.total
            }
            foreach ($counter in $record.counters.PSObject.Properties) {
                if (-not $counters.ContainsKey($counter.Name)) { $counters[$counter.Name] = 0L }
                $counters[$counter.Name] += [long]$counter.Value
            }
        }
        $scopes = @($cpu.Keys | ForEach-Object { [pscustomobject]@{ name = $_; ms_per_rendered_frame = $cpu[$_] / $frames } } | Sort-Object ms_per_rendered_frame -Descending)
        $fps = $frames / $seconds
        $gpuAverage = if ($gpuSamples -gt 0) { $gpuTotal / $gpuSamples } else { $null }
        $knownContext = $first.context -and $first.context.viewport.Count -eq 2 -and [int]$first.context.viewport[0] -gt 0 -and [int]$first.context.viewport[1] -gt 0
        $group = [pscustomobject]@{
            session = $sessionKey; gpu = $first.opengl.renderer; build_config = $first.build_config
            scene = $first.scene; context = $first.context; context_known = [bool]$knownContext
            windows = $records.Count; frames = $frames; seconds = $seconds; fps = $fps
            window_p95_max_ms = $p95Max; window_p99_max_ms = $p99Max; interval_max_ms = $intervalMax
            dropped_interval_samples = $dropped; below_60fps_window_seconds = $slowSeconds
            gpu_average_ms = $gpuAverage; gpu_samples = $gpuSamples
            private_commit_peak_bytes = $peakPrivate
            private_commit_delta_bytes = $(if ($null -ne $firstPrivate) { $lastPrivate - $firstPrivate } else { $null })
            target_gpu_matches = $first.opengl.renderer -like ('*' + $TargetGpu + '*')
            target_measurement_eligible = $first.build_config -eq 'release' -and $knownContext -and $first.opengl.renderer -like ('*' + $TargetGpu + '*') -and $seconds -ge 30
            observed_budget = $(if ($fps -ge 60 -and $p99Max -le (1000.0 / 60.0)) { 'within_observed_windows' } else { 'over_budget' })
            cpu_scopes = $scopes; counters = $counters
        }
        $groups.Add($group)
    }
}
if (-not $groups.Count) { throw '워밍업/정지/혼합 구간을 제외한 유효 표본이 없습니다.' }
$report = [pscustomobject]@{
    schema = 'cq01_h3_performance_v1'; created_utc = [DateTime]::UtcNow.ToString('o')
    files = @($files.FullName); warmup_seconds = $WarmupSeconds; target_gpu = $TargetGpu
    invalid_lines = $invalidLines; warmup_windows = $warmupWindows; inactive_windows = $inactiveWindows; mixed_windows = $mixedWindows
    limitations = @('창별 p95/p99의 최댓값이며 전체 프레임의 p95/p99가 아닙니다.', 'CPU 범위는 중첩되므로 더해서 프레임 시간으로 해석하지 않습니다.', '메모리 증감만으로 누수를 판정하지 않습니다.', '이전 로그는 해상도/메뉴/실내 구분이 불완전하여 목표 기기 판정에 사용하지 않습니다.', '관측 구간의 예산 통과가 다른 장면/장시간 60FPS를 보장하지 않습니다.')
    groups = @($groups.ToArray())
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = [IO.Path]::GetFullPath($OutputDirectory)
[IO.File]::WriteAllText((Join-Path $output 'summary.json'), ($report | ConvertTo-Json -Depth 14), $utf8)
function Format-Number([double]$value) { return $value.ToString('F2', [Globalization.CultureInfo]::InvariantCulture) }
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('# CQ-01 H3 성능 로그 분석')
$lines.Add('')
$lines.Add('실행 중인 게임을 조작하지 않고 기존 로그를 분석한 결과입니다. 창별 백분위는 전체 표본 백분위로 합치지 않습니다.')
$lines.Add('')
$lines.Add('| GPU / 구성 | 장면 | 시간(s) | FPS | 창별 p95 최대(ms) | 창별 p99 최대(ms) | 최대 private(MiB) |')
$lines.Add('|---|---|---:|---:|---:|---:|---:|')
foreach ($group in $groups) {
    $lines.Add(('| {0} / {1} | {2} | {3} | {4} | {5} | {6} | {7} |' -f $group.gpu, $group.build_config, $group.scene, (Format-Number $group.seconds), (Format-Number $group.fps), (Format-Number $group.window_p95_max_ms), (Format-Number $group.window_p99_max_ms), (Format-Number ($group.private_commit_peak_bytes / 1MB))))
}
$lines.Add('')
foreach ($limitation in $report.limitations) { $lines.Add('- ' + $limitation) }
$lines.Add('')
$lines.Add(('제외 구간: 워밍업 {0}, 비활동 {1}, 혼합 {2}. 읽지 못한 행 {3}.' -f $warmupWindows, $inactiveWindows, $mixedWindows, $invalidLines))
[IO.File]::WriteAllLines((Join-Path $output 'summary.md'), $lines, $utf8)
Write-Output ('성능 분석 완료: ' + (Join-Path $output 'summary.md'))
$groups | Select-Object scene, build_config, windows, @{n='fps';e={[Math]::Round($_.fps,2)}}, @{n='p99_window_max_ms';e={$_.window_p99_max_ms}}, target_measurement_eligible | Format-Table -AutoSize
