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
        $record.error_context = [ordered]@{
            stage = 'current.token'; api = 'GetTokenInformation'; object = 'token-user'
            origin = 'validation'; reason = 'token-size-probe'; code = 6; codeKind = 'win32'
        }
        [Console]::Error.Write($unicode)
    }
    'diagnostic-unsafe' {
        $record.Remove('job_id')
        $record.stage = 'control-arguments'
        $record.hresult = 2147942405
        $record.error_context = @{
            stage = ('a' * 64); api = 'private-path-not-an-api'; object = 'raw-user-sid'
            origin = 'unknown'; reason = 'private-value'; codeKind = 'secret'; code = 'private-code'
            arbitrary = 'must-not-be-published'
        }
    }
    'elevation-api-error' {
        $record.Remove('job_id')
        $record.stage = 'control-arguments'
        $record.hresult = 2147942406
        $record.error_context = [ordered]@{
            stage = 'current.token'; api = 'GetTokenInformation'; object = 'token-elevation'
            origin = 'win32'; reason = 'value-query'; code = 6; codeKind = 'win32'
        }
    }
    'elevation-size-error' {
        $record.Remove('job_id')
        $record.stage = 'control-arguments'
        $record.hresult = 2147942405
        $record.error_context = [ordered]@{
            stage = 'current.token'; api = 'validation'; object = 'token-elevation'
            origin = 'validation'; reason = 'token-fixed-size'
        }
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
if ($Mode -cin @('native-error', 'diagnostic-unsafe', 'elevation-api-error', 'elevation-size-error')) { exit 1 }
