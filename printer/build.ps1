[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')][string]$Architecture = 'x64',
    [ValidateSet('2022', '2026')][string]$VisualStudioVersion = '2022',
    [ValidateSet('10.0.26100.0')]
    [string]$SdkVersion = '10.0.26100.0',
    [switch]$Sign
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Windows with MSVC is required.' }
$root = $PSScriptRoot
. (Join-Path $root 'BuildSupport.ps1')
$hostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
$processArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
$plan = Get-PrinterBuildPlan -Architecture $Architecture -VisualStudioVersion $VisualStudioVersion `
    -HostArchitecture $hostArchitecture -Root $root
$out = $plan.Out
$build = $plan.Build
$stage = $plan.Stage
# SDK tools run on the build host, independently of the requested package target.
$sdk = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin\$SdkVersion\$($plan.SdkToolArchitecture)"
$makeappx = Join-Path $sdk 'makeappx.exe'
if (-not (Test-Path -LiteralPath $makeappx)) {
    throw "Install Windows SDK $SdkVersion including x64 MakeAppx host tools (also used under Windows 11 ARM64 emulation)."
}
if ($Sign -and -not (Test-Path -LiteralPath (Join-Path $sdk 'signtool.exe'))) {
    throw "Install Windows SDK $SdkVersion including x64 SignTool host tools."
}
Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($makeappx)) -Architecture x64 -Name 'MakeAppx host tool'
$component = $plan.RequiredComponent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "Install Visual Studio $VisualStudioVersion C++ tools and vswhere." }
$instances = @(& $vswhere -latest -products '*' -version $plan.VisualStudioRange `
    -requires $component -property installationPath)
if ($LASTEXITCODE -ne 0 -or $instances.Count -ne 1 -or -not $instances[0]) {
    throw "Install Visual Studio $VisualStudioVersion MSVC v143 component $component for target $Architecture."
}
$vsInstance = $instances[0]
Write-Host "Build host OS: $hostArchitecture; PowerShell process: $processArchitecture; target: $($plan.Architecture)"
Write-Host "SDK $SdkVersion tools: x64 (emulated on ARM64); test execution: $($plan.TestExecution)"
Write-Host "Generator: $($plan.CMakeGenerator); toolset: v143"
Write-Host "Build: $build`nStage: $stage`nPackage: $($plan.Package)"

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited $LASTEXITCODE" }
}

& (Join-Path $root 'tests/Check-Source.ps1')
& (Join-Path $root 'tests/Check-BuildSupport.ps1')
$cmakeHelp = & cmake --help
if ($LASTEXITCODE -ne 0) { throw 'CMake --help failed.' }
if (($cmakeHelp -join "`n") -notmatch [regex]::Escape($plan.CMakeGenerator)) {
    throw "Install CMake with generator '$($plan.CMakeGenerator)' (CMake 4.2+ for VS 2026)."
}
Invoke-Checked cmake @('-S', $root, '-B', $build, '-G', $plan.CMakeGenerator,
    '-A', $plan.CMakePlatform, '-T', 'v143', "-DCMAKE_SYSTEM_VERSION=$SdkVersion",
    "-DCMAKE_GENERATOR_INSTANCE=$vsInstance")
Invoke-Checked cmake @('--build', $build, '--config', 'Release', '--parallel')
foreach ($name in @('PsaDiagnostic.exe', 'PsaTask.dll', 'PrintProbe.exe', 'ActivationProbe.exe')) {
    Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes((Join-Path $build "Release/$name"))) `
        -Architecture $Architecture -Name $name
    Write-Host "Verified PE target: $name = $($plan.Architecture)"
}
if ($plan.TestExecution -eq 'not-run-cross-compile') {
    Write-Warning 'ARM64 cross-compile on x64: native CTests NOT RUN. Portable build/source checks ran; execute CTests on native ARM64.'
} else {
    Invoke-Checked ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--no-tests=error')
}
New-Item $stage -ItemType Directory -Force | Out-Null
New-Item (Join-Path $stage 'Assets') -ItemType Directory -Force | Out-Null
New-Item (Join-Path $stage 'Config') -ItemType Directory -Force | Out-Null
Copy-Item (Join-Path $build 'Release\PsaDiagnostic.exe') $stage
Copy-Item (Join-Path $build 'Release\PsaTask.dll') $stage
Set-PrinterManifestArchitecture -Source (Join-Path $root 'package/AppxManifest.xml') `
    -Destination (Join-Path $stage 'AppxManifest.xml') -Architecture $Architecture
Copy-Item (Join-Path $root 'package\DiagnosticPdc.xml') (Join-Path $stage 'Config')
Copy-Item (Join-Path $root 'THIRD-PARTY-NOTICES.txt') $stage

Add-Type -AssemblyName System.Drawing
$sizes = @{ StoreLogo = @(50, 50); Square44x44Logo = @(44, 44);
    Square150x150Logo = @(150, 150); SplashScreen = @(620, 300) }
foreach ($name in $sizes.Keys) {
    $size = $sizes[$name]
    $image = [System.Drawing.Bitmap]::new($size[0], $size[1])
    $graphics = [System.Drawing.Graphics]::FromImage($image)
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(32, 48, 64))
        $graphics.FillRectangle([System.Drawing.Brushes]::Orange, 5, 5, 15, 15)
        $image.Save((Join-Path $stage "Assets\$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $image.Dispose() }
}
$package = $plan.Package
Assert-PrinterPackage -Path $stage -Architecture $Architecture
Invoke-Checked $makeappx @('pack', '/d', $stage, '/p', $package, '/o')
Assert-PrinterPackage -Path $package -Architecture $Architecture -Archive
if ($Sign) {
    # Explicit opt-in; no trust-store changes, service installation, or printer configuration here.
    $cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=PsaFeasibilityDev' `
        -KeyUsage DigitalSignature -FriendlyName 'PSA feasibility development only' `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    Export-Certificate -Cert $cert -FilePath (Join-Path $out 'PsaDiagnostic.cer') | Out-Null
    $signtool = Join-Path $sdk 'signtool.exe'
    Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($signtool)) -Architecture x64 -Name 'SignTool host tool'
    Invoke-Checked $signtool @('sign', '/fd', 'SHA256', '/sha1',
        $cert.Thumbprint, '/s', 'My', $package)
}
[ordered]@{
    hostOSArchitecture = $hostArchitecture
    processArchitecture = $processArchitecture
    targetArchitecture = $plan.Architecture
    sdkVersion = $SdkVersion
    sdkToolArchitecture = $plan.SdkToolArchitecture
    visualStudioVersion = $plan.VisualStudioVersion
    toolset = 'v143'
    testExecution = $plan.TestExecution
    ctests = $(if ($plan.TestExecution -eq 'not-run-cross-compile') { 'not-run' } else { 'passed' })
    peAndManifestChecks = 'passed'
    packageVersion = '0.1.1.0'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'build-evidence.json') -Encoding utf8
Write-Host "Diagnostic package: $package (not a PDF converter)"
