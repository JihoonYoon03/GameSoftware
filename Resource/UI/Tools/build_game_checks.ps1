$ErrorActionPreference = 'Stop'
$uiCheckRoot = Join-Path (Get-Location) 'x64/UIRevision20261028'
New-Item -ItemType Directory -Force -Path $uiCheckRoot | Out-Null
$sources = @(Get-ChildItem SimpleGame -Recurse -Filter *.cpp | Where-Object { $_.FullName -notmatch '\\Dependencies\\|\\x64\\' -and $_.Name -ne 'SimpleGame.cpp' } | Select-Object -ExpandProperty FullName)
& cl /nologo /O2 /MP /c /EHsc /std:c++17 /utf-8 /Y- /I SimpleGame "/Fo:$uiCheckRoot/" @sources
if ($LASTEXITCODE -ne 0) { throw 'UI core compilation failed' }
$objects = @($sources | ForEach-Object { Join-Path $uiCheckRoot (([IO.Path]::GetFileNameWithoutExtension($_)) + '.obj') })
& lib /nologo "/OUT:$uiCheckRoot/GameCore.lib" @objects
if ($LASTEXITCODE -ne 0) { throw 'UI core library failed' }
foreach ($testName in @('GameplayTests','RenderingTests')) {
  & cl /nologo /O2 /EHsc /std:c++17 /utf-8 /Y- /I SimpleGame "/Fo:$uiCheckRoot/" "/Fe:$uiCheckRoot/$testName.exe" "tests/$testName.cpp" /link "$uiCheckRoot/GameCore.lib" /LIBPATH:SimpleGame/Dependencies/x64 freeglut.lib glew32.lib opengl32.lib user32.lib gdi32.lib psapi.lib ws2_32.lib
  if ($LASTEXITCODE -ne 0) { throw "$testName compilation failed" }
}
$uiDlls = @(Get-ChildItem x64/RevisionChecks -Filter *.dll)
foreach ($uiDll in $uiDlls) { Copy-Item -LiteralPath $uiDll.FullName -Destination $uiCheckRoot -Force }
