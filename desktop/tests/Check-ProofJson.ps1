Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$program = Join-Path $PSScriptRoot 'Invoke-ProductProof.ps1'
$sourceDirectory = $PSScriptRoot
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($program, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'The production proof script did not parse.' }
$writer = @($ast.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq 'Write-NewProofJson'
})
$main = @($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] })
if ($writer.Count -ne 1 -or $main.Count -ne 1) { throw 'Expected the production writer and main try/finally.' }
. ([scriptblock]::Create($writer[0].Extent.Text))

# Execute the actual post-control body, catch/finally and terminal error block.
# Only native worker/comparator invocations are replaced; JSON I/O is real.
$body = @($main[0].Body.Statements)
$start = @($body | Where-Object { $_.Extent.Text -ceq "`$stage = 'worker-lifecycle'" })
if ($start.Count -ne 1) { throw 'Expected one post-control measurement entry point.' }
$index = [Array]::IndexOf($body, $start[0])
$tail = ($body[$index..($body.Count - 1)] | ForEach-Object { $_.Extent.Text }) -join "`n"
$catches = ($main[0].CatchClauses | ForEach-Object { $_.Extent.Text }) -join "`n"
$terminal = $ast.EndBlock.Statements[-1]
if ($terminal -isnot [Management.Automation.Language.IfStatementAst]) { throw 'Expected the terminal failure block.' }
$flow = [scriptblock]::Create("`$PSScriptRoot = `$sourceDirectory`ntry {`n$tail`n}`n$catches`nfinally " +
    $main[0].Finally.Extent.Text + "`n" + $terminal.Extent.Text)
$writes = @($main[0].FindAll({
    param($node)
    $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Write-NewProofJson'
}, $true))
if ($writes.Count -ne 3) { throw 'All three production JSON write sites must use the tested writer.' }

$root = Join-Path ([IO.Path]::GetTempPath()) "pdf-proof-json-$([Guid]::NewGuid().ToString('N'))"
[IO.Directory]::CreateDirectory($root) | Out-Null
$directories = [Collections.Generic.List[string]]::new()
$checks = 0
$unicode = "glyphs $([char]0x03a9)$([char]0x0416) $([char]0x65e5)$([char]0x672c)"
function Read-StrictJson([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) {
        throw 'JSON must be UTF-8 without a BOM for the existing native/Python readers.'
    }
    return [Text.UTF8Encoding]::new($false, $true).GetString($bytes) | ConvertFrom-Json
}
function Invoke-JsonCase([string]$Name, [string[]]$Collisions = @(), [string]$FailStep = '',
    [string]$ExpectedStage = '', [int]$ExpectedCalls = 2) {
    $PrivateOutputRoot = Join-Path $root "$Name [$unicode]"
    [IO.Directory]::CreateDirectory($PrivateOutputRoot) | Out-Null
    $directories.Add($PrivateOutputRoot)
    $originals = @{}
    foreach ($filename in $Collisions) {
        $path = Join-Path $PrivateOutputRoot $filename
        $bytes = [byte[]]@(0, 255, 1, 7, 13, 10)
        [IO.File]::WriteAllBytes($path, $bytes)
        $originals[$path] = [Convert]::ToBase64String($bytes)
    }
    $inputs = @()
    $comparisons = @()
    foreach ($id in @('text', 'vectors', 'mixed')) {
        $inputs += [ordered]@{ id = $id; input = Join-Path $PrivateOutputRoot "$id.oxps" }
        $comparisons += [ordered]@{
            id = $id; baseline = Join-Path $PrivateOutputRoot "$id-microsoft.pdf"
            worker = Join-Path $PrivateOutputRoot "worker/job-$($inputs.Count - 1)/output.pdf"
            expected_text = @($unicode); minimum_vector_paths = 1
        }
    }
    $report = [ordered]@{
        schema = '4d-pdf-printer.product-proof-batch'; version = 1; result = 'blocked'
        productUsable = $false; manualReviewRequired = $true; error = $null
        native = @(); controls = @(@{ synthetic = $unicode })
        nested = @{ a = @{ b = @{ c = @{ d = @{ e = @{ f = @{ g = @{ h = @{ i = $unicode } } } } } } } } }
    }
    $created = $true
    $primaryFailure = $null
    $reportWriteFailure = $null
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    $RuntimeRoot = 'not-executed-runtime'
    $ProofExecutable = 'not-executed-proof'
    $PythonPath = 'not-executed-python'
    $ToolsRoot = 'not-executed-tools'
    $VerboseLocal = $true
    $calls = [Collections.Generic.List[string]]::new()
    function Invoke-Bounded([string]$Executable, [string[]]$Arguments, [string]$ResultPath, [int]$Seconds) {
        $step = if ($Executable -ceq $ProofExecutable) { 'worker' } else { 'comparator' }
        $calls.Add($step)
        $manifestIndex = [Array]::IndexOf($Arguments, '--manifest')
        $value = Read-StrictJson $Arguments[$manifestIndex + 1]
        if ($step -ceq 'worker') {
            if ($value.schema -cne '4d-pdf-printer.proof-inputs' -or $value.version -ne 1 -or
                $value.inputs.Count -ne 3 -or $value.inputs[2].input -cne $inputs[2].input) {
                throw 'The actual worker input JSON lost schema/order/Unicode path integrity.'
            }
        } elseif ($value.schema -cne '4d-pdf-printer.proof-comparison' -or $value.version -ne 1 -or
            $value.fixtures.Count -ne 3 -or $value.fixtures[2].expected_text[0] -cne $unicode) {
            throw 'The actual comparison input JSON lost schema/Unicode text integrity.'
        }
        if ($FailStep -ceq $step) { throw [InvalidOperationException]::new("synthetic-primary-$step") }
        return [pscustomobject]@{ result = 'completed'; productUsable = $false; manual_review_required = $true }
    }
    $caught = $null
    try { . $flow 2>$null } catch { $caught = $_ }
    if ($calls.Count -ne $ExpectedCalls) { throw "Unexpected child-call count in $Name." }
    foreach ($path in $originals.Keys) {
        if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -cne $originals[$path]) {
            throw "An existing JSON file changed in $Name."
        }
    }
    if ($ExpectedStage) {
        if ($null -eq $caught -or $caught.Exception.Message -cne
            'Product proof did not complete; no upload, fallback, installation or retry was performed.') {
            throw "Missing typed terminal failure in $Name."
        }
        if ($caught.Exception.Data['ProofError'].stage -cne $ExpectedStage -or
            $null -eq $caught.Exception.InnerException -or $report.error.stage -cne $ExpectedStage) {
            throw "Primary failure stage/cause was not preserved in $Name."
        }
        if ($FailStep -and $caught.Exception.InnerException.Message -cne "synthetic-primary-$FailStep") {
            throw "Reporting replaced the primary exception in $Name."
        }
        if ('batch.json' -in $Collisions -and
            $caught.Exception.Data['ReportWriteError'].stage -cne 'batch-report') {
            throw "Secondary reporting failure was not exposed in $Name."
        }
    } elseif ($null -ne $caught -or $report.result -cne 'automated-observations-complete-manual-review-required') {
        throw "The complete actual JSON-writing path failed in $Name."
    }
    if ('batch.json' -notin $Collisions) {
        $batch = Read-StrictJson (Join-Path $PrivateOutputRoot 'batch.json')
        if ($batch.schema -cne $report.schema -or $batch.productUsable -or -not $batch.manualReviewRequired -or
            $batch.controls[0].synthetic -cne $unicode -or $batch.nested.a.b.c.d.e.f.g.h.i -cne $unicode) {
            throw "Batch JSON lost Unicode/depth/parse integrity in $Name."
        }
        if ($ExpectedStage -and $batch.error.stage -cne $ExpectedStage) { throw 'Batch omitted the primary failure.' }
    }
    foreach ($filename in @('inputs.json', 'comparison-inputs.json')) {
        $path = Join-Path $PrivateOutputRoot $filename
        if ($filename -notin $Collisions -and [IO.File]::Exists($path)) { Read-StrictJson $path | Out-Null }
    }
}
try {
    Invoke-JsonCase 'complete'
    ++$checks
    Invoke-JsonCase 'input-collision' @('inputs.json') -ExpectedStage 'worker-lifecycle' -ExpectedCalls 0
    ++$checks
    Invoke-JsonCase 'comparison-collision' @('comparison-inputs.json') -ExpectedStage 'read-only-fidelity-size' -ExpectedCalls 1
    ++$checks
    Invoke-JsonCase 'batch-collision' @('batch.json') -ExpectedStage 'batch-report'
    ++$checks
    Invoke-JsonCase 'input-and-report-collision' @('inputs.json', 'batch.json') -ExpectedStage 'worker-lifecycle' -ExpectedCalls 0
    ++$checks
    Invoke-JsonCase 'comparison-and-report-collision' @('comparison-inputs.json', 'batch.json') `
        -ExpectedStage 'read-only-fidelity-size' -ExpectedCalls 1
    ++$checks
    Invoke-JsonCase 'worker-failure' -FailStep 'worker' -ExpectedStage 'worker-lifecycle' -ExpectedCalls 1
    ++$checks
    Invoke-JsonCase 'worker-failure-and-report-collision' @('batch.json') -FailStep 'worker' `
        -ExpectedStage 'worker-lifecycle' -ExpectedCalls 1
    ++$checks
    Invoke-JsonCase 'comparator-failure-and-report-collision' @('batch.json') -FailStep 'comparator' `
        -ExpectedStage 'read-only-fidelity-size'
    ++$checks
    Write-Output "$checks actual post-control JSON/report cases passed; native invocations substituted, NOT printing/worker evidence."
} finally {
    foreach ($directory in $directories) {
        foreach ($file in [IO.Directory]::GetFiles($directory)) { [IO.File]::Delete($file) }
        [IO.Directory]::Delete($directory)
    }
    [IO.Directory]::Delete($root)
}
