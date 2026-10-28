Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$checks = 0
$root = Join-Path ([IO.Path]::GetTempPath()) "pdf-proof-unit-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $root | Out-Null
$rejected = Join-Path $root 'must-not-be-created'
try {
    try {
        . (Join-Path $PSScriptRoot 'Invoke-ProductProof.ps1') `
            -ProofExecutable 'not-run.exe' -RuntimeRoot 'not-read' -PythonPath 'not-run-python' `
            -PrivateOutputRoot $rejected -ErrorAction SilentlyContinue 2>$null
        throw 'Missing consent was accepted.'
    } catch {
        if ($_.Exception.Message -cne 'Product proof did not complete; no upload, fallback, installation or retry was performed.') { throw }
    }
    if (Test-Path -LiteralPath $rejected) { throw 'Consent rejection mutated the output folder.' }
    ++$checks
    try {
        . (Join-Path $PSScriptRoot 'Invoke-ProductProof.ps1') `
            -ProofExecutable 'not-run.exe' -RuntimeRoot 'not-read' -PythonPath 'not-run-python' `
            -PrivateOutputRoot $rejected -AllowDesktopProof -ErrorAction SilentlyContinue 2>$null
        throw 'Missing local-folder confirmation was accepted.'
    } catch {
        if ($_.Exception.Message -cne 'Product proof did not complete; no upload, fallback, installation or retry was performed.') { throw }
    }
    if (Test-Path -LiteralPath $rejected) { throw 'Local-folder confirmation rejection mutated the output folder.' }
    ++$checks
    $pwsh = (Get-Process -Id $PID).Path
    $script:report = @{ native = @() }
    $script:deadline = [Diagnostics.Stopwatch]::StartNew()
    $new = Join-Path $root 'round trip.json'
    $program = Join-Path $PSScriptRoot 'fixtures/proof-subprocess.ps1'
    $arguments = @('-NoProfile', '-File', $program, '-Report', $new)
    $value = Invoke-Bounded $pwsh $arguments $new 10
    if ($value.schema -cne 'proof-test' -or $value.stage -cne 'done') { throw 'Subprocess report/arguments changed.' }
    if ($value.result -cne 'completed' -or $value.within_reference_target -or
        $value.reference_target_approved -or $value.productUsable -or -not $value.manual_review_required) {
        throw 'A reference miss was not preserved as a completed measurement requiring review.'
    }
    ++$checks
    try {
        Invoke-Bounded $pwsh @('-NoProfile', '-File', $program, '-Report',
            (Join-Path $root 'hard-error.json'), '-Mode', 'error') (Join-Path $root 'hard-error.json') 10
        throw 'A real subprocess failure was accepted as a completed measurement.'
    } catch {
        if ($_.Exception.Message -cne 'Proof subprocess failed; see typed private record.') { throw }
    }
    ++$checks
    try {
        Invoke-Bounded $pwsh @('-NoProfile', '-File', $program, '-Mode', 'large') `
            (Join-Path $root 'absent-large.json') 5
        throw 'Unbounded stdout was accepted.'
    } catch {
        if ($_.Exception.Message -cne 'Proof subprocess diagnostic limit exceeded.') { throw }
    }
    ++$checks
    try {
        Invoke-Bounded $pwsh @('-NoProfile', '-File', $program, '-Mode', 'sleep') (Join-Path $root 'absent-timeout.json') 1
        throw 'A deadline violation was accepted.'
    } catch {
        if ($_.Exception.Message -cne 'Proof subprocess exceeded its deadline.') { throw }
    }
    ++$checks
    try {
        Invoke-Bounded (Join-Path $root 'missing-program') @() (Join-Path $root 'absent-start.json') 1
        throw 'A missing executable was accepted.'
    } catch {
        if ($_.Exception.Message -ceq 'A missing executable was accepted.' -or $_.Exception.Message -match 'not been started') { throw }
    }
    ++$checks
    Write-Output "$checks proof consent/subprocess unit cases passed; NOT Windows/renderer/printing evidence."
} finally {
    foreach ($file in Get-ChildItem -LiteralPath $root -File) { Remove-Item -LiteralPath $file.FullName }
    Remove-Item -LiteralPath $root
}
