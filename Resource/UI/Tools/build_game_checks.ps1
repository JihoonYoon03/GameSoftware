$ErrorActionPreference = 'Stop'
$uiCheckRoot = Join-Path (Get-Location) 'x64/UIRevision20261028'
New-Item -ItemType Directory -Force -Path $uiCheckRoot | Out-Null
$sources = @(Get-ChildItem SimpleGame -Recurse -Filter *.cpp | Where-Object { $_.FullName -notmatch '\\Dependencies\\|\\x64\\' -and $_.Name -ne 'SimpleGame.cpp' } | Select-Object -ExpandProperty FullName)
& msbuild SimpleGame/Dependencies/GraphicsDependencies.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw 'Graphics dependency build failed' }
$graphicsOptions = @('/MT','/UNDEBUG','/DNOMINMAX','/DGLEW_STATIC','/DGLEW_NO_GLU','/DGLFW_INCLUDE_NONE')
& cl /nologo /O2 /MP /c /EHsc /std:c++17 /utf-8 /Y- @graphicsOptions /I SimpleGame "/Fo:$uiCheckRoot/" @sources
if ($LASTEXITCODE -ne 0) { throw 'UI core compilation failed' }
$objects = @($sources | ForEach-Object { Join-Path $uiCheckRoot (([IO.Path]::GetFileNameWithoutExtension($_)) + '.obj') })
& lib /nologo "/OUT:$uiCheckRoot/GameCore.lib" @objects
if ($LASTEXITCODE -ne 0) { throw 'UI core library failed' }
foreach ($testName in @('GameplayTests','RenderingTests')) {
  & cl /nologo /O2 /EHsc /std:c++17 /utf-8 /Y- @graphicsOptions /I SimpleGame "/Fo:$uiCheckRoot/" "/Fe:$uiCheckRoot/$testName.exe" "tests/$testName.cpp" /link "$uiCheckRoot/GameCore.lib" build/graphics/x64/Release/GameGraphics.lib opengl32.lib user32.lib gdi32.lib shell32.lib windowscodecs.lib ole32.lib psapi.lib ws2_32.lib winmm.lib
  if ($LASTEXITCODE -ne 0) { throw "$testName compilation failed" }
}
foreach ($name in @('freeglut.dll', 'glew32.dll')) {
  $staleDll = Join-Path $uiCheckRoot $name
  if (Test-Path -LiteralPath $staleDll) { Remove-Item -LiteralPath $staleDll }
}
