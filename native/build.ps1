param([string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw -Encoding utf8 | ConvertFrom-Json
foreach ($item in @(@($lock.json.path,$lock.json.sha256),@($lock.json.noticePath,$lock.json.noticeSha256))) {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = [BitConverter]::ToString($sha256.ComputeHash([IO.File]::ReadAllBytes((Join-Path $repoRoot $item[0])))).Replace('-', '').ToLowerInvariant()
    } finally { $sha256.Dispose() }
    if ($hash -ne $item[1]) { throw "Pinned JSON dependency changed: $($item[0])" }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$instances = @((& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json) | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0 -or -not $instances.Count) { throw 'Install the Visual Studio C++ x64 build tools.' }
$instance = $instances[0]
$visualStudio = $instance.installationPath
$generator = switch (([version]$instance.installationVersion).Major) {
    17 { 'Visual Studio 17 2022' }
    18 { 'Visual Studio 18 2026' }
    default { throw 'Unsupported Visual Studio version; update the supported CMake generator explicitly.' }
}
$cmake = Join-Path $visualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot '.mywallpaper/build/windows-x86_64' }
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
Push-Location -LiteralPath $repoRoot
try { & node scripts/generate-manifest.mjs; if ($LASTEXITCODE -ne 0) { throw 'Manifest generation failed.' } }
finally { Pop-Location }
& $cmake -S $repoRoot -B $buildRoot -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$visualStudio"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $buildRoot --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
& $ctest --test-dir $buildRoot -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Model contract tests failed.' }
$outputRoot = Join-Path $repoRoot 'native/out/windows-x86_64'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'Release/companion.exe') -Destination $outputRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $outputRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination $outputRoot
$noticeRoot = Join-Path $outputRoot 'notices'
New-Item -ItemType Directory -Force -Path $noticeRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot $lock.json.noticePath) -Destination (Join-Path $noticeRoot 'nlohmann-json.LICENSE.MIT')
Write-Output "Local Windows bundle: $outputRoot"
