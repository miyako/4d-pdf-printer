[CmdletBinding()]
param([Parameter(Mandatory)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Windows desktop required; no simulation fallback.' }
. (Join-Path $PSScriptRoot 'Invoke-Body.ps1')
$packages = @(Get-AppxPackage -Name 'Miyako.PsaActivationSmoke')
if ($packages.Count -ne 1 -or $packages[0].Version -ne '0.1.1.0' -or
    $packages[0].Publisher -cne 'CN=DesktopPdfPrototypeDev') { throw 'Expected exactly the separately installed smoke 0.1.1.0 package.' }
$package = $packages[0]
if ($package.Architecture.ToString() -ine [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()) {
    throw 'Smoke package must match native OS architecture.'
}
$queues = @(Get-Printer -Name '4D PDF Activation Smoke (No PDF)' -ErrorAction Stop)
if ($queues.Count -ne 1) { throw 'Expected exactly one fixed smoke queue.' }
$id = [Guid]::NewGuid().ToString('N')
$executable = Join-Path $package.InstallLocation 'SmokeObserver.exe'
Write-Host "One original synthetic page; testId=$id; expected failed/canceled print job; NO PDF."
Invoke-SmokeBody -Executable $executable -Arguments @('--observe', $id) -Evidence $EvidenceDirectory
