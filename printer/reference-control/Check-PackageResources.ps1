[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [Parameter(Mandatory)][string]$SourceRoot,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$app = Join-Path $SourceRoot 'PSASamples/UWP/CSharp/PrintSupportApp'
$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $PackagePath).Path)
try {
    $files = @()
    foreach ($name in @('Config/PrinterPdc.xml', 'Config/PrinterPdr.xml')) {
        $entry = $zip.GetEntry($name)
        if (-not $entry) { throw "Missing original reference content: $name" }
        $stream = $entry.Open()
        try { $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)) }
        finally { $stream.Dispose() }
        $expected = (Get-FileHash -LiteralPath (Join-Path $app $name) -Algorithm SHA256).Hash
        if ($actual -ne $expected) { throw "Packaged reference bytes differ: $name" }
        $files += [ordered]@{ path = $name; sha256 = $actual.ToLowerInvariant(); upstreamBytes = 'identical' }
    }
    $reader = [IO.StreamReader]::new($zip.GetEntry('AppxManifest.xml').Open())
    try { [xml]$manifest = $reader.ReadToEnd() } finally { $reader.Dispose() }
    [xml]$original = Get-Content -LiteralPath (Join-Path $app 'Package.appxmanifest') -Raw
    $xpath = '//*[local-name()="Extension" and @Category="windows.printSupportVirtualPrinterWorkflow"]'
    $expectedNodes = @($original.SelectNodes($xpath))
    $actualNodes = @($manifest.SelectNodes($xpath))
    if ($expectedNodes.Count -ne 2 -or $actualNodes.Count -ne 2) { throw 'Expected two static virtual-printer declarations.' }
    for ($i = 0; $i -lt 2; $i++) {
        if ($expectedNodes[$i].OuterXml -cne $actualNodes[$i].OuterXml) {
            # Compare XML without formatting-only text nodes; namespace declarations
            # inherited from the package root do not form part of either element.
            [xml]$expectedXml = $expectedNodes[$i].OuterXml
            [xml]$actualXml = $actualNodes[$i].OuterXml
            if ($expectedXml.OuterXml -cne $actualXml.OuterXml) { throw "Static declaration $i differs from upstream." }
        }
    }
    $pri = $zip.GetEntry('resources.pri')
    if (-not $pri -or $pri.Length -eq 0) { throw 'Missing compiled upstream PRI.' }
    $priPath = Join-Path $OutputDirectory 'resources.pri'
    [IO.Compression.ZipFileExtensions]::ExtractToFile($pri, $priPath)
} finally { $zip.Dispose() }
$makepri = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64/makepri.exe'
$dumpPath = Join-Path $OutputDirectory 'resources.xml'
& $makepri dump /if $priPath /of $dumpPath /o | Out-Host
if ($LASTEXITCODE -ne 0) { throw "MakePri dump failed: $LASTEXITCODE" }
$dump = Get-Content -LiteralPath $dumpPath -Raw
foreach ($resource in @('PdfPrintDisplayName', 'PsPrintDisplayName', 'VirtualPdfPrinter', 'VirtualPostScriptPrinter')) {
    if (-not $dump.Contains($resource)) { throw "Original queue resource missing from PRI: $resource" }
}
[ordered]@{
    staticExtensions = 'two; identical to upstream'
    content = $files
    resourceBuild = 'upstream UWP XAML/PRI; original queue names found in MakePri dump'
    resourcesPriSha256 = (Get-FileHash -LiteralPath $priPath -Algorithm SHA256).Hash.ToLowerInvariant()
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'resource-evidence.json') -Encoding utf8
