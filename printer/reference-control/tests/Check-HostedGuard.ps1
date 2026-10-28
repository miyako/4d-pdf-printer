$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$output = Join-Path ([IO.Path]::GetTempPath()) "control-guard-$([guid]::NewGuid().ToString('N')).json"
$original = $env:GITHUB_ACTIONS
try {
    $env:GITHUB_ACTIONS = 'false'
    $rejected = $false
    try {
        & (Join-Path $PSScriptRoot '../Test-HostedRegistration.ps1') `
            -PackagePath 'not-accessed.msix' -CertificatePath 'not-accessed.cer' -OutputPath $output
    } catch {
        if ($_.Exception.Message -notmatch 'requires-disposable-github-hosted-job') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Hosted probe accepted a non-hosted invocation.' }
    $report = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
    if ($report.runtime -ne 'not-run' -or $report.outcome -ne 'blocked' -or
        $report.error.stage -ne 'host-guard' -or $report.trust.created -or
        $report.cleanup.packagesRemoved -ne 0 -or -not $report.cleanup.completed) {
        throw 'Blocked probe did not preserve the no-side-effects report.'
    }
    Write-Host 'Hosted-only guard rejected local execution and emitted sanitized blocked evidence.'
} finally {
    $env:GITHUB_ACTIONS = $original
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
}
