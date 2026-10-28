param(
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Architecture,
    [Parameter(Mandatory = $true)][ValidateSet('Prepare', 'Build', 'Test')][string]$Stage
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Architecture = $Architecture.ToLowerInvariant()
if (![Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows)) {
    throw 'This helper requires a matching native Windows runner'
}
$osArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
if ($osArchitecture -ne $Architecture) { throw "Expected native $Architecture Windows, found $osArchitecture" }
$renderer = Split-Path $PSScriptRoot -Parent
$repository = Split-Path $renderer -Parent
$build = "$renderer\build-windows-$Architecture"
$installed = "$renderer\build-installed-$Architecture"
$runtime = "$renderer\build-runtime-$Architecture"
$evidence = "$renderer\build-evidence-$Architecture"
$vcpkg = "$renderer\build-vcpkg-$Architecture"
$cache = "$renderer\build-vcpkg-cache-$Architecture"
$triplets = "$evidence\triplets"
$triplet = "$Architecture-windows"
$pin = '866e9e258a76437074a5cf02d4c2d71b53bff2f8'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE) { throw "$Program failed with exit code $LASTEXITCODE" }
}

if ($Stage -eq 'Prepare') {
    foreach ($directory in @($build, $runtime, $evidence)) {
        if (Test-Path -LiteralPath $directory) { Remove-Item -LiteralPath $directory -Recurse -Force }
    }
    New-Item -ItemType Directory -Force $triplets, $cache | Out-Null
    $manifest = Get-Content -Raw "$renderer\vcpkg.json" | ConvertFrom-Json
    $overlay = Get-Content -Raw "$renderer\ports\libgxps\vcpkg.json" | ConvertFrom-Json
    if ($manifest.'builtin-baseline' -ne $pin -or $overlay.version -ne '0.3.2' -or $overlay.'port-version' -lt 7) {
        throw 'Expected the pinned baseline and fail-closed/interleaved libgxps 0.3.2 overlay #7 or newer'
    }
    $cmakeOutput = & cmake --version
    if ($LASTEXITCODE) { throw 'Cannot determine CMake version' }
    $cmakeVersion = @($cmakeOutput)[0]
    if ($cmakeVersion -notmatch 'cmake version ([0-9]+\.[0-9]+\.[0-9]+)') { throw 'Cannot parse CMake version' }
    if ($Architecture -eq 'arm64' -and [version]$Matches[1] -lt [version]'4.2.0') { throw 'VS2026 requires CMake 4.2+' }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsRange = if ($Architecture -eq 'arm64') { '[18.0,19.0)' } else { '[17.0,18.0)' }
    $instances = & $vswhere -products '*' -version $vsRange -format json | ConvertFrom-Json
    if ($LASTEXITCODE) { throw 'vswhere failed' }
    $toolset = $null
    $instance = $null
    foreach ($candidate in @($instances | Sort-Object { [version]$_.installationVersion } -Descending)) {
        $tools = @(Get-ChildItem "$($candidate.installationPath)\VC\Tools\MSVC\14.44.*" -Directory |
            Sort-Object { [version]$_.Name } -Descending | Where-Object {
                (Test-Path "$($_.FullName)\bin\Hostx64\$Architecture\cl.exe") -and
                (Test-Path "$($_.FullName)\lib\$Architecture\msvcrt.lib") -and
                (Test-Path "$($_.FullName)\lib\$Architecture\msvcrtd.lib")
            })
        if ($tools.Count) { $toolset = $tools[0]; $instance = $candidate; break }
    }
    if ($null -eq $toolset) {
        throw "VS $vsRange with v143/14.44 Hostx64 $Architecture tools and matching release/debug CRT libraries is required; no toolset fallback"
    }
    $sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Include" -Directory |
        Where-Object { (Test-Path "$($_.FullName)\um\Windows.h") -and
            (Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Lib\$($_.Name)\um\$Architecture\kernel32.lib") } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (!$sdk) { throw "No Windows SDK for $Architecture" }
    $generator = if ($Architecture -eq 'arm64') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
    foreach ($arch in @($Architecture)) {
        @"
set(VCPKG_TARGET_ARCHITECTURE $arch)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_PLATFORM_TOOLSET_VERSION $($toolset.Name))
set(VCPKG_VISUAL_STUDIO_PATH [[$($instance.installationPath)]])
set(VCPKG_CMAKE_SYSTEM_VERSION $($sdk.Name))
"@ | Set-Content -Encoding utf8 "$triplets\$arch-windows.cmake"
    }
    $compilerTools = @(foreach ($hostTools in @('Hostx64', 'Hostarm64')) {
        foreach ($targetArch in @($Architecture)) {
            foreach ($tool in @('cl.exe', 'link.exe')) {
                $relative = "bin/$hostTools/$targetArch/$tool"
                $toolPath = Join-Path $toolset.FullName $relative
                if (Test-Path -LiteralPath $toolPath) {
                    [ordered]@{ file = $relative; sha256 = (Get-FileHash -LiteralPath $toolPath -Algorithm SHA256).Hash }
                }
            }
        }
    })
    $fingerprint = @($instance.installationVersion, $toolset.Name, $sdk.Name, $cmakeVersion, $compilerTools) |
        ConvertTo-Json -Compress -Depth 6
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $key = [Convert]::ToHexString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($fingerprint))).ToLowerInvariant()
    } finally { $sha.Dispose() }
    $sourceCommit = & git -C $repository rev-parse HEAD
    if ($LASTEXITCODE) { throw 'Cannot identify source commit' }
    $recipes = @(Get-Item "$renderer\vcpkg.json"; Get-ChildItem "$renderer\ports" -Recurse -File) |
        Sort-Object FullName | ForEach-Object {
            [ordered]@{ file = [IO.Path]::GetRelativePath($renderer, $_.FullName).Replace('\', '/')
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        }
    [ordered]@{
        source_commit = $sourceCommit; vcpkg_commit = $pin; architecture = $Architecture
        host_triplet = $triplet
        os_architecture = $osArchitecture
        process_architecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
        generator = $generator; visual_studio = $instance.installationPath
        visual_studio_version = $instance.installationVersion; toolset = $toolset.Name
        windows_sdk = $sdk.Name; cmake = $cmakeVersion; toolchain_cache_key = $key
        compiler_tools = $compilerTools
        dumpbin = "$($toolset.FullName)\bin\Hostx64\x64\dumpbin.exe"
        recipes = $recipes
    } | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 "$evidence\build-provenance.json"
    Copy-Item "$renderer\vcpkg.json" "$evidence\manifest.json"
    if ($env:GITHUB_OUTPUT) { "toolchain=$key" | Add-Content -Encoding utf8 $env:GITHUB_OUTPUT }
    Write-Host "Prepared native $Architecture using $generator, v143/$($toolset.Name), SDK $($sdk.Name)"
    exit 0
}

$provenance = Get-Content -Raw "$evidence\build-provenance.json" | ConvertFrom-Json
if ($provenance.os_architecture -ne $osArchitecture) { throw 'Prepared toolchain architecture mismatch' }
$env:VCPKG_DISABLE_METRICS = '1'
$env:VCPKG_BINARY_SOURCES = "clear;files,$cache,readwrite"
$env:VCPKG_DEFAULT_TRIPLET = $triplet
$env:VCPKG_DEFAULT_HOST_TRIPLET = $triplet
$env:VCPKG_VISUAL_STUDIO_PATH = $provenance.visual_studio
$env:VCPKG_ROOT = $vcpkg

if ($Stage -eq 'Build') {
    if (!(Test-Path "$vcpkg\.git")) {
        Invoke-Checked git @('init', $vcpkg)
        Invoke-Checked git @('-C', $vcpkg, 'remote', 'add', 'origin', 'https://github.com/microsoft/vcpkg.git')
    }
    Invoke-Checked git @('-C', $vcpkg, 'fetch', '--depth', '1', 'origin', $pin)
    Invoke-Checked git @('-C', $vcpkg, 'checkout', '--detach', 'FETCH_HEAD')
    $actualPin = & git -C $vcpkg rev-parse HEAD
    if ($LASTEXITCODE -or $actualPin -ne $pin) { throw 'vcpkg pin mismatch' }
    Invoke-Checked git @('-C', $vcpkg, 'diff', '--quiet', 'HEAD', '--')
    Invoke-Checked "$vcpkg\bootstrap-vcpkg.bat" @('-disableMetrics')
    Invoke-Checked "$vcpkg\vcpkg.exe" @('install', '--triplet', $triplet, '--host-triplet', $triplet,
        "--x-manifest-root=$renderer", "--x-install-root=$installed", "--overlay-triplets=$triplets")
    Invoke-Checked python @('-m', 'pip', 'install', '-r', "$renderer\tests\requirements.txt")
    $env:PKG_CONFIG_PATH = "$installed\$triplet\lib\pkgconfig;$installed\$triplet\share\pkgconfig"
    $env:PKG_CONFIG_LIBDIR = $env:PKG_CONFIG_PATH
    Remove-Item Env:PKG_CONFIG_SYSROOT_DIR -ErrorAction SilentlyContinue
    $platform = if ($Architecture -eq 'arm64') { 'ARM64' } else { 'x64' }
    Invoke-Checked cmake @('-S', $renderer, '-B', $build, '-G', $provenance.generator,
        '-A', "$platform,version=$($provenance.windows_sdk)", '-T', "v143,host=x64,version=$($provenance.toolset)",
        "-DCMAKE_GENERATOR_INSTANCE=$($provenance.visual_studio)",
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake",
        "-DVCPKG_INSTALLED_DIR=$installed", "-DVCPKG_TARGET_TRIPLET=$triplet",
        "-DVCPKG_HOST_TRIPLET=$triplet", "-DVCPKG_OVERLAY_TRIPLETS=$triplets",
        '-DVCPKG_MANIFEST_INSTALL=OFF', '-DVCPKG_APPLOCAL_DEPS=OFF', '-DBUILD_TESTING=ON',
        "-DPKG_CONFIG_EXECUTABLE=$installed/$triplet/tools/pkgconf/pkgconf.exe",
        "-DPython3_EXECUTABLE=$((Get-Command python).Source)",
        "-DPDF_RENDER_WORKER_TEST_EXECUTABLE=$runtime/pdf-render-worker.exe")
    Invoke-Checked cmake @('--build', $build, '--config', 'Release', '--parallel')
    exit 0
}

& "$PSScriptRoot\inventory-windows.ps1" -Architecture $Architecture -InstalledRoot $installed `
    -WorkerPath "$build\Release\pdf-render-worker.exe" -RuntimeRoot $runtime -OutputRoot $evidence `
    -DumpbinPath $provenance.dumpbin -BuildProvenance "$evidence\build-provenance.json"
# Run with the packaged DLL closure, not the broad installed/bin or build-tool PATH.
$ctest = (Get-Command ctest).Source
$oldPath = $env:PATH
$env:PATH = "$runtime;$env:SystemRoot\System32;$env:SystemRoot"
try {
    Invoke-Checked $ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--verbose')
} finally { $env:PATH = $oldPath }
Copy-Item "$build\Testing\Temporary\LastTest.log" "$evidence\test-results.txt"
[ordered]@{
    architecture = $Architecture; os_architecture = $osArchitecture
    worker = 'pdf-render-worker.exe'; imports = 'runtime-imports.json'; build_provenance = 'build-provenance.json'
    worker_sha256 = (Get-FileHash "$runtime\pdf-render-worker.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
    test_log_sha256 = (Get-FileHash "$evidence\test-results.txt" -Algorithm SHA256).Hash.ToLowerInvariant()
    tests = @('worker-contract', 'output-transactions'); result = 'passed'
} | ConvertTo-Json | Set-Content -Encoding utf8 "$runtime\native-test-receipt.json"
Copy-Item "$runtime\native-test-receipt.json" $evidence
