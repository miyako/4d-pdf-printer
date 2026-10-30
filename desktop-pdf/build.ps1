[CmdletBinding()]
param(
    [ValidateSet('Build', 'Package', 'SignCopy')][string]$Stage = 'Build',
    [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$Architecture,
    [ValidateSet('2022', '2026')][string]$VisualStudioVersion = '2022',
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$BuildDirectory, [string]$CMakePath, [string]$PythonPath,
    [string]$UnsignedPackage, [string]$SignedOutput, [string]$CertificateThumbprint
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Windows only; this helper does not install tools.' }
$root = $PSScriptRoot
. (Join-Path $root '../printer/BuildSupport.ps1')
function Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE); stop without repair/retry." }
}
function Digest([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Fresh([string]$Path) {
    if (-not [IO.Path]::IsPathFullyQualified($Path) -or
        -not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($Path)) -PathType Container)) {
        throw 'Use an absolute new output path with an existing parent.'
    }
    New-Item -ItemType Directory -Path $Path -ErrorAction Stop | Out-Null
}
function Manifest([xml]$Xml) {
    $ns = [Xml.XmlNamespaceManager]::new($Xml.NameTable)
    foreach ($pair in @{
        m = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
        u10 = 'http://schemas.microsoft.com/appx/manifest/uap/windows10/10'
        d = 'http://schemas.microsoft.com/appx/manifest/desktop/windows10'
        p = 'http://schemas.microsoft.com/appx/manifest/printsupport/windows10/2'
        r = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities'
    }.GetEnumerator()) { $ns.AddNamespace($pair.Key, $pair.Value) }
    $identity = $Xml.SelectSingleNode('/m:Package/m:Identity', $ns)
    $families = @($Xml.SelectNodes('/m:Package/m:Dependencies/m:TargetDeviceFamily', $ns))
    if ($identity.Name -cne 'Miyako.DesktopPdfIntegration' -or $identity.Version -cne '0.1.0.0' -or
        $identity.Publisher -cne 'CN=DesktopPdfPrototypeDev' -or $identity.ProcessorArchitecture -cne $arch) {
        throw 'Unexpected integration identity/version/publisher/architecture.'
    }
    if ($families.Count -ne 1 -or $families[0].Name -cne 'Windows.Desktop' -or
        $families[0].MinVersion -cne '10.0.26100.0' -or $families[0].MaxVersionTested -cne '10.0.26100.0') { throw 'Wrong desktop OS contract.' }
    $apps = @($Xml.SelectNodes('/m:Package/m:Applications/m:Application', $ns))
    if (($apps.Id | Sort-Object) -join ',' -cne 'Client,SaveAs,Silent') { throw 'Unexpected AppIDs.' }
    foreach ($id in @('SaveAs', 'Silent')) {
        $app = $Xml.SelectSingleNode("/m:Package/m:Applications/m:Application[@Id='$id']", $ns)
        $extension = $app.SelectSingleNode('m:Extensions/d:Extension', $ns)
        $psa = $app.SelectSingleNode('m:Extensions/p:Extension', $ns)
        $queue = $app.SelectSingleNode('m:Extensions/p:Extension/p:PrintSupportVirtualPrinter', $ns)
        $uri = if ($id -eq 'SaveAs') { 'desktop-pdf:save-as' } else { 'desktop-pdf:silent' }
        if ($app.Executable -cne 'IntegrationApp.exe' -or $app.EntryPoint -cne 'DesktopPdfIntegration.App' -or
            $extension.Category -cne 'windows.fullTrustProcess' -or $extension.Executable -cne 'IntegrationHost.exe' -or
            $app.SelectNodes('m:Extensions/*', $ns).Count -ne 2 -or
            $psa.Category -cne 'windows.printSupportVirtualPrinterWorkflow' -or $psa.EntryPoint -cne 'DesktopPdfIntegration.Task' -or
            $queue.PrinterUri -cne $uri -or $queue.DisplayName -cne "ms-resource:$($id)Queue" -or
            $queue.PdcFile -cne 'Config\IntegrationPdc.xml' -or $queue.PreferredInputFormat -cne 'application/oxps' -or
            $queue.GetAttribute('OutputFileTypes') -cne $(if ($id -eq 'SaveAs') { 'pdf' } else { '' })) {
            throw "Incorrect endpoint/activation wiring: $id"
        }
    }
    $client = $apps | Where-Object Id -CEQ 'Client'
    $server = $Xml.SelectSingleNode('/m:Package/m:Extensions/m:Extension/m:InProcessServer', $ns)
    $cap = @($Xml.SelectNodes('/m:Package/m:Capabilities/*', $ns))
    if ($client.Executable -cne 'IntegrationClient.exe' -or $client.EntryPoint -cne 'Windows.FullTrustApplication' -or
        $client.GetAttribute('RuntimeBehavior', $ns.LookupNamespace('u10')) -cne 'packagedClassicApp' -or
        $client.GetAttribute('TrustLevel', $ns.LookupNamespace('u10')) -cne 'mediumIL' -or
        $server.Path -cne 'IntegrationTask.dll' -or $server.ParentNode.Category -cne 'windows.activatableClass.inProcessServer' -or
        $server.ActivatableClass.ActivatableClassId -cne 'DesktopPdfIntegration.Task' -or $server.ActivatableClass.ThreadingModel -cne 'both' -or
        $cap.Count -ne 1 -or $cap[0].Name -cne 'runFullTrust' -or $cap[0].NamespaceURI -cne $ns.LookupNamespace('r')) {
        throw 'Incorrect Client/Task/capability wiring.'
    }
}
function PackageEvidence([string]$Package, [string]$GeneratedPri) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Package)
    try {
        $expected = @('AppxManifest.xml', 'AppxBlockMap.xml', '[Content_Types].xml', 'resources.pri',
            'Config/IntegrationPdc.xml', 'Assets/StoreLogo.png', 'Assets/Square44x44Logo.png',
            'Assets/Square150x150Logo.png', 'Assets/SplashScreen.png') + $payload
        $names = @($zip.Entries | Where-Object { -not $_.FullName.EndsWith('/') } | ForEach-Object FullName)
        if (Compare-Object ($expected | Sort-Object) ($names | Sort-Object) -CaseSensitive) { throw 'Unexpected/missing MSIX payload (tests/runtime forbidden).' }
        $members = foreach ($entry in $zip.Entries) {
            if ($entry.FullName.EndsWith('/')) { continue }
            $stream = $entry.Open(); $buffer = [IO.MemoryStream]::new()
            try { $stream.CopyTo($buffer); $bytes = $buffer.ToArray() } finally { $stream.Dispose(); $buffer.Dispose() }
            if ($payload -ccontains $entry.FullName) {
                Assert-PrinterPeMachine -Bytes $bytes -Architecture $Architecture -Name $entry.FullName
                if ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant() -cne
                    (Digest (Join-Path $stagePath $entry.FullName))) { throw 'Packaged binary drift.' }
            }
            if ($entry.FullName -ceq 'AppxManifest.xml') { Manifest ([xml][Text.Encoding]::UTF8.GetString($bytes)) }
            if ($entry.FullName -ceq 'resources.pri') {
                $packagedPri = Join-Path $out 'packaged-resources.pri'
                [IO.File]::WriteAllBytes($packagedPri, $bytes)
            }
            [ordered]@{ file = $entry.FullName; bytes = $bytes.Length
                sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant() }
        }
    } finally { $zip.Dispose() }
    if ((Digest $packagedPri) -cne (Digest $GeneratedPri)) { throw 'Actual packaged PRI differs.' }
    $dump = Join-Path $out 'packaged-resources.xml'
    Checked $makepri @('dump', '/if', $packagedPri, '/of', $dump)
    [xml]$resources = Get-Content -LiteralPath $dump -Raw
    if ($resources.PriInfo.ResourceMap.name -cne 'Miyako.DesktopPdfIntegration') { throw 'Wrong PRI identity.' }
    foreach ($pair in @{ SaveAsQueue = '4D PDF Integration (Save As)'; SilentQueue = '4D PDF Integration (Silent)' }.GetEnumerator()) {
        $values = @($resources.SelectNodes("/PriInfo/ResourceMap/ResourceMapSubtree[@name='Resources']/NamedResource[@name='$($pair.Key)']/Candidate[@type='String']/Value"))
        if ($values.Count -ne 1 -or $values[0].InnerText -cne $pair.Value) { throw 'Wrong/ambiguous packaged queue label.' }
    }
    return $members
}
Fresh $OutputRoot
$out = [IO.Path]::GetFullPath($OutputRoot)
$arch = $Architecture.ToLowerInvariant()
$receipt = [ordered]@{ schema = '4d-pdf-printer.own-package'; version = 1; stage = $Stage
    outcome = 'not-started'; architecture = $arch; renderer = $false; productUsable = $false }
$primary = $null
try {
    $receipt.outcome = 'preflight'
    $os = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    if ($os.ToLowerInvariant() -cne $arch) { throw 'Native target OS required; no target/toolchain fallback.' }
    $receipt.os_architecture = $os
    $receipt.process_architecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    $sha = & git -C $root rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $sha -cnotmatch '^[0-9a-f]{40}$') { throw 'Exact Git source checkout required.' }
    $dirty = & git -C $root status --porcelain --untracked-files=normal
    if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Clean exact-source checkout required; put new outputs under ignored out or outside the repository.' }
    $receipt.source_sha = $sha
    $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64'
    $payload = @('IntegrationApp.exe', 'IntegrationTask.dll', 'IntegrationHost.exe', 'IntegrationClient.exe')
    if ($Stage -eq 'Package' -and (-not [IO.Path]::IsPathFullyQualified($BuildDirectory) -or
        -not (Test-Path -LiteralPath $BuildDirectory -PathType Container))) { throw 'Package requires an absolute existing BuildDirectory.' }
    if ($Stage -eq 'SignCopy') {
        if ($CertificateThumbprint -cnotmatch '^[0-9a-fA-F]{40}$' -or
            -not [IO.Path]::IsPathFullyQualified($UnsignedPackage) -or
            -not [IO.Path]::IsPathFullyQualified($SignedOutput) -or
            [IO.Path]::GetExtension($UnsignedPackage) -ine '.msix' -or [IO.Path]::GetExtension($SignedOutput) -ine '.msix' -or
            [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($UnsignedPackage)) -ieq [IO.Path]::GetDirectoryName($SignedOutput)) {
            throw 'SignCopy requires explicit existing certificate, unsigned package and NEW external signed-copy path.'
        }
        $cert = Get-Item -LiteralPath "Cert:/CurrentUser/My/$CertificateThumbprint"
        $eku = @($cert.Extensions | Where-Object { $_.Oid.Value -ceq '2.5.29.37' })
        if (-not $cert.HasPrivateKey -or $cert.Subject -cne 'CN=DesktopPdfPrototypeDev' -or
            $cert.NotBefore -gt (Get-Date) -or $cert.NotAfter -lt (Get-Date) -or
            $eku.Count -ne 1 -or '1.3.6.1.5.5.7.3.3' -notin @($eku[0].EnhancedKeyUsages.Value)) { throw 'Existing matching code-signing certificate required.' }
        $signtool = Join-Path $sdk 'signtool.exe'
        Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($signtool)) -Architecture x64 -Name 'SignTool host'
        $before = Digest $UnsignedPackage
        $unsignedReceipt = Get-Content -LiteralPath (Join-Path ([IO.Path]::GetDirectoryName($UnsignedPackage)) 'package-evidence.json') -Raw | ConvertFrom-Json
        if ($unsignedReceipt.schema -cne '4d-pdf-printer.own-package' -or $unsignedReceipt.version -ne 1 -or
            $unsignedReceipt.stage -cne 'Package' -or $unsignedReceipt.outcome -cne 'success' -or
            $unsignedReceipt.source_sha -cne $sha -or $unsignedReceipt.architecture -cne $arch -or
            $unsignedReceipt.package_sha256 -cne $before) { throw 'Matching successful unsigned package receipt required.' }
        $archive = [IO.Compression.ZipFile]::OpenRead($UnsignedPackage)
        try {
            if ($null -ne $archive.GetEntry('AppxSignature.p7x')) { throw 'Unsigned input already signed.' }
            $reader = [IO.StreamReader]::new($archive.GetEntry('AppxManifest.xml').Open())
            try { Manifest ([xml]$reader.ReadToEnd()) } finally { $reader.Dispose() }
        } finally { $archive.Dispose() }
        [IO.File]::Copy($UnsignedPackage, $SignedOutput, $false)
        if ((Digest $SignedOutput) -cne $before) { throw 'Unsigned copy drifted.' }
        Checked $signtool @('sign', '/fd', 'SHA256', '/sha1', $CertificateThumbprint, '/s', 'My', $SignedOutput)
        Checked $signtool @('verify', '/pa', $SignedOutput)
        if ((Digest $UnsignedPackage) -cne $before) { throw 'Unsigned original changed.' }
        $receipt.unsigned_sha256 = $before; $receipt.signed_sha256 = Digest $SignedOutput
    } else {
        $plan = Get-PrinterBuildPlan -Root $root -Architecture $Architecture -HostArchitecture $os -VisualStudioVersion $VisualStudioVersion
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $instances = @(& $vswhere -latest -products '*' -version $plan.VisualStudioRange -requires $plan.RequiredComponent -property installationPath)
        if ($LASTEXITCODE -ne 0 -or $instances.Count -ne 1 -or -not $instances[0]) { throw 'Requested existing v143 VS component missing.' }
        $vsVersion = & $vswhere -latest -products '*' -version $plan.VisualStudioRange -requires $plan.RequiredComponent -property installationVersion
        if ($LASTEXITCODE -ne 0 -or -not $vsVersion) { throw 'Cannot identify selected Visual Studio version.' }
        if (-not (Test-Path -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include/10.0.26100.0/um/Windows.h'))) {
            throw 'Preinstalled SDK 26100 required.'
        }
        if (-not $CMakePath) {
            $CMakePath = Join-Path $instances[0] 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        }
        if (-not [IO.Path]::IsPathFullyQualified($CMakePath)) { throw 'CMakePath must be an absolute existing executable path.' }
        $CMakePath = (Get-Item -LiteralPath $CMakePath).FullName
        $ctest = Join-Path ([IO.Path]::GetDirectoryName($CMakePath)) 'ctest.exe'
        $cmakeVersion = @(& $CMakePath --version)
        if ($LASTEXITCODE -ne 0 -or $cmakeVersion.Count -eq 0 -or $cmakeVersion[0] -notmatch '^cmake version ([0-9]+\.[0-9]+\.[0-9]+)') { throw 'Cannot identify existing CMake.' }
        $minimum = if ($VisualStudioVersion -eq '2026') { [version]'4.2.0' } else { [version]'3.24.0' }
        if ([version]$Matches[1] -lt $minimum) { throw "CMake $minimum+ required; no download/install." }
        $ctestVersion = @(& $ctest --version)
        if ($LASTEXITCODE -ne 0 -or $ctestVersion.Count -eq 0 -or $ctestVersion[0] -cne $cmakeVersion[0].Replace('cmake', 'ctest')) { throw 'Matching existing CTest required.' }
        $generators = & $CMakePath --help
        if ($LASTEXITCODE -ne 0 -or ($generators -join "`n") -notmatch [regex]::Escape($plan.CMakeGenerator)) { throw 'Required VS generator unavailable.' }
        $receipt.tools = [ordered]@{ cmake = $CMakePath; cmake_version = $cmakeVersion[0]; ctest = $ctest
            ctest_version = $ctestVersion[0]; visual_studio = $instances[0]; visual_studio_version = $vsVersion
            generator = $plan.CMakeGenerator; sdk = '10.0.26100.0' }
        if ($Stage -eq 'Build') {
            foreach ($ref in @('8b34e5669bec822bb82768fd61b0483bbdd74567', '12cc27915d08fcf29c5b1f00c422e0a16cc23e1b')) {
                & git -C $root cat-file -e "$ref^{commit}"
                if ($LASTEXITCODE -ne 0) { throw "Missing pinned source object $ref; obtain it in a separately approved source checkout. No automatic fetch/install." }
            }
            if (-not $PythonPath) { $PythonPath = (Get-Command python -CommandType Application).Source }
            if (-not [IO.Path]::IsPathFullyQualified($PythonPath) -or $PythonPath -match '\\WindowsApps\\') {
                throw 'Real preinstalled Python executable path required, not Store alias.'
            }
            Checked $PythonPath @((Join-Path $root 'tests/Source.py'))
            $BuildDirectory = Join-Path $out 'build'
            Checked $CMakePath @('-S', $root, '-B', $BuildDirectory, '-G', $plan.CMakeGenerator,
                '-A', $plan.CMakePlatform, '-T', 'v143', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0', "-DCMAKE_GENERATOR_INSTANCE=$($instances[0])")
            Checked $CMakePath @('--build', $BuildDirectory, '--config', 'Release', '--parallel', '2')
            $pe = foreach ($name in $payload + @('IntegrationContract.exe', 'IntegrationNative.exe', 'IntegrationProcessTests.exe', 'IntegrationWorkerFixture.exe')) {
                $path = Join-Path $BuildDirectory "Release/$name"
                Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($path)) -Architecture $Architecture -Name $name
                [ordered]@{ file = $name; sha256 = Digest $path }
            }
            Checked $ctest @('--test-dir', $BuildDirectory, '-C', 'Release', '--output-on-failure', '--verbose', '--no-tests=error', '--timeout', '60')
            [ordered]@{ source_sha = $sha; architecture = $arch; ctests = 'passed'; pe = @($pe) } |
                ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $BuildDirectory 'own-build.json') -Encoding utf8
            $receipt.build_directory = $BuildDirectory
        } else {
            $build = Get-Content -LiteralPath (Join-Path $BuildDirectory 'own-build.json') -Raw | ConvertFrom-Json
            if ($build.source_sha -cne $sha -or $build.architecture -cne $arch -or $build.ctests -cne 'passed') { throw 'Exact-source successful native build receipt required.' }
            foreach ($name in $payload) {
                $entry = @($build.pe | Where-Object file -CEQ $name)
                if ($entry.Count -ne 1 -or (Digest (Join-Path $BuildDirectory "Release/$name")) -cne $entry[0].sha256) { throw 'Tested own binary changed/missing.' }
            }
            $makepri = Join-Path $sdk 'makepri.exe'; $makeappx = Join-Path $sdk 'makeappx.exe'
            foreach ($tool in @($makepri, $makeappx)) {
                Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($tool)) -Architecture x64 -Name 'SDK host tool (emulated on ARM)'
            }
            $receipt.tools.sdk_host_tools = @($makepri, $makeappx | ForEach-Object {
                [ordered]@{ path = $_; version = [Diagnostics.FileVersionInfo]::GetVersionInfo($_).FileVersion } })
            $stagePath = Join-Path $out 'stage'
            Checked $CMakePath @('--install', $BuildDirectory, '--config', 'Release', '--prefix', $stagePath)
            foreach ($name in $payload) {
                if ((Digest (Join-Path $stagePath $name)) -cne ($build.pe | Where-Object file -CEQ $name).sha256) { throw 'Installed binary differs from tested binary.' }
            }
            Manifest ([xml](Get-Content -LiteralPath (Join-Path $stagePath 'AppxManifest.xml') -Raw))
            Add-Type -AssemblyName System.Drawing
            $assets = Join-Path $stagePath 'Assets'; New-Item $assets -ItemType Directory | Out-Null
            foreach ($pair in @{ StoreLogo = @(50,50); Square44x44Logo = @(44,44); Square150x150Logo = @(150,150); SplashScreen = @(620,300) }.GetEnumerator()) {
                $image = [Drawing.Bitmap]::new($pair.Value[0], $pair.Value[1]); $graphics = [Drawing.Graphics]::FromImage($image)
                try {
                    $graphics.Clear([Drawing.Color]::FromArgb(32,48,64))
                    $graphics.FillRectangle([Drawing.Brushes]::Orange, 5, 5, 15, 15)
                    $image.Save((Join-Path $assets "$($pair.Key).png"), [Drawing.Imaging.ImageFormat]::Png)
                } finally { $graphics.Dispose(); $image.Dispose() }
            }
            $config = Join-Path $out 'priconfig.xml'; $pri = Join-Path $stagePath 'resources.pri'
            Checked $makepri @('createconfig', '/cf', $config, '/dq', 'lang-en-US')
            Checked $makepri @('new', '/pr', $stagePath, '/cf', $config, '/of', $pri)
            Remove-Item -LiteralPath (Join-Path $stagePath 'Strings/en-US/Resources.resw')
            Remove-Item -LiteralPath (Join-Path $stagePath 'Strings/en-US')
            Remove-Item -LiteralPath (Join-Path $stagePath 'Strings')
            $package = Join-Path $out 'DesktopPdfIntegration.msix'
            Checked $makeappx @('pack', '/d', $stagePath, '/p', $package)
            $receipt.members = @(PackageEvidence $package $pri)
            $receipt.package_sha256 = Digest $package; $receipt.pri_sha256 = Digest $pri
            $receipt.app_ids = @('SaveAs', 'Silent', 'Client'); $receipt.package = $package
        }
    }
    $receipt.outcome = 'success'
} catch {
    $primary = $_; $receipt.outcome = 'failure'; $receipt.error = $_.Exception.Message
    throw
} finally {
    try { $receipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $out 'package-evidence.json') -Encoding utf8 }
    catch { if ($primary) { Write-Error -ErrorAction Continue "Additional receipt-write failure: $_" } else { throw } }
}
