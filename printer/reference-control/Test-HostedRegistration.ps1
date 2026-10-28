# This experiment is restricted to an elevated, disposable GitHub-hosted Windows ARM64 job.
# Supply the signed MSIX/APPX and public CER produced by Sign-Control.ps1. The output must be a new file.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [Parameter(Mandatory)][string]$CertificatePath,
    [string]$DependencyDirectory,
    [Parameter(Mandatory)][string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$VerbosePreference = 'SilentlyContinue'
$DebugPreference = 'SilentlyContinue'

$identityName = 'Miyako.PsaReferenceControl'
$publisher = 'CN=PsaReferenceControlDev'
$packageVersion = [version]'1.1.14.0'
$expectedQueues = @('VirtualPdfPrinter', 'VirtualPostScriptPrinter')
$inboxQueues = @('Microsoft Print to PDF', 'Microsoft XPS Document Writer', 'Fax')
$microsoftPublishers = @(
    'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US',
    'CN=Microsoft Windows, O=Microsoft Corporation, L=Redmond, S=Washington, C=US'
)
$stage = 'output'
$failureCode = $null
$evidenceStream = $null
$publicCertificate = $null
$certificateCreated = $false
$certificateThumbprint = $null
$installTargets = [Collections.Generic.List[object]]::new()
$cleanupErrors = [Collections.Generic.List[object]]::new()
$report = [ordered]@{
    schemaVersion = 1
    experiment = 'static-reference-hosted-registration'
    runtime = 'not-run'
    outcome = 'blocked'
    host = [ordered]@{
        githubActions = ($env:GITHUB_ACTIONS -ceq 'true')
        githubHosted = ($env:RUNNER_ENVIRONMENT -ceq 'github-hosted')
        windows = $false
        osArchitecture = $null
        processArchitecture = $null
        osVersion = $null
        build = $null
        clientOS = $null
        elevated = $null
        powershellVersion = $PSVersionTable.PSVersion.ToString()
        runnerImage = $env:ImageVersion
        kernelFileVersion = $null
    }
    package = [ordered]@{
        identity = $identityName
        publisher = $publisher
        version = $packageVersion.ToString()
        architecture = 'arm64'
        sha256 = $null
        signature = 'not-checked'
        registration = 'not-run'
        status = 'not-read'
    }
    trust = [ordered]@{ store = 'LocalMachine/TrustedPeople'; created = $false; removed = $false }
    dependencies = [ordered]@{ validated = 0; attempted = 0; alreadySatisfied = 0; sharedRegistrationAttempts = 0; signatureChecks = @() }
    spooler = [ordered]@{ before = 'not-read'; after = 'not-read' }
    printers = [ordered]@{
        before = $null
        after = $null
        observationCount = 0
        intervalSeconds = 10
        maximumObservations = 6
        outcome = 'not-observed'
    }
    cleanup = [ordered]@{ packagesRemoved = 0; sharedFrameworkRegistrationsRemoved = 0; completed = $false; errors = @() }
    error = $null
}

function Stop-Experiment([string]$Code) {
    $script:failureCode = $Code
    throw [InvalidOperationException]::new($Code)
}

function Get-SanitizedError($Record, [string]$AtStage, [string]$Code) {
    $hex = '0x{0:X8}' -f ($Record.Exception.HResult -band 0xffffffffL)
    $messages = [Collections.Generic.List[string]]::new()
    $exception = $Record.Exception
    for ($i = 0; $null -ne $exception -and $i -lt 6; $i++) {
        $messages.Add($exception.Message)
        $exception = $exception.InnerException
    }
    if ($null -ne $Record.ErrorDetails) { $messages.Add($Record.ErrorDetails.Message) }
    $messages.Add($Record.ToString())
    $text = $messages -join "`n"
    $codes = @([regex]::Matches($text, '\b0x[0-9a-fA-F]{8}\b') |
        ForEach-Object { $_.Value.ToUpperInvariant().Replace('0X', '0x') } | Select-Object -Unique)
    if ($codes.Count -ne 0) { $hex = $codes[0] }
    $category = if ($text -match 'special profile') { 'special-profile-restriction' }
        elseif ($text -match 'not supported|NotSupportedException') { 'operation-not-supported' }
        elseif ($text -match 'higher version|newer version') { 'higher-version-present' }
        elseif ($text -match 'certificate.*not trusted|untrusted root') { 'certificate-not-trusted' }
        else { 'unclassified-error' }
    return [ordered]@{
        stage = $AtStage
        code = $Code
        hresult = $hex
        hresults = $codes
        exceptionType = $Record.Exception.GetType().FullName
        diagnosticCategory = $category
    }
}

function Get-AppxManifest([string]$Path) {
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entries = @($archive.Entries | Where-Object { $_.FullName -ieq 'AppxManifest.xml' })
        if ($entries.Count -ne 1 -or $entries[0].Length -gt 1048576) {
            Stop-Experiment 'invalid-appx-manifest'
        }
        $stream = $entries[0].Open()
        try {
            $settings = [Xml.XmlReaderSettings]::new()
            $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
            $settings.XmlResolver = $null
            $settings.MaxCharactersInDocument = 1048576
            $reader = [Xml.XmlReader]::Create($stream, $settings)
            try {
                $document = [Xml.XmlDocument]::new()
                $document.XmlResolver = $null
                $document.Load($reader)
            }
            finally { $reader.Dispose() }
        }
        finally { $stream.Dispose() }
        $namespace = $document.DocumentElement.NamespaceURI
        if ($document.DocumentElement.LocalName -cne 'Package' -or
            $namespace -cnotin @('http://schemas.microsoft.com/appx/manifest/foundation/windows10',
                'http://schemas.microsoft.com/appx/2010/manifest')) {
            Stop-Experiment 'unsupported-appx-manifest'
        }
        $ns = [Xml.XmlNamespaceManager]::new($document.NameTable)
        $ns.AddNamespace('p', $namespace)
        $identity = $document.SelectSingleNode('/p:Package/p:Identity', $ns)
        if ($null -eq $identity) { Stop-Experiment 'missing-appx-identity' }
        $framework = $document.SelectSingleNode('/p:Package/p:Properties/p:Framework', $ns)
        $resource = $document.SelectSingleNode('/p:Package/p:Properties/p:ResourcePackage', $ns)
        return [pscustomobject]@{
            Name = $identity.GetAttribute('Name')
            Publisher = $identity.GetAttribute('Publisher')
            Version = [version]$identity.GetAttribute('Version')
            Architecture = $identity.GetAttribute('ProcessorArchitecture').ToLowerInvariant()
            ResourceId = $identity.GetAttribute('ResourceId')
            ManifestNamespace = $namespace
            Framework = ($null -ne $framework -and $framework.InnerText.Trim() -cin @('true', '1'))
            ResourcePackage = ($null -ne $resource -and $resource.InnerText.Trim() -cin @('true', '1'))
            Dependencies = @($document.SelectNodes('/p:Package/p:Dependencies/p:PackageDependency', $ns) |
                ForEach-Object {
                    [pscustomobject]@{
                        Name = $_.GetAttribute('Name')
                        Publisher = $_.GetAttribute('Publisher')
                        MinVersion = [version]$_.GetAttribute('MinVersion')
                    }
                })
        }
    }
    finally { $archive.Dispose() }
}

function Get-PackageSigner([string]$Path) {
    # Read the APPX/MSIX signature directly; Get-AuthenticodeSignature can lack its SIP.
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entries = @($archive.Entries | Where-Object { $_.FullName -ieq 'AppxSignature.p7x' })
        if ($entries.Count -ne 1 -or $entries[0].Length -gt 1048576) {
            Stop-Experiment 'invalid-package-signature-container'
        }
        $stream = $entries[0].Open()
        $memory = [IO.MemoryStream]::new()
        try {
            $stream.CopyTo($memory)
            $bytes = $memory.ToArray()
        }
        finally { $memory.Dispose(); $stream.Dispose() }
        if ($bytes.Length -lt 5 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -cne 'PKCX') {
            Stop-Experiment 'invalid-package-signature-header'
        }
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new()
        $cms.Decode([byte[]]$bytes[4..($bytes.Length - 1)])
        if ($cms.SignerInfos.Count -ne 1 -or $null -eq $cms.SignerInfos[0].Certificate) {
            Stop-Experiment 'requires-single-package-signer'
        }
        # This identifies the signer only. Native SignTool/deployment checks hashes and trust.
        $certificate = $cms.SignerInfos[0].Certificate
        try {
            return [pscustomobject]@{
                Subject = $certificate.Subject
                CertificateBase64 = [Convert]::ToBase64String($certificate.RawData)
            }
        }
        finally { $certificate.Dispose() }
    }
    finally { $archive.Dispose() }
}

function Confirm-TrustedFrameworkSignature([string]$Path) {
    # Match the x64 SDK verifier used by the build and sign-only script; emulated on ARM64.
    $tool = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe'
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { Stop-Experiment 'requires-sdk26100-signature-verifier' }
    $PSNativeCommandUseErrorActionPreference = $false
    $ErrorActionPreference = 'Continue'
    $global:LASTEXITCODE = $null
    $text = (@(& $tool verify /pa /v $Path 2>&1) | Out-String)
    $exitCode = $global:LASTEXITCODE
    $category = if ($exitCode -eq 0) { 'verified' }
        elseif ($text -match 'root certificate.*not trusted|untrusted root') { 'untrusted-root' }
        elseif ($text -match 'expired|not within its validity period') { 'certificate-validity' }
        elseif ($text -match 'No signature found|not signed') { 'signature-not-found' }
        else { 'verification-failed' }
    $report.dependencies.signatureChecks += [ordered]@{
        file = [IO.Path]::GetFileName($Path)
        toolArchitecture = 'x64; emulated on ARM64'
        exitCode = $exitCode
        hresults = @([regex]::Matches($text, '\b0x[0-9a-fA-F]{8}\b') | ForEach-Object { $_.Value } | Select-Object -Unique)
        category = $category
    }
    if ($null -eq $exitCode -or $exitCode -ne 0) {
        Stop-Experiment 'framework-dependency-signature-verification-failed'
    }
}

function Get-PrinterSnapshot {
    # Intentionally unfiltered: publish only aggregate counts and these fixed, non-user names.
    $printers = @(Get-Printer -ErrorAction Stop -WarningAction SilentlyContinue)
    $names = @($printers | ForEach-Object { $_.Name })
    return [ordered]@{
        totalCount = $printers.Count
        VirtualPdfPrinter = ($names -contains $expectedQueues[0])
        VirtualPostScriptPrinter = ($names -contains $expectedQueues[1])
        knownInboxQueues = @($inboxQueues | Where-Object { $names -contains $_ })
    }
}

function Get-SpoolerState {
    # Absence or a stopped service is an observation, not an inferred registration failure.
    $services = @(Get-Service -ErrorAction Stop | Where-Object { $_.Name -ieq 'Spooler' })
    if ($services.Count -eq 0) { return 'absent' }
    return $services[0].Status.ToString()
}

function Get-ExactPackages($Manifest, [switch]$AllUsers) {
    $arguments = @{ Name = $Manifest.Name; ErrorAction = 'Stop'; WarningAction = 'SilentlyContinue' }
    if ($AllUsers) { $arguments.AllUsers = $true }
    return @(Get-AppxPackage @arguments | Where-Object {
        $_.Name -ceq $Manifest.Name -and $_.Publisher -ceq $Manifest.Publisher -and
        [version]$_.Version -eq $Manifest.Version -and
        $_.Architecture.ToString() -ieq $Manifest.Architecture -and
        [string]$_.ResourceId -ceq $Manifest.ResourceId
    })
}

function New-InstallTarget($Manifest, [string]$Path) {
    $existing = @(Get-ExactPackages $Manifest -AllUsers)
    $current = @(Get-ExactPackages $Manifest)
    return [pscustomobject]@{
        Manifest = $Manifest
        Path = $Path
        ExistingFullNames = @($existing | ForEach-Object { $_.PackageFullName })
        ExistingCurrentUserFullNames = @($current | ForEach-Object { $_.PackageFullName })
        Attempted = $false
    }
}

function Get-OwnedPackageRegistrations($Target) {
    $shared = $Target.ExistingFullNames.Count -ne 0
    $existing = if ($shared) { $Target.ExistingCurrentUserFullNames } else { $Target.ExistingFullNames }
    $packages = @(Get-ExactPackages $Target.Manifest -AllUsers:(-not $shared))
    foreach ($package in $packages) {
        if ($existing -cnotcontains $package.PackageFullName) {
            [pscustomobject]@{
                PackageFullName = $package.PackageFullName
                AllUsers = (-not $shared)
            }
        }
    }
}

try {
    $output = [IO.Path]::GetFullPath($OutputPath)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output)) | Out-Null
    $evidenceStream = [IO.File]::Open($output, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::Read)

    $stage = 'host-guard'
    if (-not $report.host.githubActions -or -not $report.host.githubHosted) {
        Stop-Experiment 'requires-disposable-github-hosted-job'
    }
    $report.host.windows = ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT)
    if (-not $report.host.windows) { Stop-Experiment 'requires-windows' }
    $report.host.osArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    $report.host.processArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    if ($report.host.osArchitecture -ine 'Arm64') { Stop-Experiment 'requires-native-arm64-os' }
    $os = Get-CimInstance -ClassName Win32_OperatingSystem -Property Version, BuildNumber, ProductType
    $report.host.osVersion = ([version]$os.Version).ToString()
    $report.host.build = [int]$os.BuildNumber
    $report.host.kernelFileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo(
        (Join-Path $env:WINDIR 'System32/ntoskrnl.exe')).FileVersion
    $report.host.clientOS = ($os.ProductType -eq 1)
    if (-not $report.host.clientOS -or ([version]$os.Version).Major -ne 10 -or
        $report.host.build -lt 26100) {
        Stop-Experiment 'requires-windows11-build-26100-or-newer'
    }
    $windowsIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($windowsIdentity)
        $report.host.elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    }
    finally { $windowsIdentity.Dispose() }
    if (-not $report.host.elevated) { Stop-Experiment 'requires-elevated-administrator' }

    $stage = 'modules'
    if ($PSVersionTable.PSEdition -eq 'Core') {
        Import-Module Appx -UseWindowsPowerShell -ErrorAction Stop -WarningAction SilentlyContinue
        Import-Module PrintManagement -UseWindowsPowerShell -ErrorAction Stop -WarningAction SilentlyContinue
    }
    else {
        Import-Module Appx -ErrorAction Stop -WarningAction SilentlyContinue
        Import-Module PrintManagement -ErrorAction Stop -WarningAction SilentlyContinue
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if ($PSVersionTable.PSEdition -eq 'Core') {
        Add-Type -AssemblyName System.Security.Cryptography.Pkcs
    }
    else {
        Add-Type -AssemblyName System.Security
    }

    $stage = 'validate-main-package'
    $packageFile = (Get-Item -LiteralPath $PackagePath -ErrorAction Stop)
    if ($packageFile.PSIsContainer -or $packageFile.Extension -notin @('.appx', '.msix')) {
        Stop-Experiment 'requires-single-appx-or-msix'
    }
    $mainManifest = Get-AppxManifest $packageFile.FullName
    if ($mainManifest.Name -cne $identityName -or $mainManifest.Publisher -cne $publisher -or
        $mainManifest.Version -ne $packageVersion -or $mainManifest.Architecture -cne 'arm64' -or
        $mainManifest.ManifestNamespace -cne 'http://schemas.microsoft.com/appx/manifest/foundation/windows10' -or
        $mainManifest.Framework -or $mainManifest.ResourcePackage -or $mainManifest.ResourceId -cne '') {
        Stop-Experiment 'unexpected-main-package-identity'
    }
    $report.package.sha256 = (Get-FileHash -LiteralPath $packageFile.FullName -Algorithm SHA256).Hash

    $stage = 'validate-certificate'
    $certificateBytes = [IO.File]::ReadAllBytes((Get-Item -LiteralPath $CertificatePath).FullName)
    if ([Security.Cryptography.X509Certificates.X509Certificate2]::GetCertContentType($certificateBytes) -ne
        [Security.Cryptography.X509Certificates.X509ContentType]::Cert) {
        Stop-Experiment 'requires-public-certificate-only'
    }
    $publicCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certificateBytes)
    $certificateThumbprint = $publicCertificate.Thumbprint
    $now = [DateTime]::Now
    $codeSigning = @($publicCertificate.Extensions |
        Where-Object { $_ -is [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension] } |
        ForEach-Object { $_.EnhancedKeyUsages } |
        Where-Object { $_.Value -eq '1.3.6.1.5.5.7.3.3' })
    if ($publicCertificate.HasPrivateKey -or $publicCertificate.Subject -cne $publisher -or
        $publicCertificate.Issuer -cne $publisher -or $now -lt $publicCertificate.NotBefore -or
        $now -gt $publicCertificate.NotAfter -or $codeSigning.Count -eq 0) {
        Stop-Experiment 'unexpected-signing-certificate'
    }
    $signature = Get-PackageSigner $packageFile.FullName
    if ($signature.Subject -cne $publisher -or $signature.CertificateBase64 -cne
        [Convert]::ToBase64String($publicCertificate.RawData)) {
        Stop-Experiment 'invalid-main-package-signature'
    }
    $report.package.signature = 'signer-matches-provided-certificate-deployment-validation-pending'

    $stage = 'preflight'
    if (@(Get-AppxPackage -AllUsers -Name $identityName -ErrorAction Stop).Count -ne 0) {
        Stop-Experiment 'reference-package-already-exists'
    }
    $report.spooler.before = Get-SpoolerState
    $report.printers.before = Get-PrinterSnapshot
    if ($report.printers.before.VirtualPdfPrinter -or $report.printers.before.VirtualPostScriptPrinter) {
        Stop-Experiment 'original-control-queue-name-conflict'
    }

    $stage = 'validate-dependencies'
    $dependencyTargets = [Collections.Generic.List[object]]::new()
    $seenNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    if ($DependencyDirectory) {
        $directory = Get-Item -LiteralPath $DependencyDirectory -ErrorAction Stop
        if (-not $directory.PSIsContainer) { Stop-Experiment 'requires-dependency-directory' }
        # Inspect only immediate APPXs, never recurse into downloaded or installed trees.
        $files = @(Get-ChildItem -LiteralPath $directory.FullName -File |
            Where-Object { $_.Extension -ieq '.appx' })
        foreach ($file in $files) {
            $manifest = Get-AppxManifest $file.FullName
            $declarations = @($mainManifest.Dependencies | Where-Object {
                $_.Name -ceq $manifest.Name -and $_.Publisher -ceq $manifest.Publisher
            })
            if (-not $manifest.Framework -or $manifest.ResourcePackage -or $manifest.ResourceId -cne '' -or
                $manifest.Architecture -notin @('arm64', 'neutral') -or
                $manifest.Publisher -cnotin $microsoftPublishers -or $declarations.Count -ne 1 -or
                -not $seenNames.Add($manifest.Name)) {
                Stop-Experiment 'unapproved-framework-dependency'
            }
            if ($manifest.Version -lt $declarations[0].MinVersion) {
                Stop-Experiment 'framework-dependency-version-too-low'
            }
            $dependencySignature = Get-PackageSigner $file.FullName
            if ($dependencySignature.Subject -cne $manifest.Publisher) {
                Stop-Experiment 'framework-dependency-signer-publisher-mismatch'
            }
            Confirm-TrustedFrameworkSignature $file.FullName
            $dependencyTargets.Add((New-InstallTarget $manifest $file.FullName))
            $report.dependencies.validated++
        }
    }
    # Keep dependency-first order for reverse cleanup; deployment resolves all supplied paths together.
    $pending = [Collections.Generic.List[object]]::new()
    foreach ($target in $dependencyTargets) { $pending.Add($target) }
    while ($pending.Count -ne 0) {
        $ready = @($pending | Where-Object {
            $dependencyNames = @($_.Manifest.Dependencies | ForEach-Object { $_.Name })
            @($pending | Where-Object { $_.Manifest.Name -cin $dependencyNames }).Count -eq 0
        })
        if ($ready.Count -eq 0) { Stop-Experiment 'cyclic-framework-dependencies' }
        foreach ($target in $ready) {
            $installTargets.Add($target)
            $pending.Remove($target) | Out-Null
        }
    }
    $mainTarget = New-InstallTarget $mainManifest $packageFile.FullName
    $installTargets.Add($mainTarget)

    $stage = 'trust-certificate'
    $certificateStore = [Security.Cryptography.X509Certificates.X509Store]::new('TrustedPeople', 'LocalMachine')
    try {
        $certificateStore.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        $existingCertificates = @($certificateStore.Certificates | Where-Object {
            $_.Thumbprint -ceq $certificateThumbprint
        })
        if ($existingCertificates.Count -ne 0) { Stop-Experiment 'trust-certificate-already-exists' }
        # Add only the supplied public leaf. No roots, intermediate certificates, or signing keys.
        # Ownership starts after the absence check, so a partially successful import is cleaned too.
        $certificateCreated = $true
        $certificateStore.Add($publicCertificate)
        $report.trust.created = $true
    }
    finally { $certificateStore.Dispose() }

    $report.runtime = 'running'
    $dependencyPaths = [Collections.Generic.List[string]]::new()
    foreach ($target in $dependencyTargets) {
        $stage = 'prepare-dependency-deployment'
        $declaration = @($mainManifest.Dependencies | Where-Object { $_.Name -ceq $target.Manifest.Name })[0]
        $satisfied = @(Get-AppxPackage -Name $target.Manifest.Name -ErrorAction Stop | Where-Object {
            $_.Name -ceq $target.Manifest.Name -and $_.Publisher -ceq $target.Manifest.Publisher -and
            [version]$_.Version -ge $declaration.MinVersion -and $_.IsFramework -and
            $_.Architecture.ToString() -iin @('arm64', 'neutral') -and $_.Status.ToString() -ieq 'Ok'
        })
        if ($satisfied.Count -ne 0) {
            $report.dependencies.alreadySatisfied++
            continue
        }
        if ($target.ExistingCurrentUserFullNames.Count -ne 0) {
            Stop-Experiment 'will-not-change-unhealthy-preexisting-current-user-framework'
        }
        $dependencyPaths.Add($target.Path)
    }

    $stage = 'install-main-package'
    foreach ($target in $dependencyTargets) {
        if ($dependencyPaths.Contains($target.Path)) {
            $target.Attempted = $true
            $report.dependencies.attempted++
            if ($target.ExistingFullNames.Count -ne 0) { $report.dependencies.sharedRegistrationAttempts++ }
        }
    }
    $mainTarget.Attempted = $true
    $report.package.registration = 'attempted'
    $deployment = @{ Path = $mainTarget.Path; ErrorAction = 'Stop'; WarningAction = 'SilentlyContinue' }
    if ($dependencyPaths.Count -ne 0) { $deployment.DependencyPath = $dependencyPaths.ToArray() }
    Add-AppxPackage @deployment | Out-Null
    $registered = @(Get-ExactPackages $mainManifest)
    if ($registered.Count -ne 1) { Stop-Experiment 'main-registration-not-found' }
    $report.package.registration = 'installed'
    $report.package.signature = 'validated-by-appx-deployment'
    $report.package.status = if ($registered[0].Status.ToString() -ieq 'Ok') { 'ok' } else { 'not-ok' }

    $stage = 'observe-printers'
    for ($attempt = 1; $attempt -le 6; $attempt++) {
        Start-Sleep -Seconds 10
        $report.printers.after = Get-PrinterSnapshot
        $report.printers.observationCount = $attempt
        if ($report.printers.after.VirtualPdfPrinter -and $report.printers.after.VirtualPostScriptPrinter) {
            break
        }
    }
    $report.spooler.after = Get-SpoolerState
    $registered = @(Get-ExactPackages $mainManifest)
    $report.package.status = if ($registered.Count -eq 1 -and $registered[0].Status.ToString() -ieq 'Ok') {
        'ok'
    } else { 'not-ok' }
    $presentCount = [int]$report.printers.after.VirtualPdfPrinter +
        [int]$report.printers.after.VirtualPostScriptPrinter
    $report.printers.outcome = @('absent', 'some', 'present')[$presentCount]
    $report.runtime = 'completed'
    $report.outcome = $report.printers.outcome
}
catch {
    $code = if ($failureCode) { $failureCode } else { 'operation-failed' }
    $report.error = Get-SanitizedError $_ $stage $code
    if ($report.runtime -eq 'running') { $report.runtime = 'failed'; $report.outcome = 'failed' }
    throw [InvalidOperationException]::new(
        "Hosted registration failed: stage=$stage; code=$code; HRESULT=$($report.error.hresult).")
}
finally {
    # Rediscover after every attempted deployment, including one that threw after partially installing.
    for ($index = $installTargets.Count - 1; $index -ge 0; $index--) {
        $target = $installTargets[$index]
        if (-not $target.Attempted) { continue }
        try {
            $created = @(Get-OwnedPackageRegistrations $target)
            foreach ($package in $created) {
                $remove = @{ Package = $package.PackageFullName; ErrorAction = 'Stop'; WarningAction = 'SilentlyContinue' }
                if ($package.AllUsers) { $remove.AllUsers = $true }
                Remove-AppxPackage @remove | Out-Null
                if (@(Get-ExactPackages $target.Manifest -AllUsers:$package.AllUsers | Where-Object {
                    $_.PackageFullName -ceq $package.PackageFullName
                }).Count -ne 0) {
                    throw [InvalidOperationException]::new('Exact package remains registered.')
                }
                if ($package.AllUsers) { $report.cleanup.packagesRemoved++ }
                else { $report.cleanup.sharedFrameworkRegistrationsRemoved++ }
            }
            if ($target.ExistingFullNames.Count -ne 0) {
                $remaining = @(Get-ExactPackages $target.Manifest -AllUsers | ForEach-Object { $_.PackageFullName })
                foreach ($name in $target.ExistingFullNames) {
                    if ($remaining -cnotcontains $name) {
                        throw [InvalidOperationException]::new('Pre-existing shared framework no longer present.')
                    }
                }
            }
        }
        catch {
            $cleanupErrors.Add((Get-SanitizedError $_ 'cleanup-package' 'exact-package-cleanup-failed'))
        }
    }
    if ($certificateCreated) {
        try {
            $store = [Security.Cryptography.X509Certificates.X509Store]::new('TrustedPeople', 'LocalMachine')
            try {
                $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
                $createdCertificates = @($store.Certificates | Where-Object {
                    $_.Thumbprint -ceq $certificateThumbprint -and
                    [Convert]::ToBase64String($_.RawData) -ceq [Convert]::ToBase64String($publicCertificate.RawData)
                })
                if ($createdCertificates.Count -ne 0) { $report.trust.created = $true }
                foreach ($certificate in $createdCertificates) { $store.Remove($certificate) }
                if (@($store.Certificates | Where-Object {
                    $_.Thumbprint -ceq $certificateThumbprint
                }).Count -ne 0) {
                    throw [InvalidOperationException]::new('Exact certificate remains trusted.')
                }
                $report.trust.removed = $true
            }
            finally { $store.Dispose() }
        }
        catch {
            $cleanupErrors.Add((Get-SanitizedError $_ 'cleanup-certificate' 'exact-certificate-cleanup-failed'))
        }
    }
    if ($null -ne $publicCertificate) { $publicCertificate.Dispose() }
    $report.cleanup.errors = @($cleanupErrors.ToArray())
    $report.cleanup.completed = ($cleanupErrors.Count -eq 0)
    if ($cleanupErrors.Count -ne 0) { $report.outcome = 'cleanup-failed' }
    if ($null -ne $evidenceStream) {
        try {
            $json = $report | ConvertTo-Json -Depth 10
            $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json + [Environment]::NewLine)
            $evidenceStream.Write($bytes, 0, $bytes.Length)
            $evidenceStream.Flush()
        }
        catch {
            $safe = Get-SanitizedError $_ 'write-evidence' 'evidence-write-failed'
            throw [InvalidOperationException]::new(
                "Hosted registration failed: stage=write-evidence; HRESULT=$($safe.hresult).")
        }
        finally { $evidenceStream.Dispose() }
    }
    if ($cleanupErrors.Count -ne 0) {
        throw [InvalidOperationException]::new(
            "Hosted registration cleanup failed: stage=$($cleanupErrors[0].stage); HRESULT=$($cleanupErrors[0].hresult).")
    }
}
