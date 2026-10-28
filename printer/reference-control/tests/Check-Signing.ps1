# Portable, dependency-free validation tests. Never creates certificates or signs.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$signScript = Join-Path (Split-Path $PSScriptRoot -Parent) 'Sign-Control.ps1'
. $signScript
$script:passed = 0

function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'." }
}
function Expect-Failure([scriptblock]$Action, [string]$Message) {
    try { & $Action | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Message) {
            throw "Expected failure matching '$Message', got: $($_.Exception.Message)"
        }
        return
    }
    throw "Expected an explicit failure matching '$Message'."
}
function Test-Case([string]$Label, [scriptblock]$Action) {
    & $Action
    $script:passed++
    Write-Host "PASS: $Label"
}
function Set-U16([byte[]]$Bytes, [int]$Offset, [uint16]$Value) {
    [BitConverter]::GetBytes($Value).CopyTo($Bytes, $Offset)
}
function Set-U32([byte[]]$Bytes, [int]$Offset, [uint32]$Value) {
    [BitConverter]::GetBytes($Value).CopyTo($Bytes, $Offset)
}
function New-TestPe([uint16]$Machine = 0xaa64, [switch]$Managed, [uint32]$Flags = 1) {
    $bytes = [byte[]]::new(1536)
    Set-U16 $bytes 0 0x5a4d
    Set-U32 $bytes 0x3c 128
    Set-U32 $bytes 128 0x4550
    Set-U16 $bytes 132 $Machine
    Set-U16 $bytes 134 1
    $pe32 = $Machine -in @(0x014c, 0x01c0, 0x01c4)
    $optionalSize = if ($pe32) { 224 } else { 240 }
    $magic = if ($pe32) { 0x10b } else { 0x20b }
    $directory = if ($pe32) { 96 } else { 112 }
    $count = if ($pe32) { 92 } else { 108 }
    Set-U16 $bytes 148 $optionalSize
    Set-U16 $bytes 152 $magic
    Set-U32 $bytes (152 + $count) 16
    $section = 152 + $optionalSize
    Set-U32 $bytes ($section + 8) 1024
    Set-U32 $bytes ($section + 12) 0x2000
    Set-U32 $bytes ($section + 16) 1024
    Set-U32 $bytes ($section + 20) 512
    if ($Managed) {
        Set-U32 $bytes (152 + $directory + 14 * 8) 0x2000
        Set-U32 $bytes (152 + $directory + 14 * 8 + 4) 72
        Set-U32 $bytes 512 72
        Set-U16 $bytes 516 2
        Set-U16 $bytes 518 5
        Set-U32 $bytes 520 0x2080
        Set-U32 $bytes 524 32
        Set-U32 $bytes 528 $Flags
        Set-U32 $bytes 640 0x424a5342
    }
    return ,$bytes
}

$manifestText = @'
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
 xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10">
 <Identity Name="Miyako.PsaReferenceControl" Publisher="CN=PsaReferenceControlDev"
  Version="1.1.14.0" ProcessorArchitecture="arm64" />
 <Properties><DisplayName>Reference control</DisplayName><PublisherDisplayName>Development</PublisherDisplayName><Logo>Assets/Logo.png</Logo></Properties>
 <Dependencies><TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.26100.0" MaxVersionTested="10.0.26100.0" /></Dependencies>
 <Applications><Application Id="App" Executable="Control.exe" EntryPoint="Reference.App">
  <uap:VisualElements DisplayName="Reference control" Description="Diagnostic" Square150x150Logo="Assets/Logo.png" Square44x44Logo="Assets/Logo.png" BackgroundColor="transparent" />
 </Application></Applications>
</Package>
'@

function New-TestPackage {
    param(
        [string]$Manifest = $manifestText,
        [hashtable]$Extra = @{},
        [string[]]$Omit = @(),
        [string]$Duplicate = '',
        [string]$Name = 'PsaReferenceControl.arm64.msix'
    )
    $directory = Join-Path $script:fixture ([Guid]::NewGuid().ToString('N'))
    New-Item -Path $directory -ItemType Directory | Out-Null
    $path = Join-Path $directory $Name
    $entries = [ordered]@{
        'AppxManifest.xml' = [Text.Encoding]::UTF8.GetBytes($Manifest)
        'AppxBlockMap.xml' = [Text.Encoding]::UTF8.GetBytes('<BlockMap />')
        '[Content_Types].xml' = [Text.Encoding]::UTF8.GetBytes('<Types />')
        'Control.exe' = New-TestPe
        'Assets/Logo.png' = [byte[]]@(1, 2, 3)
    }
    foreach ($entry in $Extra.Keys) { $entries[$entry] = $Extra[$entry] }
    $zip = [IO.Compression.ZipFile]::Open($path, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($name in $entries.Keys) {
            if ($name -in $Omit) { continue }
            $stream = $zip.CreateEntry($name).Open()
            try {
                $bytes = [byte[]]$entries[$name]
                $stream.Write($bytes, 0, $bytes.Length)
            } finally { $stream.Dispose() }
        }
        if ($Duplicate) {
            $stream = $zip.CreateEntry($Duplicate).Open()
            try { $stream.WriteByte(1) } finally { $stream.Dispose() }
        }
    } finally { $zip.Dispose() }
    return $path
}
function Get-TestPlanArgs([string]$Path) {
    return @{
        PackagePath = $Path
        ExpectedSha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        OutputDirectory = Join-Path $script:fixture ('signed-' + [Guid]::NewGuid().ToString('N'))
    }
}

# Keep fixtures in this checkout, never in the system temporary directory.
$script:fixture = Join-Path $PSScriptRoot ('.signing-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -Path $script:fixture -ItemType Directory | Out-Null
try {
    Test-Case 'ARM64 native and .NET Native-style payload accepted' {
        $path = New-TestPackage -Extra @{ 'Native/runtime.dll' = New-TestPe }
        $argsForPlan = Get-TestPlanArgs $path
        $plan = Get-ControlSigningPlan @argsForPlan
        Assert-Equal $plan.Identity.Name 'Miyako.PsaReferenceControl'
        Assert-Equal $plan.Identity.Publisher 'CN=PsaReferenceControlDev'
        Assert-Equal $plan.Identity.Version '1.1.14.0'
        Assert-Equal $plan.Identity.ProcessorArchitecture 'arm64'
        Assert-Equal $plan.InputSha256 $argsForPlan.ExpectedSha256
        Assert-Equal ([IO.Path]::GetFileName($plan.PackagePath)) 'PsaReferenceControl.arm64.msix'
        Assert-Equal $plan.SignedPackagePath (Join-Path $plan.OutputDirectory 'PsaReferenceControl.arm64.msix')
        Assert-Equal (Test-Path -LiteralPath $plan.OutputDirectory) $false
    }
    Test-Case 'managed ILONLY AnyCPU DLL and WINMD accepted' {
        $path = New-TestPackage -Extra @{
            'Managed/helper.dll' = New-TestPe 0x014c -Managed
            'Managed/api.winmd' = New-TestPe 0x014c -Managed
        }
        $null = Assert-ControlPackage $path
    }
    Test-Case 'package inspector exposes identity, XML, applications and device families' {
        $info = Assert-ControlPackage (New-TestPackage)
        Assert-Equal $info.Name $info.Identity.Name
        Assert-Equal $info.Publisher $info.Identity.Publisher
        Assert-Equal $info.Version $info.Identity.Version
        Assert-Equal $info.ProcessorArchitecture $info.Identity.ProcessorArchitecture
        Assert-Equal ($info.Manifest -is [Xml.XmlDocument]) $true
        Assert-Equal $info.Manifest.DocumentElement.LocalName 'Package'
        Assert-Equal ($info.Dependencies -is [array]) $true
        Assert-Equal $info.Dependencies.Count 0
        Assert-Equal ($info.TargetDeviceFamilies -is [array]) $true
        Assert-Equal $info.TargetDeviceFamilies.Count 1
        Assert-Equal $info.TargetDeviceFamilies[0].Name 'Windows.Universal'
        Assert-Equal $info.TargetDeviceFamilies[0].MinVersion '10.0.26100.0'
        Assert-Equal $info.TargetDeviceFamilies[0].MaxVersionTested '10.0.26100.0'
        Assert-Equal ($info.Applications -is [array]) $true
        Assert-Equal $info.Applications.Count 1
        Assert-Equal $info.Applications[0].Id 'App'
        Assert-Equal $info.Applications[0].Executable 'Control.exe'
        Assert-Equal $info.Applications[0].EntryPoint 'Reference.App'
        Assert-Equal $info.IsSigned $false
        Assert-Equal $info.PeFileCount 1
    }
    Test-Case 'external framework declarations returned in order without requiring embedded dependencies' {
        $xml = $manifestText.Replace('<Dependencies>', @'
<Dependencies>
 <PackageDependency Name="Microsoft.NET.Native.Framework.2.2" Publisher="CN=Microsoft Corporation" MinVersion="2.2.29512.0" />
 <PackageDependency Name="Microsoft.NET.Native.Runtime.2.2" Publisher="CN=Microsoft Corporation" MinVersion="2.2.28604.0" ProcessorArchitecture="arm64" />
'@)
        $info = Assert-ControlPackage (New-TestPackage -Manifest $xml)
        Assert-Equal ($info.Dependencies -is [array]) $true
        Assert-Equal $info.Dependencies.Count 2
        Assert-Equal $info.Dependencies[0].Name 'Microsoft.NET.Native.Framework.2.2'
        Assert-Equal $info.Dependencies[0].Publisher 'CN=Microsoft Corporation'
        Assert-Equal $info.Dependencies[0].MinVersion '2.2.29512.0'
        Assert-Equal $info.Dependencies[0].ProcessorArchitecture ''
        Assert-Equal $info.Dependencies[1].Name 'Microsoft.NET.Native.Runtime.2.2'
        Assert-Equal $info.Dependencies[1].ProcessorArchitecture 'arm64'
        Assert-Equal $info.PeFileCount 1
    }
    Test-Case 'single framework declaration remains an array' {
        $xml = $manifestText.Replace('<Dependencies>', '<Dependencies><PackageDependency Name="Framework" />')
        $info = Assert-ControlPackage (New-TestPackage -Manifest $xml)
        Assert-Equal ($info.Dependencies -is [array]) $true
        Assert-Equal $info.Dependencies.Count 1
        Assert-Equal $info.Dependencies[0].Name 'Framework'
        Assert-Equal $info.Dependencies[0].Publisher ''
        Assert-Equal $info.Dependencies[0].MinVersion ''
        Assert-Equal $info.Dependencies[0].ProcessorArchitecture ''
    }
    Test-Case 'signed inspection exposes signature presence without claiming trust' {
        $path = New-TestPackage -Extra @{ 'AppxSignature.p7x' = [byte[]]@(1) }
        $info = Assert-ControlPackage $path -SignatureState Signed
        Assert-Equal $info.IsSigned $true
        Assert-Equal $info.Identity.Name 'Miyako.PsaReferenceControl'
    }
    foreach ($machine in @(0x014c, 0x8664, 0x01c0, 0x01c4, 0xa641)) {
        Test-Case ('native PE 0x{0:x4} rejected' -f $machine) {
            $path = New-TestPackage -Extra @{ 'Native/helper.dll' = New-TestPe $machine }
            Expect-Failure { Assert-ControlPackage $path } 'Unsupported PE architecture'
        }
    }
    foreach ($flags in @(0, 2, 3, 0x20001, 0x11)) {
        Test-Case ('I386 non-AnyCPU CLR flags 0x{0:x} rejected' -f $flags) {
            Expect-Failure { Assert-ControlPe (New-TestPe 0x014c -Managed -Flags $flags) 'mixed.dll' } 'Unsupported PE architecture'
        }
    }
    Test-Case 'managed x64 rejected even with ILONLY' {
        Expect-Failure { Assert-ControlPe (New-TestPe 0x8664 -Managed) 'x64.dll' } 'Unsupported PE architecture'
    }
    Test-Case 'I386 managed-native image rejected' {
        $bytes = New-TestPe 0x014c -Managed
        Set-U32 $bytes 576 0x2100
        Set-U32 $bytes 580 32
        Expect-Failure { Assert-ControlPe $bytes 'ready-to-run.dll' } 'Unsupported PE architecture'
    }
    Test-Case 'extensionless x64 PE cannot evade inspection' {
        $path = New-TestPackage -Extra @{ 'runtime/payload.data' = New-TestPe 0x8664 }
        Expect-Failure { Assert-ControlPackage $path } 'Unsupported PE architecture'
    }
    Test-Case 'non-PE DLL rejected' {
        $path = New-TestPackage -Extra @{ 'bad.dll' = [byte[]]@(1, 2, 3) }
        Expect-Failure { Assert-ControlPackage $path } 'missing DOS header'
    }
    Test-Case 'truncated DOS header rejected' {
        Expect-Failure { Assert-ControlPe ([byte[]]::new(63)) 'short.dll' } 'missing DOS header'
    }
    Test-Case 'out-of-range PE pointer rejected' {
        $bytes = New-TestPe
        Set-U32 $bytes 0x3c 0x7fffffff
        Expect-Failure { Assert-ControlPe $bytes 'bad.dll' } 'out-of-range PE header'
    }
    Test-Case 'invalid PE signature rejected' {
        $bytes = New-TestPe
        $bytes[128] = 0
        Expect-Failure { Assert-ControlPe $bytes 'bad.dll' } 'PE header'
    }
    Test-Case 'truncated optional header rejected' {
        $bytes = New-TestPe
        Set-U16 $bytes 148 2
        Expect-Failure { Assert-ControlPe $bytes 'bad.dll' } 'short optional header'
    }
    Test-Case 'out-of-range CLR directory rejected' {
        $bytes = New-TestPe 0x014c -Managed
        Set-U32 $bytes (152 + 96 + 14 * 8) 0x9000
        Expect-Failure { Assert-ControlPe $bytes 'bad.dll' } 'unmapped CLR data'
    }
    Test-Case 'short CLR directory rejected' {
        $bytes = New-TestPe 0x014c -Managed
        Set-U32 $bytes (152 + 96 + 14 * 8 + 4) 16
        Expect-Failure { Assert-ControlPe $bytes 'bad.dll' } 'short CLR header'
    }
    Test-Case 'invalid CLR metadata rejected' {
        $bytes = New-TestPe 0x014c -Managed
        $bytes[640] = 0
        Expect-Failure { Assert-ControlPe $bytes 'bad.dll' } 'metadata signature'
    }
    foreach ($replacement in @(
        @('Miyako.PsaReferenceControl', 'Miyako.PsaFeasibility', 'Name'),
        @('CN=PsaReferenceControlDev', 'CN=OtherPublisher', 'Publisher'),
        @('1.1.14.0', '1.1.13.0', 'Version'),
        @('ProcessorArchitecture="arm64"', 'ProcessorArchitecture="x64"', 'ProcessorArchitecture'),
        @('ProcessorArchitecture="arm64"', 'ProcessorArchitecture="arm"', 'ProcessorArchitecture'),
        @('ProcessorArchitecture="arm64"', 'ProcessorArchitecture="neutral"', 'ProcessorArchitecture')
    )) {
        Test-Case "wrong manifest $($replacement[2]) rejected ($($replacement[1]))" {
            $path = New-TestPackage -Manifest $manifestText.Replace($replacement[0], $replacement[1])
            Expect-Failure { Assert-ControlPackage $path } "Unexpected Identity $($replacement[2])"
        }
    }
    Test-Case 'duplicate Identity rejected' {
        $xml = $manifestText.Replace('<Properties>', '<Identity Name="Other" /><Properties>')
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'exactly one package Identity'
    }
    foreach ($property in @('ResourcePackage', 'Framework')) {
        Test-Case "$property rejected" {
            $xml = $manifestText.Replace('<Properties>', "<Properties><$property>true</$property>")
            Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'non-resource main package'
        }
    }
    Test-Case 'ResourceId rejected' {
        $xml = $manifestText.Replace('ProcessorArchitecture="arm64"', 'ProcessorArchitecture="arm64" ResourceId="en-US"')
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'Resource packages'
    }
    Test-Case 'optional package rejected' {
        $xml = $manifestText.Replace('<Dependencies>', '<Dependencies><MainPackageDependency Name="Other" />')
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'Optional packages'
    }
    Test-Case 'full-trust desktop app rejected' {
        $xml = $manifestText.Replace('Reference.App', 'Windows.FullTrustApplication')
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'UWP EntryPoint'
    }
    Test-Case 'missing application rejected' {
        $xml = $manifestText -replace '(?s)<Applications>.*?</Applications>', ''
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'missing Application'
    }
    Test-Case 'missing target device family rejected' {
        $xml = $manifestText -replace '<TargetDeviceFamily [^>]*/>', ''
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'missing TargetDeviceFamily'
    }
    Test-Case 'incorrect manifest namespace rejected' {
        $xml = $manifestText.Replace('foundation/windows10', 'foundation/windows8')
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'UWP Package manifest'
    }
    Test-Case 'XML DTD rejected without entity expansion' {
        $xml = '<!DOCTYPE Package [<!ENTITY x "test">]>' + $manifestText
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest $xml) } 'DTD'
    }
    Test-Case 'malformed manifest rejected' {
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Manifest '<Package>') } 'Load|XML'
    }
    foreach ($file in @('AppxManifest.xml', 'AppxBlockMap.xml', '[Content_Types].xml', 'Control.exe')) {
        Test-Case "missing $file rejected" {
            Expect-Failure { Assert-ControlPackage (New-TestPackage -Omit $file) } 'missing'
        }
    }
    Test-Case 'empty declared executable rejected' {
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Extra @{ 'Control.exe' = [byte[]]::new(0) }) } 'missing declared executable'
    }
    Test-Case 'already signed input rejected, even empty signature' {
        Expect-Failure { Assert-ControlPackage (New-TestPackage -Extra @{ 'AppxSignature.p7x' = [byte[]]::new(0) }) } 'already signed'
    }
    Test-Case 'signed-output check requires signature' {
        Expect-Failure { Assert-ControlPackage (New-TestPackage) -SignatureState Signed } 'did not produce'
    }
    foreach ($name in @('extra.appx', 'extra.msix', 'AppxMetadata/AppxBundleManifest.xml')) {
        Test-Case "bundle or embedded $name rejected" {
            Expect-Failure { Assert-ControlPackage (New-TestPackage -Extra @{ $name = [byte[]]@(1) }) } 'single main MSIX'
        }
    }
    foreach ($name in @('AppxManifest.xml', 'APPXMANIFEST.XML')) {
        Test-Case "duplicate $name rejected" {
            Expect-Failure { Assert-ControlPackage (New-TestPackage -Duplicate $name) } 'Duplicate ZIP entry'
        }
    }
    foreach ($name in @('../escape.dll', '/root.dll', 'bad\file.dll', 'C:drive.dll', 'bad./file.dll')) {
        Test-Case "unsafe ZIP entry $name rejected" {
            Expect-Failure { Assert-ControlPackage (New-TestPackage -Extra @{ $name = [byte[]]@(1) }) } 'Invalid ZIP entry'
        }
    }
    $valid = New-TestPackage
    foreach ($parameter in @('PackagePath', 'ExpectedSha256', 'OutputDirectory')) {
        Test-Case "missing $parameter rejected by script without Windows/certificate access" {
            $arguments = Get-TestPlanArgs $valid
            $arguments.Remove($parameter)
            Expect-Failure { & $signScript @arguments } "$parameter is required"
        }
    }
    foreach ($hash in @('abc', ('z' * 64), ('a' * 63), ('a' * 65), (('a' * 64) + "`n"))) {
        Test-Case "invalid hash length/characters rejected ($($hash.Length))" {
            $arguments = Get-TestPlanArgs $valid
            $arguments.ExpectedSha256 = $hash
            Expect-Failure { & $signScript @arguments } 'exactly 64 hexadecimal'
        }
    }
    Test-Case 'incorrect exact input hash rejected by script' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.ExpectedSha256 = '0' * 64
        Expect-Failure { & $signScript @arguments } 'SHA256 does not match'
        Assert-Equal (Test-Path -LiteralPath $arguments.OutputDirectory) $false
    }
    Test-Case 'lowercase SHA256 accepted and normalized in plan' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.ExpectedSha256 = $arguments.ExpectedSha256.ToLowerInvariant()
        Assert-Equal (Get-ControlSigningPlan @arguments).InputSha256 $arguments.ExpectedSha256.ToUpperInvariant()
    }
    Test-Case 'new output directory accepts a trailing separator' {
        $arguments = Get-TestPlanArgs $valid
        $expectedDirectory = $arguments.OutputDirectory
        $arguments.OutputDirectory += [IO.Path]::DirectorySeparatorChar
        Assert-Equal (Get-ControlSigningPlan @arguments).OutputDirectory $expectedDirectory
    }
    Test-Case 'wrong input filename rejected' {
        $arguments = Get-TestPlanArgs (New-TestPackage -Name 'Miyako.PsaFeasibility.msix')
        Expect-Failure { & $signScript @arguments } 'must name PsaReferenceControl'
    }
    Test-Case 'old APPX basename rejected by sign-only interface' {
        $arguments = Get-TestPlanArgs (New-TestPackage -Name 'PsaReferenceControl.arm64.appx')
        Expect-Failure { & $signScript @arguments } 'must name PsaReferenceControl.arm64.msix'
    }
    Test-Case 'missing input rejected' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.PackagePath = Join-Path $script:fixture 'missing.msix'
        Expect-Failure { & $signScript @arguments } 'not a file'
    }
    Test-Case 'directory input rejected' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.PackagePath = $script:fixture
        Expect-Failure { & $signScript @arguments } 'not a file'
    }
    Test-Case 'existing output directory rejected without modification' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.OutputDirectory = Split-Path $valid -Parent
        Expect-Failure { & $signScript @arguments } 'must not already exist'
        Assert-Equal (Get-FileHash -LiteralPath $valid).Hash $arguments.ExpectedSha256
    }
    Test-Case 'existing output file and input-as-output rejected' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.OutputDirectory = $valid
        Expect-Failure { & $signScript @arguments } 'must not already exist'
    }
    Test-Case 'missing output parent rejected' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.OutputDirectory = Join-Path $script:fixture 'missing-parent/output'
        Expect-Failure { & $signScript @arguments } 'parent of OutputDirectory'
    }
    Test-Case 'non-filesystem paths rejected' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.OutputDirectory = 'Variable:controlOutput'
        Expect-Failure { & $signScript @arguments } 'Only FileSystem'
    }
    Test-Case 'non-ZIP input rejected' {
        $directory = Join-Path $script:fixture 'not-a-zip'
        New-Item -Path $directory -ItemType Directory | Out-Null
        $path = Join-Path $directory 'PsaReferenceControl.arm64.msix'
        [IO.File]::WriteAllText($path, 'not a ZIP')
        $arguments = Get-TestPlanArgs $path
        Expect-Failure { & $signScript @arguments } 'central directory|archive'
    }
    Test-Case 'pinned absolute x64 SDK tool accepted by portable plan' {
        $toolDirectory = Join-Path $script:fixture '10.0.26100.0/x64'
        New-Item -Path $toolDirectory -ItemType Directory | Out-Null
        $script:toolPath = Join-Path $toolDirectory 'signtool.exe'
        [IO.File]::WriteAllBytes($script:toolPath, (New-TestPe 0x8664))
        $arguments = Get-TestPlanArgs $valid
        $arguments.SignToolPath = $script:toolPath
        Assert-Equal (Get-ControlSigningPlan @arguments).SignToolPath $script:toolPath
    }
    foreach ($tool in @('', 'signtool.exe', '10.0.26100.0/x64/signtool.exe')) {
        Test-Case "empty or relative tool path rejected ('$tool')" {
            $arguments = Get-TestPlanArgs $valid
            $arguments.SignToolPath = $tool
            Expect-Failure { & $signScript @arguments } 'SignToolPath'
        }
    }
    foreach ($suffix in @('10.0.22621.0/x64/signtool.exe', '10.0.26100.0/arm64/signtool.exe', '10.0.26100.0/x86/signtool.exe')) {
        Test-Case "unpinned tool path rejected ($suffix)" {
            $arguments = Get-TestPlanArgs $valid
            $arguments.SignToolPath = Join-Path $script:fixture $suffix
            Expect-Failure { & $signScript @arguments } 'absolute SDK 10.0.26100.0/x64'
        }
    }
    Test-Case 'missing pinned SDK tool rejected' {
        $arguments = Get-TestPlanArgs $valid
        $arguments.SignToolPath = Join-Path $script:fixture 'missing/10.0.26100.0/x64/signtool.exe'
        Expect-Failure { & $signScript @arguments } 'SignTool does not exist'
    }
    Test-Case 'ARM64 executable masquerading as x64 SDK tool rejected' {
        [IO.File]::WriteAllBytes($script:toolPath, (New-TestPe))
        $arguments = Get-TestPlanArgs $valid
        $arguments.SignToolPath = $script:toolPath
        Expect-Failure { & $signScript @arguments } 'SignTool must be x64'
    }
    Test-Case 'new-file writer never overwrites an existing file' {
        $path = Join-Path $script:fixture 'exclusive-write'
        Write-ControlNewFile $path ([byte[]]@(1))
        Expect-Failure { Write-ControlNewFile $path ([byte[]]@(2)) } 'exists'
        Assert-Equal ([IO.File]::ReadAllBytes($path)[0]) 1
    }
    if (-not $IsWindows) {
        Test-Case 'valid invocation fails on non-Windows before output or certificate creation' {
            $arguments = Get-TestPlanArgs $valid
            Expect-Failure { & $signScript @arguments } 'Actual signing requires Windows'
            Assert-Equal (Test-Path -LiteralPath $arguments.OutputDirectory) $false
        }
    }
    Write-Host "Passed $script:passed portable signing checks. No certificates created; no signing performed."
} finally {
    Remove-Item -LiteralPath $script:fixture -Recurse -Force
}
