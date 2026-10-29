$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../Check-Package.ps1')
$root = Split-Path $PSScriptRoot
[xml]$manifest = Get-Content -LiteralPath (Join-Path $root 'package/AppxManifest.xml') -Raw
Assert-SmokeManifest $manifest 'x64'
[xml]$strings = Get-Content -LiteralPath (Join-Path $root 'package/Strings/en-US/Resources.resw') -Raw
if ($strings.root.data.value -cne '4D PDF Activation Smoke (No PDF)') { throw 'Source queue name mismatch.' }
$bad = [xml]$manifest.OuterXml
$bad.Package.Identity.Name = 'Miyako.PsaFeasibility'
$rejected = $false
try { Assert-SmokeManifest $bad 'x64' } catch { $rejected = $true }
if (-not $rejected) { throw 'case=identity isolation assertion: old package accepted' }
$bad = [xml]$manifest.OuterXml
$ns = [Xml.XmlNamespaceManager]::new($bad.NameTable)
$ns.AddNamespace('psa', 'http://schemas.microsoft.com/appx/manifest/printsupport/windows10/2')
$bad.SelectSingleNode('//psa:PrintSupportVirtualPrinter', $ns).SetAttribute('OutputFileTypes', 'pdf')
$rejected = $false
try { Assert-SmokeManifest $bad 'x64' } catch { $rejected = $true }
if (-not $rejected) { throw 'case=no Save As assertion: OutputFileTypes accepted' }
Get-ChildItem -LiteralPath $root -Filter '*.ps1' -Recurse | ForEach-Object {
    $tokens = $null; $errors = $null
    $null = [Management.Automation.Language.Parser]::ParseFile($_.FullName, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
}
Write-Host 'Smoke source manifest/name/isolation/PowerShell parse checks passed; no packaged behavior claim.'
