[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceRoot,
    [Parameter(Mandatory)][string]$InventoryPath,
    [Parameter(Mandatory)][string]$ReportPath,
    [switch]$AfterBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$inventory = @(Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json)
$manifestPath = 'PSASamples/UWP/CSharp/PrintSupportApp/Package.appxmanifest'
$changes = @()
$generated = @()
foreach ($file in $inventory) {
    $path = Join-Path $SourceRoot $file.path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing upstream file: $($file.path)" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -eq $file.sha256) { continue }
    if ($AfterBuild -and $file.path -eq 'PSASamples/XpsUtil/XpsUtil.h') {
        $header = [IO.File]::ReadAllText($path)
        if ($header -notmatch 'automatically generated|ALWAYS GENERATED') { throw 'Unexpected non-generated XpsUtil.h.' }
        $generated += [ordered]@{
            path = $file.path
            upstreamSha256 = $file.sha256
            generatedSha256 = $actual
            reason = 'Upstream MIDL HeaderFileName regenerates its checked-in generated header from unchanged XpsUtil.idl.'
        }
        continue
    }
    if ($file.path -ne $manifestPath) { throw "Unexpected upstream source change: $($file.path)" }
    $text = [IO.File]::ReadAllText($path)
    if ($text -notmatch 'Name="Miyako.PsaReferenceControl"' -or
        $text -notmatch 'Publisher="CN=PsaReferenceControlDev"') { throw 'Incorrect adapted identity.' }
    $original = $text.Replace('Name="Miyako.PsaReferenceControl"', 'Name="dd5de805-d9bb-4785-88e9-399cbff2130e"').
        Replace('Publisher="CN=PsaReferenceControlDev"', 'Publisher="CN=Contoso"')
    $encoding = [Text.UTF8Encoding]::new($true)
    $bytes = [byte[]]($encoding.GetPreamble() + $encoding.GetBytes($original))
    $restored = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    if ($restored -ne $file.sha256) { throw 'Manifest differs beyond the two documented identity attributes.' }
    $changes += [ordered]@{
        path = $file.path
        upstreamSha256 = $file.sha256
        adaptedSha256 = $actual
        changes = @('Identity.Name', 'Identity.Publisher')
    }
}
if ($changes.Count -ne 1) { throw 'Expected exactly one identity-adapted manifest.' }
[ordered]@{
    upstreamCommit = '652343e4980cc962fb099496125cf749aee51c71'
    originalFileCount = $inventory.Count
    unchangedFileCount = $inventory.Count - $changes.Count - $generated.Count
    changedFiles = $changes
    buildRegeneratedFiles = $generated
    staticDeclarations = 'unchanged'
    pdcPdrResourcesAssetsAndAuthoredCode = 'unchanged; MIDL-generated header separately recorded'
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding utf8
