[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Build requires Windows, VS2022 UWP C#/C++ ARM64 tools and SDK 26100.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw "Use a fresh output directory: $OutputDirectory" }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
if ($out.Length -gt 40) { throw 'Use a short output path (at most 40 characters), e.g. C:\psa-rc, for legacy UWP/NuGet tools.' }
[ordered]@{
    hostOSArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    processArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    os = (Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber)
    runnerImage = $env:ImageVersion
    targetArchitecture = 'arm64'
    sdk = '10.0.26100.0'
    runtime = 'not-tested'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'host-evidence.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'upstream.json') -Destination $out
$source = & (Join-Path $PSScriptRoot 'Fetch-Source.ps1') -Destination (Join-Path $out 'source')
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $out 'Microsoft-LICENSE.txt')
$app = Join-Path $source 'PSASamples/UWP/CSharp/PrintSupportApp'
$project = Join-Path $app 'PrintSupportApp.sln'
$manifest = Join-Path $app 'Package.appxmanifest'
$text = [IO.File]::ReadAllText($manifest)
if ($text -notmatch 'Name="dd5de805-d9bb-4785-88e9-399cbff2130e"' -or $text -notmatch 'Publisher="CN=Contoso"') {
    throw 'Unexpected upstream identity.'
}
$text = $text.Replace('Name="dd5de805-d9bb-4785-88e9-399cbff2130e"', 'Name="Miyako.PsaReferenceControl"').
    Replace('Publisher="CN=Contoso"', 'Publisher="CN=PsaReferenceControlDev"')
[IO.File]::WriteAllText($manifest, $text, [Text.UTF8Encoding]::new($true))
& (Join-Path $PSScriptRoot 'Check-Provenance.ps1') -SourceRoot $source `
    -InventoryPath (Join-Path $out 'source/original-files.json') -ReportPath (Join-Path $out 'source-comparison.json')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = @(& $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationPath)
if ($LASTEXITCODE -ne 0 -or $vs.Count -ne 1) { throw 'Visual Studio 2022 is required for the upstream UWP/.NET Native build.' }
$msbuild = Join-Path $vs[0] 'MSBuild/Current/Bin/MSBuild.exe'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited $LASTEXITCODE" }
}
$packageOut = Join-Path $out 'packages'
New-Item -ItemType Directory -Path $packageOut | Out-Null
# Upstream XpsUtil imports packages from this exact relative directory.
Invoke-Checked nuget @('restore', (Join-Path $source 'PSASamples/XpsUtil/packages.config'),
    '-PackagesDirectory', (Join-Path $app 'packages'), '-NonInteractive',
    '-ConfigFile', (Join-Path $source 'PSASamples/UWP/CSharp/NuGet.Config'))
$arguments = @($project, '/restore', '/m', '/v:minimal', '/nologo',
    '/p:Configuration=Release', '/p:Platform=ARM64',
    '/p:TargetPlatformVersion=10.0.26100.0', '/p:WindowsTargetPlatformVersion=10.0.26100.0',
    '/p:AppxBundle=Never', '/p:AppxPackageSigningEnabled=false', '/p:AppxAutoIncrementPackageRevision=false',
    '/p:GenerateTestArtifacts=false', '/p:UapAppxPackageBuildMode=SideloadOnly',
    "/p:AppxPackageDir=$packageOut\", "/flp:logfile=$out/build.log;verbosity=normal")
Invoke-Checked $msbuild $arguments
& (Join-Path $PSScriptRoot 'Check-Provenance.ps1') -SourceRoot $source `
    -InventoryPath (Join-Path $out 'source/original-files.json') -ReportPath (Join-Path $out 'source-comparison.json')
$packages = @(Get-ChildItem -LiteralPath $packageOut -Recurse -File |
    Where-Object { $_.Extension -in @('.appx', '.msix') -and $_.DirectoryName -notmatch '[\\/]Dependencies([\\/]|$)' })
if ($packages.Count -ne 1) { throw "Expected one ARM64 main package; found $($packages.Count)." }
$delivery = Join-Path $out 'delivery'
New-Item -ItemType Directory -Path $delivery | Out-Null
$package = Join-Path $delivery 'PsaReferenceControl.arm64.msix'
Copy-Item -LiteralPath $packages[0].FullName -Destination $package
& (Join-Path $PSScriptRoot 'Check-PackageResources.ps1') -PackagePath $package `
    -SourceRoot $source -OutputDirectory (Join-Path $out 'resources')
$dependencies = @(Get-ChildItem -LiteralPath $packageOut -Recurse -File |
    Where-Object { $_.Extension -in @('.appx', '.msix') -and $_.DirectoryName -match '[\\/]Dependencies[\\/]arm64$' })
New-Item -ItemType Directory -Path (Join-Path $delivery 'Dependencies/arm64') -Force | Out-Null
foreach ($dependency in $dependencies) {
    Copy-Item -LiteralPath $dependency.FullName -Destination (Join-Path $delivery 'Dependencies/arm64')
}
foreach ($name in @('Microsoft-LICENSE.txt', 'upstream.json', 'source-comparison.json', 'host-evidence.json')) {
    Copy-Item -LiteralPath (Join-Path $out $name) -Destination $delivery
}
Copy-Item -LiteralPath (Join-Path $out 'resources/resource-evidence.json') -Destination $delivery
[ordered]@{
    hostOSArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    processArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    targetArchitecture = 'arm64'
    buildMode = 'Release; upstream .NET Native'
    sdk = '10.0.26100.0'
    visualStudio = (& $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationVersion)
    unsignedPackageSha256 = (Get-FileHash -LiteralPath $package).Hash
    runtime = 'not-tested'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'build-evidence.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $out 'build-evidence.json') -Destination $delivery
