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
    $message = ''
    try { Write-SmokeNewFile (Join-Path $good 'stdout.txt') ([byte[]](0)) }
    catch { $message = $_.Exception.Message }
    Check ($message.Length -gt 0) 'create-new never overwrite'
    Write-Host 'Wrapper execution/UTF8/JSON/failure/deadline cases passed (not packaged PSA proof).'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force
}
