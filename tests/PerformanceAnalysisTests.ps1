$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $projectRoot ('x64/H3Checks/analysis-fixture-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Check([bool]$value, [string]$message) { if (-not $value) { throw $message } }
function Window([int]$index, [int]$frames, [double]$seconds, [double]$p95, [double]$gpuMs, [int]$gpuSamples, [double]$updateMs) {
    return [ordered]@{
        schema = 'game_performance_v1'; event = 'performance_window'; window_index = $index; frame_id = $index * 1000
        scene = 'planet_surface'; build_config = 'release'; window_seconds = $seconds; frames = $frames
        opengl = @{ renderer = 'NVIDIA GeForce RTX 3080 Laptop GPU' }
        context = @{ viewport = @(1280,800); paused = $false; full_map = $false; menu = $false; mounted = $false }
        mixed_context = $false
        frame_interval_ms = @{p95 = $p95; p99 = $p95 + 1; max = $p95 + 2; dropped_samples = 0}
        gpu_elapsed_ms = @{avg = $gpuMs; samples = $gpuSamples}
        process_memory_bytes = @{private_commit = 1000000 + $index * 1000}
        cpu_scopes_ms = @{update = @{total = $updateMs; samples = $frames}}
        counters = @{draw_calls = $frames * 10}
    }
}
$lines = [Collections.Generic.List[string]]::new()
foreach ($index in 1..10) { $lines.Add(((Window $index 60 1 15 1 60 60) | ConvertTo-Json -Depth 8 -Compress)) }
$lines.Add(((Window 11 30 1 20 4 10 100) | ConvertTo-Json -Depth 8 -Compress))
$lines.Add(((Window 12 120 2 10 2 30 80) | ConvertTo-Json -Depth 8 -Compress))
$paused = Window 13 60 1 15 1 60 60; $paused.context.paused = $true
$lines.Add(($paused | ConvertTo-Json -Depth 8 -Compress))
$mixed = Window 14 60 1 15 1 60 60; $mixed.mixed_context = $true; $mixed.context = $null
$lines.Add(($mixed | ConvertTo-Json -Depth 8 -Compress))
$lines.Add('{"schema":') # A partially written live-log tail must be counted, not used as a zero sample.
$file = Join-Path $fixture 'session-0.jsonl'
[IO.File]::WriteAllLines($file, $lines, $utf8)
$copy = Join-Path $fixture 'session-1.jsonl'
[IO.File]::WriteAllLines($copy, @($lines[10], $lines[11]), $utf8)
$output = Join-Path $fixture 'report'
& (Join-Path $projectRoot 'tools/Measure-GamePerformance.ps1') -LogPath $file,$copy -OutputDirectory $output -WarmupSeconds 10
$report = Get-Content -LiteralPath (Join-Path $output 'summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
Check ($report.groups.Count -eq 1) 'Rotated duplicate windows were counted twice or contexts merged.'
$group = $report.groups[0]
Check ($group.frames -eq 150 -and $group.windows -eq 2 -and [Math]::Abs($group.fps - 50) -lt .0001) 'FPS must use total frames / total seconds.'
Check ($group.window_p95_max_ms -eq 20 -and $group.window_p99_max_ms -eq 21) 'Window percentiles must not be averaged.'
Check ([Math]::Abs($group.gpu_average_ms - 2.5) -lt .0001) 'GPU times must be weighted by GPU sample count.'
Check ([Math]::Abs($group.cpu_scopes[0].ms_per_rendered_frame - 1.2) -lt .0001) 'CPU scope totals must be divided by rendered frames.'
Check ($report.invalid_lines -eq 1 -and $report.inactive_windows -eq 1 -and $report.mixed_windows -eq 1) 'Invalid/paused/mixed windows were not handled.'
Check (-not $group.target_measurement_eligible) 'Three seconds cannot establish target hardware performance.'
Check ($group.observed_budget -eq 'over_budget') 'A failing sample was reported within budget.'
$legacy = Window 1 60 1 15 1 60 60; $legacy.Remove('context'); $legacy.Remove('mixed_context'); $legacy.build_config = 'debug'
$legacyFile = Join-Path $fixture 'legacy.jsonl'
[IO.File]::WriteAllText($legacyFile, ($legacy | ConvertTo-Json -Depth 8 -Compress), $utf8)
& (Join-Path $projectRoot 'tools/Measure-GamePerformance.ps1') -LogPath $legacyFile -OutputDirectory (Join-Path $fixture 'legacy-report') -WarmupSeconds 0
$legacyReport = Get-Content -LiteralPath (Join-Path $fixture 'legacy-report/summary.json') -Raw -Encoding UTF8 | ConvertFrom-Json
Check (-not $legacyReport.groups[0].context_known -and -not $legacyReport.groups[0].target_measurement_eligible) 'Legacy Debug logs must not establish target performance.'
Write-Output ('성능 분석 검사 통과. 합성 로그와 결과: ' + $fixture)
