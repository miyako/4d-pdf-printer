$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$renderer = Split-Path $PSScriptRoot -Parent
. (Join-Path $renderer 'tools/BuildSupport.ps1')
$script:passed = 0

function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'." }
}
function Expect-Failure([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch {
        if ($_.Exception.Message -notlike "*$Message*") { throw }
        return
    }
    throw 'Expected an explicit failure.'
}
function Test-Case([string]$Name, [scriptblock]$Action) {
    & $Action
    $script:passed++
    Write-Host "PASS: $Name"
}

foreach ($target in @('x64', 'arm64')) {
    Test-Case "$target defaults to VS2022 with CMake 3.24" {
        $plan = Get-RendererBuildPlan -Architecture $target -CMakeVersion 3.24.0
        Assert-Equal $plan.Architecture $target
        Assert-Equal $plan.VisualStudioVersion '2022'
        Assert-Equal $plan.CMakeGenerator 'Visual Studio 17 2022'
        Assert-Equal $plan.VisualStudioRange '[17.0,18.0)'
    }
    foreach ($vs in @('2022', '2026')) {
        Test-Case "explicit VS$vs / $target selection" {
            $plan = Get-RendererBuildPlan -Architecture $target -VisualStudioVersion $vs -CMakeVersion 4.2.0
            Assert-Equal $plan.Architecture $target
            Assert-Equal $plan.VisualStudioVersion $vs
            Assert-Equal $plan.CMakeGenerator $(if ($vs -eq '2026') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' })
            Assert-Equal $plan.VisualStudioRange $(if ($vs -eq '2026') { '[18.0,19.0)' } else { '[17.0,18.0)' })
        }
    }
    foreach ($version in @('3.31.6', '4.1.9')) {
        Test-Case "VS2026 / $target rejects CMake $version" {
            Expect-Failure {
                Get-RendererBuildPlan -Architecture $target -VisualStudioVersion 2026 -CMakeVersion $version
            } 'VS2026 requires CMake 4.2+'
        }
    }
    Test-Case "VS2022 / $target accepts pre-4.2 CMake" {
        $plan = Get-RendererBuildPlan -Architecture $target -VisualStudioVersion 2022 -CMakeVersion 3.31.6
        Assert-Equal $plan.CMakeGenerator 'Visual Studio 17 2022'
    }
    Test-Case "VS2026 / $target accepts tested hosted CMake" {
        $plan = Get-RendererBuildPlan -Architecture $target -VisualStudioVersion 2026 -CMakeVersion 4.4.4
        Assert-Equal $plan.CMakeGenerator 'Visual Studio 18 2026'
    }
}
Test-Case 'unsupported VS selection is rejected' {
    try { Get-RendererBuildPlan -Architecture arm64 -VisualStudioVersion 2019 -CMakeVersion 4.4.4 }
    catch {
        if ($_.FullyQualifiedErrorId -notmatch 'ParameterArgumentValidationError') { throw }
        return
    }
    throw 'Invalid VS selection accepted.'
}
$entry = Join-Path $renderer 'tools/build-windows.ps1'
Test-Case 'entry parameter defaults to VS2022 and validates the same choices' {
    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($entry, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
    $parameter = @($ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'VisualStudioVersion' })
    Assert-Equal $parameter.Count 1
    Assert-Equal $parameter[0].DefaultValue.SafeGetValue() '2022'
    $validation = @($parameter[0].Attributes | Where-Object { $_.TypeName.Name -eq 'ValidateSet' })
    Assert-Equal $validation.Count 1
    Assert-Equal (($validation[0].PositionalArguments | ForEach-Object { $_.SafeGetValue() }) -join ',') '2022,2026'
}
Test-Case 'Prepare consumes the selector plan rather than an architecture-based VS choice' {
    $source = Get-Content -Raw -LiteralPath $entry
    foreach ($expected in @('$plan = Get-RendererBuildPlan -Architecture $Architecture -VisualStudioVersion $VisualStudioVersion',
                            '$vsRange = $plan.VisualStudioRange', '$generator = $plan.CMakeGenerator',
                            'visual_studio_selection = $plan.VisualStudioVersion')) {
        if (!$source.Contains($expected)) { throw "Missing selector wiring: $expected" }
    }
}
Test-Case 'CI explicitly preserves VS2026 for ARM64 and VS2022 for x64' {
    $workflow = Get-Content -Raw (Join-Path (Split-Path $renderer -Parent) '.github/workflows/renderer-windows.yml')
    $expected = @'
-VisualStudioVersion '${{ matrix.arch == 'arm64' && '2026' || '2022' }}'
'@
    if (!$workflow.Contains($expected.Trim())) { throw 'Hosted Visual Studio selection is not explicit.' }
}
foreach ($file in @('tools/BuildSupport.ps1', 'tools/build-windows.ps1', 'tests/Check-BuildSupport.ps1')) {
    Test-Case "$file parses without executing Windows stages" {
        $tokens = $null
        $parseErrors = $null
        [Management.Automation.Language.Parser]::ParseFile((Join-Path $renderer $file),
            [ref]$tokens, [ref]$parseErrors) | Out-Null
        if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
    }
}
Write-Host "$script:passed portable selector checks passed; no Windows Prepare/Build/Test executed."
