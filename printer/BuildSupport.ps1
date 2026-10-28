Set-StrictMode -Version Latest

function Get-PrinterBuildPlan {
    param(
        [ValidateSet('x64', 'ARM64')][string]$Architecture = 'x64',
        [ValidateSet('2022', '2026')][string]$VisualStudioVersion = '2022',
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$HostArchitecture,
        [Parameter(Mandatory)][string]$Root
    )
    $target = $Architecture.ToLowerInvariant()
    $out = Join-Path $Root 'out'
    if ($target -eq 'arm64') { $out = Join-Path $out 'arm64' }
    $execution = if ($target -eq $HostArchitecture.ToLowerInvariant()) { 'native' }
        elseif ($target -eq 'x64') { 'emulated-x64' }
        else { 'not-run-cross-compile' }
    $componentSuffix = if ($target -eq 'arm64') { 'ARM64' } else { 'x86.x64' }
    [pscustomobject]@{
        Architecture = $target
        CMakePlatform = $(if ($target -eq 'arm64') { 'ARM64' } else { 'x64' })
        Out = $out
        Build = Join-Path $out 'build'
        Stage = Join-Path $out 'package'
        Package = Join-Path $out 'PsaDiagnostic.msix'
        SdkToolArchitecture = 'x64'
        TestExecution = $execution
        VisualStudioVersion = $VisualStudioVersion
        CMakeGenerator = $(if ($VisualStudioVersion -eq '2026') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' })
        VisualStudioRange = $(if ($VisualStudioVersion -eq '2026') { '[18.0,19.0)' } else { '[17.0,18.0)' })
        RequiredComponent = $(if ($VisualStudioVersion -eq '2026') {
            "Microsoft.VisualStudio.Component.VC.14.44.17.14.$componentSuffix"
        } else { "Microsoft.VisualStudio.Component.VC.Tools.$componentSuffix" })
    }
}

function Get-PrinterPeMachine {
    param([Parameter(Mandatory)][byte[]]$Bytes)
    if ($Bytes.Length -lt 64 -or [BitConverter]::ToUInt16($Bytes, 0) -ne 0x5a4d) {
        throw 'Invalid PE: missing DOS header.'
    }
    $offset = [BitConverter]::ToInt32($Bytes, 0x3c)
    if ($offset -lt 64 -or $offset -gt $Bytes.Length - 6 -or
        [BitConverter]::ToUInt32($Bytes, $offset) -ne 0x4550) {
        throw 'Invalid PE: missing or out-of-range PE header.'
    }
    [BitConverter]::ToUInt16($Bytes, $offset + 4)
}

function Assert-PrinterPeMachine {
    param(
        [Parameter(Mandatory)][byte[]]$Bytes,
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$Architecture,
        [Parameter(Mandatory)][string]$Name
    )
    $expected = if ($Architecture -eq 'ARM64') { 0xaa64 } else { 0x8664 }
    $actual = Get-PrinterPeMachine -Bytes $Bytes
    if ($actual -ne $expected) {
        throw ('PE architecture mismatch for {0}: expected {1} (0x{2:x4}), got 0x{3:x4}.' -f
            $Name, $Architecture, $expected, $actual)
    }
}

function Set-PrinterManifestArchitecture {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$Architecture
    )
    [xml]$manifest = Get-Content -LiteralPath $Source -Raw
    $manifest.Package.Identity.ProcessorArchitecture = $Architecture.ToLowerInvariant()
    $manifest.Save($Destination)
}

function Assert-PrinterManifest {
    param(
        [Parameter(Mandatory)][xml]$Manifest,
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$Architecture
    )
    if ($Manifest.Package.Identity.ProcessorArchitecture -cne $Architecture.ToLowerInvariant()) {
        throw 'Package manifest architecture does not match the requested target.'
    }
    if ($Manifest.Package.Identity.Version -ne '0.1.1.0') {
        throw 'Expected development package version 0.1.1.0.'
    }
}

function Assert-PrinterPackage {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$Architecture,
        [switch]$Archive
    )
    if (-not $Archive) {
        [xml]$manifest = Get-Content -LiteralPath (Join-Path $Path 'AppxManifest.xml') -Raw
        Assert-PrinterManifest -Manifest $manifest -Architecture $Architecture
        foreach ($name in @('PsaDiagnostic.exe', 'PsaTask.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $Path $name) -PathType Leaf)) {
                throw "Package missing $name."
            }
        }
        $binaries = @(Get-ChildItem -LiteralPath $Path -Recurse -File |
            Where-Object Extension -In @('.exe', '.dll'))
        foreach ($binary in $binaries) {
            Assert-PrinterPeMachine -Bytes ([IO.File]::ReadAllBytes($binary.FullName)) `
                -Architecture $Architecture -Name $binary.Name
        }
        return
    }
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entry = $zip.GetEntry('AppxManifest.xml')
        if ($null -eq $entry) { throw 'MSIX missing AppxManifest.xml.' }
        $reader = [IO.StreamReader]::new($entry.Open())
        try { [xml]$manifest = $reader.ReadToEnd() } finally { $reader.Dispose() }
        Assert-PrinterManifest -Manifest $manifest -Architecture $Architecture
        foreach ($name in @('PsaDiagnostic.exe', 'PsaTask.dll')) {
            if ($null -eq $zip.GetEntry($name)) { throw "MSIX missing $name." }
        }
        foreach ($entry in $zip.Entries) {
            if ([IO.Path]::GetExtension($entry.FullName) -notin @('.exe', '.dll')) { continue }
            $stream = $entry.Open()
            $buffer = [IO.MemoryStream]::new()
            try {
                $stream.CopyTo($buffer)
                Assert-PrinterPeMachine -Bytes $buffer.ToArray() -Architecture $Architecture `
                    -Name $entry.FullName
            } finally { $buffer.Dispose(); $stream.Dispose() }
        }
    } finally { $zip.Dispose() }
}
