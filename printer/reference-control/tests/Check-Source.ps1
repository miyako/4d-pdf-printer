[CmdletBinding()]
param([string]$ArchivePath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
$temp = Join-Path ([IO.Path]::GetTempPath()) "reference-source-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $args = @{ Destination = (Join-Path $temp 'fetch') }
    if ($ArchivePath) { $args.ArchivePath = $ArchivePath }
    $source = & (Join-Path $root 'Fetch-Source.ps1') @args
    $manifest = Join-Path $source 'PSASamples/UWP/CSharp/PrintSupportApp/Package.appxmanifest'
    $text = [IO.File]::ReadAllText($manifest).
        Replace('Name="dd5de805-d9bb-4785-88e9-399cbff2130e"', 'Name="Miyako.PsaReferenceControl"').
        Replace('Publisher="CN=Contoso"', 'Publisher="CN=PsaReferenceControlDev"')
    [IO.File]::WriteAllText($manifest, $text, [Text.UTF8Encoding]::new($true))
    $check = @{
        SourceRoot = $source
        InventoryPath = (Join-Path $temp 'fetch/original-files.json')
        ReportPath = (Join-Path $temp 'comparison.json')
    }
    & (Join-Path $root 'Check-Provenance.ps1') @check
    $report = Get-Content -LiteralPath $check.ReportPath -Raw | ConvertFrom-Json
    if ($report.changedFiles.Count -ne 1 -or $report.unchangedFileCount -lt 50) { throw 'Incomplete source comparison.' }
    [IO.File]::WriteAllText($manifest, $text.Replace('contoso-psa:print-to-pdf', 'test:changed'), [Text.UTF8Encoding]::new($true))
    $rejected = $false
    try { & (Join-Path $root 'Check-Provenance.ps1') @check }
    catch {
        if ($_.Exception.Message -notmatch 'differs beyond') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Undocumented static declaration change accepted.' }
    $badArchive = Join-Path $temp 'bad.tar.gz'
    [IO.File]::WriteAllText($badArchive, 'not the pinned archive')
    $rejected = $false
    try { & (Join-Path $root 'Fetch-Source.ps1') -ArchivePath $badArchive -Destination (Join-Path $temp 'bad') }
    catch {
        if ($_.Exception.Message -notmatch 'SHA256 mismatch') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Wrong archive hash accepted.' }
    Write-Host "Source pin, $($report.originalFileCount) file hashes, identity-only adaptation and negative tests passed."
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
