param([Parameter(Mandatory)][string]$Fixture)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../Invoke-Body.ps1')
$root = Join-Path ([IO.Path]::GetTempPath()) "psa-smoke-wrapper-$([Guid]::NewGuid().ToString('N'))"
[IO.Directory]::CreateDirectory($root) | Out-Null
function Check([bool]$Condition, [string]$Case) {
    if (-not $Condition) { throw "case=$Case assertion-line=$($MyInvocation.ScriptLineNumber) API=wrapper body/assertion" }
}
try {
    $good = Join-Path $root 'unicode'
    Invoke-SmokeBody -Executable $Fixture -Arguments @('--fixture') -Evidence $good -DeadlineMs 5000
    $bytes = [IO.File]::ReadAllBytes((Join-Path $good 'stdout.txt'))
    $expected = [byte[]](123,34,109,101,115,115,97,103,101,34,58,34,230,151,165,230,156,172,34,125,10)
    $matches = [Convert]::ToHexString($bytes) -ceq [Convert]::ToHexString($expected)
    if (-not $matches) {
        [Console]::Error.WriteLine("Synthetic fixture only: expectedLength=$($expected.Length) expectedHex=$([Convert]::ToHexString($expected)) actualLength=$($bytes.Length) actualHexPrefix128=$([Convert]::ToHexString($bytes, 0, [Math]::Min(128, $bytes.Length)))")
    }
    Check $matches 'raw known UTF8 bytes'
    $report = Get-Content -LiteralPath (Join-Path $good 'wrapper.json') -Raw | ConvertFrom-Json
    Check ($report.exitCode -eq 0 -and $null -eq $report.primaryError -and -not $report.pdfProduced) 'actual report JSON'
    Check ([IO.File]::ReadAllText((Join-Path $good 'stderr.txt')).Contains('fixture stderr')) 'raw stderr'
    $failed = Join-Path $root 'failure'
    $message = ''
    try { Invoke-SmokeBody -Executable $Fixture -Arguments @('--fail') -Evidence $failed -DeadlineMs 5000 }
    catch { $message = $_.Exception.Message }
    Check ($message.Contains('exit code 7')) 'nonzero primary error'
    $report = Get-Content -LiteralPath (Join-Path $failed 'wrapper.json') -Raw | ConvertFrom-Json
    Check ($report.primaryError.Contains('exit code 7')) 'primary JSON error path'
    $message = ''
    try { Invoke-SmokeBody -Executable $Fixture -Arguments @('--fixture') -Evidence $good }
    catch { $message = $_.Exception.Message }
    Check ($message.Contains('must not already exist')) 'primary retained when report create-new also fails'
    $timeout = Join-Path $root 'timeout'
    $message = ''
    try { Invoke-SmokeBody -Executable $Fixture -Arguments @('--sleep') -Evidence $timeout -DeadlineMs 30 }
    catch { $message = $_.Exception.Message }
    Check ($message.Contains('deadline')) 'controlled timeout'
    $report = Get-Content -LiteralPath (Join-Path $timeout 'wrapper.json') -Raw | ConvertFrom-Json
    Check ($report.primaryError.Contains('deadline') -and $report.cleanup.treeTerminationRequested -and
        $report.cleanup.observerExitObserved -and $null -eq $report.cleanup.error -and
        -not $report.cleanup.systemLaunchedHostCovered) 'actual production timeout tree kill and checked observer exit'
    $engine = (Get-Process -Id $PID).Path
    $treeScript = Join-Path $root 'tree-fixture.ps1'
    $workerScript = Join-Path $root 'wrapper-worker.ps1'
    [IO.File]::WriteAllText($treeScript, @'
param([string]$Receipt)
$ErrorActionPreference = 'Stop'
$start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
foreach ($arg in @('-NoProfile', '-Command', 'Start-Sleep -Seconds 30')) { $start.ArgumentList.Add($arg) }
$child = [Diagnostics.Process]::Start($start)
try {
    [IO.File]::WriteAllText($Receipt, ($child.Id.ToString()))
    Start-Sleep -Seconds 30
} finally { $child.Dispose() }
'@)
    [IO.File]::WriteAllText($workerScript, @'
param([string]$Body, [string]$Engine, [string]$Fixture, [string]$Receipt, [string]$Evidence)
$ErrorActionPreference = 'Stop'
. $Body
try {
    Invoke-SmokeBody -Executable $Engine -Arguments @('-NoProfile', '-File', $Fixture, '-Receipt', $Receipt) -Evidence $Evidence -DeadlineMs 6000
} catch { [Console]::Error.WriteLine($_); exit 1 }
'@)
    $receipt = Join-Path $root 'descendant.pid'
    $treeEvidence = Join-Path $root 'tree-evidence'
    $start = [Diagnostics.ProcessStartInfo]::new($engine)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    foreach ($arg in @('-NoProfile', '-File', $workerScript, '-Body', (Join-Path $PSScriptRoot '../Invoke-Body.ps1'),
        '-Engine', $engine, '-Fixture', $treeScript, '-Receipt', $receipt, '-Evidence', $treeEvidence)) {
        $start.ArgumentList.Add($arg)
    }
    $worker = $null; $descendant = $null
    try {
        $worker = [Diagnostics.Process]::Start($start)
        $watch = [Diagnostics.Stopwatch]::StartNew()
        while (-not [IO.File]::Exists($receipt) -and -not $worker.HasExited -and $watch.ElapsedMilliseconds -lt 10000) {
            Start-Sleep -Milliseconds 10
        }
        $fixtureError = Join-Path $treeEvidence 'stderr.txt'
        if (-not [IO.File]::Exists($receipt) -and [IO.File]::Exists($fixtureError)) {
            $diagnostic = [IO.File]::ReadAllText($fixtureError)
            [Console]::Error.WriteLine("Controlled tree fixture stderr: $($diagnostic.Substring(0, [Math]::Min(2048, $diagnostic.Length)))")
        }
        Check ([IO.File]::Exists($receipt)) 'controlled descendant PID receipt before timeout'
        $descendant = [Diagnostics.Process]::GetProcessById([int][IO.File]::ReadAllText($receipt))
        $null = $descendant.Handle
        Check ($worker.WaitForExit(8000) -and $worker.ExitCode -eq 1) 'production wrapper timeout remains nonzero'
        Check ($descendant.WaitForExit(2000)) 'held exact descendant handle exit after production Kill(true)'
        $report = Get-Content -LiteralPath (Join-Path $treeEvidence 'wrapper.json') -Raw | ConvertFrom-Json
        Check ($report.primaryError.Contains('deadline') -and $report.cleanup.treeTerminationRequested -and
            $report.cleanup.observerExitObserved -and $null -eq $report.cleanup.error) 'tree cleanup receipt preserves primary timeout'
    } finally {
        foreach ($owned in @($descendant, $worker)) {
            if ($null -eq $owned) { continue }
            try {
                if (-not $owned.HasExited) {
                    $owned.Kill($true)
                    if (-not $owned.WaitForExit(2000)) { throw 'Controlled test process exit not observed.' }
                }
            } catch { [Console]::Error.WriteLine("Secondary controlled-test cleanup failure: $_") }
            finally { $owned.Dispose() }
        }
    }
    $message = ''
    try { Write-SmokeNewFile (Join-Path $good 'stdout.txt') ([byte[]](0)) }
    catch { $message = $_.Exception.Message }
    Check ($message.Length -gt 0) 'create-new never overwrite'
    Write-Host 'Wrapper execution/UTF8/JSON/failure/deadline/owned-tree cleanup cases passed (not packaged PSA proof).'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force
}
