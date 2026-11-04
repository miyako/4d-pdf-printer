# Reader models only: no build helper main, native code, package or installed-state access.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../BuildValidation.ps1')
function Need([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message } }
$base='{"checks":[{"started_utc":"2026-11-04T00:00:00Z","ended_utc":"2026-11-04T00:00:01Z"}]}'
$positive=0
foreach ($end in @('2026-11-04T00:00:01Z','2026-11-04T00:00:01.1Z',
    '2026-11-04T00:00:01.1234567Z','2026-11-04T00:00:01.0000000Z','2026-11-04T00:00:00Z')) {
    $raw=$base.Replace('2026-11-04T00:00:01Z',$end)
    $before=$raw
    $value=ConvertFrom-ScopedBuildReceipt $raw
    Need ($value.checks[0].started_utc -is [string] -and $value.checks[0].ended_utc -is [string]) 'Reader lost string tokens.'
    Need ($value.checks[0].started_utc -ceq '2026-11-04T00:00:00Z' -and $value.checks[0].ended_utc -ceq $end) 'Reader changed timestamp values.'
    Need ($raw -ceq $before) 'Reader changed original JSON text.'
    ++$positive
}
$escaped=$base.Replace('00:00:00Z','00:00:00\u005A')
$value=ConvertFrom-ScopedBuildReceipt $escaped
Need ($value.checks[0].started_utc -ceq '2026-11-04T00:00:00Z' -and $escaped.Contains('\u005A')) 'Decoded string and raw JSON bytes conflated.'
$negative=[Collections.Generic.List[string]]::new()
foreach ($token in @('0','true','null','[]','{}')) {
    $negative.Add($base.Replace('"2026-11-04T00:00:00Z"',$token))
    $negative.Add($base.Replace('"2026-11-04T00:00:01Z"',$token))
}
foreach ($date in @('2026-02-29T00:00:00Z','2026-13-04T00:00:00Z','2026-11-32T00:00:00Z',
    '2026-11-04T24:00:00Z','2026-11-04T00:60:00Z','2026-11-04T00:00:60Z','0000-11-04T00:00:00Z',
    '2026-11-04T00:00:00+09:00','2026-11-04T00:00:00+00:00','2026-11-04T00:00:00',
    '2026-11-04 00:00:00Z','2026-11-04T00:00:00z','2026-11-04T00:00:00.12345678Z',
    '2026-11-04T00:00:00.Z',' 2026-11-04T00:00:00Z')) {
    $negative.Add($base.Replace('2026-11-04T00:00:00Z',$date))
}
$negative.Add($base.Replace('"started_utc":"2026-11-04T00:00:00Z",',''))
$negative.Add($base.Replace(',"ended_utc":"2026-11-04T00:00:01Z"',''))
$negative.Add($base.Replace('2026-11-04T00:00:01Z','2026-11-03T23:59:59Z'))
$negative.Add($base.Replace('"started_utc":','"started_utc":"2026-11-04T00:00:00Z","started_utc":'))
$negative.Add($base.Replace('"ended_utc":','"ended_utc":"2026-11-04T00:00:01Z","ended_utc":'))
$negative.Add($base.Replace('"started_utc":','"started_\u0075tc":"2026-11-04T00:00:00Z","started_utc":'))
$negative.Add($base.Replace('"checks":','"checks":[],"checks":'))
$negative.Add($base.Replace('"checks":','"che\u0063ks":[],"checks":'))
$negative.Add($base.Substring(0,$base.Length-1))
$negative.Add($base.Replace('}]}','},]}'))
$negative.Add('{"checks":null}')
foreach ($raw in $negative) {
    $rejected=$false
    try { $null=ConvertFrom-ScopedBuildReceipt $raw } catch { $rejected=$true }
    Need $rejected "Invalid timestamp transport admitted: $raw"
}
Write-Output "PASS: $positive exact whole/fractional UTC string cases, 1 escaped-token/raw-text case, $($negative.Count) negative reader cases; no native evidence."
