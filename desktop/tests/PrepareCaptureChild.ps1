param([string]$Mode)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$record = [ordered]@{
    schema = '4d-pdf-printer.activation-probe'; version = 1
    stage = 'prepared-private-correlation'; job_id = ('a' * 64)
    pdfProduced = $false; activationGatePassed = $false
}
$unicode = [string][char]0x65e5 + [char]0x672c + [char]0x8a9e
switch ($Mode) {
    'native-error' {
        $record.Remove('job_id')
        $record.stage = 'control-arguments'
        $record.hresult = 2147942405
        [Console]::Error.Write($unicode)
    }
    'empty' { exit 0 }
    'malformed' { [Console]::Write('{ invalid'); exit 0 }
    'array' { [Console]::Write('[]'); exit 0 }
    'bad-stage' { $record.stage = 'not-a-prepare-result' }
    'bad-id' { $record.job_id = '../not-a-job' }
    'flood' { [Console]::Write('x' * 20000); exit 0 }
    'stderr-flood' { [Console]::Error.Write('x' * 20000); exit 0 }
    'invalid-utf8' { [Console]::OpenStandardOutput().WriteByte(255); exit 0 }
    'timeout' { Start-Sleep -Seconds 10; exit 0 }
    'success' { [Console]::Error.Write($unicode) }
    default { throw 'Unknown controlled preparation case.' }
}
[Console]::Write((ConvertTo-Json -InputObject $record -Compress))
if ($Mode -ceq 'native-error') { exit 1 }
