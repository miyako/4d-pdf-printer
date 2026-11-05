#Requires -Version 7.4
<#
.SYNOPSIS
Turns a verified engineering package (build.ps1 -Stage Package) into a release MSIX.

.DESCRIPTION
Stages (each writes into a NEW absolute -OutputRoot):
  Package   One architecture. Unpacks the engineering MSIX, bundles the renderer runtime
            under Runtime\, applies the release identity, adds the VC++ framework dependency
            and the generic-pdf.exe command alias, rebuilds resources.pri and packs
            <IdentityName>_<Version>_<arch>.msix.
  Bundle    Combines Package outputs (x64 + ARM64) into one .msixbundle and, when every
            runtime is release-cleared, the .msixupload that Partner Center accepts.
  SignCopy  Signs a copy of a release .msix/.msixbundle for sideloading with a code-signing
            certificate whose subject equals the package Publisher, and exports its .cer.

The engineering build pipeline (build.ps1) is not modified; its tested binaries are used as is.
See STORE.md for the Partner Center values and the full release procedure.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Package', 'Bundle', 'SignCopy')][string]$Stage,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$EngineeringPackage, [string]$RuntimeRoot, [string]$AssetsDirectory,
    [string]$IdentityName = 'Miyako.DesktopPdfIntegration',
    [string]$Publisher = 'CN=DesktopPdfPrototypeDev',
    [string]$PublisherDisplayName = 'miyako',
    [string]$DisplayName = 'Generic Print to PDF',
    [string]$Description = 'Virtual PDF printer: an interactive Save As queue and a silent queue for automation.',
    [string]$Version = '0.2.0.0',
    [switch]$AllowInternalTestRuntime,
    [string[]]$ReleasePackages,
    [string]$Package, [string]$CertificateThumbprint
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Windows only.' }

$root = $PSScriptRoot
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64'
$makeappx = Join-Path $sdk 'makeappx.exe'; $makepri = Join-Path $sdk 'makepri.exe'; $signtool = Join-Path $sdk 'signtool.exe'
$Alias = 'generic-pdf.exe'
$VCLibs = [ordered]@{ Name = 'Microsoft.VCLibs.140.00.UWPDesktop'; MinVersion = '14.0.30704.0'
    Publisher = 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US' }
$ns = [ordered]@{
    m = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
    uap = 'http://schemas.microsoft.com/appx/manifest/uap/windows10'
    uap5 = 'http://schemas.microsoft.com/appx/manifest/uap/windows10/5'
}

function Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($Program)) failed ($LASTEXITCODE)." }
}
function Digest([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Absolute([string]$Path, [string]$What, [string]$Kind = 'Leaf') {
    if (-not $Path -or -not [IO.Path]::IsPathFullyQualified($Path) -or -not (Test-Path -LiteralPath $Path -PathType $Kind)) {
        throw "$What must be an absolute existing path."
    }
    [IO.Path]::GetFullPath($Path)
}
function Fresh([string]$Path) {
    if (-not [IO.Path]::IsPathFullyQualified($Path) -or
        -not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($Path)) -PathType Container)) {
        throw 'Use an absolute new -OutputRoot with an existing parent.'
    }
    New-Item -ItemType Directory -Path $Path -ErrorAction Stop | Out-Null
    [IO.Path]::GetFullPath($Path)
}
function Json([string]$Path) { Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }

# Mirrors InspectRuntime in WorkerProcess.cpp so a bad runtime fails here, not on the first print.
function Assert-Runtime([string]$Path, [string]$Arch) {
    $inventory = Json (Join-Path $Path 'runtime-imports.json')
    $receipt = Json (Join-Path $Path 'native-test-receipt.json')
    if (-not (Test-Path -LiteralPath (Join-Path $Path 'build-provenance.json') -PathType Leaf)) { throw 'Runtime build-provenance.json missing.' }
    if ($inventory.schema -cne '4d-pdf-printer.pe-inventory' -or $inventory.version -ne 2 -or $inventory.architecture -cne $Arch -or
        $receipt.architecture -cne $Arch -or "$($receipt.os_architecture)".ToLowerInvariant() -cne $Arch -or
        $receipt.result -cne 'passed' -or $receipt.worker -cne 'pdf-render-worker.exe') {
        throw "Runtime inventory/receipt is not a passed $Arch runtime."
    }
    $listed = @{}
    foreach ($entry in $inventory.files) {
        $name = $entry.file
        if (-not $name -or $name.IndexOfAny([char[]]'/\:') -ge 0 -or $listed.ContainsKey($name.ToLowerInvariant()) -or
            ($name -cne 'pdf-render-worker.exe' -and [IO.Path]::GetExtension($name) -ine '.dll')) { throw "Bad runtime member $name." }
        $listed[$name.ToLowerInvariant()] = $true
        $file = Join-Path $Path $name
        if ((Digest $file) -cne "$($entry.sha256)".ToLowerInvariant() -or (Get-Item -LiteralPath $file).Length -ne $entry.bytes) {
            throw "Runtime drift: $name."
        }
    }
    if (-not $listed.ContainsKey('pdf-render-worker.exe') -or
        (Digest (Join-Path $Path 'pdf-render-worker.exe')) -cne "$($receipt.worker_sha256)".ToLowerInvariant()) { throw 'Worker/receipt mismatch.' }
    foreach ($item in Get-ChildItem -LiteralPath $Path -Recurse -File -Force) {
        if ($item.Extension -in '.dll', '.exe' -and
            ($item.DirectoryName -ne $Path -or -not $listed.ContainsKey($item.Name.ToLowerInvariant()))) {
            throw "Unlisted runtime binary $($item.FullName)."
        }
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Runtime reparse point $($item.FullName)." }
    }
    [ordered]@{ inventory = Digest (Join-Path $Path 'runtime-imports.json'); receipt = Digest (Join-Path $Path 'native-test-receipt.json')
        provenance = Digest (Join-Path $Path 'build-provenance.json'); files = $listed.Count }
}

function New-Assets([string]$Directory) {
    Add-Type -AssemblyName System.Drawing
    $sizes = [ordered]@{ StoreLogo = @(50, 50); Square44x44Logo = @(44, 44); Square150x150Logo = @(150, 150); SplashScreen = @(620, 300) }
    foreach ($pair in $sizes.GetEnumerator()) {
        $w = $pair.Value[0]; $h = $pair.Value[1]; $s = [Math]::Min($w, $h)
        $image = [Drawing.Bitmap]::new($w, $h); $g = [Drawing.Graphics]::FromImage($image)
        try {
            $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
            $g.Clear([Drawing.Color]::FromArgb(32, 48, 64))
            $pw = [int]($s * 0.56); $ph = [int]($s * 0.72); $x = [int](($w - $pw) / 2); $y = [int](($h - $ph) / 2)
            $fold = [int]($pw * 0.3)
            $page = [Drawing.Point[]]@([Drawing.Point]::new($x, $y), [Drawing.Point]::new($x + $pw - $fold, $y),
                [Drawing.Point]::new($x + $pw, $y + $fold), [Drawing.Point]::new($x + $pw, $y + $ph), [Drawing.Point]::new($x, $y + $ph))
            $g.FillPolygon([Drawing.Brushes]::White, $page)
            $g.FillPolygon([Drawing.Brushes]::LightGray, [Drawing.Point[]]@([Drawing.Point]::new($x + $pw - $fold, $y),
                [Drawing.Point]::new($x + $pw - $fold, $y + $fold), [Drawing.Point]::new($x + $pw, $y + $fold)))
            $band = [Drawing.Rectangle]::new($x - [int]($pw * 0.08), $y + [int]($ph * 0.5), [int]($pw * 1.16), [int]($ph * 0.28))
            $g.FillRectangle([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(200, 40, 40)), $band)
            $font = [Drawing.Font]::new('Segoe UI', [float]([Math]::Max(5, $band.Height * 0.62)), [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
            $format = [Drawing.StringFormat]::new(); $format.Alignment = 'Center'; $format.LineAlignment = 'Center'
            $g.DrawString('PDF', $font, [Drawing.Brushes]::White, [Drawing.RectangleF]$band, $format)
            $font.Dispose(); $format.Dispose()
            $image.Save((Join-Path $Directory "$($pair.Key).png"), [Drawing.Imaging.ImageFormat]::Png)
        } finally { $g.Dispose(); $image.Dispose() }
    }
}

function Set-ReleaseManifest([string]$Path, [string]$Arch) {
    $xml = [xml]::new(); $xml.PreserveWhitespace = $false; $xml.Load($Path)
    $m = [Xml.XmlNamespaceManager]::new($xml.NameTable)
    foreach ($pair in $ns.GetEnumerator()) { $m.AddNamespace($pair.Key, $pair.Value) }
    $package = $xml.DocumentElement
    if (-not $package.HasAttribute('xmlns:uap5')) { $package.SetAttribute('xmlns:uap5', $ns.uap5) }
    $ignorable = @($package.GetAttribute('IgnorableNamespaces').Split(' ', [StringSplitOptions]::RemoveEmptyEntries))
    if ('uap5' -notin $ignorable) { $package.SetAttribute('IgnorableNamespaces', (($ignorable + 'uap5') -join ' ')) }

    $identity = $xml.SelectSingleNode('/m:Package/m:Identity', $m)
    if ($identity.GetAttribute('ProcessorArchitecture') -cne $Arch) { throw 'Engineering manifest architecture mismatch.' }
    $identity.SetAttribute('Name', $IdentityName); $identity.SetAttribute('Publisher', $Publisher)
    $identity.SetAttribute('Version', $Version)
    $xml.SelectSingleNode('/m:Package/m:Properties/m:DisplayName', $m).InnerText = $DisplayName
    $xml.SelectSingleNode('/m:Package/m:Properties/m:PublisherDisplayName', $m).InnerText = $PublisherDisplayName

    $dependencies = $xml.SelectSingleNode('/m:Package/m:Dependencies', $m)
    if (-not $dependencies.SelectSingleNode("m:PackageDependency[@Name='$($VCLibs.Name)']", $m)) {
        $dependency = $xml.CreateElement('PackageDependency', $ns.m)
        foreach ($pair in $VCLibs.GetEnumerator()) { $dependency.SetAttribute($pair.Key, $pair.Value) }
        $dependencies.AppendChild($dependency) | Out-Null
    }

    $saveAs = $xml.SelectSingleNode("/m:Package/m:Applications/m:Application[@Id='SaveAs']/uap:VisualElements", $m)
    $saveAs.SetAttribute('DisplayName', $DisplayName); $saveAs.SetAttribute('Description', $Description)

    $client = $xml.SelectSingleNode("/m:Package/m:Applications/m:Application[@Id='Client']", $m)
    $visual = $client.SelectSingleNode('uap:VisualElements', $m)
    $visual.SetAttribute('DisplayName', "$DisplayName command line")
    $visual.SetAttribute('Description', "Command line ($($Alias -replace '\.exe$')) to register, wait for, inspect and cancel silent print jobs")
    $visual.SetAttribute('AppListEntry', 'none')
    $extensions = $client.SelectSingleNode('m:Extensions', $m)
    if (-not $extensions) { $extensions = $client.AppendChild($xml.CreateElement('Extensions', $ns.m)) }
    $extension = $xml.CreateElement('uap5', 'Extension', $ns.uap5)
    $extension.SetAttribute('Category', 'windows.appExecutionAlias')
    $aliasList = $extension.AppendChild($xml.CreateElement('uap5', 'AppExecutionAlias', $ns.uap5))
    $aliasNode = $aliasList.AppendChild($xml.CreateElement('uap5', 'ExecutionAlias', $ns.uap5))
    $aliasNode.SetAttribute('Alias', $Alias)
    $extensions.AppendChild($extension) | Out-Null

    $settings = [Xml.XmlWriterSettings]::new(); $settings.Indent = $true; $settings.IndentChars = ' '
    $settings.Encoding = [Text.UTF8Encoding]::new($false)
    $writer = [Xml.XmlWriter]::Create($Path, $settings)
    try { $xml.Save($writer) } finally { $writer.Dispose() }
}

function Read-ZipText([string]$Archive, [string]$Entry) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $item = $zip.GetEntry($Entry); if (-not $item) { return $null }
        $reader = [IO.StreamReader]::new($item.Open()); try { $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
}

$out = Fresh $OutputRoot
$evidence = [ordered]@{ schema = '4d-pdf-printer.release'; version = 1; stage = $Stage; outcome = 'started' }
$primary = $null
try {
    foreach ($tool in $makeappx, $makepri, $signtool) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Windows SDK 10.0.26100.0 tool missing: $tool" }
    }
    if ($Stage -eq 'Package') {
        if ($Version -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}\.0$') { throw 'Version must be major.minor.build.0 (the Store reserves the revision).' }
        if ($IdentityName -notmatch '^[A-Za-z0-9.-]{3,50}$') { throw 'IdentityName must match the Partner Center Package/Identity/Name.' }
        if ($Publisher -notmatch '^CN=') { throw 'Publisher must be the Partner Center Package/Identity/Publisher (CN=...).' }
        $engineering = Absolute $EngineeringPackage '-EngineeringPackage'
        $runtime = Absolute $RuntimeRoot '-RuntimeRoot' 'Container'
        $receipt = Json (Join-Path ([IO.Path]::GetDirectoryName($engineering)) 'package-evidence.json')
        if ($receipt.schema -cne '4d-pdf-printer.own-package' -or $receipt.stage -cne 'Package' -or $receipt.outcome -cne 'success' -or
            $receipt.package_sha256 -cne (Digest $engineering)) { throw 'Engineering package must be the unsigned build.ps1 -Stage Package output with its successful package-evidence.json.' }
        $arch = $receipt.architecture
        if ($null -ne (Read-ZipText $engineering 'AppxSignature.p7x')) { throw 'Use the unsigned engineering package.' }
        $evidence.architecture = $arch; $evidence.source_sha = $receipt.source_sha
        $evidence.engineering_package = [ordered]@{ path = $engineering; sha256 = $receipt.package_sha256 }

        $evidence.runtime = Assert-Runtime $runtime $arch
        $evidence.runtime.root = $runtime
        $internal = Test-Path -LiteralPath (Join-Path $runtime 'INTERNAL-TEST-DELIVERY.txt')
        if ($internal -and -not $AllowInternalTestRuntime) {
            throw 'Runtime carries INTERNAL-TEST-DELIVERY.txt (not release-cleared). Use -AllowInternalTestRuntime for sideload testing only.'
        }
        $evidence.internal_test_runtime = $internal

        $resw = 'desktop-pdf/package/Strings/en-US/Resources.resw'
        $blob = & git -C $root rev-parse "$($receipt.source_sha):$resw"
        $local = & git -C $root hash-object -- (Join-Path $root 'package/Strings/en-US/Resources.resw')
        if ($LASTEXITCODE -ne 0 -or -not $blob -or $blob -cne $local) {
            throw "Checkout Resources.resw must equal the engineering source $($receipt.source_sha) (needed to rebuild resources.pri)."
        }

        $stagePath = Join-Path $out 'stage'
        Checked $makeappx @('unpack', '/p', $engineering, '/d', $stagePath)
        foreach ($name in 'AppxBlockMap.xml', '[Content_Types].xml', 'AppxSignature.p7x', 'AppxMetadata', 'resources.pri') {
            $item = Join-Path $stagePath $name
            if (Test-Path -LiteralPath $item) { Remove-Item -LiteralPath $item -Recurse -Force }
        }
        if (Test-Path -LiteralPath (Join-Path $stagePath 'Runtime')) { throw 'Engineering package unexpectedly contains Runtime\.' }
        Copy-Item -LiteralPath $runtime -Destination (Join-Path $stagePath 'Runtime') -Recurse
        if ((Assert-Runtime (Join-Path $stagePath 'Runtime') $arch).inventory -cne $evidence.runtime.inventory) { throw 'Runtime copy drifted.' }

        $assets = Join-Path $stagePath 'Assets'
        Remove-Item -LiteralPath $assets -Recurse -Force; New-Item -ItemType Directory -Path $assets | Out-Null
        if ($AssetsDirectory) {
            $source = Absolute $AssetsDirectory '-AssetsDirectory' 'Container'
            foreach ($name in 'StoreLogo', 'Square44x44Logo', 'Square150x150Logo', 'SplashScreen') {
                if (-not (Get-ChildItem -LiteralPath $source -Filter "$name*.png" -File)) { throw "-AssetsDirectory lacks $name.png (or a scale variant)." }
            }
            Get-ChildItem -LiteralPath $source -Filter '*.png' -File | Copy-Item -Destination $assets
            $evidence.assets = $source
        } else { New-Assets $assets; $evidence.assets = 'generated' }

        $manifest = Join-Path $stagePath 'AppxManifest.xml'
        Set-ReleaseManifest $manifest $arch
        $strings = Join-Path $stagePath 'Strings/en-US'
        New-Item -ItemType Directory -Path $strings | Out-Null
        Copy-Item -LiteralPath (Join-Path $root 'package/Strings/en-US/Resources.resw') -Destination $strings
        $config = Join-Path $out 'priconfig.xml'
        Checked $makepri @('createconfig', '/cf', $config, '/dq', 'lang-en-US')
        Checked $makepri @('new', '/pr', $stagePath, '/cf', $config, '/mn', $manifest, '/of', (Join-Path $stagePath 'resources.pri'))
        Remove-Item -LiteralPath (Join-Path $stagePath 'Strings') -Recurse -Force

        $file = Join-Path $out "$($IdentityName)_$($Version)_$arch.msix"
        Checked $makeappx @('pack', '/d', $stagePath, '/p', $file)
        $evidence.identity = [ordered]@{ name = $IdentityName; publisher = $Publisher; version = $Version
            display_name = $DisplayName; publisher_display_name = $PublisherDisplayName; alias = $Alias }
        $evidence.package = $file; $evidence.package_sha256 = Digest $file
    } elseif ($Stage -eq 'Bundle') {
        if (-not $ReleasePackages -or $ReleasePackages.Count -lt 1) { throw '-ReleasePackages requires the release .msix files.' }
        $inputs = Join-Path $out 'bundle-input'; New-Item -ItemType Directory -Path $inputs | Out-Null
        $parts = @(foreach ($path in $ReleasePackages) {
            $msix = Absolute $path 'Each -ReleasePackages entry'
            $record = Json (Join-Path ([IO.Path]::GetDirectoryName($msix)) 'release-evidence.json')
            if ($record.schema -cne '4d-pdf-printer.release' -or $record.stage -cne 'Package' -or $record.outcome -cne 'success' -or
                $record.package_sha256 -cne (Digest $msix)) { throw "$msix lacks a matching successful release-evidence.json." }
            Copy-Item -LiteralPath $msix -Destination $inputs
            $record
        })
        $first = $parts[0].identity
        foreach ($part in $parts) {
            if ($part.identity.name -cne $first.name -or $part.identity.publisher -cne $first.publisher -or $part.identity.version -cne $first.version) {
                throw 'All bundled packages must share identity name, publisher and version.'
            }
        }
        $architectures = @($parts | ForEach-Object architecture)
        if (@($architectures | Sort-Object -Unique).Count -ne $architectures.Count) { throw 'One package per architecture.' }
        $bundle = Join-Path $out "$($first.name)_$($first.version).msixbundle"
        Checked $makeappx @('bundle', '/d', $inputs, '/p', $bundle, '/bv', $first.version)
        $evidence.identity = $first; $evidence.architectures = $architectures
        $evidence.bundle = $bundle; $evidence.bundle_sha256 = Digest $bundle
        if (@($parts | Where-Object internal_test_runtime).Count -eq 0) {
            $upload = Join-Path $out "$($first.name)_$($first.version).msixupload"
            $zip = [IO.Compression.ZipFile]::Open($upload, [IO.Compression.ZipArchiveMode]::Create)
            try { [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $bundle, [IO.Path]::GetFileName($bundle)) | Out-Null }
            finally { $zip.Dispose() }
            $evidence.upload = $upload; $evidence.upload_sha256 = Digest $upload
        } else {
            $evidence.upload = 'skipped: a bundled runtime is not release-cleared (INTERNAL-TEST-DELIVERY.txt)'
        }
    } else {
        $unsigned = Absolute $Package '-Package'
        $extension = [IO.Path]::GetExtension($unsigned).ToLowerInvariant()
        if ($extension -notin '.msix', '.msixbundle') { throw '-Package must be a release .msix or .msixbundle.' }
        if ($CertificateThumbprint -cnotmatch '^[0-9a-fA-F]{40}$') { throw '-CertificateThumbprint is required.' }
        $manifestText = if ($extension -eq '.msix') { Read-ZipText $unsigned 'AppxManifest.xml' } else { Read-ZipText $unsigned 'AppxMetadata/AppxBundleManifest.xml' }
        if ($null -ne (Read-ZipText $unsigned 'AppxSignature.p7x')) { throw 'Input already signed.' }
        $publisherName = ([xml]$manifestText).DocumentElement.Identity.Publisher
        $cert = Get-Item -LiteralPath "Cert:/CurrentUser/My/$CertificateThumbprint"
        $eku = @($cert.Extensions | Where-Object { $_.Oid.Value -ceq '2.5.29.37' })
        if (-not $cert.HasPrivateKey -or $cert.Subject -cne $publisherName -or $cert.NotBefore -gt (Get-Date) -or $cert.NotAfter -lt (Get-Date) -or
            $eku.Count -ne 1 -or '1.3.6.1.5.5.7.3.3' -notin @($eku[0].EnhancedKeyUsages.Value)) {
            throw "A valid code-signing certificate with private key and subject '$publisherName' is required."
        }
        $signed = Join-Path $out ([IO.Path]::GetFileName($unsigned))
        $before = Digest $unsigned
        Copy-Item -LiteralPath $unsigned -Destination $signed
        Checked $signtool @('sign', '/fd', 'SHA256', '/sha1', $CertificateThumbprint, '/s', 'My', $signed)
        Checked $signtool @('verify', '/pa', $signed)
        if ((Digest $unsigned) -cne $before) { throw 'Unsigned input changed.' }
        $cer = Join-Path $out "$([IO.Path]::GetFileNameWithoutExtension($unsigned)).cer"
        [IO.File]::WriteAllBytes($cer, $cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
        foreach ($name in 'tools/Install.cmd', 'tools/Install-Sideload.ps1', 'tools/Uninstall.ps1', 'tools/VerifyInstall.ps1', 'INSTALL.md') {
            Copy-Item -LiteralPath (Join-Path $root $name) -Destination $out
        }
        $evidence.unsigned_sha256 = $before; $evidence.signed = $signed; $evidence.signed_sha256 = Digest $signed
        $evidence.certificate = [ordered]@{ path = $cer; subject = $cert.Subject; thumbprint = $cert.Thumbprint; not_after = $cert.NotAfter.ToString('o') }
    }
    $evidence.outcome = 'success'
} catch {
    $primary = $_; $evidence.outcome = 'failure'; $evidence.error = $_.Exception.Message
    throw
} finally {
    try { $evidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'release-evidence.json') -Encoding utf8 }
    catch { if ($primary) { Write-Error -ErrorAction Continue "Additional evidence-write failure: $_" } else { throw } }
}
