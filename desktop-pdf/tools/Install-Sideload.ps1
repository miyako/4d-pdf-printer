# Installs a signed sideload build of Generic Print to PDF for the current user.
# Works in the inbox Windows PowerShell 5.1 and in PowerShell 7. Store installs need none of this.
#   1. trusts the package's signing certificate (one UAC prompt, only if not yet trusted);
#   2. fetches the Microsoft VC++ desktop framework only if it is missing;
#   3. installs or updates the package and waits for both printer queues.
[CmdletBinding()]
param(
    [string]$Package,
    [string]$Certificate,
    [int]$QueueTimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Single([string]$Filter, [string]$What) {
    $found = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter $Filter -File)
    if ($found.Count -ne 1) { throw "Expected exactly one $What next to this script; pass -$What explicitly." }
    $found[0].FullName
}

$os = [Environment]::OSVersion.Version
if ($os.Build -lt 26100) { throw "Windows 11 24H2 (build 26100) or later is required; this is build $($os.Build)." }
$arch = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
if ($arch -notin 'x64', 'arm64') { throw "Unsupported Windows architecture $arch." }

if (-not $Package) {
    $Package = @(Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in '.msixbundle', '.msix' } |
        Sort-Object { $_.Extension -ne '.msixbundle' } | Select-Object -First 1 | ForEach-Object FullName)
    if (-not $Package) { throw 'No .msixbundle or .msix next to this script; pass -Package.' }
    $Package = "$Package"
}
$Package = (Resolve-Path -LiteralPath $Package).Path
$signature = Get-AuthenticodeSignature -LiteralPath $Package
if (-not $signature.SignerCertificate) { throw "$Package is not signed." }
$signer = $signature.SignerCertificate

Step "Certificate $($signer.Subject) ($($signer.Thumbprint))"
$trusted = @(Get-ChildItem Cert:\LocalMachine\TrustedPeople, Cert:\LocalMachine\Root |
    Where-Object Thumbprint -eq $signer.Thumbprint).Count -gt 0
if ($trusted) {
    Write-Host '    already trusted'
} else {
    if (-not $Certificate) { $Certificate = Single '*.cer' 'Certificate' }
    $certPath = (Resolve-Path -LiteralPath $Certificate).Path
    $file = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certPath)
    if ($file.Thumbprint -ne $signer.Thumbprint) { throw "$Certificate is not the certificate that signed $Package." }
    Write-Host '    adding it to Local Machine > Trusted People (approve the UAC prompt)'
    $command = "Import-Certificate -FilePath '$($certPath -replace "'", "''")' -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null"
    $process = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -Wait -PassThru `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $command)
    if ($process.ExitCode -ne 0) { throw "Certificate import failed ($($process.ExitCode))." }
}

$dependencies = @()
$framework = 'Microsoft.VCLibs.140.00.UWPDesktop'
$present = @(Get-AppxPackage -Name $framework | Where-Object {
    $_.Architecture.ToString().ToLowerInvariant() -eq $arch -and [version]$_.Version -ge [version]'14.0.30704.0' })
Step "Framework $framework ($arch)"
if ($present.Count -gt 0) {
    Write-Host "    present ($($present[0].Version))"
} else {
    $url = "https://aka.ms/Microsoft.VCLibs.$arch.14.00.Desktop.appx"
    $download = Join-Path ([IO.Path]::GetTempPath()) "Microsoft.VCLibs.$arch.14.00.Desktop.appx"
    Write-Host "    downloading $url"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $url -OutFile $download -UseBasicParsing
    $dependencies += $download
}

Step "Installing $([IO.Path]::GetFileName($Package))"
if ($dependencies.Count) { Add-AppxPackage -Path $Package -DependencyPath $dependencies -ForceApplicationShutdown }
else { Add-AppxPackage -Path $Package -ForceApplicationShutdown }

$installed = $null
$publisher = $signer.Subject
foreach ($candidate in @(Get-AppxPackage | Where-Object { $_.Publisher -eq $publisher -and $_.Architecture.ToString().ToLowerInvariant() -eq $arch })) {
    if (Test-Path -LiteralPath (Join-Path $candidate.InstallLocation 'Config\IntegrationPdc.xml')) { $installed = $candidate }
}
if (-not $installed) { throw 'Package installed but could not be found for this user.' }
Write-Host "    $($installed.PackageFullName)"

Step 'Waiting for the printer queues'
$deadline = (Get-Date).AddSeconds($QueueTimeoutSeconds)
do {
    $queues = @(Get-Printer | Where-Object { $_.PortName -like "$($installed.PackageFamilyName)_desktop-pdf:*" })
    if ($queues.Count -ge 2) { break }
    Start-Sleep -Seconds 2
} while ((Get-Date) -lt $deadline)
foreach ($queue in $queues) { Write-Host "    $($queue.Name)" }
if ($queues.Count -lt 2) { throw "Only $($queues.Count) of 2 queues appeared within $QueueTimeoutSeconds s; sign out and in, then run VerifyInstall.ps1." }

$verify = Join-Path $PSScriptRoot 'VerifyInstall.ps1'
$pwsh = Get-Command pwsh -CommandType Application -ErrorAction SilentlyContinue
if ((Test-Path -LiteralPath $verify) -and $pwsh) {
    Step 'Verifying'
    & $pwsh.Source -NoProfile -ExecutionPolicy Bypass -File $verify -PackageName $installed.Name
    if ($LASTEXITCODE -ne 0) { throw "VerifyInstall reported a problem (exit $LASTEXITCODE)." }
}
Step 'Done. Print to "Generic Print to PDF" to choose a file name; use generic-pdf.exe with the (Silent) queue for automation.'
