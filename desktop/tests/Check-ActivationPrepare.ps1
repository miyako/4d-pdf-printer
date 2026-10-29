Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$program = Join-Path $PSScriptRoot 'Invoke-ActivationProbe.ps1'
$childProgram = Join-Path $PSScriptRoot 'PrepareCaptureChild.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($program, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'The production wrapper did not parse.' }
foreach ($name in @('Save-NewProbeRecord', 'Invoke-PrepareCapture')) {
    $definition = @($ast.EndBlock.Statements | Where-Object {
        $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name
    })
    if ($definition.Count -ne 1) { throw 'Expected one actual production helper.' }
    . ([scriptblock]::Create($definition[0].Extent.Text))
}
Set-Item Function:Invoke-ActualPrepareCapture (Get-Item Function:Invoke-PrepareCapture).ScriptBlock
$main = @($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] })
if ($main.Count -ne 1) { throw 'Expected one actual production main block.' }
$body = @($main[0].Body.Statements)
$first = @($body | Where-Object { $_.Extent.Text -ceq "`$stage = 'prepare'" })
if ($first.Count -ne 1) { throw 'Expected exact preparation entry point.' }
$begin = [Array]::IndexOf($body, $first[0])
$segment = ($body[$begin..($body.Count - 1)] | ForEach-Object { $_.Extent.Text }) -join "`n"
$reportInitializer = @($ast.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -ceq '$report'
})
if ($reportInitializer.Count -ne 1) { throw 'Expected actual report identity initializer.' }
$catches = ($main[0].CatchClauses | ForEach-Object { $_.Extent.Text }) -join "`n"
$terminal = $ast.EndBlock.Statements[-1]
$flow = [scriptblock]::Create("`$PSCommandPath = `$program`n" + $reportInitializer[0].Extent.Text + "`ntry {`n$segment`n`$preparePassed = `$true`n}`n$catches`nfinally " +
    $main[0].Finally.Extent.Text + "`n" + $terminal.Extent.Text)

# Only substitute the native executable; the capture, prepare parsing, catch,
# preparation-only/print branch, private/result writes, finally and terminal
# exception are production code.
function Invoke-PrepareCapture([Diagnostics.ProcessStartInfo]$StartInfo, [string]$Root,
    [Collections.IDictionary]$Observation) {
    if ($StartInfo.FileName -cne 'not-an-executed-windows-alias' -or
        $StartInfo.ArgumentList.Count -ne 1 -or $StartInfo.ArgumentList[0] -cne '--prepare') {
        throw 'Production fixed alias/arguments changed unexpectedly.'
    }
    $StartInfo.FileName = Join-Path $PSHOME $(if ($IsWindows) { 'pwsh.exe' } else { 'pwsh' })
    if ($mode -ceq 'start-failure') { $StartInfo.FileName = Join-Path $Root 'does-not-exist-unit-process' }
    $StartInfo.ArgumentList.Clear()
    foreach ($argument in @('-NoLogo', '-NoProfile', '-File', $childProgram, $mode)) {
        $StartInfo.ArgumentList.Add($argument)
    }
    Invoke-ActualPrepareCapture $StartInfo $Root $Observation -Seconds $(if ($mode -ceq 'timeout') { 0.3 } else { 10 })
}
$root = Join-Path ([IO.Path]::GetTempPath()) "activation-prepare-unit-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $root | Out-Null
$directories = [Collections.Generic.List[string]]::new()
$checks = 0
function Invoke-PrepareCase([string]$Name, [string]$Mode, [string[]]$Collisions = @(), [switch]$Success) {
    $PrivateOutputRoot = Join-Path $root $Name
    New-Item -ItemType Directory -Path $PrivateOutputRoot | Out-Null
    $directories.Add($PrivateOutputRoot)
    $originals = @{}
    foreach ($filename in $Collisions) {
        $path = Join-Path $PrivateOutputRoot $filename
        [IO.File]::WriteAllBytes($path, [byte[]]@(0, 255, 13, 10, 7))
        $originals[$path] = [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))
    }
    $alias = 'not-an-executed-windows-alias'
    $created = $true; $primary = $null; $reportWriteFailure = $null; $processes = @(); $preparePassed = $false
    $PreparationOnly = $true
    $PSCommandPath = $program
    $unexpectedProcessCalls = [Collections.Generic.List[string]]::new()
    function Start-Process {
        $unexpectedProcessCalls.Add('unexpected-observer-or-submission')
        throw 'Observer/submission MUST NOT run in preparation-only mode.'
    }
    $caught = $null
    try { . $flow 2>$null } catch { $caught = $_ }
    if ($unexpectedProcessCalls.Count -ne 0 -or $stage -cne 'prepare' -or $processes.Count -ne 0 -or
        $report.mode -cne 'preparation-only-not-activation' -or $report.wrapperVersion -ne 2 -or
        $report.wrapperSha256 -cne (Get-FileHash -LiteralPath $program -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw "Preparation-only escaped its branch or lost wrapper identity in $Name."
    }
    foreach ($path in $originals.Keys) {
        if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -cne $originals[$path]) {
            throw "An existing private/report file changed in $Name."
        }
    }
    if ($Success) {
        if ($null -ne $caught -or -not $preparePassed -or $report.prepare.exitCode -ne 0 -or
            -not $report.prepare.processExitObserved -or -not $report.prepare.captureComplete -or
            $token -cne ('a' * 64) -or $report.activationGatePassed -or
            $report.result -cne 'preparation-only-not-activation') { throw "Preparation success lost its contract in $Name." }
    } else {
        if ($null -eq $caught -or $preparePassed -or $report.error.stage -cne 'prepare' -or
            $caught.Exception.InnerException -ne $primary.Exception -or $report.activationGatePassed) {
            throw "Primary prepare failure was not retained in $Name."
        }
        if ('result.json' -in $Collisions -and $caught.Exception.Data['ReportWriteError'].stage -cne 'result-report') {
            throw "Secondary reporting error replaced/omitted the primary in $Name."
        }
        if ('failure.private.json' -notin $Collisions) {
            $detail = Get-Content -LiteralPath (Join-Path $PrivateOutputRoot 'failure.private.json') -Raw | ConvertFrom-Json
            if ($detail.message -cne $primary.Exception.Message -or $detail.stage -cne 'prepare') {
                throw "Original private exception message was lost in $Name."
            }
        } elseif (-not $report.error.privateDiagnosticWriteHresult) { throw 'Missing private-write failure evidence.' }
    }
    if ($Mode -ceq 'native-error' -and 'prepare.stdout.private' -notin $Collisions -and
        'prepare.stderr.private' -notin $Collisions) {
        if ($report.prepare.native.hresult -cne '0x80070005' -or $report.prepare.exitCode -ne 1 -or
            $report.prepare.native.stage -cne 'control-arguments' -or
            -not $report.prepare.processExitObserved) { throw 'Native HRESULT/exit/stage was masked.' }
    }
    if ($Mode -cin @('flood', 'stderr-flood') -and $report.prepare.failureReason -cne 'capture-limit') {
        throw 'Stream flood was not classified as a capture limit.'
    }
    if ($Mode -ceq 'timeout' -and ($report.prepare.failureReason -cne 'capture-deadline' -or
        -not $report.prepare.processExitObserved)) { throw 'Timed-out controlled client exit was not observed.' }
    if ($Mode -ceq 'start-failure' -and ($report.prepare.processStarted -or
        $report.prepare.captureApi -cne 'Process.Start' -or -not $report.prepare.captureWin32Error)) {
        throw 'Process-start failure was not retained.'
    }
    foreach ($name in @('prepare.stdout.private', 'prepare.stderr.private')) {
        $path = Join-Path $PrivateOutputRoot $name
        if ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 16384) {
            throw 'Capture exceeded its hard stored-byte bound.'
        }
    }
    if ('result.json' -notin $Collisions) {
        $public = Get-Content -LiteralPath (Join-Path $PrivateOutputRoot 'result.json') -Raw
        if ($public.Contains('a' * 64) -or $public.Contains($PrivateOutputRoot) -or
            $public.Contains('not-an-executed-windows-alias')) { throw 'Public report leaked private data.' }
        $value = $public | ConvertFrom-Json
        if ($value.activationGatePassed -or $value.pdfProduced) { throw 'Preparation alone reported activation/PDF success.' }
    }
    if ($Mode -cin @('success', 'native-error') -and 'prepare.stderr.private' -notin $Collisions -and
        'prepare.stdout.private' -notin $Collisions) {
        $actual = [Text.UTF8Encoding]::new($false, $true).GetString(
            [IO.File]::ReadAllBytes((Join-Path $PrivateOutputRoot 'prepare.stderr.private')))
        $expected = [string][char]0x65e5 + [char]0x672c + [char]0x8a9e
        if ($actual -cne $expected) { throw 'Raw private Unicode stderr was lost.' }
    }
}
try {
    Invoke-PrepareCase 'success' 'success' -Success; ++$checks
    Invoke-PrepareCase 'native-error' 'native-error'; ++$checks
    foreach ($mode in @('empty', 'malformed', 'array', 'bad-stage', 'bad-id', 'flood', 'stderr-flood',
        'invalid-utf8', 'timeout', 'start-failure')) {
        Invoke-PrepareCase $mode $mode; ++$checks
    }
    foreach ($collision in @('prepare.stdout.private', 'prepare.stderr.private', 'failure.private.json', 'result.json')) {
        Invoke-PrepareCase "collision-$collision" 'native-error' @($collision); ++$checks
    }
    Write-Output "$checks actual production prepare/catch/finally/report cases passed; controlled pwsh replaces native executable. NO Windows alias/PSA/printing evidence."
} finally {
    foreach ($directory in $directories) {
        foreach ($file in [IO.Directory]::GetFiles($directory)) { [IO.File]::Delete($file) }
        [IO.Directory]::Delete($directory)
    }
    [IO.Directory]::Delete($root)
}
