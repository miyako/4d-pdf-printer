<#
.SYNOPSIS
Preflights a reviewed, locally signed reference control before normal user installation.
.DESCRIPTION
Execute in Windows PowerShell 5.1 with the local Appx module. Does not sign,
import trust, launch applications, remove packages or reinstall an existing control.
Requires the complete unsigned artifact and Sign-Control.ps1's local signed output.
ExpectedUnsignedSha256 is the separately published MSIX hash, not the ZIP hash.
Dot-source to load the validation/install functions for portable mocked tests.
#>
[CmdletBinding()]
param(
    [string]$ArtifactDirectory,
    [string]$SignedDirectory,
    [string]$ExpectedUnsignedSha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert-InstallObject($Value, [string[]]$Properties) {
    if ($null -eq $Value -or $Value -isnot [pscustomobject] -or
        @($Value.PSObject.Properties).Count -ne $Properties.Count) {
        throw 'Invalid evidence/receipt object shape.'
    }
    foreach ($property in $Properties) {
        if ($property -cnotin @($Value.PSObject.Properties.Name)) {
            throw "Missing evidence/receipt property '$property'."
        }
    }
}

function Assert-InstallString($Value) {
    if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value)) {
        throw 'Expected a nonempty evidence/receipt string.'
    }
}

function Assert-InstallVersion($Value) {
    Assert-InstallString $Value
    if ($Value -cnotmatch '\A\d{1,5}\.\d{1,5}\.\d{1,5}\.\d{1,5}\z' -or
        @($Value.Split('.') | Where-Object { [int]$_ -gt 65535 }).Count -ne 0) {
        throw 'Invalid four-part package version.'
    }
}

function Resolve-InstallPath([string]$Path, [switch]$Directory) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSProvider.Name -cne 'FileSystem' -or $item.PSIsContainer -ne [bool]$Directory) {
        throw "Unexpected path type: $Path"
    }
    $ancestor = $item
    while ($null -ne $ancestor) {
        if (($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Linked/reparse paths are not accepted: $Path"
        }
        $ancestor = if ($ancestor -is [IO.FileInfo]) { $ancestor.Directory } else { $ancestor.Parent }
    }
    return $item.FullName
}

function Assert-InstallHash([string]$Path, $Hash) {
    if ($Hash -isnot [string] -or $Hash -cnotmatch '\A[0-9a-fA-F]{64}\z' -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Hash) {
        throw "SHA256 mismatch or invalid hash: $Path"
    }
}

function Read-InstallManifest([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entries = @($zip.Entries | Where-Object { $_.FullName -ieq 'AppxManifest.xml' })
        if ($entries.Count -ne 1 -or $entries[0].Length -gt 4MB) { throw 'Invalid package manifest count/size.' }
        $settings = [Xml.XmlReaderSettings]::new()
        $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
        $settings.XmlResolver = $null
        $settings.MaxCharactersInDocument = 4MB
        $stream = $entries[0].Open()
        $reader = [Xml.XmlReader]::Create($stream, $settings)
        try {
            $manifest = [Xml.XmlDocument]::new()
            $manifest.XmlResolver = $null
            $manifest.Load($reader)
        } finally { $reader.Dispose(); $stream.Dispose() }
        $ns = [Xml.XmlNamespaceManager]::new($manifest.NameTable)
        $uri = $manifest.DocumentElement.NamespaceURI
        if ($manifest.DocumentElement.LocalName -cne 'Package' -or
            $uri -cnotin @('http://schemas.microsoft.com/appx/manifest/foundation/windows10',
                'http://schemas.microsoft.com/appx/2010/manifest')) { throw 'Unexpected package manifest schema.' }
        $ns.AddNamespace('p', $uri)
        $identities = $manifest.SelectNodes('/p:Package/p:Identity', $ns)
        if ($identities.Count -ne 1) { throw 'Expected one package identity.' }
        $id = $identities[0]
        $framework = $manifest.SelectSingleNode('/p:Package/p:Properties/p:Framework', $ns)
        $resource = $manifest.SelectSingleNode('/p:Package/p:Properties/p:ResourcePackage', $ns)
        return [pscustomobject]@{
            Name = $id.GetAttribute('Name')
            Publisher = $id.GetAttribute('Publisher')
            Version = $id.GetAttribute('Version')
            ProcessorArchitecture = $id.GetAttribute('ProcessorArchitecture')
            IsFramework = ($null -ne $framework -and $framework.InnerText -ceq 'true')
            IsResource = ($id.GetAttribute('ResourceId') -ne '' -or
                ($null -ne $resource -and $resource.InnerText -cnotin @('false', '0')))
            Dependencies = @($manifest.SelectNodes('/p:Package/p:Dependencies/p:PackageDependency', $ns) |
                ForEach-Object {
                    [pscustomobject]@{
                        Name = $_.GetAttribute('Name')
                        Publisher = $_.GetAttribute('Publisher')
                        MinVersion = $_.GetAttribute('MinVersion')
                        ProcessorArchitecture = $_.GetAttribute('ProcessorArchitecture')
                    }
                })
        }
    } finally { $zip.Dispose() }
}

function Confirm-InstallSignature([string]$Path, [string]$Subject, [string]$Thumbprint) {
    $tool = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe'
    $tool = Resolve-InstallPath $tool
    & $tool verify /pa $Path | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "SignTool signature/trust verification failed: $Path" }
    Add-Type -AssemblyName System.Security
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entries = @($zip.Entries | Where-Object { $_.FullName -ieq 'AppxSignature.p7x' })
        if ($entries.Count -ne 1 -or $entries[0].Length -gt 1MB) { throw 'Invalid package signature container.' }
        $stream = $entries[0].Open()
        $memory = [IO.MemoryStream]::new()
        try { $stream.CopyTo($memory); $bytes = $memory.ToArray() }
        finally { $memory.Dispose(); $stream.Dispose() }
        if ($bytes.Length -lt 5 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -cne 'PKCX') {
            throw 'Invalid package signature header.'
        }
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new()
        $cms.Decode([byte[]]$bytes[4..($bytes.Length - 1)])
        $cms.CheckSignature($true)
        if ($cms.SignerInfos.Count -ne 1 -or $null -eq $cms.SignerInfos[0].Certificate) {
            throw 'Expected one package signer.'
        }
        $signer = $cms.SignerInfos[0].Certificate
        if ($signer.Subject -cne $Subject -or ($Thumbprint -and $signer.Thumbprint -ine $Thumbprint)) {
            throw 'Package signer differs from the expected publisher/signing receipt.'
        }
    } finally { $zip.Dispose() }
}

function Get-ControlInstallPlan {
    param(
        [Parameter(Mandatory)][string]$ArtifactDirectory,
        [Parameter(Mandatory)][string]$SignedDirectory,
        [Parameter(Mandatory)][string]$ExpectedUnsignedSha256
    )
    $artifact = Resolve-InstallPath $ArtifactDirectory -Directory
    $signed = Resolve-InstallPath $SignedDirectory -Directory
    $unsignedPath = Resolve-InstallPath (Join-Path $artifact 'PsaReferenceControl.arm64.msix')
    $packagePath = Resolve-InstallPath (Join-Path $signed 'PsaReferenceControl.arm64.msix')
    $certificatePath = Resolve-InstallPath (Join-Path $signed 'PsaReferenceControlDev.cer')
    $receiptPath = Resolve-InstallPath (Join-Path $signed 'SigningReceipt.json')
    $evidencePath = Resolve-InstallPath (Join-Path $artifact 'architecture-evidence.json')
    $dependencyDirectory = Resolve-InstallPath (Join-Path $artifact 'Dependencies/arm64') -Directory
    if ($unsignedPath -ieq $packagePath) { throw 'Signed and unsigned package paths must differ.' }
    Assert-InstallHash $unsignedPath $ExpectedUnsignedSha256

    # Assign the JSON root first. PS5.1 pipelines otherwise filter the array as one record.
    $evidence = Get-Content -LiteralPath $evidencePath -Raw | ConvertFrom-Json
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if ($evidence -isnot [array] -or $evidence.Count -ne 5) { throw 'Expected five architecture evidence records.' }
    Assert-InstallObject $receipt @('SchemaVersion', 'SignedAtUtc', 'InputPath', 'InputSha256',
        'OutputPath', 'OutputSha256', 'Certificate', 'Identity', 'Dependencies', 'SignToolPath', 'Timestamped')
    if (($receipt.SchemaVersion -isnot [int] -and $receipt.SchemaVersion -isnot [long]) -or $receipt.SchemaVersion -ne 1 -or
        $receipt.Timestamped -isnot [bool] -or $receipt.Timestamped) { throw 'Unexpected signing receipt schema.' }
    # Recent portable-test hosts deserialize ISO JSON strings as DateTime.
    $signedAt = $receipt.SignedAtUtc
    if ($signedAt -is [DateTime]) { $signedAt = $signedAt.ToString('o') }
    Assert-InstallString $signedAt
    $null = [DateTimeOffset]::ParseExact($signedAt, 'o', [Globalization.CultureInfo]::InvariantCulture)
    foreach ($pair in @(@($receipt.InputPath, $unsignedPath), @($receipt.OutputPath, $packagePath))) {
        Assert-InstallString $pair[0]
        if (-not [IO.Path]::IsPathRooted($pair[0]) -or $pair[0] -ine $pair[1]) {
            throw 'Signing receipt paths differ from this local signing run.'
        }
    }
    if ($receipt.InputSha256 -ine $ExpectedUnsignedSha256) { throw 'Receipt input hash differs from published hash.' }
    Assert-InstallHash $unsignedPath $receipt.InputSha256
    Assert-InstallHash $packagePath $receipt.OutputSha256
    Assert-InstallString $receipt.SignToolPath
    if ($receipt.SignToolPath.Replace('\', '/') -cnotmatch '/10\.0\.26100\.0/x64/signtool\.exe$') {
        throw 'Unexpected signing receipt SignTool path.'
    }
    Assert-InstallObject $receipt.Identity @('Name', 'Publisher', 'Version', 'ProcessorArchitecture')
    $expectedIdentity = @('Miyako.PsaReferenceControl', 'CN=PsaReferenceControlDev', '1.1.14.0', 'arm64')
    $fields = @('Name', 'Publisher', 'Version', 'ProcessorArchitecture')
    $main = Read-InstallManifest $packagePath
    $unsigned = Read-InstallManifest $unsignedPath
    for ($i = 0; $i -lt $fields.Count; $i++) {
        $field = $fields[$i]
        Assert-InstallString $receipt.Identity.$field
        if ($receipt.Identity.$field -cne $expectedIdentity[$i] -or
            $main.$field -cne $expectedIdentity[$i] -or $unsigned.$field -cne $expectedIdentity[$i]) {
            throw "Unexpected main package/receipt identity: $field"
        }
    }
    if ($main.IsFramework -or $main.IsResource -or $unsigned.IsFramework -or $unsigned.IsResource) {
        throw 'Expected a main, non-resource package.'
    }
    Assert-InstallObject $receipt.Certificate @('Thumbprint', 'Subject', 'Store', 'PublicCertificatePath')
    foreach ($field in @('Thumbprint', 'Subject', 'Store', 'PublicCertificatePath')) {
        Assert-InstallString $receipt.Certificate.$field
    }
    if ($receipt.Certificate.Subject -cne $expectedIdentity[1] -or
        $receipt.Certificate.Store -cne 'CurrentUser/My' -or
        $receipt.Certificate.Thumbprint -cnotmatch '\A[0-9A-F]{40}\z' -or
        $receipt.Certificate.PublicCertificatePath -ine $certificatePath) { throw 'Unexpected certificate receipt.' }
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certificatePath)
    try {
        if ($certificate.Subject -cne $receipt.Certificate.Subject -or
            $certificate.Thumbprint -cne $receipt.Certificate.Thumbprint) { throw 'Public certificate differs from receipt.' }
    } finally { $certificate.Dispose() }

    $publisher = 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US'
    $frameworkNames = @('Microsoft.NET.Native.Framework.2.2', 'Microsoft.NET.Native.Runtime.2.2',
        'Microsoft.UI.Xaml.2.4', 'Microsoft.VCLibs.140.00')
    foreach ($declarations in @($receipt.Dependencies, $main.Dependencies, $unsigned.Dependencies)) {
        if ($declarations -isnot [array] -or $declarations.Count -ne 4) { throw 'Expected four framework declarations.' }
        $seen = @()
        foreach ($declaration in $declarations) {
            Assert-InstallObject $declaration @('Name', 'Publisher', 'MinVersion', 'ProcessorArchitecture')
            Assert-InstallString $declaration.Name
            Assert-InstallString $declaration.Publisher
            if ($declaration.ProcessorArchitecture -isnot [string]) { throw 'Invalid framework architecture shape.' }
            if ($declaration.Name -cnotin $frameworkNames -or $declaration.Name -iin $seen -or
                $declaration.Publisher -cne $publisher -or $declaration.ProcessorArchitecture -cnotin @('', 'arm64')) {
                throw 'Unexpected/duplicate framework declaration or publisher.'
            }
            Assert-InstallVersion $declaration.MinVersion
            $seen += $declaration.Name
            $reference = @($main.Dependencies | Where-Object { $_.Name -ceq $declaration.Name })
            if ($reference.Count -ne 1 -or $reference[0].MinVersion -cne $declaration.MinVersion -or
                $reference[0].ProcessorArchitecture -cne $declaration.ProcessorArchitecture) {
                throw 'Receipt/unsigned dependencies differ from signed manifest.'
            }
        }
    }
    $names = @()
    $files = @()
    $frameworks = @(
        foreach ($record in $evidence) {
            Assert-InstallObject $record @('file', 'sha256', 'name', 'version', 'architecture', 'signature', 'peFiles')
            foreach ($field in @('file', 'sha256', 'name', 'version', 'architecture', 'signature')) {
                Assert-InstallString $record.$field
            }
            if ($record.name -cnotin (@($expectedIdentity[0]) + $frameworkNames) -or $record.name -iin $names -or
                $record.file -isnot [string] -or $record.file -cnotmatch '\A[A-Za-z0-9_.-]+\.(appx|msix)\z' -or
                $record.file -iin $files -or $record.architecture -cne 'arm64') {
                throw 'Unexpected/duplicate evidence name, file or architecture.'
            }
            $names += $record.name
            $files += $record.file
            Assert-InstallVersion $record.version
            if ($record.peFiles -isnot [array] -or $record.peFiles.Count -eq 0) { throw 'Expected PE evidence array.' }
            $pePaths = @()
            foreach ($pe in $record.peFiles) {
                Assert-InstallObject $pe @('path', 'machine', 'architecture')
                foreach ($field in @('path', 'machine', 'architecture')) { Assert-InstallString $pe.$field }
                if ($pe.path -match '(^/|[\\:<>"|?*\x00-\x1f]|(^|/)\.\.?(/|$))' -or
                    $pe.path -iin $pePaths -or
                    -not (($pe.machine -ceq '0xaa64' -and $pe.architecture -ceq 'ARM64') -or
                        ($pe.machine -ceq '0x014c' -and $pe.architecture -ceq 'ILONLY AnyCPU (not native x86)'))) {
                    throw 'Invalid PE architecture evidence.'
                }
                $pePaths += $pe.path
            }
            if ($record.name -ceq $expectedIdentity[0]) {
                if ($record.file -cne 'PsaReferenceControl.arm64.msix' -or $record.version -cne '1.1.14.0' -or
                    $record.signature -cne 'unsigned') { throw 'Unexpected main package evidence.' }
                Assert-InstallHash $unsignedPath $record.sha256
                continue
            }
            if ($record.signature -cne 'SignTool /pa passed') { throw 'Framework evidence lacks verified publisher signature.' }
            $path = Resolve-InstallPath (Join-Path $dependencyDirectory $record.file)
            Assert-InstallHash $path $record.sha256
            $manifest = Read-InstallManifest $path
            $declaration = @($main.Dependencies | Where-Object { $_.Name -ceq $record.name })[0]
            if ($manifest.Name -cne $record.name -or $manifest.Version -cne $record.version -or
                $manifest.Publisher -cne $publisher -or $manifest.ProcessorArchitecture -cne 'arm64' -or
                -not $manifest.IsFramework -or $manifest.IsResource -or
                [version]$manifest.Version -lt [version]$declaration.MinVersion) { throw 'Invalid supplied ARM64 framework.' }
            [pscustomobject]@{ Path = $path; Name = $record.name; Publisher = $publisher; MinVersion = $declaration.MinVersion }
        }
    )
    $suppliedFiles = @(Get-ChildItem -LiteralPath $dependencyDirectory -Force)
    if ($frameworks.Count -ne 4 -or $suppliedFiles.Count -ne 4 -or
        @($suppliedFiles | Where-Object { $_.PSIsContainer -or $_.Name -cnotin $files }).Count -ne 0) {
        throw 'Expected exactly four supplied framework files.'
    }
    Confirm-InstallSignature $packagePath $receipt.Certificate.Subject $receipt.Certificate.Thumbprint
    foreach ($framework in $frameworks) { Confirm-InstallSignature $framework.Path $framework.Publisher }
    return [pscustomobject]@{ PackagePath = $packagePath; Identity = $receipt.Identity; Frameworks = $frameworks }
}

function Invoke-ControlInstall {
    param(
        [Parameter(Mandatory)][string]$ArtifactDirectory,
        [Parameter(Mandatory)][string]$SignedDirectory,
        [Parameter(Mandatory)][string]$ExpectedUnsignedSha256
    )
    $plan = Get-ControlInstallPlan @PSBoundParameters
    if (@(Get-AppxPackage -Name $plan.Identity.Name -ErrorAction Stop).Count -ne 0) {
        throw 'Reference control is already installed. Do not reinstall; enumerate its status and queues instead.'
    }
    $printers = @(Get-Printer -ErrorAction Stop)
    if (@($printers | Where-Object { $_.Name -iin @('VirtualPdfPrinter', 'VirtualPostScriptPrinter') }).Count -ne 0) {
        throw 'Expected queue name collision; do not rename/remove the existing queue.'
    }
    $missing = @(
        foreach ($framework in $plan.Frameworks) {
            $installed = @(Get-AppxPackage -Name $framework.Name -ErrorAction Stop)
            $satisfied = @($installed | Where-Object {
                $_.Name -ceq $framework.Name -and $_.Publisher -ceq $framework.Publisher -and
                $_.IsFramework -eq $true -and [version]$_.Version -ge [version]$framework.MinVersion -and
                $_.Architecture.ToString() -iin @('arm64', 'neutral') -and $_.Status.ToString() -ieq 'Ok'
            })
            if ($satisfied.Count -eq 0) { $framework.Path }
        }
    )
    $install = @{ Path = $plan.PackagePath; ErrorAction = 'Stop' }
    if ($missing.Count -ne 0) { $install.DependencyPath = [string[]]$missing }
    Add-AppxPackage @install
    $control = @(Get-AppxPackage -Name $plan.Identity.Name -ErrorAction Stop)
    if ($control.Count -ne 1 -or $control[0].Name -cne $plan.Identity.Name -or
        $control[0].Publisher -cne $plan.Identity.Publisher -or
        [version]$control[0].Version -ne [version]$plan.Identity.Version -or
        $control[0].Architecture.ToString() -ine 'Arm64' -or $control[0].Status.ToString() -ine 'Ok') {
        throw 'Reference package is not healthy/registered as the expected ARM64 identity.'
    }
    $control | Select-Object Name, Version, Architecture, Status
}

if ($MyInvocation.InvocationName -ne '.') {
    if ($PSVersionTable.PSEdition -cne 'Desktop' -or $PSVersionTable.PSVersion.Major -ne 5 -or
        $PSVersionTable.PSVersion.Minor -ne 1 -or $env:OS -cne 'Windows_NT') {
        throw 'Run this helper in Windows PowerShell 5.1, not PowerShell 7/compatibility remoting.'
    }
    foreach ($command in @('Get-AppxPackage', 'Add-AppxPackage')) {
        $resolved = Get-Command $command -ErrorAction Stop
        if ($resolved.CommandType -ne 'Cmdlet' -or $resolved.ModuleName -cne 'Appx') {
            throw 'Requires local Appx cmdlets, not compatibility-remoting proxies.'
        }
    }
    foreach ($parameter in @('ArtifactDirectory', 'SignedDirectory', 'ExpectedUnsignedSha256')) {
        if (-not $PSBoundParameters.ContainsKey($parameter) -or
            [string]::IsNullOrWhiteSpace($PSBoundParameters[$parameter])) { throw "Required parameter: $parameter" }
    }
    Invoke-ControlInstall @PSBoundParameters
}
