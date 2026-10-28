$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
[xml]$manifest = Get-Content (Join-Path $root 'package/AppxManifest.xml') -Raw
$ns = [System.Xml.XmlNamespaceManager]::new($manifest.NameTable)
$ns.AddNamespace('m', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
$ns.AddNamespace('psa', 'http://schemas.microsoft.com/appx/manifest/printsupport/windows10/2')
$ns.AddNamespace('uap10', 'http://schemas.microsoft.com/appx/manifest/uap/windows10/10')
$identity = $manifest.SelectSingleNode('/m:Package/m:Identity', $ns)
if ($identity.Version -ne '0.1.1.0' -or $identity.ProcessorArchitecture -ne 'x64') {
    throw 'Expected version 0.1.1.0 x64 source template; build generates the selected target.'
}
$application = $manifest.SelectSingleNode('//m:Application', $ns)
if ($application.GetAttribute('SupportsMultipleInstances', $ns.LookupNamespace('uap10')) -ne 'true') {
    throw 'SDK 26100 requires multi-instance support for virtual-printer workflow.'
}
$printers = $manifest.SelectNodes('//psa:PrintSupportVirtualPrinter', $ns)
if ($printers.Count -ne 2) { throw 'Expected two separate diagnostic endpoints.' }
if ($printers[0].OutputFileTypes -ne 'oxps') { throw 'Never label captured OXPS as PDF.' }
if ($printers[1].HasAttribute('OutputFileTypes')) { throw 'Automation probe must omit file UI.' }
foreach ($printer in $printers) {
    if ($printer.PreferredInputFormat -ne 'application/oxps') { throw 'Expected OXPS input.' }
    if ($printer.SelectNodes('psa:SupportedFormats', $ns).Count) { throw 'No unsupported passthrough formats.' }
}
$family = $manifest.SelectSingleNode('//m:TargetDeviceFamily', $ns)
if ($family.MinVersion -ne '10.0.26100.0') { throw 'Wrong platform minimum.' }
[xml]$pdc = Get-Content (Join-Path $root 'package/DiagnosticPdc.xml') -Raw
if ($pdc.DocumentElement.LocalName -ne 'PrintDeviceCapabilities') { throw 'Expected PDC, not PrintCapabilities.' }
Get-ChildItem (Join-Path $root 'psa') -Filter '*.cpp' | ForEach-Object {
    $text = Get-Content $_.FullName -Raw
    if ($text -match 'Process\.Start|CreateProcess|XpsToPdf|GetPdlConverter') {
        throw 'Prototype must not spawn a helper or silently use Microsoft conversion.'
    }
}
Write-Host 'Source invariants passed (not SDK or runtime validation).'
