$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$path = [IO.Path]::GetFullPath([IO.Path]::Combine($PSScriptRoot, '../Test-HostedRegistration.ps1'))
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors) { throw ($errors | Out-String) }
$definition = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Confirm-TrustedFrameworkSignature'
}, $true)
if (-not $definition) { throw 'Signature-verifier function not found.' }
. ([scriptblock]::Create($definition.Extent.Text))

# Native commands update the global automatic variable, not a local shadow.
function Test-NativeScope {
    $LASTEXITCODE = $null
    if ($IsWindows) { & $env:ComSpec /c exit 0 }
    else { & /usr/bin/true }
    if ($null -ne $LASTEXITCODE -or $global:LASTEXITCODE -ne 0) { throw 'Unexpected native exit-code scope semantics.' }
}
Test-NativeScope

function Join-Path { param($Path, $ChildPath) 'Invoke-SyntheticVerifier' }
function Test-Path { param($LiteralPath, $PathType) $true }
function Stop-Experiment { param($Code) throw $Code }
function Invoke-SyntheticVerifier {
    $global:LASTEXITCODE = $script:testExitCode
    if ($script:testExitCode -eq 1) {
        'SignTool Error: root certificate is not trusted. 0x800B0109'
    } else { 'Successfully verified.' }
}
$report = [ordered]@{ dependencies = [ordered]@{ signatureChecks = @() } }
$testExitCode = 0
Confirm-TrustedFrameworkSignature 'framework.appx'
if ($report.dependencies.signatureChecks.Count -ne 1 -or
    $report.dependencies.signatureChecks[0].exitCode -ne 0 -or
    $report.dependencies.signatureChecks[0].category -ne 'verified') {
    throw 'Successful native verifier exit was not accepted and recorded.'
}
foreach ($testExitCode in @(1, $null)) {
    $rejected = $false
    try { Confirm-TrustedFrameworkSignature 'framework.appx' }
    catch {
        if ($_.Exception.Message -ne 'framework-dependency-signature-verification-failed') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Failed/missing verifier exit was accepted.' }
}
if ($report.dependencies.signatureChecks[1].category -ne 'untrusted-root' -or
    $report.dependencies.signatureChecks[1].hresults[0] -ne '0x800B0109') {
    throw 'Verifier failure was not sanitized into its exact diagnostic fields.'
}
$global:LASTEXITCODE = 0
Write-Host 'Native exit-code scope and verifier success/failure/missing-code regressions passed.'
