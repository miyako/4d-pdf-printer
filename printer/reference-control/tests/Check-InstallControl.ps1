# No deployment, certificate creation or trust-store changes. Only Appx/PrintManagement
# and signature verification are mocked; JSON, paths, hashes, CER and manifests are real.
[CmdletBinding()]
param(
    [switch]$RequireWindowsPowerShell,
    [string]$RealArtifactDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RequireWindowsPowerShell -and ($PSVersionTable.PSEdition -cne 'Desktop' -or
    $PSVersionTable.PSVersion.Major -ne 5 -or $PSVersionTable.PSVersion.Minor -ne 1 -or
    $env:OS -cne 'Windows_NT')) { throw 'CI must execute this test in Windows PowerShell 5.1.' }
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'Install-Control.ps1')
$script:realSignatureVerifier = ${function:Confirm-InstallSignature}
$script:passed = 0
$script:microsoftPublisher = 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US'

# Exact architecture-evidence.json from run 40668170711, artifact 12806758379.
# Synthetic package tests replace only the hashes; the optional real-artifact test
# uses the downloaded packages and their original hashes. Never download in tests.
$script:artifactJson = @'
[
  {
    "file": "PsaReferenceControl.arm64.msix",
    "sha256": "e780082071c909bbf677ad60a78109a67af725742c093530d91c297189e9f52f",
    "name": "Miyako.PsaReferenceControl",
    "version": "1.1.14.0",
    "architecture": "arm64",
    "signature": "unsigned",
    "peFiles": [
      {
        "path": "clrcompression.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "XpsUtil.winmd",
        "machine": "0x014c",
        "architecture": "ILONLY AnyCPU (not native x86)"
      },
      {
        "path": "XpsUtil.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "PrintSupportApp.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "PrintSupportApp.exe",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "Tasks.winmd",
        "machine": "0xaa64",
        "architecture": "ARM64"
      }
    ]
  },
  {
    "file": "Microsoft.NET.Native.Framework.2.2.appx",
    "sha256": "0f79f06c45a7e8925fd7191c7227c416f417bde07ae62dac89ad02f88f1906d6",
    "name": "Microsoft.NET.Native.Framework.2.2",
    "version": "2.2.27912.0",
    "architecture": "arm64",
    "signature": "SignTool /pa passed",
    "peFiles": [
      {
        "path": "SharedLibrary.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      }
    ]
  },
  {
    "file": "Microsoft.NET.Native.Runtime.2.2.appx",
    "sha256": "47de27af62a9a31d123a522c4a74056b8a10d15307de9ebead0adf684f3df45d",
    "name": "Microsoft.NET.Native.Runtime.2.2",
    "version": "2.2.28604.0",
    "architecture": "arm64",
    "signature": "SignTool /pa passed",
    "peFiles": [
      {
        "path": "mrt100.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "mrt100_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "mrt_map.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      }
    ]
  },
  {
    "file": "Microsoft.UI.Xaml.2.4.appx",
    "sha256": "8d45750f2428d9fdf374218700a8d071714196c9be4a3f356011bd6bc6205081",
    "name": "Microsoft.UI.Xaml.2.4",
    "version": "2.42005.1001.0",
    "architecture": "arm64",
    "signature": "SignTool /pa passed",
    "peFiles": [
      {
        "path": "Microsoft.UI.Xaml.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "Microsoft.UI.Xaml.winmd",
        "machine": "0x014c",
        "architecture": "ILONLY AnyCPU (not native x86)"
      }
    ]
  },
  {
    "file": "Microsoft.VCLibs.ARM64.14.00.appx",
    "sha256": "c73d0f55dda331f9dcbefc99ff5a420b62120773d2917387639382aa478533ee",
    "name": "Microsoft.VCLibs.140.00",
    "version": "14.0.33519.0",
    "architecture": "arm64",
    "signature": "SignTool /pa passed",
    "peFiles": [
      {
        "path": "concrt140_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "msvcp140_1_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "msvcp140_2_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "msvcp140_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "msvcp140_atomic_wait_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "msvcp140_codecvt_ids_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "vcamp140_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "vccorlib140_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "vcomp140_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      },
      {
        "path": "vcruntime140_app.dll",
        "machine": "0xaa64",
        "architecture": "ARM64"
      }
    ]
  }
]
'@
# Public-only throwaway certificate. No private key is retained or needed;
# validity/trust and PKCX verification belong to the mocked signature boundary.
$script:certificateBase64 = 'MIICwzCCAaugAwIBAgIJAJlDtFKbYP1GMA0GCSqGSIb3DQEBCwUAMCExHzAdBgNVBAMTFlBzYVJlZmVyZW5jZUNvbnRyb2xEZXYwHhcNMjYxMDI3MTAzNjI3WhcNMjYxMDI5MTAzNjI3WjAhMR8wHQYDVQQDExZQc2FSZWZlcmVuY2VDb250cm9sRGV2MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAufBnMLxFIqt2kNUOM4LBITzLw7qh5ns4dUSVOMItBE3egqWekCgjAi+YE7+dBv/cn5i2zp528RsFMCtoYqknNIdhrTbaFaCZWXmb0vXT/4nbezqCS/Mmw+t/2oNyUn/y9d0WajUvS/+TEtkp5T4pLGB2RB+3rIDbtB7fQvcLWVyrrgdm435twuxoS18HL7f4rtQeHMXCayy3XK6zpzyXELyvD5s5ZAYVX30nPljYIJycNV4d5DsmPiEdQXUIm8MDvUECA7d8BijV6fPXVv/01bj9a1lUUdwtfdXtCuDAKh8OXaO7X7PBcKovKuxw/9o8sr32OyL2Clr3ZNZOjIJqrQIDAQABMA0GCSqGSIb3DQEBCwUAA4IBAQAHYk8cMIi8j2/bU5gv6kzU77cF0Z0l2y/7+Um1EpNVmHeUTXVe088qmx6RzsnQ4nZEj/rIy+rehZ5hes+2HOn9jD6VYU8e8nqBSirrYwTvpWiK8K/TUYtqTFLT0reqFcnq0RFb6cF8Z6rScQsr0INxMV2/okz5sG0r6NRi0eeElAOtSjutwlzQ/9B+ph4evkjWBi7aY2DPPE6TjlorIRDcX1HI0hB9sXEhW+Bko2Y4DYz2pNX7vZuWquLKPx7sRazWgAJKwi6PbPMsv0gj1CEm/uz3hLz14jCndwoFCLky5HhiqU8APRDROSjcQ+bEhmMdVTkn9nTP32z3IXj8hDZZ'

function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'." }
}
function Expect-Failure([scriptblock]$Action, [string]$Pattern, [int]$ExpectedAddCalls = 0) {
    try { & $Action | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) {
            throw "Expected '$Pattern', got: $($_.Exception.Message)"
        }
        Assert-Equal $script:addCalls.Count $ExpectedAddCalls
        return
    }
    throw "Expected failure matching '$Pattern'."
}
function Save-Evidence {
    # Serialize records, not PS5.1's extended JSON-root array wrapper.
    $script:evidence | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $script:evidencePath -Encoding UTF8
}
function Save-Receipt {
    $script:receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $script:receiptPath -Encoding UTF8
}
function Set-FixtureManifest([int]$Index, [scriptblock]$Change) {
    $path = if ($Index -eq 0) { $script:receipt.OutputPath } else {
        Join-Path $script:installArgs.ArtifactDirectory ('Dependencies/arm64/' + $script:evidence[$Index].file)
    }
    $zip = [IO.Compression.ZipFile]::Open($path, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $entry = $zip.GetEntry('AppxManifest.xml')
        $reader = [IO.StreamReader]::new($entry.Open())
        try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $entry.Delete()
        $writer = [IO.StreamWriter]::new($zip.CreateEntry('AppxManifest.xml').Open())
        try { $writer.Write((& $Change $text)) } finally { $writer.Dispose() }
    } finally { $zip.Dispose() }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($Index -eq 0) { $script:receipt.OutputSha256 = $hash; Save-Receipt }
    else { $script:evidence[$Index].sha256 = $hash; Save-Evidence }
}
function Write-TestPackage([string]$Path, $Record, [object[]]$Dependencies = @()) {
    $declarations = (@($Dependencies | ForEach-Object {
        '<PackageDependency Name="{0}" Publisher="{1}" MinVersion="{2}" />' -f $_.Name, $_.Publisher, $_.MinVersion
    }) -join '')
    $isMain = $Record.name -ceq 'Miyako.PsaReferenceControl'
    $publisher = if ($isMain) { 'CN=PsaReferenceControlDev' } else { $script:microsoftPublisher }
    $framework = if ($isMain) { '' } else { '<Framework>true</Framework>' }
    $manifest = @"
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
 <Identity Name="$($Record.name)" Publisher="$publisher" Version="$($Record.version)" ProcessorArchitecture="arm64" />
 <Properties>$framework</Properties><Dependencies>$declarations</Dependencies>
</Package>
"@
    $zip = [IO.Compression.ZipFile]::Open($Path, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $writer = [IO.StreamWriter]::new($zip.CreateEntry('AppxManifest.xml').Open())
        try { $writer.Write($manifest) } finally { $writer.Dispose() }
    } finally { $zip.Dispose() }
}
function New-Fixture([string]$RealArtifact) {
    $caseRoot = Join-Path $script:fixture ([Guid]::NewGuid().ToString('N'))
    $artifact = Join-Path $caseRoot 'artifact'
    $signed = Join-Path $caseRoot 'signed'
    $deps = Join-Path $artifact 'Dependencies/arm64'
    New-Item -ItemType Directory -Path $deps, $signed -Force | Out-Null
    $script:evidence = ConvertFrom-Json -InputObject $script:artifactJson
    $declarations = @($script:evidence | Where-Object { $_.name -cne 'Miyako.PsaReferenceControl' } |
        ForEach-Object {
            [pscustomobject]@{
                Name = $_.name; Publisher = $script:microsoftPublisher
                MinVersion = $_.version; ProcessorArchitecture = ''
            }
        })
    foreach ($record in $script:evidence) {
        $relative = if ($record.name -ceq 'Miyako.PsaReferenceControl') { $record.file } else { 'Dependencies/arm64/' + $record.file }
        $path = Join-Path $artifact $relative
        if ($RealArtifact) {
            Copy-Item -LiteralPath (Join-Path $RealArtifact $relative) -Destination $path
        } else {
            Write-TestPackage $path $record $declarations
            $record.sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        }
    }
    $unsignedPath = Join-Path $artifact 'PsaReferenceControl.arm64.msix'
    $packagePath = Join-Path $signed 'PsaReferenceControl.arm64.msix'
    Copy-Item -LiteralPath $unsignedPath -Destination $packagePath
    $zip = [IO.Compression.ZipFile]::Open($packagePath, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $stream = $zip.CreateEntry('AppxSignature.p7x').Open()
        try { $stream.WriteByte(1) } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
    $certificatePath = Join-Path $signed 'PsaReferenceControlDev.cer'
    [IO.File]::WriteAllBytes($certificatePath, [Convert]::FromBase64String($script:certificateBase64))
    $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certificatePath)
    try {
        $script:receipt = [pscustomobject]@{
            SchemaVersion = 1; SignedAtUtc = '2026-10-28T09:59:00.0000000Z'
            InputPath = $unsignedPath; InputSha256 = $script:evidence[0].sha256
            OutputPath = $packagePath; OutputSha256 = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
            Certificate = [pscustomobject]@{
                Thumbprint = $cert.Thumbprint; Subject = $cert.Subject; Store = 'CurrentUser/My'
                PublicCertificatePath = $certificatePath
            }
            Identity = [pscustomobject]@{
                Name = 'Miyako.PsaReferenceControl'; Publisher = 'CN=PsaReferenceControlDev'
                Version = '1.1.14.0'; ProcessorArchitecture = 'arm64'
            }
            Dependencies = $declarations
            SignToolPath = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
            Timestamped = $false
        }
    } finally { $cert.Dispose() }
    $script:evidencePath = Join-Path $artifact 'architecture-evidence.json'
    $script:receiptPath = Join-Path $signed 'SigningReceipt.json'
    if ($RealArtifact) {
        Copy-Item -LiteralPath (Join-Path $RealArtifact 'architecture-evidence.json') -Destination $script:evidencePath
    } else { Save-Evidence }
    Save-Receipt
    $script:installArgs = @{
        ArtifactDirectory = $artifact; SignedDirectory = $signed
        ExpectedUnsignedSha256 = $script:evidence[0].sha256
    }
    $script:installed = @{}
    $script:controlPresent = $false
    $script:addCalls = @()
    $script:getCalls = @()
    $script:signatureChecks = @()
    $script:signatureFailure = ''
    $script:printers = @([pscustomobject]@{ Name = 'Microsoft Print to PDF' })
    $script:appxFailure = $false
    $script:deploymentFailure = $false
}

function Confirm-InstallSignature([string]$Path, [string]$Subject, [string]$Thumbprint) {
    $script:signatureChecks += [pscustomobject]@{ Path = $Path; Subject = $Subject; Thumbprint = $Thumbprint }
    if ($script:signatureFailure -and $Path -like "*$($script:signatureFailure)*") {
        throw 'Simulated signature/trust/publisher failure.'
    }
}
function Get-TestFramework([int]$Index, [string]$Version = '', [string]$Architecture = 'Arm64') {
    $record = $script:evidence[$Index]
    if (-not $Version) { $Version = $record.version }
    return [pscustomobject]@{
        Name = $record.name; Publisher = $script:microsoftPublisher; Version = $Version
        IsFramework = $true; Architecture = $Architecture; Status = 'Ok'
    }
}
function Get-AppxPackage {
    [CmdletBinding()]
    param([string]$Name)
    $script:getCalls += $Name
    if ($script:appxFailure) { throw 'Simulated local Appx query failure.' }
    if ($Name -ceq 'Miyako.PsaReferenceControl') {
        if ($script:controlPresent) {
            [pscustomobject]@{
                Name = $Name; Publisher = 'CN=PsaReferenceControlDev'; Version = '1.1.14.0'
                Architecture = 'Arm64'; Status = 'Ok'
            }
        }
    } elseif ($script:installed.ContainsKey($Name)) { $script:installed[$Name] }
}
function Get-Printer {
    [CmdletBinding()]
    param()
    $script:printers
}
function Add-AppxPackage {
    [CmdletBinding()]
    param([string]$Path, [string[]]$DependencyPath)
    $script:addCalls += [pscustomobject]@{
        Path = $Path; DependencyPath = $DependencyPath; HasDependencyPath = $PSBoundParameters.ContainsKey('DependencyPath')
    }
    if ($script:deploymentFailure) { throw 'Simulated deployment failure.' }
    $script:controlPresent = $true
}
function Test-Case([string]$Label, [scriptblock]$Action, [string]$RealArtifact = '') {
    New-Fixture $RealArtifact
    & $Action
    $script:passed++
    Write-Host "PASS: $Label"
}
function Invoke-TestInstall { Invoke-ControlInstall @script:installArgs | Out-Null }
function Assert-AllPreflighted {
    Assert-Equal $script:signatureChecks.Count 5
    Assert-Equal $script:signatureChecks[0].Thumbprint $script:receipt.Certificate.Thumbprint
    foreach ($check in $script:signatureChecks[1..4]) { Assert-Equal $check.Subject $script:microsoftPublisher }
}

$script:fixture = Join-Path $PSScriptRoot ('.install-control-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $script:fixture | Out-Null
try {
    Test-Case 'real root-array shape enumerates five records/four frameworks on PS5.1' {
        $recordedPath = Join-Path $script:fixture 'recorded-architecture-evidence.json'
        [IO.File]::WriteAllText($recordedPath, $script:artifactJson, [Text.UTF8Encoding]::new($false))
        $root = Get-Content -LiteralPath $recordedPath -Raw | ConvertFrom-Json
        Assert-Equal $root.Count 5
        $fixed = @($root | Where-Object { $_.name -cne 'Miyako.PsaReferenceControl' })
        Assert-Equal $fixed.Count 4
        if ($PSVersionTable.PSVersion.Major -eq 5) {
            $legacy = & {
                # Match the published snippet, which did not enable StrictMode.
                Set-StrictMode -Off
                $broken = @(Get-Content -LiteralPath $recordedPath -Raw | ConvertFrom-Json |
                    Where-Object name -ne 'Miyako.PsaReferenceControl')
                [pscustomobject]@{
                    Count = $broken.Count
                    FirstType = $(if ($broken.Count -gt 0) { $broken[0].GetType().FullName } else { '<empty>' })
                }
            }
            Write-Host ("PS5.1 recorded JSON reproduction: root={0}[{1}]; old records={2}, first type={3}; fixed frameworks={4}" -f
                $root.GetType().FullName, $root.Count, $legacy.Count, $legacy.FirstType, $fixed.Count)
            Assert-Equal $legacy.Count 1
        }
        Invoke-TestInstall
        Assert-AllPreflighted
        Assert-Equal $script:addCalls.Count 1
        Assert-Equal $script:addCalls[0].Path $script:receipt.OutputPath
        Assert-Equal $script:addCalls[0].DependencyPath.Count 4
    }
    Test-Case 'same installed frameworks omit DependencyPath entirely' {
        foreach ($i in 1..4) { $script:installed[$script:evidence[$i].name] = Get-TestFramework $i }
        Invoke-TestInstall
        Assert-AllPreflighted
        Assert-Equal $script:addCalls[0].HasDependencyPath $false
    }
    Test-Case 'one missing framework passes only its supplied path' {
        foreach ($i in 1..3) { $script:installed[$script:evidence[$i].name] = Get-TestFramework $i }
        Invoke-TestInstall
        Assert-Equal $script:addCalls[0].DependencyPath.Count 1
        Assert-Equal ([IO.Path]::GetFileName($script:addCalls[0].DependencyPath[0])) $script:evidence[4].file
    }
    Test-Case 'newer ARM64 and neutral frameworks satisfy without downgrade' {
        foreach ($i in 1..4) {
            $architecture = if ($i % 2 -eq 0) { 'Neutral' } else { 'Arm64' }
            $script:installed[$script:evidence[$i].name] = Get-TestFramework $i '99.0.0.0' $architecture
        }
        Invoke-TestInstall
        Assert-Equal $script:addCalls[0].HasDependencyPath $false
    }
    Test-Case 'same-version neutral frameworks satisfy without downgrade' {
        foreach ($i in 1..4) { $script:installed[$script:evidence[$i].name] = Get-TestFramework $i '' 'Neutral' }
        Invoke-TestInstall
        Assert-Equal $script:addCalls[0].HasDependencyPath $false
    }
    Test-Case 'wrong publisher, architecture, unhealthy and too-old frameworks do not satisfy' {
        foreach ($i in 1..4) { $script:installed[$script:evidence[$i].name] = Get-TestFramework $i }
        $script:installed[$script:evidence[1].name].Publisher = 'CN=Other'
        $script:installed[$script:evidence[2].name].Architecture = 'X64'
        $script:installed[$script:evidence[3].name].Status = 'Modified'
        $script:installed[$script:evidence[4].name].Version = '1.0.0.0'
        Invoke-TestInstall
        Assert-Equal $script:addCalls[0].DependencyPath.Count 4
    }
    Test-Case 'already installed control blocks without a reinstall' {
        $script:controlPresent = $true
        Expect-Failure { Invoke-TestInstall } 'already installed'
    }
    foreach ($queue in @('VirtualPdfPrinter', 'VirtualPostScriptPrinter')) {
        Test-Case "existing queue $queue blocks without changes" {
            $script:printers += [pscustomobject]@{ Name = $queue }
            Expect-Failure { Invoke-TestInstall } 'queue name collision'
        }
    }
    Test-Case 'Appx query errors propagate without fallback' {
        $script:appxFailure = $true
        Expect-Failure { Invoke-TestInstall } 'local Appx query failure'
    }
    Test-Case 'Add-AppxPackage error propagates without retry' {
        $script:deploymentFailure = $true
        Expect-Failure { Invoke-TestInstall } 'deployment failure' 1
    }
    Test-Case 'malformed JSON stops before changes' {
        Set-Content -LiteralPath $script:evidencePath -Value '{invalid' -Encoding UTF8
        Expect-Failure { Invoke-TestInstall } 'JSON|property|parsing|convert'
    }
    Test-Case 'object root instead of array is rejected' {
        Set-Content -LiteralPath $script:evidencePath -Value '{"name":"Miyako.PsaReferenceControl"}'
        Expect-Failure { Invoke-TestInstall } 'five architecture'
    }
    Test-Case 'array element instead of evidence record is rejected' {
        $script:evidence[4] = @($script:evidence[4])
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'object shape'
    }
    Test-Case 'missing evidence field is rejected' {
        $script:evidence[4].PSObject.Properties.Remove('sha256')
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'object shape'
    }
    Test-Case 'extra framework count is rejected' {
        $script:evidence += $script:evidence[4]
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'five architecture'
    }
    Test-Case 'duplicate framework name is rejected' {
        $script:evidence[4].name = $script:evidence[3].name
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'duplicate evidence'
    }
    Test-Case 'case-insensitive framework file collision is rejected' {
        $script:evidence[4].file = $script:evidence[3].file.ToUpperInvariant().Replace('.APPX', '.appx')
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'duplicate evidence'
    }
    Test-Case 'dependency traversal is rejected' {
        $script:evidence[4].file = '../Microsoft.VCLibs.ARM64.14.00.appx'
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'evidence name, file'
    }
    Test-Case 'extra supplied dependency is rejected' {
        Set-Content -LiteralPath (Join-Path $script:installArgs.ArtifactDirectory 'Dependencies/arm64/extra.appx') -Value 'extra'
        Expect-Failure { Invoke-TestInstall } 'exactly four supplied'
    }
    Test-Case 'missing dependency file is rejected even when already satisfied' {
        $script:installed[$script:evidence[4].name] = Get-TestFramework 4
        Remove-Item -LiteralPath (Join-Path $script:installArgs.ArtifactDirectory ('Dependencies/arm64/' + $script:evidence[4].file))
        Expect-Failure { Invoke-TestInstall } 'does not exist|cannot find|could not find'
    }
    Test-Case 'supplied hash mismatch is rejected even when already satisfied' {
        $script:installed[$script:evidence[4].name] = Get-TestFramework 4
        $script:evidence[4].sha256 = '0' * 64
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'SHA256'
    }
    Test-Case 'unsigned published hash mismatch is rejected' {
        $script:installArgs.ExpectedUnsignedSha256 = '0' * 64
        Expect-Failure { Invoke-TestInstall } 'SHA256'
    }
    Test-Case 'signed output hash mismatch is rejected' {
        $script:receipt.OutputSha256 = '0' * 64
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'SHA256'
    }
    Test-Case 'receipt schema type is checked' {
        $script:receipt.SchemaVersion = '1'
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'receipt schema'
    }
    Test-Case 'receipt unknown schema is rejected' {
        $script:receipt.SchemaVersion = 2
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'receipt schema'
    }
    Test-Case 'receipt dependency object instead of array is rejected' {
        $script:receipt.Dependencies = $script:receipt.Dependencies[0]
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'four framework declarations'
    }
    Test-Case 'receipt duplicate declaration is rejected' {
        $script:receipt.Dependencies[3] = $script:receipt.Dependencies[2]
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'duplicate framework declaration'
    }
    Test-Case 'receipt missing dependency is rejected' {
        $script:receipt.Dependencies = @($script:receipt.Dependencies[0..2])
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'four framework declarations'
    }
    Test-Case 'invalid receipt dependency publisher is rejected' {
        $script:receipt.Dependencies[3].Publisher = 'CN=Other'
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'publisher'
    }
    Test-Case 'receipt minimum differing from signed manifest is rejected' {
        $script:receipt.Dependencies[3].MinVersion = '14.0.33520.0'
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'differ from signed manifest'
    }
    Test-Case 'receipt identity publisher is exact' {
        $script:receipt.Identity.Publisher = 'CN=Other'
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'identity'
    }
    Test-Case 'array-valued receipt identity cannot conflate equal strings' {
        $script:receipt.Identity.Name = @('Miyako.PsaReferenceControl', 'Miyako.PsaReferenceControl')
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'nonempty evidence/receipt string'
    }
    Test-Case 'array-valued evidence signature cannot conflate equal strings' {
        $script:evidence[4].signature = @('SignTool /pa passed', 'SignTool /pa passed')
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'nonempty evidence/receipt string'
    }
    Test-Case 'supplied framework manifest publisher is checked independently of receipt' {
        Set-FixtureManifest 4 { param($text) $text.Replace($script:microsoftPublisher, 'CN=Other') }
        Expect-Failure { Invoke-TestInstall } 'Invalid supplied ARM64 framework'
    }
    Test-Case 'supplied framework manifest architecture is exact' {
        Set-FixtureManifest 4 { param($text) $text.Replace('ProcessorArchitecture="arm64"', 'ProcessorArchitecture="x64"') }
        Expect-Failure { Invoke-TestInstall } 'Invalid supplied ARM64 framework'
    }
    Test-Case 'supplied dependency must be a framework' {
        Set-FixtureManifest 4 { param($text) $text.Replace('<Framework>true</Framework>', '<Framework>false</Framework>') }
        Expect-Failure { Invoke-TestInstall } 'Invalid supplied ARM64 framework'
    }
    Test-Case 'signed main manifest identity must match receipt exactly' {
        Set-FixtureManifest 0 { param($text) $text.Replace('CN=PsaReferenceControlDev', 'CN=Other') }
        Expect-Failure { Invoke-TestInstall } 'identity'
    }
    Test-Case 'receipt identity architecture is exact' {
        $script:receipt.Identity.ProcessorArchitecture = 'x64'
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'identity'
    }
    Test-Case 'receipt output path cannot name another run' {
        $script:receipt.OutputPath += '.other'
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'receipt paths'
    }
    Test-Case 'receipt public certificate thumbprint must match' {
        $script:receipt.Certificate.Thumbprint = '0' * 40
        Save-Receipt
        Expect-Failure { Invoke-TestInstall } 'certificate differs'
    }
    Test-Case 'malformed version is rejected' {
        $script:evidence[4].version = '14.0'
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'four-part'
    }
    Test-Case 'wrong supplied architecture evidence is rejected' {
        $script:evidence[4].architecture = 'neutral'
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'evidence name, file or architecture'
    }
    Test-Case 'wrong PE architecture evidence is rejected' {
        $script:evidence[4].peFiles[0].machine = '0x8664'
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'PE architecture'
    }
    Test-Case 'unverified framework evidence is rejected' {
        $script:evidence[4].signature = 'present; trust not checked on this host'
        Save-Evidence
        Expect-Failure { Invoke-TestInstall } 'verified publisher signature'
    }
    Test-Case 'framework signature failure blocks even when installed version satisfies' {
        $script:installed[$script:evidence[4].name] = Get-TestFramework 4 '99.0.0.0'
        $script:signatureFailure = $script:evidence[4].file
        Expect-Failure { Invoke-TestInstall } 'signature/trust/publisher failure'
    }
    Test-Case 'main signer/trust failure blocks before changes' {
        $script:signatureFailure = 'PsaReferenceControl.arm64.msix'
        Expect-Failure { Invoke-TestInstall } 'signature/trust/publisher failure'
    }
    if ($RealArtifactDirectory) {
        Test-Case 'recorded artifact JSON, all real package hashes and manifests pass preflight' {
            Assert-Equal $script:installArgs.ExpectedUnsignedSha256 'e780082071c909bbf677ad60a78109a67af725742c093530d91c297189e9f52f'
            Invoke-TestInstall
            Assert-AllPreflighted
            Assert-Equal $script:addCalls[0].DependencyPath.Count 4
        } (Resolve-InstallPath $RealArtifactDirectory -Directory)
        if ($RequireWindowsPowerShell) {
            Test-Case 'real supplied Microsoft signatures verify in Windows PowerShell 5.1' {
                foreach ($i in 1..4) {
                    $path = Join-Path $script:installArgs.ArtifactDirectory ('Dependencies/arm64/' + $script:evidence[$i].file)
                    & $script:realSignatureVerifier $path $script:microsoftPublisher $null
                }
                Assert-Equal $script:addCalls.Count 0
            } (Resolve-InstallPath $RealArtifactDirectory -Directory)
        }
    }
    Write-Host "$script:passed install-control tests passed on $($PSVersionTable.PSEdition) $($PSVersionTable.PSVersion)."
} finally {
    Remove-Item -LiteralPath $script:fixture -Recurse -Force
}
