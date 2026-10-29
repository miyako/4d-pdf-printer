[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PrivateOutputRoot,
    [string]$BuildEvidencePath,
    [switch]$AllowOneSyntheticPrint
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$stage = 'consent'
$processes = @()
$created = $false
$primary = $null
$report = [ordered]@{
    schema = '4d-pdf-printer.activation-probe-desktop'; version = 1
    result = 'blocked'; activationGatePassed = $false; pdfProduced = $false
    error = $null; lateLaunchNotExcludedOnFailure = $true
}
function Save-NewProbeRecord([string]$Path, [object]$Value) {
    $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes((ConvertTo-Json -InputObject $Value -Depth 12))
    if ($bytes.Length -gt 16384) { throw 'Probe JSON exceeds16KiB.' }
    $file = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $file.Write($bytes, 0, $bytes.Length); $file.Flush($true) } finally { $file.Dispose() }
}
try {
    if (-not $AllowOneSyntheticPrint) { throw 'Separate approval for ONE synthetic activation print is required.' }
    $stage = 'desktop-preflight'
    if (-not $IsWindows -or [Environment]::OSVersion.Version.Build -lt 26100 -or
        (Get-CimInstance Win32_OperatingSystem).ProductType -ne 1 -or
        [Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0 -or
        [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -ne
        [Runtime.InteropServices.RuntimeInformation]::OSArchitecture) { throw 'Requires a native signed-in Windows11 client session.' }
    $user = [Security.Principal.WindowsIdentity]::GetCurrent()
    if ([Security.Principal.WindowsPrincipal]::new($user).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Use a non-elevated desktop shell.'
    }
    $package = @(Get-AppxPackage -Name Miyako.DesktopPdfPrototype)
    if ($package.Count -ne 1 -or $package[0].Version -ne [version]'0.2.0.0') {
        throw 'The separately installed/signed current-user development activation package0.2.0.0 is required.'
    }
    if (-not $BuildEvidencePath -or (Get-Item -LiteralPath $BuildEvidencePath).Length -gt 16384) {
        throw 'The exact local activation-build-evidence.json receipt is required.'
    }
    $build = Get-Content -LiteralPath $BuildEvidencePath -Raw | ConvertFrom-Json
    if ($build.stage -cne 'activation-probe-compile-package-units-only' -or
        $build.sourceCommit -cnotmatch '^[0-9a-f]{40}$' -or $build.sourceDirty -ne $false -or
        $build.architecture -ine [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()) {
        throw 'Build receipt is not a clean native activation-probe build.'
    }
    foreach ($name in @('DesktopPdfProbeCtl.exe', 'DesktopPdfProbeHost.exe', 'DesktopPdfApp.exe', 'DesktopPdfProbeTask.dll')) {
        $expected = $build.ownCodeSha256.$name
        if ($expected -cnotmatch '^[0-9a-f]{64}$' -or
            (Get-FileHash -LiteralPath (Join-Path $package[0].InstallLocation $name) -Algorithm SHA256).Hash -ine $expected) {
            throw 'Installed own-code identity does not match the reviewed local build receipt.'
        }
    }
    $report.sourceCommit = $build.sourceCommit
    $report.packageFullName = $package[0].PackageFullName
    $report.ownCodeSha256 = $build.ownCodeSha256
    $report.windowsBuild = [Environment]::OSVersion.Version.Build
    $report.session = [Diagnostics.Process]::GetCurrentProcess().SessionId
    $diagnostic = @(Get-AppxPackage -Name Miyako.PsaFeasibility | Select-Object PackageFullName, Status)
    if ('4D PDF Prototype (Automation)' -notin @(Get-Printer | Select-Object -ExpandProperty Name)) {
        throw 'The development automation/probe queue is missing; nothing is installed by this script.'
    }
    if (-not [IO.Path]::IsPathFullyQualified($PrivateOutputRoot) -or $PrivateOutputRoot.StartsWith('\\') -or
        (Test-Path -LiteralPath $PrivateOutputRoot)) { throw 'Choose a NEW absolute local private output directory.' }
    $parent = Split-Path -Parent $PrivateOutputRoot
    $part = $parent
    while ($part) {
        if (((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Reparse paths are not supported.'
        }
        $next = Split-Path -Parent $part
        if ($next -ceq $part) { break }
        $part = $next
    }
    $trusted = @($user.User.Value, 'S-1-5-18', 'S-1-5-32-544', 'S-1-3-0', 'S-1-3-4')
    $writes = [Security.AccessControl.FileSystemRights]::Write -bor
        [Security.AccessControl.FileSystemRights]::Delete -bor
        [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor
        [Security.AccessControl.FileSystemRights]::ChangePermissions -bor
        [Security.AccessControl.FileSystemRights]::TakeOwnership
    foreach ($rule in (Get-Acl -LiteralPath $parent).GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
        if ($rule.AccessControlType -eq 'Allow' -and ($rule.FileSystemRights -band $writes) -ne 0 -and
            $rule.IdentityReference.Value -notin $trusted) { throw 'Output parent permits unexpected writers.' }
    }
    New-Item -ItemType Directory -Path $PrivateOutputRoot | Out-Null
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false); $acl.SetOwner($user.User)
    foreach ($sid in @($user.User, [Security.Principal.SecurityIdentifier]::new('S-1-5-18'))) {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid, 'FullControl',
            'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    }
    Set-Acl -LiteralPath $PrivateOutputRoot -AclObject $acl
    $created = $true
    $alias = Join-Path $env:LOCALAPPDATA 'Microsoft/WindowsApps/4dpdf-probe.exe'
    $stage = 'prepare'
    $private = & $alias --prepare | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or $private.stage -cne 'prepared-private-correlation' -or
        $private.job_id -cnotmatch '^[0-9a-f]{64}$') { throw 'Probe preparation failed; no print was attempted.' }
    $token = $private.job_id
    $stage = 'one-synthetic-print-and-observation'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $observer = Start-Process -FilePath $alias -ArgumentList @('--observe', $token) -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $PrivateOutputRoot 'observer.json') `
        -RedirectStandardError (Join-Path $PrivateOutputRoot 'observer.stderr.private')
    $processes += $observer
    $submission = Start-Process -FilePath $alias -ArgumentList @('--print', $token) -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $PrivateOutputRoot 'submission.json') `
        -RedirectStandardError (Join-Path $PrivateOutputRoot 'submission.stderr.private')
    $processes += $submission
    while (@($processes | Where-Object { -not $_.HasExited }).Count -gt 0) {
        if ($watch.Elapsed.TotalSeconds -ge 45) { throw 'Probe watchdog reached45s; exact client process trees are stopped, but a future system launch is not excluded.' }
        foreach ($name in @('observer.json', 'submission.json', 'observer.stderr.private', 'submission.stderr.private')) {
            $path = Join-Path $PrivateOutputRoot $name
            if ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 16384) {
                throw 'Probe output exceeded16KiB; stopping this observation.'
            }
        }
        Start-Sleep -Milliseconds 50
    }
    foreach ($process in $processes) { $process.WaitForExit() }
    foreach ($name in @('observer.json', 'submission.json')) {
        if ((Get-Item -LiteralPath (Join-Path $PrivateOutputRoot $name)).Length -gt 16384) {
            throw 'Probe report exceeded its bound.'
        }
    }
    $report.observer = Get-Content -LiteralPath (Join-Path $PrivateOutputRoot 'observer.json') -Raw | ConvertFrom-Json
    $report.submission = Get-Content -LiteralPath (Join-Path $PrivateOutputRoot 'submission.json') -Raw | ConvertFrom-Json
    if ($observer.ExitCode -ne 0 -or $submission.ExitCode -ne 0 -or
        -not $report.observer.activationGatePassed -or
        $report.submission.stage -cne 'submitted-not-printer-success' -or
        $report.observer.job_hash -cne $report.submission.job_hash) { throw 'Actual callback/host/exit probe did not pass; see sanitized records.' }
    $after = @(Get-AppxPackage -Name Miyako.PsaFeasibility | Select-Object PackageFullName, Status)
    if ((ConvertTo-Json -InputObject $diagnostic -Compress) -cne (ConvertTo-Json -InputObject $after -Compress)) {
        throw 'Diagnostic package state changed unexpectedly.'
    }
    $report.activationGatePassed = $true
    $report.result = 'actual-callback-host-roundtrip-no-pdf'
} catch {
    $primary = $_
    $report.error = @{ stage = $stage; exceptionType = $_.Exception.GetType().FullName
        hresult = ('0x{0:X8}' -f ($_.Exception.HResult -band 0xffffffffL)) }
    Write-Error -ErrorAction Continue "Activation probe blocked/failed at $stage; no PDF or printer success is established."
} finally {
    foreach ($process in $processes) {
        try {
            if (-not $process.HasExited) {
                $process.Kill($true)
                if (-not $process.WaitForExit(5000)) { throw 'Client exit not observed.' }
            }
        } catch {
            $report.clientCleanupError = $_.Exception.GetType().FullName
            $report.activationGatePassed = $false
            $report.result = 'failed-client-cleanup'
            if ($null -eq $primary) { $primary = $_ }
        } finally { $process.Dispose() }
    }
    if ($created) {
        try { Save-NewProbeRecord (Join-Path $PrivateOutputRoot 'result.json') $report } catch {
            Write-Error -ErrorAction Continue 'Activation-probe result could not be written; no overwrite/retry performed.'
            if ($null -eq $primary) { $primary = $_ }
        }
    }
}
if ($null -ne $primary) {
    throw [InvalidOperationException]::new('Activation probe did not complete; preserve all records. No automatic retry.', $primary.Exception)
}
