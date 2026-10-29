[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [Parameter(Mandatory)][string]$OutputPath,
    [switch]$AllowDevelopmentInstall,
    [switch]$PsaBootstrapOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$stage = 'preflight'
$private = $null
$process = $null
$report = [ordered]@{
    schema = '4d-pdf-printer.desktop-acceptance'; version = 1
    scope = 'handoff-gate-only'; result = 'blocked'; pdfProduced = $false
    osBuild = [Environment]::OSVersion.Version.Build
    architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    processArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    processSessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
    sourceCommit = $env:GITHUB_SHA
    packageSha256 = $null; diagnosticPreserved = $false
    client = $null; psa = @(); error = $null
}
if ($PsaBootstrapOnly) { $report.scope = 'psa-bootstrap-only' }
$diagnosticBefore = @(Get-AppxPackage -Name Miyako.PsaFeasibility |
    Select-Object PackageFullName, Status)
try {
    if (-not $IsWindows -or [Environment]::OSVersion.Version.Build -lt 26100 -or
        (Get-CimInstance Win32_OperatingSystem).ProductType -ne 1) {
        throw 'Requires Windows 11 24H2 or later client OS.'
    }
    if (-not $AllowDevelopmentInstall) { throw 'Explicit -AllowDevelopmentInstall is required.' }
    if (Test-Path -LiteralPath $OutputPath) { throw 'Evidence output must be new.' }
    $report.packageSha256 = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $existing = @(Get-AppxPackage -Name Miyako.DesktopPdfPrototype)
    if ($existing.Count -gt 0) { throw 'Use an ordinary higher-version prototype update; this gate will not remove an existing package.' }
    $stage = 'current-user-install'
    Add-AppxPackage -Path $PackagePath
    $installed = Get-AppxPackage -Name Miyako.DesktopPdfPrototype
    $stage = 'queue-registration'
    $found = $false
    for ($i = 0; $i -lt 12; $i++) {
        $names = @(Get-Printer | Select-Object -ExpandProperty Name)
        $found = '4D PDF Prototype (Automation)' -in $names -and '4D PDF Prototype (Save As)' -in $names
        if ($found) { break }
        Start-Sleep -Seconds 5
    }
    if (-not $found) { throw 'Prototype queues were not registered within 60 seconds.' }
    $stage = 'native-gate'
    $alias = Join-Path $env:LOCALAPPDATA 'Microsoft/WindowsApps/4dpdf-prototype.exe'
    if (-not (Test-Path -LiteralPath $alias)) { throw 'Packaged execution alias is unavailable.' }
    $private = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $private | Out-Null
    $stdout = Join-Path $private 'stdout.json'
    $stderr = Join-Path $private 'stderr.txt'
    $argument = if ($PsaBootstrapOnly) { '--psa-bootstrap' } else { '--gate' }
    $process = Start-Process -FilePath $alias -ArgumentList $argument -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $process.WaitForExit(120000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw 'Native gate timed out; its exact submitter process tree was terminated.'
    }
    $process.WaitForExit()
    $report.client = Get-Content -LiteralPath $stdout -Raw | ConvertFrom-Json
    $expectedStage = if ($PsaBootstrapOnly) { 'psa-bootstrap-verified-no-pdf' } else { 'gate-passed-no-pdf' }
    if ($process.ExitCode -ne 0 -or $report.client.stage -cne $expectedStage) {
        throw 'Native handoff gate failed; see typed client/PSA evidence.'
    }
    $report.result = if ($PsaBootstrapOnly) { 'partial' } else { 'passed' }
}
catch {
    $report.error = [ordered]@{
        stage = $stage; hresult = ('0x{0:X8}' -f ($_.Exception.HResult -band 0xffffffffL))
        exceptionType = $_.Exception.GetType().FullName
    }
}
finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        $process.Dispose()
    }
    $installed = Get-AppxPackage -Name Miyako.DesktopPdfPrototype
    if ($null -ne $installed) {
        $gate = Join-Path $env:LOCALAPPDATA "Packages/$($installed.PackageFamilyName)/LocalState/Gate"
        if (Test-Path -LiteralPath $gate) {
            $report.psa = @(Get-ChildItem -LiteralPath $gate -File -Filter '*.json' |
                ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json })
        }
    }
    $diagnosticAfter = @(Get-AppxPackage -Name Miyako.PsaFeasibility |
        Select-Object PackageFullName, Status)
    $report.diagnosticPreserved = (ConvertTo-Json -InputObject $diagnosticBefore -Compress) -ceq
        (ConvertTo-Json -InputObject $diagnosticAfter -Compress)
    if (-not $report.diagnosticPreserved) { $report.result = 'failed' }
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $OutputPath -Encoding utf8
    if ($null -ne $private) {
        foreach ($name in @('stdout.json', 'stderr.txt')) {
            $file = Join-Path $private $name
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
        }
        Remove-Item -LiteralPath $private
    }
}
if ($report.result -cne 'passed') { throw "Desktop gate blocked/failed at $stage; sanitized report written." }
