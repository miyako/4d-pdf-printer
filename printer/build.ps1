[CmdletBinding()]
param(
    [string]$SdkVersion = '10.0.26100.0',
    [switch]$Sign
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Windows with MSVC is required.' }
$root = $PSScriptRoot
$out = Join-Path $root 'out'
$build = Join-Path $out 'build'
$stage = Join-Path $out 'package'
$sdk = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin\$SdkVersion\x64"
$makeappx = Join-Path $sdk 'makeappx.exe'
if (-not (Test-Path $makeappx)) { throw "Install Windows SDK $SdkVersion (including MakeAppx)." }

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited $LASTEXITCODE" }
}

Invoke-Checked cmake @('-S', $root, '-B', $build, '-G', 'Visual Studio 17 2022',
    '-A', 'x64', "-DCMAKE_SYSTEM_VERSION=$SdkVersion")
Invoke-Checked cmake @('--build', $build, '--config', 'Release', '--parallel')
Invoke-Checked ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure')
New-Item $stage -ItemType Directory -Force | Out-Null
New-Item (Join-Path $stage 'Assets') -ItemType Directory -Force | Out-Null
New-Item (Join-Path $stage 'Config') -ItemType Directory -Force | Out-Null
Copy-Item (Join-Path $build 'Release\PsaDiagnostic.exe') $stage
Copy-Item (Join-Path $build 'Release\PsaTask.dll') $stage
Copy-Item (Join-Path $root 'package\AppxManifest.xml') $stage
Copy-Item (Join-Path $root 'package\DiagnosticPdc.xml') (Join-Path $stage 'Config')
Copy-Item (Join-Path $root 'THIRD-PARTY-NOTICES.txt') $stage

Add-Type -AssemblyName System.Drawing
$sizes = @{ StoreLogo = @(50, 50); Square44x44Logo = @(44, 44);
    Square150x150Logo = @(150, 150); SplashScreen = @(620, 300) }
foreach ($name in $sizes.Keys) {
    $size = $sizes[$name]
    $image = [System.Drawing.Bitmap]::new($size[0], $size[1])
    $graphics = [System.Drawing.Graphics]::FromImage($image)
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(32, 48, 64))
        $graphics.FillRectangle([System.Drawing.Brushes]::Orange, 5, 5, 15, 15)
        $image.Save((Join-Path $stage "Assets\$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $image.Dispose() }
}
$package = Join-Path $out 'PsaDiagnostic.msix'
Invoke-Checked $makeappx @('pack', '/d', $stage, '/p', $package, '/o')
if ($Sign) {
    # Explicit opt-in; no trust-store changes, service installation, or printer configuration here.
    $cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=PsaFeasibilityDev' `
        -KeyUsage DigitalSignature -FriendlyName 'PSA feasibility development only' `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    Export-Certificate -Cert $cert -FilePath (Join-Path $out 'PsaDiagnostic.cer') | Out-Null
    Invoke-Checked (Join-Path $sdk 'signtool.exe') @('sign', '/fd', 'SHA256', '/sha1',
        $cert.Thumbprint, '/s', 'My', $package)
}
Write-Host "Diagnostic package: $package (not a PDF converter)"
