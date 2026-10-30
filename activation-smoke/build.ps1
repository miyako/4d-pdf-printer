[CmdletBinding()]
param([ValidateSet('x64', 'ARM64')][string]$Architecture,
      [ValidateSet('2022', '2026')][string]$VisualStudioVersion = '2022')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Native Windows SDK/MSVC build only.' }
$hostArch = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if (-not $Architecture -or $Architecture -ine $hostArch) { throw 'Explicit native architecture required; no cross-compile test proxy.' }
$root = $PSScriptRoot
. (Join-Path $root '../printer/BuildSupport.ps1')
. (Join-Path $root 'Check-Package.ps1')
& (Join-Path $root 'tests/Source.ps1')
$plan = Get-PrinterBuildPlan -Root $root -Architecture $Architecture -HostArchitecture $hostArch -VisualStudioVersion $VisualStudioVersion
$out = Join-Path $root "out/$($Architecture.ToLowerInvariant())/$([Guid]::NewGuid().ToString('N'))"
$build = Join-Path $out 'build'
$stage = Join-Path $out 'package'
$evidence = Join-Path $out 'evidence'
[IO.Directory]::CreateDirectory($evidence) | Out-Null
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64'
$makeappx = Join-Path $sdk 'makeappx.exe'
$makepri = Join-Path $sdk 'makepri.exe'
foreach ($tool in @($makeappx, $makepri)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing SDK 26100 host tool: $tool" }
    Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($tool)) -Architecture x64 -Name $tool
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$instances = @(& $vswhere -latest -products '*' -version $plan.VisualStudioRange -requires $plan.RequiredComponent -property installationPath)
if ($LASTEXITCODE -ne 0 -or $instances.Count -ne 1 -or -not $instances[0]) { throw "Missing native v143 MSVC component $($plan.RequiredComponent)." }
function Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "API/stage=$Program $($Arguments -join ' '); exit=$LASTEXITCODE. STOP; do not repair/rerun this native batch." }
}
Checked cmake @('-S', $root, '-B', $build, '-G', $plan.CMakeGenerator, '-A', $Architecture, '-T', 'v143',
    '-DCMAKE_SYSTEM_VERSION=10.0.26100.0', "-DCMAKE_GENERATOR_INSTANCE=$($instances[0])")
Checked cmake @('--build', $build, '--config', 'Release', '--parallel', '2')
foreach ($name in @('SmokeApp.exe', 'SmokeTask.dll', 'SmokeHost.exe', 'SmokeObserver.exe', 'SmokeTests.exe', 'SmokeIdentifiers.exe')) {
    Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes((Join-Path $build "Release/$name"))) -Architecture $Architecture -Name $name
}
try { Checked ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--no-tests=error') }
finally {
    $log = Join-Path $build 'Testing/Temporary/LastTest.log'
    if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $evidence 'native-tests.txt') }
}
[IO.Directory]::CreateDirectory((Join-Path $stage 'Assets')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $stage 'Config')) | Out-Null
foreach ($name in @('SmokeApp.exe', 'SmokeTask.dll', 'SmokeHost.exe', 'SmokeObserver.exe')) {
    Copy-Item -LiteralPath (Join-Path $build "Release/$name") -Destination $stage
}
Set-PrinterManifestArchitecture -Source (Join-Path $root 'package/AppxManifest.xml') `
    -Destination (Join-Path $stage 'AppxManifest.xml') -Architecture $Architecture
Assert-SmokeManifest ([xml](Get-Content -LiteralPath (Join-Path $stage 'AppxManifest.xml') -Raw)) $Architecture
Copy-Item -LiteralPath (Join-Path $root '../printer/package/DiagnosticPdc.xml') -Destination (Join-Path $stage 'Config/SmokePdc.xml')
Add-Type -AssemblyName System.Drawing
$sizes = @{ StoreLogo = @(50,50); Square44x44Logo = @(44,44); Square150x150Logo = @(150,150); SplashScreen = @(620,300) }
foreach ($name in $sizes.Keys) {
    $image = [Drawing.Bitmap]::new($sizes[$name][0], $sizes[$name][1])
    $graphics = [Drawing.Graphics]::FromImage($image)
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(32,48,64))
        $image.Save((Join-Path $stage "Assets/$name.png"), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $image.Dispose() }
}
$strings = Join-Path $stage 'Strings/en-US'
[IO.Directory]::CreateDirectory($strings) | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'package/Strings/en-US/Resources.resw') -Destination $strings
$config = Join-Path $out 'priconfig.xml'
$pri = Join-Path $stage 'resources.pri'
Checked $makepri @('createconfig', '/cf', $config, '/dq', 'lang-en-US', '/o')
Checked $makepri @('new', '/pr', $stage, '/cf', $config, '/of', $pri, '/o')
Remove-Item -LiteralPath (Join-Path $strings 'Resources.resw')
Remove-Item -LiteralPath $strings
Remove-Item -LiteralPath (Join-Path $stage 'Strings')
$msix = Join-Path $out 'PsaActivationSmoke.msix'
Checked $makeappx @('pack', '/d', $stage, '/p', $msix)
$archive = [IO.Compression.ZipFile]::OpenRead($msix)
try {
    $reader = [IO.StreamReader]::new($archive.GetEntry('AppxManifest.xml').Open())
    try { Assert-SmokeManifest ([xml]$reader.ReadToEnd()) $Architecture } finally { $reader.Dispose() }
    $packagedPri = Join-Path $out 'packaged.pri'
    [IO.Compression.ZipFileExtensions]::ExtractToFile($archive.GetEntry('resources.pri'), $packagedPri)
    foreach ($name in @('SmokeApp.exe','SmokeTask.dll','SmokeHost.exe','SmokeObserver.exe')) {
        $entry = $archive.GetEntry($name)
        if ($null -eq $entry) { throw "MSIX missing $name" }
        $stream = $entry.Open(); $buffer = [IO.MemoryStream]::new()
        try {
            $stream.CopyTo($buffer)
            Assert-PrinterPeMachine -Bytes $buffer.ToArray() -Architecture $Architecture -Name $name
            if ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($buffer.ToArray())) -cne
                (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash) { throw "Packaged binary differs from own compiled source: $name" }
        } finally { $stream.Dispose(); $buffer.Dispose() }
    }
} finally { $archive.Dispose() }
if ((Get-FileHash -LiteralPath $pri).Hash -cne (Get-FileHash -LiteralPath $packagedPri).Hash) { throw 'Packaged PRI differs.' }
$dump = Join-Path $out 'pri.xml'
Checked $makepri @('dump', '/if', $packagedPri, '/of', $dump, '/o')
Assert-SmokePri ([xml](Get-Content -LiteralPath $dump -Raw))
$sha = (& git -C $root rev-parse HEAD)
if ($LASTEXITCODE) { throw 'Cannot record original-source SHA.' }
$record = [ordered]@{ sourceSha = $sha; architecture = $Architecture; sdk = '10.0.26100.0'; toolset = 'v143'
    nativeUnitTests = 'passed'; unsignedMsixValidation = 'passed'; packagedPsaCallbackTest = 'NOT RUN'
    packageSha256 = (Get-FileHash -LiteralPath $msix).Hash; activationFeasibilityPassed = $false; pdfProduced = $false }
[IO.File]::WriteAllText((Join-Path $evidence 'build.json'), ($record | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Write-Host "Unsigned isolated package (not uploaded): $msix"
