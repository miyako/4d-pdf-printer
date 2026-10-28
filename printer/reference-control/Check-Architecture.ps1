[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [Parameter(Mandatory)][string]$DependencyDirectory,
    [Parameter(Mandatory)][string]$ReportPath,
    [switch]$VerifyPublisherSignatures
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$inputPackage = $PackagePath
. (Join-Path $PSScriptRoot 'Sign-Control.ps1')
$main = Assert-ControlPackage -Path $inputPackage
$dependencies = @(Get-ChildItem -LiteralPath $DependencyDirectory -File)
if ($main.Dependencies.Count -ne 4 -or $dependencies.Count -ne 4) { throw 'Expected four original framework dependencies.' }
$paths = @($inputPackage) + @($dependencies.FullName)
$seen = @()
$evidence = @(foreach ($path in $paths) {
    $zip = [IO.Compression.ZipFile]::OpenRead($path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('AppxManifest.xml').Open())
        try { [xml]$manifest = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $identity = $manifest.Package.Identity
        $isDependency = $path -ne $inputPackage
        if ($isDependency) {
            $declarations = @($main.Dependencies | Where-Object { $_.Name -ceq $identity.Name })
            if ($declarations.Count -ne 1 -or $identity.Name -in $seen) { throw "Unexpected/duplicate dependency: $($identity.Name)" }
            $declaration = $declarations[0]
            if ($manifest.Package.Properties.Framework -cne 'true' -or
                $identity.Publisher -cne $declaration.Publisher -or
                [version]$identity.Version -lt [version]$declaration.MinVersion -or
                $identity.ProcessorArchitecture -cne 'arm64' -or
                -not $zip.GetEntry('AppxSignature.p7x')) { throw "Invalid ARM64 framework: $($identity.Name)" }
            $seen += $identity.Name
            if ($VerifyPublisherSignatures) {
                if (-not $IsWindows) { throw 'Publisher verification requires Windows SDK SignTool.' }
                $tool = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe'
                & $tool verify /pa $path | Out-Host
                if ($LASTEXITCODE -ne 0) { throw "Original framework signature verification failed: $($identity.Name)" }
            }
        }
        $pe = @(foreach ($entry in $zip.Entries) {
            if ($entry.FullName.EndsWith('/')) { continue }
            $stream = $entry.Open()
            try { $mz = $stream.ReadByte() -eq 0x4d -and $stream.ReadByte() -eq 0x5a }
            finally { $stream.Dispose() }
            if ($mz -or [IO.Path]::GetExtension($entry.FullName) -in @('.exe', '.dll', '.winmd', '.sys', '.ocx', '.cpl', '.scr', '.drv')) {
                $bytes = Read-ControlZipBytes $entry
                Assert-ControlPe -Bytes $bytes -Name $entry.FullName
                $info = Get-ControlPeInfo -Bytes $bytes -Name $entry.FullName
                [ordered]@{
                    path = $entry.FullName
                    machine = '0x{0:x4}' -f $info.Machine
                    architecture = $(if ($info.IsAnyCpu) { 'ILONLY AnyCPU (not native x86)' } else { 'ARM64' })
                }
            }
        })
        [ordered]@{
            file = [IO.Path]::GetFileName($path)
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            name = $identity.Name
            version = $identity.Version
            architecture = $identity.ProcessorArchitecture
            signature = $(if (-not $isDependency) { 'unsigned' } elseif ($VerifyPublisherSignatures) { 'SignTool /pa passed' } else { 'present; trust not checked on this host' })
            peFiles = $pe
        }
    } finally { $zip.Dispose() }
})
$evidence | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $ReportPath -Encoding utf8
