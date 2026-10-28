[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Destination,
    [string]$ArchivePath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$pin = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'upstream.json') -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath $Destination) { throw "Destination already exists: $Destination" }
New-Item -ItemType Directory -Path $Destination | Out-Null
$Destination = (Resolve-Path -LiteralPath $Destination).Path
if (-not $ArchivePath) {
    $ArchivePath = Join-Path $Destination 'upstream.tar.gz'
    Invoke-WebRequest -Uri $pin.archive -OutFile $ArchivePath
}
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ine $pin.archiveSha256) {
    throw 'Pinned Microsoft archive SHA256 mismatch; refusing extraction.'
}
$members = @($pin.paths | ForEach-Object { "$($pin.root)/$_" })
& tar -xzf $ArchivePath -C $Destination @members
if ($LASTEXITCODE -ne 0) { throw "tar extraction failed: $LASTEXITCODE" }
$source = Join-Path $Destination $pin.root
$inventory = @(Get-ChildItem -LiteralPath $source -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = [IO.Path]::GetRelativePath($source, $_.FullName).Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$inventory | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'original-files.json') -Encoding utf8
Write-Output $source
