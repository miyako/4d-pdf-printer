#Requires -Version 7.4
# Read-only post-install check for the installed Miyako.DesktopPdfIntegration package.
# It changes nothing: no package, queue, registry, PrintTicket or LocalCache writes.
# Exit 0 = every check passed; 1 = at least one check failed; 2 = could not run.
# Windows can create a virtual queue without applying the app PDC; the queue then shows
# the class driver's stock capabilities (e.g. 600 dpi, 84 media sizes) instead of ours.
[CmdletBinding()]
param([string]$PackageName = 'Miyako.DesktopPdfIntegration')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$checks = [Collections.Generic.List[object]]::new()
function Check([string]$Name, [bool]$Ok, [string]$Detail) {
    $checks.Add([ordered]@{ check = $Name; ok = $Ok; detail = $Detail })
}
function OptionsByFeature([xml]$Document) {
    $map = [ordered]@{}
    foreach ($feature in $Document.SelectNodes('//*[local-name()="Feature"]')) {
        $name = if ($feature.HasAttribute('name')) { $feature.GetAttribute('name') } else { $feature.Name }
        $local = $name.Split(':')[-1]
        $map[$local] = @($feature.SelectNodes('*[local-name()="Option"]') | ForEach-Object {
            $option = if ($_.HasAttribute('name')) { $_.GetAttribute('name') } else { $_.Name }
            $option.Split(':')[-1] })
    }
    $map
}

try {
    $package = @(Get-AppxPackage -Name $PackageName)
    if ($package.Count -ne 1) { throw "Expected exactly one installed $PackageName, found $($package.Count)." }
    $package = $package[0]
    Check 'package' ($package.Status.ToString() -ceq 'Ok') "$($package.PackageFullName) status $($package.Status)"

    $pdcPath = Join-Path $package.InstallLocation 'Config\IntegrationPdc.xml'
    [xml]$pdc = Get-Content -LiteralPath $pdcPath -Raw
    $expected = [ordered]@{}
    $defaults = @{}
    foreach ($feature in $pdc.SelectNodes('/*/*[@*[local-name()="psftype"]="Feature"]')) {
        $options = @($feature.SelectNodes('*[@*[local-name()="psftype"]="Option"]'))
        $expected[$feature.LocalName] = @($options | ForEach-Object LocalName)
        $default = @($feature.SelectNodes('*[@*[local-name()="psftype"]="Option" and @*[local-name()="default"]="true"]'))
        if ($default.Count -eq 1) { $defaults[$feature.LocalName] = $default[0].LocalName }
    }
    if ($expected.Count -eq 0) { throw "No features found in $pdcPath." }

    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    foreach ($uri in 'desktop-pdf:silent', 'desktop-pdf:save-as') {
        $prefix = "$($package.PackageFamilyName)_${uri}_"
        $printer = @(Get-Printer | Where-Object { $_.PortName.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
        Check "$uri queue" ($printer.Count -eq 1) "$($printer.Count) queue(s) on port $prefix*"
        if ($printer.Count -ne 1) { continue }
        $name = $printer[0].Name

        $key = Get-Item -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Control\Print\Printers\${sid}:$name" -ErrorAction SilentlyContinue
        $changeId = if ($key) { $key.OpenSubKey('PrinterDriverData')?.GetValue('V4_PDC_ChangeID') } else { $null }
        Check "$uri app PDC applied" ($null -ne $changeId) "V4_PDC_ChangeID=$changeId"

        $configuration = Get-PrintConfiguration -PrinterName $name
        $actual = OptionsByFeature ([xml]$configuration.PrintCapabilitiesXML)
        foreach ($feature in $expected.Keys) {
            $want = ($expected[$feature] | Sort-Object) -join ','
            $have = if ($actual.Contains($feature)) { ($actual[$feature] | Sort-Object) -join ',' } else { '<missing>' }
            Check "$uri capability $feature" ($have -ceq $want) "expected [$want], queue offers [$have]"
        }
        $ticket = OptionsByFeature ([xml]$configuration.PrintTicketXML)
        foreach ($feature in $defaults.Keys) {
            $have = if ($ticket.Contains($feature)) { $ticket[$feature] -join ',' } else { '<missing>' }
            Check "$uri default $feature" ($have -ceq $defaults[$feature]) "expected $($defaults[$feature]), ticket has $have"
        }
    }

    $runtimePath = Join-Path $env:LOCALAPPDATA "Packages\$($package.PackageFamilyName)\LocalCache\DesktopPdf\runtime.json"
    if (Test-Path -LiteralPath $runtimePath) {
        $root = (Get-Content -LiteralPath $runtimePath -Raw | ConvertFrom-Json).root
        Check 'runtime configured' ([bool]$root -and (Test-Path -LiteralPath $root)) "runtime.json root $root"
    } else {
        Check 'runtime configured' $false 'runtime.json absent; run IntegrationClient configure-runtime <root>'
    }
} catch {
    [ordered]@{ ok = $false; error = $_.Exception.Message; checks = $checks } | ConvertTo-Json -Depth 4
    exit 2
}
$ok = -not ($checks | Where-Object { -not $_.ok })
[ordered]@{ ok = [bool]$ok; checks = $checks } | ConvertTo-Json -Depth 4
exit $(if ($ok) { 0 } else { 1 })
