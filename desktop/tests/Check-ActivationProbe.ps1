Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'Invoke-ActivationProbe.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($script, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Probe script does not parse.' }
$writer = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Save-NewProbeRecord'
}, $true)
if ($null -eq $writer) { throw 'Actual probe writer is missing.' }
. ([scriptblock]::Create($writer.Extent.Text))
$root = Join-Path ([IO.Path]::GetTempPath()) "activation-probe-script-unit-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $root | Out-Null
try {
    $path = Join-Path $root 'unicode.json'
    $unicode = [string][char]0x65e5 + [char]0x672c + [char]0x8a9e
    Save-NewProbeRecord $path @{ text = $unicode; nested = @{ actual = $true } }
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes[0] -eq 239 -or (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json).text -cne $unicode) {
        throw 'UTF8/no-BOM/Unicode JSON integrity failed.'
    }
    $rejected = $false
    try { Save-NewProbeRecord $path @{ replacement = $true } } catch { $rejected = $true }
    if (-not $rejected -or [Convert]::ToBase64String($bytes) -cne
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))) { throw 'CreateNew collision modified existing bytes.' }
    $large = Join-Path $root 'large.json'
    $rejected = $false
    try { Save-NewProbeRecord $large @{ value = ('a' * 17000) } } catch { $rejected = $true }
    if (-not $rejected -or (Test-Path -LiteralPath $large)) { throw 'Oversize JSON was published.' }
    $blocked = Join-Path $root 'must-not-exist'
    $rejected = $false
    try { & $script -PrivateOutputRoot $blocked 2>$null } catch { $rejected = $true }
    if (-not $rejected -or (Test-Path -LiteralPath $blocked)) { throw 'Missing consent caused work.' }
    $rejected = $false
    try { & $script -PrivateOutputRoot $blocked -PreparationOnly -AllowOneSyntheticPrint 2>$null } catch {
        $rejected = $_.Exception.Data['ProbeError'].stage -ceq 'consent'
    }
    if (-not $rejected -or (Test-Path -LiteralPath $blocked)) { throw 'Preparation-only lacked its separate write consent.' }
    if (-not $IsWindows) {
        $rejected = $false
        try { & $script -PrivateOutputRoot $blocked -AllowOneSyntheticPrint 2>$null } catch { $rejected = $true }
        if (-not $rejected -or (Test-Path -LiteralPath $blocked)) { throw 'Non-Windows preflight caused work.' }
        $rejected = $false
        try { & $script -PrivateOutputRoot $blocked -PreparationOnly -AllowPackagePreparation 2>$null } catch {
            $rejected = $_.Exception.Data['ProbeError'].stage -ceq 'desktop-preflight'
        }
        if (-not $rejected -or (Test-Path -LiteralPath $blocked)) { throw 'Preparation-only platform guard caused work.' }
    }
    [xml]$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../package/activation/AppxManifest.xml') -Raw
    $queues = @($manifest.SelectNodes("//*[local-name()='PrintSupportVirtualPrinter']"))
    $hostExtensions = @($manifest.SelectNodes("//*[local-name()='Extension' and @Category='windows.fullTrustProcess']"))
    if ($manifest.Package.Identity.Name -cne 'Miyako.DesktopPdfPrototype' -or
        $manifest.Package.Identity.Version -cne '0.2.0.0' -or $queues.Count -ne 1 -or
        $queues[0].HasAttribute('OutputFileTypes') -or $hostExtensions.Count -ne 1 -or
        $hostExtensions[0].Executable -cne 'DesktopPdfProbeHost.exe') { throw 'Fixed probe manifest contract changed.' }
    Write-Output 'Probe script: actual writer Unicode/collision/bound, consent and platform guards, fixed manifest passed. NO native/printing/activation evidence.'
} finally {
    foreach ($file in @('unicode.json', 'large.json')) {
        $path = Join-Path $root $file
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
    Remove-Item -LiteralPath $root
}
& (Join-Path $PSScriptRoot 'Check-ActivationPrepare.ps1')
