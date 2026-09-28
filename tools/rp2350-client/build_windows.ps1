# SPDX-License-Identifier: GPL-3.0-or-later
# Builds, tests and packages the RP2350 client. Run inside a VS2022 x64 environment.
# Source layout expected: <SourceRoot>/tools/rp2350-client and <SourceRoot>/docs/rp2350-protocol-vectors.json
param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
if (Test-Path $BuildRoot) { throw "Refusing existing build directory $BuildRoot" }
if (Test-Path $OutputDirectory) { throw "Refusing existing output directory $OutputDirectory" }
$proj = Join-Path $SourceRoot 'tools\rp2350-client'
$vectors = Join-Path $SourceRoot 'docs\rp2350-protocol-vectors.json'
foreach ($p in @("$proj\rp2350-client.pro", $vectors, "$QtRoot\bin\qmake.exe", "$QtRoot\lib\Qt6SerialPort.lib")) {
    if (!(Test-Path $p)) { throw "Missing $p" }
}
New-Item -ItemType Directory -Path $BuildRoot | Out-Null
$env:PATH = "$QtRoot\bin;$env:PATH"
Push-Location $BuildRoot
try {
    & "$QtRoot\bin\qmake.exe" "$proj\rp2350-client.pro" 'CONFIG+=release' 'CONFIG-=debug_and_release'
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & nmake /nologo
    if ($LASTEXITCODE -ne 0) { throw 'nmake failed' }
} finally { Pop-Location }

$exe = Get-ChildItem $BuildRoot -Recurse -Filter 'rkmoon-rp2350-client.exe' | Select-Object -First 1
$tst = Get-ChildItem $BuildRoot -Recurse -Filter 'rp2350-client-tests.exe' | Select-Object -First 1
if (!$exe -or !$tst) { throw 'Build outputs missing' }

# Offline QtTest run (offscreen, no serial hardware).
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_PLUGIN_PATH = "$QtRoot\plugins"
$testLog = Join-Path $BuildRoot 'qtest.txt'
& $tst.FullName "-o" "$testLog,txt"
$testRc = $LASTEXITCODE
Get-Content $testLog
if ($testRc -ne 0) { throw "QtTest failed rc=$testRc" }
Remove-Item Env:QT_QPA_PLATFORM, Env:QT_PLUGIN_PATH

# Package: runnable folder.
$dir = Join-Path $OutputDirectory 'RKMoon-RP2350-Client'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
Copy-Item $exe.FullName $dir
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --no-quick-import --no-network --dir $dir "$dir\rkmoon-rp2350-client.exe" *> "$OutputDirectory\windeployqt.log"
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }
if (!(Test-Path "$dir\Qt6SerialPort.dll")) { throw 'Qt6SerialPort.dll not deployed' }
if (!(Test-Path "$dir\platforms\qwindows.dll")) { throw 'qwindows platform plugin not deployed' }
$crt = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
if (!(Test-Path "$crt\vcruntime140.dll")) { throw 'VS2022 CRT redistributable not found' }
Copy-Item "$crt\*.dll" $dir
Copy-Item "$proj\README.md" "$dir\README.md"
Copy-Item $vectors "$dir\rp2350-protocol-vectors.json"
Copy-Item $testLog "$dir\qtest-offline-result.txt"

# Source identity (the shared repository is not committed by this task).
$src = @{}
foreach ($f in (Get-ChildItem $proj -Recurse -File) + (Get-Item $vectors) + (Get-Item (Join-Path $SourceRoot 'docs\ADR-012-rp2350-hid-bridge.md'))) {
    $rel = $f.FullName.Substring($SourceRoot.Length).TrimStart('\').Replace('\','/')
    $src[$rel] = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
}
$build = [ordered]@{
    product = 'rkmoon-rp2350-client'
    qt = (& "$QtRoot\bin\qmake.exe" -query QT_VERSION)
    msvc = $env:VCToolsVersion
    built = (Get-Date).ToUniversalTime().ToString('o')
    binary_sha256 = (Get-FileHash "$dir\rkmoon-rp2350-client.exe" -Algorithm SHA256).Hash.ToLower()
    offline_tests = 'QtTest offscreen, no serial hardware, no board'
    sources = $src
}
$build | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 "$dir\build-manifest.json"

# Packaged smoke: clean PATH, bundled plugins only, process must stay alive 4 s.
$old = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_QPA_PLATFORM = 'offscreen'
$p = Start-Process -FilePath "$dir\rkmoon-rp2350-client.exe" -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds 4
$alive = !$p.HasExited
if ($alive) { Stop-Process -Id $p.Id -Force }
$env:PATH = $old
Remove-Item Env:QT_QPA_PLATFORM
if (!$alive) { throw "Packaged smoke failed, exit $($p.ExitCode)" }
Write-Output 'PACKAGED_SMOKE_OK'

# Delivery manifest over every file except itself.
$files = [ordered]@{}
Get-ChildItem $dir -Recurse -File | Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($dir.Length + 1).Replace('\','/')
    $files[$rel] = [ordered]@{ size = $_.Length; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() }
}
[ordered]@{ folder = 'RKMoon-RP2350-Client'; files = $files } | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 "$dir\delivery-manifest.json"
$count = (Get-ChildItem $dir -Recurse -File).Count
Write-Output "PACKAGE_DIR=$dir FILES=$count MANIFEST_SHA256=$((Get-FileHash "$dir\delivery-manifest.json" -Algorithm SHA256).Hash.ToLower())"

