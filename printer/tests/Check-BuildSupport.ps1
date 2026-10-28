$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'BuildSupport.ps1')
$script:passed = 0

function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'." }
}
function Expect-Failure([scriptblock]$Action) {
    try { & $Action } catch { return }
    throw 'Expected an explicit failure.'
}
function Test-Case([string]$Name, [scriptblock]$Action) {
    & $Action
    $script:passed++
    Write-Host "PASS: $Name"
}
function New-TestPe([int]$Machine) {
    $bytes = [byte[]]::new(256)
    [BitConverter]::GetBytes([uint16]0x5a4d).CopyTo($bytes, 0)
    [BitConverter]::GetBytes([int]128).CopyTo($bytes, 0x3c)
    [BitConverter]::GetBytes([uint32]0x4550).CopyTo($bytes, 128)
    [BitConverter]::GetBytes([uint16]$Machine).CopyTo($bytes, 132)
    return ,$bytes
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) "psa-build-check-$([Guid]::NewGuid().ToString('N'))"
New-Item $fixture -ItemType Directory | Out-Null
try {
    foreach ($target in @('x64', 'ARM64')) {
        foreach ($hostArchitecture in @('x64', 'ARM64')) {
            Test-Case "$target target / $hostArchitecture host plan" {
                $plan = Get-PrinterBuildPlan -Architecture $target -HostArchitecture $hostArchitecture -Root $fixture
                $expectedOut = Join-Path $fixture 'out'
                if ($target -eq 'ARM64') { $expectedOut = Join-Path $expectedOut 'arm64' }
                Assert-Equal $plan.Out $expectedOut
                Assert-Equal $plan.Build (Join-Path $expectedOut 'build')
                Assert-Equal $plan.Stage (Join-Path $expectedOut 'package')
                Assert-Equal $plan.Package (Join-Path $expectedOut 'PsaDiagnostic.msix')
                Assert-Equal $plan.CMakePlatform $target
                Assert-Equal $plan.Architecture $target.ToLowerInvariant()
                Assert-Equal $plan.SdkToolArchitecture 'x64'
                $execution = if ($target -eq $hostArchitecture) { 'native' }
                    elseif ($target -eq 'x64') { 'emulated-x64' } else { 'not-run-cross-compile' }
                Assert-Equal $plan.TestExecution $execution
            }
        }
    }
    Test-Case 'x64 default preserved' {
        $plan = Get-PrinterBuildPlan -HostArchitecture ARM64 -Root $fixture
        Assert-Equal $plan.Architecture 'x64'
        Assert-Equal $plan.Out (Join-Path $fixture 'out')
        Assert-Equal $plan.VisualStudioVersion '2022'
        Assert-Equal $plan.CMakeGenerator 'Visual Studio 17 2022'
    }
    foreach ($vs in @('2022', '2026')) {
        foreach ($target in @('x64', 'ARM64')) {
            Test-Case "VS $vs / $target compiler component selection" {
                $plan = Get-PrinterBuildPlan -Architecture $target -VisualStudioVersion $vs `
                    -HostArchitecture ARM64 -Root $fixture
                $suffix = if ($target -eq 'ARM64') { 'ARM64' } else { 'x86.x64' }
                $generator = if ($vs -eq '2026') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
                $component = if ($vs -eq '2026') {
                    "Microsoft.VisualStudio.Component.VC.14.44.17.14.$suffix"
                } else { "Microsoft.VisualStudio.Component.VC.Tools.$suffix" }
                Assert-Equal $plan.CMakeGenerator $generator
                Assert-Equal $plan.RequiredComponent $component
            }
        }
    }
    Test-Case 'unsupported VS version rejected' {
        Expect-Failure { Get-PrinterBuildPlan -VisualStudioVersion 2019 -HostArchitecture x64 -Root $fixture }
    }
    Test-Case 'build selector rejects unsupported VS version before tool access' {
        try { & (Join-Path $root 'build.ps1') -VisualStudioVersion 2019 }
        catch {
            if ($_.FullyQualifiedErrorId -notmatch 'ParameterArgumentValidationError') { throw }
            return
        }
        throw 'Invalid VS version accepted.'
    }
    Test-Case 'unsupported target rejected' {
        Expect-Failure { Get-PrinterBuildPlan -Architecture ARM64EC -HostArchitecture x64 -Root $fixture }
    }
    Test-Case 'unsupported host rejected' {
        Expect-Failure { Get-PrinterBuildPlan -Architecture ARM64 -HostArchitecture x86 -Root $fixture }
    }
    Test-Case 'build selector rejects invalid target before tool access' {
        try { & (Join-Path $root 'build.ps1') -Architecture ARM64EC }
        catch {
            if ($_.FullyQualifiedErrorId -notmatch 'ParameterArgumentValidationError') { throw }
            return
        }
        throw 'Invalid build target accepted.'
    }
    Test-Case 'other SDK rejected before tool access' {
        try { & (Join-Path $root 'build.ps1') -SdkVersion '10.0.22621.0' }
        catch {
            if ($_.FullyQualifiedErrorId -notmatch 'ParameterArgumentValidationError') { throw }
            return
        }
        throw 'Unpinned SDK accepted.'
    }
    foreach ($target in @('x64', 'ARM64')) {
        $machine = if ($target -eq 'ARM64') { 0xaa64 } else { 0x8664 }
        $bytes = New-TestPe $machine
        Test-Case "$target PE accepted, opposite target rejected" {
            Assert-PrinterPeMachine -Bytes $bytes -Architecture $target -Name 'synthetic PE'
            $opposite = if ($target -eq 'ARM64') { 'x64' } else { 'ARM64' }
            Expect-Failure { Assert-PrinterPeMachine -Bytes $bytes -Architecture $opposite -Name 'synthetic PE' }
        }
    }
    Test-Case 'truncated PE rejected' {
        Expect-Failure { Get-PrinterPeMachine -Bytes ([byte[]]::new(63)) }
    }
    Test-Case 'out-of-range PE offset rejected' {
        $bytes = New-TestPe 0xaa64
        [BitConverter]::GetBytes([int]::MaxValue).CopyTo($bytes, 0x3c)
        Expect-Failure { Get-PrinterPeMachine -Bytes $bytes }
    }
    Test-Case 'invalid PE signature rejected' {
        $bytes = New-TestPe 0xaa64
        $bytes[128] = 0
        Expect-Failure { Get-PrinterPeMachine -Bytes $bytes }
    }
    Test-Case 'ARM64EC not mislabeled ARM64' {
        Expect-Failure {
            Assert-PrinterPeMachine -Bytes (New-TestPe 0xa641) -Architecture ARM64 -Name 'synthetic ARM64EC'
        }
    }
    $stage = Join-Path $fixture 'package'
    New-Item $stage -ItemType Directory | Out-Null
    $manifestPath = Join-Path $stage 'AppxManifest.xml'
    $archive = Join-Path $fixture 'synthetic.msix'
    [IO.File]::WriteAllBytes((Join-Path $stage 'resources.pri'), [byte[]]@(1))
    foreach ($target in @('x64', 'ARM64')) {
        $machine = if ($target -eq 'ARM64') { 0xaa64 } else { 0x8664 }
        Test-Case "$target generated manifest, staged and archived PE consistency" {
            Set-PrinterManifestArchitecture -Source (Join-Path $root 'package/AppxManifest.xml') `
                -Destination $manifestPath -Architecture $target
            foreach ($name in @('PsaDiagnostic.exe', 'PsaTask.dll')) {
                [IO.File]::WriteAllBytes((Join-Path $stage $name), (New-TestPe $machine))
            }
            Assert-PrinterPackage -Path $stage -Architecture $target
            if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive }
            [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive)
            Assert-PrinterPackage -Path $archive -Architecture $target -Archive
        }
    }
    Test-Case 'manifest target mismatch rejected' {
        Expect-Failure { Assert-PrinterPackage -Path $stage -Architecture x64 }
    }
    Test-Case 'mixed-architecture staged DLL rejected' {
        [IO.File]::WriteAllBytes((Join-Path $stage 'PsaTask.dll'), (New-TestPe 0x8664))
        Expect-Failure { Assert-PrinterPackage -Path $stage -Architecture ARM64 }
    }
    Test-Case 'mixed-architecture archived DLL rejected' {
        Remove-Item -LiteralPath $archive
        [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive)
        Expect-Failure { Assert-PrinterPackage -Path $archive -Architecture ARM64 -Archive }
    }
    Test-Case 'missing staged binary rejected' {
        Remove-Item -LiteralPath (Join-Path $stage 'PsaTask.dll')
        Expect-Failure { Assert-PrinterPackage -Path $stage -Architecture ARM64 }
    }
    Test-Case 'missing archived binary rejected' {
        Remove-Item -LiteralPath $archive
        [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive)
        Expect-Failure { Assert-PrinterPackage -Path $archive -Architecture ARM64 -Archive }
    }
    Test-Case 'old package version rejected' {
        [xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw
        $manifest.Package.Identity.Version = '0.1.1.0'
        Expect-Failure { Assert-PrinterManifest -Manifest $manifest -Architecture ARM64 }
    }
    [xml]$dump = @'
<PriInfo><ResourceMap name="Miyako.PsaFeasibility"><ResourceMapSubtree name="Resources">
<NamedResource name="InteractiveDisplayName"><Candidate type="String"><Value>PSA Diagnostic OXPS (Save As)</Value></Candidate></NamedResource>
<NamedResource name="CaptureDisplayName"><Candidate type="String"><Value>PSA Diagnostic Capture (no file UI)</Value></Candidate></NamedResource>
</ResourceMapSubtree></ResourceMap></PriInfo>
'@
    Test-Case 'exact product PRI resource values accepted' { Assert-PrinterResourceDump -Dump $dump }
    Test-Case 'wrong PRI resource map rejected' {
        [xml]$wrong = $dump.OuterXml
        $wrong.PriInfo.ResourceMap.name = 'Another.Package'
        Expect-Failure { Assert-PrinterResourceDump -Dump $wrong }
    }
    Test-Case 'swapped PRI queue values rejected' {
        [xml]$wrong = $dump.OuterXml
        $wrong.SelectSingleNode("//NamedResource[@name='CaptureDisplayName']/Candidate/Value").InnerText = 'PSA Diagnostic OXPS (Save As)'
        Expect-Failure { Assert-PrinterResourceDump -Dump $wrong }
    }
    Test-Case 'missing PRI queue key rejected' {
        [xml]$wrong = $dump.OuterXml
        $node = $wrong.SelectSingleNode("//NamedResource[@name='CaptureDisplayName']")
        $node.ParentNode.RemoveChild($node) | Out-Null
        Expect-Failure { Assert-PrinterResourceDump -Dump $wrong }
    }
    Test-Case 'duplicate PRI queue key rejected' {
        [xml]$wrong = $dump.OuterXml
        $node = $wrong.SelectSingleNode("//NamedResource[@name='CaptureDisplayName']")
        $node.ParentNode.AppendChild($node.CloneNode($true)) | Out-Null
        Expect-Failure { Assert-PrinterResourceDump -Dump $wrong }
    }
    [IO.File]::WriteAllBytes((Join-Path $stage 'PsaTask.dll'), (New-TestPe 0xaa64))
    Test-Case 'empty staged PRI rejected' {
        [IO.File]::WriteAllBytes((Join-Path $stage 'resources.pri'), [byte[]]::new(0))
        Expect-Failure { Assert-PrinterPackage -Path $stage -Architecture ARM64 }
    }
    Test-Case 'missing archived PRI rejected' {
        Remove-Item -LiteralPath (Join-Path $stage 'resources.pri')
        Remove-Item -LiteralPath $archive
        [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive)
        Expect-Failure { Assert-PrinterPackage -Path $archive -Architecture ARM64 -Archive }
    }
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
Write-Host "$script:passed build-support cases passed (synthetic PE/ZIP tests, not Windows execution or MakeAppx validation)."
