param([string]$Report, [ValidateSet('success', 'large', 'sleep')][string]$Mode = 'success')
$ErrorActionPreference = 'Stop'
if ($Mode -eq 'large') { [Console]::Out.Write(('x' * 131072)); Start-Sleep 30; exit }
if ($Mode -eq 'sleep') { Start-Sleep 30; exit }
[Console]::WriteLine('bounded test subprocess; not a renderer')
[IO.File]::WriteAllText($Report, '{"schema":"proof-test","stage":"done"}')
