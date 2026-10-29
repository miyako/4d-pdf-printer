[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')][string]$Architecture = 'x64',
    [ValidateSet('2022', '2026')][string]$VisualStudioVersion = '2022',
    [string]$CertificateThumbprint,
    [ValidateSet('PrototypeGate', 'Proof', 'ActivationProbe')][string]$Mode = 'PrototypeGate'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Windows with MSVC and SDK 26100 is required.' }
. (Join-Path $PSScriptRoot '../printer/BuildSupport.ps1')
$plan = Get-PrinterBuildPlan -Architecture $Architecture -VisualStudioVersion $VisualStudioVersion `
    -HostArchitecture ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()) -Root $PSScriptRoot
$out = Join-Path $PSScriptRoot "out/$($Architecture.ToLowerInvariant())"
$build = Join-Path $out 'build'
if ($Mode -ceq 'Proof') { $build = Join-Path $out 'proof-build' }
if ($Mode -ceq 'ActivationProbe') { $build = Join-Path $out 'activation-build' }
$stage = Join-Path $out 'package'
if ($Mode -ceq 'ActivationProbe') { $stage = Join-Path $out 'activation-package' }
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdk = Join-Path $sdkRoot 'bin/10.0.26100.0/x64'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$instances = @(& $vswhere -latest -products '*' -version $plan.VisualStudioRange `
    -requires $plan.RequiredComponent -property installationPath)
if ($LASTEXITCODE -ne 0 -or $instances.Count -ne 1 -or -not $instances[0]) {
    throw "Required VS $VisualStudioVersion v143 compiler component is missing."
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($Program)) exited $LASTEXITCODE." }
}
$tools = if ($Mode -ceq 'ActivationProbe') { @('makeappx.exe', 'makepri.exe') } else { @('midl.exe', 'makeappx.exe', 'makepri.exe') }
if ($Mode -cne 'Proof') { foreach ($tool in $tools) {
    $file = Join-Path $sdk $tool
    Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($file)) -Architecture x64 -Name $tool
} }
Invoke-Checked cmake @('-S', $PSScriptRoot, '-B', $build, '-G', $plan.CMakeGenerator,
    '-A', $plan.CMakePlatform, '-T', 'v143', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
    "-DCMAKE_GENERATOR_INSTANCE=$($instances[0])", "-DDESKTOP_SDK_BIN=$sdk",
    "-DDESKTOP_SDK_INCLUDE=$(Join-Path $sdkRoot 'Include/10.0.26100.0')",
    "-DDESKTOP_PROOF_ONLY=$(if ($Mode -ceq 'Proof') { 'ON' } else { 'OFF' })",
    "-DDESKTOP_ACTIVATION_PROBE_ONLY=$(if ($Mode -ceq 'ActivationProbe') { 'ON' } else { 'OFF' })")
Invoke-Checked cmake @('--build', $build, '--config', 'Release', '--parallel')
if ($Mode -ceq 'Proof') {
    if ($plan.TestExecution -cne 'native') { throw 'The product proof requires matching native execution.' }
    $probe = Join-Path $build 'Release/DesktopPdfProof.exe'
    Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($probe)) -Architecture $Architecture -Name 'DesktopPdfProof.exe'
    $fixtureTest = Join-Path $build 'Release/DesktopPdfFixtureTests.exe'
    Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($fixtureTest)) -Architecture $Architecture -Name 'DesktopPdfFixtureTests.exe'
    Invoke-Checked ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--no-tests=error')
    Copy-Item -LiteralPath $probe -Destination (Join-Path $out 'DesktopPdfProof.exe')
    $sourceCommit = & git -C (Split-Path -Parent $PSScriptRoot) rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record proof source identity.' }
    $sourceChanges = @(& git -C (Split-Path -Parent $PSScriptRoot) status --porcelain --untracked-files=normal -- desktop printer/BuildSupport.ps1 printer/probe/Evidence.h)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record proof source worktree state.' }
    [ordered]@{
        schema = '4d-pdf-printer.desktop-proof-build'; version = 1
        architecture = $Architecture.ToLowerInvariant(); nativeContractTest = 'passed'
        nativeFixtureTest = 'passed'
        executableSha256 = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant()
        fixtureTestExecutableSha256 = (Get-FileHash -LiteralPath $fixtureTest -Algorithm SHA256).Hash.ToLowerInvariant()
        sourceCommit = $sourceCommit; sourceDirty = ($sourceChanges.Count -gt 0); productUsable = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'proof-build-evidence.json') -Encoding utf8
    return
}
$payload = if ($Mode -ceq 'ActivationProbe') {
    @('DesktopPdfProbeCtl.exe', 'DesktopPdfProbeHost.exe', 'DesktopPdfApp.exe', 'DesktopPdfProbeTask.dll')
} else { @('DesktopPdfCtl.exe', 'DesktopPdfCoordinator.exe', 'DesktopPdfApp.exe', 'DesktopPdfTask.dll') }
foreach ($name in $payload) {
    Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes((Join-Path $build "Release/$name"))) `
        -Architecture $Architecture -Name $name
}
if ($plan.TestExecution -eq 'native') {
    Invoke-Checked ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--no-tests=error')
} else { throw 'This gate requires native execution; use the matching native runner.' }
foreach ($path in @($stage, (Join-Path $stage 'Assets'), (Join-Path $stage 'Config'), (Join-Path $stage 'Strings/en-US'))) {
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}
foreach ($name in $payload) {
    Copy-Item -LiteralPath (Join-Path $build "Release/$name") -Destination $stage
}
$manifest = if ($Mode -ceq 'ActivationProbe') { 'package/activation/AppxManifest.xml' } else { 'package/AppxManifest.xml' }
Set-PrinterManifestArchitecture -Source (Join-Path $PSScriptRoot $manifest) `
    -Destination (Join-Path $stage 'AppxManifest.xml') -Architecture $Architecture
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../printer/package/DiagnosticPdc.xml') `
    -Destination (Join-Path $stage 'Config/PdfPdc.xml')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package/Strings/en-US/Resources.resw') `
    -Destination (Join-Path $stage 'Strings/en-US/Resources.resw')
Add-Type -AssemblyName System.Drawing
$sizes = @{ StoreLogo = @(50, 50); Square44x44Logo = @(44, 44);
    Square150x150Logo = @(150, 150); SplashScreen = @(620, 300) }
foreach ($name in $sizes.Keys) {
    $image = [Drawing.Bitmap]::new($sizes[$name][0], $sizes[$name][1])
    $graphics = [Drawing.Graphics]::FromImage($image)
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(32, 48, 64))
        $graphics.FillRectangle([Drawing.Brushes]::Orange, 5, 5, 15, 15)
        $image.Save((Join-Path $stage "Assets/$name.png"), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $image.Dispose() }
}
$config = Join-Path $out 'priconfig.xml'
$pri = Join-Path $stage 'resources.pri'
$dump = Join-Path $out 'resource-evidence.xml'
if (Test-Path -LiteralPath $pri) { Remove-Item -LiteralPath $pri }
Invoke-Checked (Join-Path $sdk 'makepri.exe') @('createconfig', '/cf', $config, '/dq', 'lang-en-US', '/o')
Invoke-Checked (Join-Path $sdk 'makepri.exe') @('new', '/pr', $stage, '/cf', $config, '/of', $pri, '/o')
Invoke-Checked (Join-Path $sdk 'makepri.exe') @('dump', '/if', $pri, '/of', $dump, '/o')
[xml]$resources = Get-Content -LiteralPath $dump -Raw
if ($resources.PriInfo.ResourceMap.name -cne 'Miyako.DesktopPdfPrototype') { throw 'Wrong PRI identity.' }
foreach ($key in @('InteractiveDisplayName', 'AutomationDisplayName')) {
    $nodes = @($resources.SelectNodes("//NamedResource[@name='$key']/Candidate[@type='String']/Value"))
    if ($nodes.Count -ne 1 -or $nodes[0].InnerText -notmatch '^4D PDF Prototype') { throw "Invalid resource $key." }
}
Remove-Item -LiteralPath (Join-Path $stage 'Strings/en-US/Resources.resw')
Remove-Item -LiteralPath (Join-Path $stage 'Strings/en-US')
Remove-Item -LiteralPath (Join-Path $stage 'Strings')
$package = Join-Path $out 'DesktopPdfPrototype.msix'
if ($Mode -ceq 'ActivationProbe') { $package = Join-Path $out 'DesktopPdfActivationProbe.msix' }
Invoke-Checked (Join-Path $sdk 'makeappx.exe') @('pack', '/d', $stage, '/p', $package, '/o')
$archive = [IO.Compression.ZipFile]::OpenRead($package)
try {
    $reader = [IO.StreamReader]::new($archive.GetEntry('AppxManifest.xml').Open())
    try { [xml]$packagedManifest = $reader.ReadToEnd() } finally { $reader.Dispose() }
    if ($packagedManifest.Package.Identity.Name -cne 'Miyako.DesktopPdfPrototype' -or
        $packagedManifest.Package.Identity.Publisher -cne 'CN=DesktopPdfPrototypeDev' -or
        $packagedManifest.Package.Identity.ProcessorArchitecture -cne $Architecture.ToLowerInvariant()) {
        throw 'Actual MSIX manifest identity/architecture mismatch.'
    }
    if ($Mode -ceq 'ActivationProbe') {
        $printers = @($packagedManifest.SelectNodes("//*[local-name()='PrintSupportVirtualPrinter']"))
        $hosts = @($packagedManifest.SelectNodes("//*[local-name()='Extension' and @Category='windows.fullTrustProcess']"))
        if ($packagedManifest.Package.Identity.Version -cne '0.2.2.0' -or $printers.Count -ne 1 -or
            $printers[0].HasAttribute('OutputFileTypes') -or $hosts.Count -ne 1 -or
            $hosts[0].Executable -cne 'DesktopPdfProbeHost.exe') { throw 'Activation probe manifest wiring changed.' }
    }
    foreach ($entry in $archive.Entries) {
        if ([IO.Path]::GetExtension($entry.FullName) -notin @('.exe', '.dll')) { continue }
        $input = $entry.Open()
        $memory = [IO.MemoryStream]::new()
        try {
            $input.CopyTo($memory)
            Assert-PrinterPeMachine -Bytes $memory.ToArray() -Architecture $Architecture -Name $entry.FullName
        } finally { $input.Dispose(); $memory.Dispose() }
    }
    $packagedPri = Join-Path $out 'packaged-resources.pri'
    [IO.Compression.ZipFileExtensions]::ExtractToFile($archive.GetEntry('resources.pri'), $packagedPri, $true)
} finally { $archive.Dispose() }
if ((Get-FileHash -LiteralPath $packagedPri -Algorithm SHA256).Hash -cne
    (Get-FileHash -LiteralPath $pri -Algorithm SHA256).Hash) { throw 'Actual MSIX PRI hash mismatch.' }
Invoke-Checked (Join-Path $sdk 'makepri.exe') @('dump', '/if', $packagedPri, '/of', $dump, '/o')
if ($CertificateThumbprint) {
    $certificate = Get-Item -LiteralPath "Cert:/CurrentUser/My/$CertificateThumbprint"
    if ($certificate.Subject -cne 'CN=DesktopPdfPrototypeDev' -or -not $certificate.HasPrivateKey) {
        throw 'Signing requires the explicitly supplied DesktopPdfPrototypeDev certificate/private key.'
    }
    Invoke-Checked (Join-Path $sdk 'signtool.exe') @('sign', '/fd', 'SHA256', '/sha1',
        $CertificateThumbprint, '/s', 'My', $package)
}
$sourceCommit = & git -C (Split-Path -Parent $PSScriptRoot) rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record package source identity.' }
$sourceChanges = @(& git -C (Split-Path -Parent $PSScriptRoot) status --porcelain --untracked-files=normal -- desktop printer/BuildSupport.ps1 printer/probe/Evidence.h)
if ($LASTEXITCODE -ne 0) { throw 'Cannot record package source state.' }
$hashes = [ordered]@{}
foreach ($name in $payload) {
    $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $stage $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
[ordered]@{
    schema = '4d-pdf-printer.desktop-build'; version = 1
    stage = $(if ($Mode -ceq 'ActivationProbe') { 'activation-probe-compile-package-units-only' } else { 'handoff-gate-only' })
    architecture = $Architecture.ToLowerInvariant(); nativeTests = 'passed'
    sourceCommit = $sourceCommit; sourceDirty = ($sourceChanges.Count -gt 0)
    ownCodeSha256 = $hashes
    activationGatePassed = $false; actualPsaCallbackExercised = $false
    packageSha256 = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
    resourcesPriSha256 = (Get-FileHash -LiteralPath $pri -Algorithm SHA256).Hash.ToLowerInvariant()
    pdfProduced = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out $(if ($Mode -ceq 'ActivationProbe') {
    'activation-build-evidence.json' } else { 'build-evidence.json' })) -Encoding utf8
