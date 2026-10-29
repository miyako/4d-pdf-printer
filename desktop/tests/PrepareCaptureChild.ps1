param([string]$Mode)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$record = [ordered]@{
    schema = '4d-pdf-printer.activation-probe'; version = 1
    stage = 'prepared-private-correlation'; job_id = ('a' * 64)
    pdfProduced = $false; activationGatePassed = $false
}
$unicode = [string][char]0x65e5 + [char]0x672c + [char]0x8a9e
$sidModes = @('sid-null', 'sid-invalid', 'sid-nonzero-success-null', 'sid-api-error', 'sid-valid-prior', 'sid-unsafe')
function Write-SyntheticUnicodeStderr {
    $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($unicode)
    $stream = [Console]::OpenStandardError()
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
}
switch ($Mode) {
    { $_ -cin $sidModes } {
        $record.Remove('job_id')
        $record.stage = 'control-arguments'
        $record.hresult = 2147942405
        $snapshot = [ordered]@{ hresult = 0; outputPresent = $false; validityChecked = $false; valid = $false }
        $context = [ordered]@{
            stage = 'prepare.security'; api = 'validation'; object = 'package-sid'
            origin = 'validation'; reason = 'sid-missing'; derivation = $snapshot
        }
        switch ($Mode) {
            'sid-invalid' {
                $snapshot.outputPresent = $true; $snapshot.validityChecked = $true
                $context.api = 'IsValidSid'; $context.reason = 'sid-validity'
            }
            'sid-nonzero-success-null' { $snapshot.hresult = 1 }
            'sid-api-error' {
                $record.hresult = 2147942487; $snapshot.hresult = 2147942487
                $context.api = 'DeriveAppContainerSidFromAppContainerName'; $context.origin = 'win32'
                $context.reason = 'none'; $context.code = 2147942487; $context.codeKind = 'hresult'
            }
            'sid-valid-prior' {
                $snapshot.outputPresent = $true; $snapshot.validityChecked = $true; $snapshot.valid = $true
                $snapshot.arbitrary = 'must-not-be-published'
                $context.object = 'user-sid'; $context.api = 'ConvertSidToStringSidW'; $context.origin = 'win32'
                $context.reason = 'none'; $context.code = 5; $context.codeKind = 'win32'
            }
            'sid-unsafe' {
                $context.derivation = @{
                    hresult = 'must-not-be-published'; outputPresent = 'raw-user-sid'
                    validityChecked = 1; valid = 'private-path-not-an-api'; arbitrary = 'must-not-be-published'
                }
            }
        }
        $record.error_context = $context
    }
    'native-error' {
        $record.Remove('job_id')
        $record.stage = 'control-arguments'
        $record.hresult = 2147942405
        $record.error_context = [ordered]@{
            stage = 'current.token'; api = 'GetTokenInformation'; object = 'token-user'
            origin = 'validation'; reason = 'token-size-probe'; code = 6; codeKind = 'win32'
        }
        Write-SyntheticUnicodeStderr
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
    'success' { Write-SyntheticUnicodeStderr }
    default { throw 'Unknown controlled preparation case.' }
}
[Console]::Write((ConvertTo-Json -InputObject $record -Compress))
if ($Mode -cin @('native-error', 'diagnostic-unsafe', 'elevation-api-error', 'elevation-size-error') -or
    $Mode -cin $sidModes) { exit 1 }
