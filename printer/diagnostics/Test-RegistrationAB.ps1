# Hosted-only static registration discriminator. Never run on a user's machine.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ProductDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted' -or
    [Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
    $PSVersionTable.PSEdition -cne 'Desktop' -or $PSVersionTable.PSVersion.Major -ne 5 -or
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -ine 'Arm64') {
    throw 'Requires local Windows PowerShell 5.1 on a disposable GitHub-hosted ARM64 Windows job.'
}
$os = Get-CimInstance Win32_OperatingSystem -Property BuildNumber, ProductType
if ($os.ProductType -ne 1 -or [int]$os.BuildNumber -lt 26100) { throw 'Requires Windows 11 build 26100+.' }
$user = [Security.Principal.WindowsIdentity]::GetCurrent()
try {
    if (-not ([Security.Principal.WindowsPrincipal]::new($user)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Requires an elevated disposable job.' }
} finally { $user.Dispose() }
Import-Module Appx -ErrorAction Stop
Import-Module PrintManagement -ErrorAction Stop
Add-Type -AssemblyName System.IO.Compression.FileSystem

$names = @('PSA Diagnostic OXPS (Save As)', 'PSA Diagnostic Capture (no file UI)')
$identities = @('Miyako.PsaFeasibility', 'Miyako.PsaRegistrationDiagnostic')
$publisher = 'CN=PsaFeasibilityDev'
foreach ($identity in $identities) {
    if (@(Get-AppxPackage -AllUsers -Name $identity).Count) { throw "Preexisting identity: $identity" }
}
$initialPrinters = @(Get-Printer | ForEach-Object Name)
if (@($initialPrinters | Where-Object { $_ -cin $names }).Count) { throw 'Preexisting diagnostic queue.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory must be new.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$output = (Get-Item -LiteralPath $OutputDirectory).FullName
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64'
$makeappx = Join-Path $sdk 'makeappx.exe'
$signtool = Join-Path $sdk 'signtool.exe'
foreach ($tool in @($makeappx, $signtool)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing SDK 26100 tool: $tool" }
}
$report = [ordered]@{
    schemaVersion = 1
    experiment = 'native-static-registration-pdc-ab'
    build = [int]$os.BuildNumber
    image = $env:ImageVersion
    powershell = $PSVersionTable.PSVersion.ToString()
    initialPrinters = $initialPrinters
    variants = @()
    conclusion = 'not-completed'
    error = $null
    trustRemoved = $false
    keyRemoved = $false
}
$certificate = $null
$trustCreated = $false

function Invoke-Tool([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "SDK tool failed with exit code $LASTEXITCODE" }
}

function Invoke-Variant([string]$Label, [string]$Directory, [string]$Package) {
    [xml]$manifest = Get-Content -LiteralPath (Join-Path $Directory 'AppxManifest.xml') -Raw
    $identity = $manifest.Package.Identity
    if ($identity.Name -cnotin $identities -or $identity.Publisher -cne $publisher -or
        $identity.ProcessorArchitecture -cne 'arm64') { throw 'Unexpected diagnostic identity.' }
    if (@(Get-AppxPackage -AllUsers -Name $identity.Name).Count) { throw 'Variant identity is not clean.' }
    $before = @(Get-Printer | ForEach-Object Name)
    if (@($before | Where-Object { $_ -cin $names }).Count) { throw 'Stale diagnostic queues before variant.' }
    $result = [ordered]@{
        label = $Label
        identity = $identity.Name
        version = $identity.Version
        sha256 = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash
        pdcSha256 = (Get-FileHash -LiteralPath (Join-Path $Directory 'Config/DiagnosticPdc.xml') -Algorithm SHA256).Hash
        installed = $null
        before = $before
        after = @()
        observations = 0
        queues = 'not-observed'
        cleanup = 'not-attempted'
    }
    $report.variants += $result
    $attempted = $false
    try {
        $attempted = $true
        Add-AppxPackage -Path $Package -ErrorAction Stop
        $registered = @(Get-AppxPackage -Name $identity.Name)
        if ($registered.Count -ne 1 -or $registered[0].Publisher -cne $publisher -or
            [version]$registered[0].Version -ne [version]$identity.Version -or
            $registered[0].Architecture.ToString() -ine 'Arm64' -or
            $registered[0].Status.ToString() -ine 'Ok') { throw 'Installed variant identity/status mismatch.' }
        $result.installed = [ordered]@{
            name = $registered[0].Name
            version = $registered[0].Version.ToString()
            architecture = $registered[0].Architecture.ToString()
            status = $registered[0].Status.ToString()
        }
        $installedManifest = Get-AppxPackageManifest -Package $registered[0].PackageFullName
        $installedManifest.Save((Join-Path $output "$Label-installed.xml"))
        $installedPdc = Join-Path $registered[0].InstallLocation 'Config/DiagnosticPdc.xml'
        if ((Get-FileHash -LiteralPath $installedPdc -Algorithm SHA256).Hash -cne $result.pdcSha256) {
            throw 'Installed PDC differs from variant.'
        }
        for ($i = 1; $i -le 6; $i++) {
            Start-Sleep -Seconds 10
            $result.after = @(Get-Printer | ForEach-Object Name)
            $result.observations = $i
            if (@($names | Where-Object { $_ -cin $result.after }).Count -eq 2) { break }
        }
        $count = @($names | Where-Object { $_ -cin $result.after }).Count
        $result.queues = @('absent', 'some', 'present')[$count]
    } finally {
        if ($attempted) {
            # Absence was checked globally before deployment. Never remove by wildcard or name alone.
            $owned = @(Get-AppxPackage -AllUsers -Name $identity.Name)
            foreach ($packageInfo in $owned) {
                if ($packageInfo.Name -cne $identity.Name -or $packageInfo.Publisher -cne $publisher -or
                    [version]$packageInfo.Version -ne [version]$identity.Version -or
                    $packageInfo.Architecture.ToString() -ine 'Arm64') { throw 'Cleanup identity mismatch.' }
                Remove-AppxPackage -Package $packageInfo.PackageFullName -AllUsers -ErrorAction Stop
            }
            if (@(Get-AppxPackage -AllUsers -Name $identity.Name).Count) { throw 'Owned variant remains registered.' }
            $remaining = @(Get-Printer | ForEach-Object Name)
            if (@($remaining | Where-Object { $_ -cin $names }).Count) { throw 'Variant queues remain after removal; stopping comparison.' }
            $result.cleanup = 'exact-owned-package-and-queues-absent'
        }
    }
}

try {
    $certificate = New-SelfSignedCertificate -Type Custom -Subject $publisher -KeyUsage DigitalSignature `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    $trustPath = "Cert:\LocalMachine\TrustedPeople\$($certificate.Thumbprint)"
    if (Test-Path -LiteralPath $trustPath) { throw 'Unexpected existing certificate trust.' }
    $publicPath = Join-Path $output 'diagnostic.cer'
    Export-Certificate -Cert $certificate -FilePath $publicPath | Out-Null
    $trustCreated = $true
    Import-Certificate -FilePath $publicPath -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
    $baseline = Join-Path $output 'product.msix'
    Copy-Item -LiteralPath (Join-Path $ProductDirectory 'PsaDiagnostic.msix') -Destination $baseline
    Invoke-Tool $signtool @('sign', '/fd', 'SHA256', '/sha1', $certificate.Thumbprint, '/s', 'My', $baseline)
    Invoke-Variant 'product-baseline' (Join-Path $ProductDirectory 'package') $baseline
    if ($report.variants[0].queues -ne 'absent') {
        $report.conclusion = 'product-baseline-registers-here-no-variant-causation-test'
    } else {
        $stage = Join-Path $output 'isolated-stage'
        Copy-Item -LiteralPath (Join-Path $ProductDirectory 'package') -Destination $stage -Recurse
        [xml]$manifest = Get-Content -LiteralPath (Join-Path $stage 'AppxManifest.xml') -Raw
        $manifest.Package.Identity.Name = $identities[1]
        $manifest.Save((Join-Path $stage 'AppxManifest.xml'))
        $isolated = Join-Path $output 'isolated-baseline.msix'
        Invoke-Tool $makeappx @('pack', '/d', $stage, '/p', $isolated, '/o')
        Invoke-Tool $signtool @('sign', '/fd', 'SHA256', '/sha1', $certificate.Thumbprint, '/s', 'My', $isolated)
        Invoke-Variant 'isolated-baseline' $stage $isolated
        if ($report.variants[1].queues -ne 'absent') {
            $report.conclusion = 'identity-isolation-changed-result-no-pdc-causation-test'
        } else {
            $pin = '652343e4980cc962fb099496125cf749aee51c71'
            $base = "https://raw.githubusercontent.com/microsoft/print-oem-samples/$pin"
            $pdc = Join-Path $stage 'Config/DiagnosticPdc.xml'
            Invoke-WebRequest "$base/PSASamples/UWP/CSharp/PrintSupportApp/Config/PrinterPdc.xml" `
                -UseBasicParsing -OutFile $pdc
            if ((Get-FileHash -LiteralPath $pdc -Algorithm SHA256).Hash -ine
                '0e9e9d7ee7efaf44bfff45834159dacb506dacfaee0c4f917017a8b11356394c') { throw 'Pinned PDC hash mismatch.' }
            Invoke-WebRequest "$base/LICENSE" -UseBasicParsing -OutFile (Join-Path $output 'Microsoft-LICENSE.txt')
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../reference-control/THIRD-PARTY-NOTICES.txt') `
                -Destination (Join-Path $output 'PDC-NOTICE.txt')
            # Fresh Add after verified exact cleanup, higher version avoids stale same-version byte conflicts.
            $manifest.Package.Identity.Version = '0.1.2.0'
            $manifest.Save((Join-Path $stage 'AppxManifest.xml'))
            $variant = Join-Path $output 'pdc-only.msix'
            Invoke-Tool $makeappx @('pack', '/d', $stage, '/p', $variant, '/o')
            Invoke-Tool $signtool @('sign', '/fd', 'SHA256', '/sha1', $certificate.Thumbprint, '/s', 'My', $variant)
            Invoke-Variant 'pdc-only' $stage $variant
            $report.conclusion = if ($report.variants[2].queues -eq 'present') {
                'pdc-only-positive-requires-product-confirmation'
            } else { 'pdc-only-not-sufficient-in-this-fresh-add-experiment' }
        }
    }
} catch {
    $report.error = [ordered]@{
        type = $_.Exception.GetType().FullName
        hresult = '0x{0:X8}' -f ($_.Exception.HResult -band 0xffffffffL)
    }
    throw
} finally {
    try {
        if ($null -ne $certificate) {
            if ($trustCreated -and (Test-Path -LiteralPath $trustPath)) {
                Remove-Item -LiteralPath $trustPath
            }
            $report.trustRemoved = -not (Test-Path -LiteralPath $trustPath)
            $keyPath = "Cert:\CurrentUser\My\$($certificate.Thumbprint)"
            if (Test-Path -LiteralPath $keyPath) { Remove-Item -LiteralPath $keyPath -DeleteKey }
            $report.keyRemoved = -not (Test-Path -LiteralPath $keyPath)
        }
    } finally {
        $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'registration-ab.json') -Encoding UTF8
    }
}
