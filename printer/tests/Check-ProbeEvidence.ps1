[CmdletBinding()]
param([Parameter(Mandatory)][string]$Probe)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$version = & $Probe --version
if ($LASTEXITCODE -ne 0 -or $version -notmatch '^PrintProbe 2 \((x64|arm64)\); evidence schema psa-probe-v2$') {
    throw 'Probe version/architecture not reported.'
}
$expectedArchitecture = $Matches[1]
$directory = Join-Path ([IO.Path]::GetTempPath()) "psa-probe-evidence-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path $directory | Out-Null
try {
    $job = 'synthetic-service-01'
    $expectedHash = (Get-FileHash -LiteralPath $Probe -Algorithm SHA256).Hash.ToLowerInvariant()
    foreach ($context in @($false, $true)) {
        $log = Join-Path $directory "$context.jsonl"
        $arguments = @('--queue', "PSA-nonexistent-$([guid]::NewGuid())", '--noninteractive',
            '--job-name', $job, '--log', $log, '--timeout-seconds', '10')
        if ($context) { $arguments += '--diagnose-context' }
        & $Probe @arguments 2> (Join-Path $directory 'stderr.txt')
        if ($LASTEXITCODE -ne 1) { throw 'Missing queue must fail with exit 1.' }
        $lines = @(Get-Content -LiteralPath $log)
        $records = @($lines | ForEach-Object { $_ | ConvertFrom-Json })
        $expectedEvents = @('probe-started')
        if ($context) { $expectedEvents += 'submission-context' }
        $expectedEvents += @('queue-open-failed', 'probe-failed')
        if (($records.event -join ',') -cne ($expectedEvents -join ',')) {
            throw 'Unexpected stages in missing-queue evidence.'
        }
        foreach ($record in $records) {
            if ($record.schema -cne 'psa-probe-v2' -or $record.probeVersion -cne '2' -or
                $record.executableSha256 -cne $expectedHash -or
                $record.processArchitecture -cne $expectedArchitecture -or $record.serviceMode -ne $false -or
                $record.jobTitleSha256 -cne 'd42fd077c5d93441b33f956cd37cced22a406c9bfd04107402d2bab07de35737' -or
                $record.spoolerJobId -ne 0 -or $record.outputSupplied -ne $false) {
                throw 'Incorrect probe/binary/job correlation.'
            }
        }
        if ($records[-1].win32Error -eq 0 -or
            $records[-1].win32Error -ne $records[-2].win32Error) {
            throw 'Terminal failure must preserve the queue error, not report zero.'
        }
        if ($context) {
            $record = $records[1]
            if ($record.tokenSessionId -ne $record.windowsSessionId -or
                $record.logonQueryNtStatus -ne 0 -or $null -eq $record.logonType -or
                $record.profileHiveOpenWin32Error -lt 0 -or
                $record.packageQueryWin32Error -notin @(0, 122) -or
                $null -eq $record.currentUserPackageCount) {
                throw 'Read-only native context queries failed; this is not service activation validation.'
            }
        }
        $json = $lines -join "`n"
        if ($json.Contains($job) -or $json.Contains($directory) -or
            $json -match 'S-1-\d+-|UserName|LogonDomain|ProfilePath|AuthenticationId') {
            throw 'Private identity/path or unhashed title leaked into evidence.'
        }
    }
    Write-Host 'Native CLI evidence/context and missing-queue failure passed; no job submitted, no service/PSA activated.'
} finally {
    Remove-Item -LiteralPath $directory -Recurse -Force
}
