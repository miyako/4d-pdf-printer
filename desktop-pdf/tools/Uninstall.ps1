# Removes Generic Print to PDF for the current user. Windows deletes both printer queues and the
# package's LocalCache (job records) with it. Printed PDFs in your own folders are not touched.
# Works in Windows PowerShell 5.1 and PowerShell 7.
[CmdletBinding()]
param(
    [string]$PackageName = 'Miyako.DesktopPdfIntegration',
    [switch]$RemoveCertificate
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$packages = @(Get-AppxPackage -Name $PackageName)
if ($packages.Count -eq 0) { Write-Host "$PackageName is not installed for this user." }
foreach ($package in $packages) {
    Write-Host "==> Removing $($package.PackageFullName)" -ForegroundColor Cyan
    $publisher = $package.Publisher
    Remove-AppxPackage -Package $package.PackageFullName
}
if ($RemoveCertificate) {
    if (-not $packages) { throw '-RemoveCertificate needs the installed package to identify its publisher.' }
    $command = "Get-ChildItem Cert:\LocalMachine\TrustedPeople | Where-Object Subject -eq '$($publisher -replace "'", "''")' | Remove-Item"
    Write-Host "==> Removing trusted certificate(s) $publisher (approve the UAC prompt)" -ForegroundColor Cyan
    $process = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -Wait -PassThru `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $command)
    if ($process.ExitCode -ne 0) { throw "Certificate removal failed ($($process.ExitCode))." }
}
