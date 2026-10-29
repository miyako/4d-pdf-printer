param([string]$Report, [ValidateSet('success', 'error', 'large', 'sleep')][string]$Mode = 'success')
$ErrorActionPreference = 'Stop'
if ($Mode -eq 'large') { [Console]::Out.Write(('x' * 131072)); Start-Sleep 30; exit }
if ($Mode -eq 'sleep') { Start-Sleep 30; exit }
[Console]::WriteLine('bounded test subprocess; not a renderer')
if ($Mode -eq 'error') {
    [IO.File]::WriteAllText($Report, '{"schema":"proof-test","stage":"protocol","result":"error"}')
    exit 7
}
[IO.File]::WriteAllText($Report, '{"schema":"proof-test","stage":"done","result":"completed","reference_target_approved":false,"within_reference_target":false,"productUsable":false,"manual_review_required":true}')
