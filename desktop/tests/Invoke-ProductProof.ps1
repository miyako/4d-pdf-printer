[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ProofExecutable,
    [Parameter(Mandatory)][string]$RuntimeRoot,
    [Parameter(Mandatory)][string]$PrivateOutputRoot,
    [Parameter(Mandatory)][string]$PythonPath,
    [string]$ToolsRoot,
    [switch]$AllowDesktopProof,
    [switch]$ConfirmLocalUnsyncedDirectory,
    [switch]$VerboseLocal
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$stage = 'consent'
$created = $false
$primaryFailure = $null
$reportWriteFailure = $null
$deadline = [Diagnostics.Stopwatch]::StartNew()
$report = [ordered]@{
    schema = '4d-pdf-printer.product-proof-batch'; version = 1
    result = 'blocked'; productUsable = $false; psaPipelineTested = $false
    manualReviewRequired = $true; error = $null; native = @()
}
function Write-NewProofJson([string]$Path, [object]$Value, [int]$Depth = 8) {
    $json = ConvertTo-Json -InputObject $Value -Depth $Depth
    $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($json + [Environment]::NewLine)
    $file = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $file.Write($bytes, 0, $bytes.Length)
        $file.Flush($true)
    } finally { $file.Dispose() }
}
function Assert-LocalPath([string]$Path, [switch]$New) {
    if (-not [IO.Path]::IsPathFullyQualified($Path) -or $Path.StartsWith('\\')) {
        throw 'A fully qualified local fixed-volume path is required.'
    }
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($Path))
    if ($drive.DriveType -ne 'Fixed') { throw 'A fixed local volume is required.' }
    $part = [IO.Path]::GetFullPath($Path)
    if ($New) {
        if (Test-Path -LiteralPath $part) { throw 'Proof output must be new.' }
        $part = Split-Path -Parent $part
    }
    while ($part) {
        $item = Get-Item -LiteralPath $part -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse paths are not supported.' }
        $parent = Split-Path -Parent $part
        if ($parent -ceq $part) { break }
        $part = $parent
    }
}
function Invoke-Bounded([string]$Executable, [string[]]$Arguments, [string]$ResultPath, [int]$Seconds = 120) {
    $remaining = 900 - $deadline.Elapsed.TotalSeconds
    if ($remaining -le 0) { throw 'Proof batch deadline reached.' }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        if (-not $process.Start()) { throw 'Proof subprocess did not start.' }
        $started = $true
        $channels = @($process.StandardOutput.BaseStream, $process.StandardError.BaseStream) | ForEach-Object {
            $buffer = [byte[]]::new(4096)
            [pscustomobject]@{ stream = $_; buffer = $buffer; task = $_.ReadAsync($buffer, 0, $buffer.Length); bytes = 0L; done = $false }
        }
        $limit = $deadline.Elapsed.TotalSeconds + [Math]::Min($Seconds, $remaining)
        do {
            foreach ($channel in $channels) {
                if (-not $channel.done -and $channel.task.IsCompleted) {
                    $read = $channel.task.GetAwaiter().GetResult()
                    $channel.bytes += $read
                    if ($channel.bytes -gt 65536) { throw 'Proof subprocess diagnostic limit exceeded.' }
                    if ($read -eq 0) { $channel.done = $true }
                    else { $channel.task = $channel.stream.ReadAsync($channel.buffer, 0, $channel.buffer.Length) }
                }
            }
            if ($deadline.Elapsed.TotalSeconds -ge $limit) { throw 'Proof subprocess exceeded its deadline.' }
            Start-Sleep -Milliseconds 20
        } while (-not $process.HasExited -or @($channels | Where-Object { -not $_.done }).Count -gt 0)
        $process.WaitForExit()
        if (-not (Test-Path -LiteralPath $ResultPath -PathType Leaf)) { throw 'Proof subprocess omitted its private report.' }
        if ((Get-Item -LiteralPath $ResultPath).Length -gt 4 * 1024 * 1024) { throw 'Proof subprocess report limit exceeded.' }
        $value = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
        if ($value.schema -ceq '4d-pdf-printer.proof-fixtures') {
            $script:report.native += [ordered]@{ stage = 'original-fixtures'; fixtureCount = @($value.fixtures).Count }
        } else { $script:report.native += $value }
        if ($process.ExitCode -ne 0) { throw 'Proof subprocess failed; see typed private record.' }
        return $value
    } finally {
        if ($started -and -not $process.HasExited) {
            $process.Kill($true)
            if (-not $process.WaitForExit(5000)) { Write-Error 'Proof subprocess cleanup/reap incomplete.' }
        }
        $process.Dispose()
    }
}
function Wait-Capture([string]$TitleHash, [string]$CaptureRoot, [string[]]$Excluded) {
    do {
        $directories = if (Test-Path -LiteralPath $CaptureRoot) { @(Get-ChildItem -LiteralPath $CaptureRoot -Directory) } else { @() }
        foreach ($directory in $directories) {
            if ($directory.Name -in $Excluded) { continue }
            $jobPath = Join-Path $directory.FullName 'job.json'
            $completePath = Join-Path $directory.FullName 'completion.txt'
            if (-not (Test-Path -LiteralPath $jobPath) -or -not (Test-Path -LiteralPath $completePath)) { continue }
            $completion = Get-Content -LiteralPath $completePath -Raw
            if (-not $completion) { continue }
            $value = Get-Content -LiteralPath $jobPath -Raw | ConvertFrom-Json
            if ($value.jobTitleSha256 -ine $TitleHash) { continue }
            if ($value.result -cne 'diagnostic-oxps-written' -or
                $value.completionRequested -cne 'succeeded' -or $value.contentType -cne 'application/oxps' -or
                $completion -cne 'CompleteJob returned: succeeded (diagnostic OXPS only)') {
                throw 'Diagnostic control did not confirm successful OXPS copy/completion.'
            }
            return [ordered]@{
                record = [ordered]@{
                    stage = $value.lastStage; contentType = $value.contentType; sourceBytes = $value.sourceBytes
                    titleSha256 = $value.jobTitleSha256; workflowSha256 = $value.workflowSessionSha256
                    targetAvailable = $value.targetAvailable; diagnosticCompleteJobReturned = $true
                }
                source = Join-Path $directory.FullName 'source.oxps'
            }
        }
        Start-Sleep -Milliseconds 200
    } while ($deadline.Elapsed.TotalSeconds -lt 900)
    throw 'No matching completed diagnostic control was observed before the batch deadline.'
}
try {
    if (-not $AllowDesktopProof) { throw 'Separate explicit host/run consent (-AllowDesktopProof) is required.' }
    if (-not $ConfirmLocalUnsyncedDirectory) { throw 'Explicit confirmation of a local private, non-synced output directory is required.' }
    $stage = 'preflight'
    if (-not $IsWindows -or [Environment]::OSVersion.Version.Build -lt 26100 -or
        (Get-CimInstance Win32_OperatingSystem).ProductType -ne 1) { throw 'Requires an ordinary Windows 11 24H2+ client desktop.' }
    if ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -ne
        [Runtime.InteropServices.RuntimeInformation]::OSArchitecture) { throw 'The proof shell must be native, not emulated.' }
    foreach ($path in @($ProofExecutable, $RuntimeRoot, $PythonPath)) { Assert-LocalPath $path }
    if ($ToolsRoot) { Assert-LocalPath $ToolsRoot }
    Assert-LocalPath $PrivateOutputRoot -New
    if ([IO.Path]::GetFileName($ProofExecutable) -cne 'DesktopPdfProof.exe') { throw 'Select the explicit own-code proof executable.' }
    $build = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $ProofExecutable) 'proof-build-evidence.json') -Raw | ConvertFrom-Json
    if ($build.schema -cne '4d-pdf-printer.desktop-proof-build' -or $build.nativeContractTest -cne 'passed' -or
        $build.executableSha256 -ine (Get-FileHash -LiteralPath $ProofExecutable -Algorithm SHA256).Hash) {
        throw 'Missing/mismatched own-code native build evidence.'
    }
    $package = @(Get-AppxPackage -Name Miyako.PsaFeasibility)
    if ($package.Count -ne 1) { throw 'The existing diagnostic package is required; this script never installs it.' }
    $queues = @(Get-Printer | Select-Object -ExpandProperty Name)
    if ('PSA Diagnostic OXPS (Save As)' -notin $queues -or 'Microsoft Print to PDF' -notin $queues) {
        throw 'Both existing diagnostic and Microsoft PDF queues are required.'
    }
    $captures = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "Packages/$($package[0].PackageFamilyName)/LocalState/Captures"
    $user = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $trusted = @($user.Value, 'S-1-5-18', 'S-1-5-32-544', 'S-1-3-0', 'S-1-3-4')
    $writeRights = [Security.AccessControl.FileSystemRights]::Write -bor
        [Security.AccessControl.FileSystemRights]::Delete -bor
        [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor
        [Security.AccessControl.FileSystemRights]::ChangePermissions -bor
        [Security.AccessControl.FileSystemRights]::TakeOwnership
    $parentAcl = Get-Acl -LiteralPath (Split-Path -Parent $PrivateOutputRoot)
    foreach ($rule in $parentAcl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
        if ($rule.AccessControlType -eq 'Allow' -and
            ($rule.FileSystemRights -band $writeRights) -ne 0 -and $rule.IdentityReference.Value -notin $trusted) {
            throw 'The output parent grants writes to identities outside the private proof boundary.'
        }
    }
    New-Item -ItemType Directory -Path $PrivateOutputRoot | Out-Null
    $created = $true
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $acl.SetOwner($user)
    foreach ($identity in @($user, [Security.Principal.SecurityIdentifier]::new('S-1-5-18'))) {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($identity,
            [Security.AccessControl.FileSystemRights]::FullControl,
            ([Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit),
            [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow))
    }
    Set-Acl -LiteralPath $PrivateOutputRoot -AclObject $acl
    $report.build = $build
    $report.osBuild = [Environment]::OSVersion.Version.Build
    $report.processArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    $stage = 'runtime-preflight'
    Invoke-Bounded $ProofExecutable @('--runtime-check', '--runtime', $RuntimeRoot, '--report',
        (Join-Path $PrivateOutputRoot 'runtime.json')) (Join-Path $PrivateOutputRoot 'runtime.json') | Out-Null
    $stage = 'original-fixtures'
    $fixturesRoot = Join-Path $PrivateOutputRoot 'fixtures'
    $fixtures = Invoke-Bounded $ProofExecutable @('--fixtures', '--directory', $fixturesRoot, '--report',
        (Join-Path $PrivateOutputRoot 'fixtures-report.json')) (Join-Path $PrivateOutputRoot 'fixtures-report.json')
    $fixtureManifest = Join-Path $fixturesRoot 'fixtures.json'
    $inputs = @()
    $comparisons = @()
    $controls = @()
    foreach ($fixture in $fixtures.fixtures) {
        $id = $fixture.id
        $oxps = Join-Path $PrivateOutputRoot "$id.oxps"
        $stage = "diagnostic-control-$id"
        $oldCaptures = if (Test-Path -LiteralPath $captures) { @(Get-ChildItem -LiteralPath $captures -Directory | Select-Object -ExpandProperty Name) } else { @() }
        Write-Host "Original synthetic control: save the OXPS as $id.oxps in the approved PRIVATE output folder. Cancel stops the batch."
        $control = Invoke-Bounded $ProofExecutable @('--print', '--manifest', $fixtureManifest, '--id', $id,
            '--queue', 'PSA Diagnostic OXPS (Save As)', '--output', $oxps, '--control', '--report',
            (Join-Path $PrivateOutputRoot "$id-control.json")) (Join-Path $PrivateOutputRoot "$id-control.json")
        $capture = Wait-Capture $control.title_sha256 $captures $oldCaptures
        if (-not (Test-Path -LiteralPath $oxps -PathType Leaf) -or
            (Get-FileHash -LiteralPath $oxps -Algorithm SHA256).Hash -cne
            (Get-FileHash -LiteralPath $capture.source -Algorithm SHA256).Hash) {
            throw 'Chosen diagnostic target does not match the captured immutable source.'
        }
        $controls += $capture.record
        $baseline = Join-Path $PrivateOutputRoot "$id-microsoft.pdf"
        $stage = "microsoft-baseline-$id"
        Invoke-Bounded $ProofExecutable @('--print', '--manifest', $fixtureManifest, '--id', $id,
            '--queue', 'Microsoft Print to PDF', '--output', $baseline, '--report',
            (Join-Path $PrivateOutputRoot "$id-baseline.json")) (Join-Path $PrivateOutputRoot "$id-baseline.json") | Out-Null
        if (-not (Test-Path -LiteralPath $baseline -PathType Leaf)) { throw 'No Microsoft PDF baseline; no size comparison is possible.' }
        $inputs += [ordered]@{ id = $id; input = $oxps }
        $comparisons += [ordered]@{
            id = $id; baseline = $baseline
            worker = Join-Path $PrivateOutputRoot "worker/job-$($inputs.Count - 1)/output.pdf"
            expected_text = @($fixture.expected_text); minimum_vector_paths = $fixture.minimum_vector_paths
        }
    }
    $report.controls = $controls
    $stage = 'worker-lifecycle'
    $inputManifest = Join-Path $PrivateOutputRoot 'inputs.json'
    Write-NewProofJson $inputManifest ([ordered]@{
        schema = '4d-pdf-printer.proof-inputs'; version = 1; inputs = $inputs
    })
    $workerArguments = @('--repeat', '--runtime', $RuntimeRoot, '--manifest', $inputManifest,
        '--directory', (Join-Path $PrivateOutputRoot 'worker'), '--report', (Join-Path $PrivateOutputRoot 'worker-report.json'))
    if ($VerboseLocal) { $workerArguments += @('--verbose-log', (Join-Path $PrivateOutputRoot 'verbose.jsonl')) }
    Invoke-Bounded $ProofExecutable $workerArguments (Join-Path $PrivateOutputRoot 'worker-report.json') 900 | Out-Null
    $stage = 'read-only-fidelity-size'
    $comparisonManifest = Join-Path $PrivateOutputRoot 'comparison-inputs.json'
    Write-NewProofJson $comparisonManifest ([ordered]@{
        schema = '4d-pdf-printer.proof-comparison'; version = 1; fixtures = $comparisons
    })
    $comparisonReport = Join-Path $PrivateOutputRoot 'comparison.json'
    $arguments = @((Join-Path $PSScriptRoot 'compare_pdfs.py'), '--manifest', $comparisonManifest,
        '--output', $comparisonReport, '--render-directory', (Join-Path $PrivateOutputRoot 'renders'))
    if ($ToolsRoot) { $arguments += @('--tools-root', $ToolsRoot) }
    $report.comparison = Invoke-Bounded $PythonPath $arguments $comparisonReport 900
    $report.result = 'automated-observations-complete-manual-review-required'
} catch {
    $primaryFailure = $_
    $report.error = [ordered]@{
        stage = $stage; exceptionType = $_.Exception.GetType().FullName
        hresult = ('0x{0:X8}' -f ($_.Exception.HResult -band 0xffffffffL))
    }
    Write-Error -ErrorAction Continue "Product proof blocked/failed at $stage; no printer success is established."
} finally {
    $report.elapsedMilliseconds = $deadline.ElapsedMilliseconds
    if ($created) {
        $path = Join-Path $PrivateOutputRoot 'batch.json'
        try { Write-NewProofJson $path $report 30 } catch {
            $reportWriteFailure = $_
            $report.reportWriteError = [ordered]@{
                stage = 'batch-report'; exceptionType = $_.Exception.GetType().FullName
                hresult = ('0x{0:X8}' -f ($_.Exception.HResult -band 0xffffffffL))
            }
            if ($null -eq $primaryFailure) {
                $report.result = 'blocked'
                $report.error = $report.reportWriteError
            }
            Write-Error -ErrorAction Continue 'Product proof batch report could not be written; no overwrite or retry was performed. Any primary failure is retained.'
        }
    }
}
if ($null -ne $primaryFailure -or $null -ne $reportWriteFailure -or
    $report.result -cne 'automated-observations-complete-manual-review-required') {
    $cause = if ($null -ne $primaryFailure) { $primaryFailure.Exception }
        elseif ($null -ne $reportWriteFailure) { $reportWriteFailure.Exception } else { $null }
    $failure = [InvalidOperationException]::new(
        'Product proof did not complete; no upload, fallback, installation or retry was performed.', $cause)
    $failure.Data['ProofError'] = $report.error
    if ($null -ne $reportWriteFailure) { $failure.Data['ReportWriteError'] = $report.reportWriteError }
    throw $failure
}
