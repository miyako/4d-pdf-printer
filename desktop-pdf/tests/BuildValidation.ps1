# Synthetic receipt-shape models ONLY. Never imports build.ps1 or executes its main.
param([Parameter(Mandatory)][string]$ModelRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../BuildValidation.ps1')
function Need([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message } }
function Clone($Value) { ConvertFrom-ScopedBuildReceipt ($Value | ConvertTo-Json -Depth 15) }
$directory=Join-Path $ModelRoot 'synthetic-not-a-native-build'
$sha='1'*40; $tree='2'*40; $hash='a'*64
function Fact([string]$Path) { [ordered]@{path=$Path;bytes=1;sha256=$hash} }
$tools=@(0..7 | ForEach-Object { Fact (Join-Path $ModelRoot "synthetic-tool-$_") })
$testRoot=Join-Path $directory 'isolated-journal-v3-files'
$checks=@(0..1 | ForEach-Object {
    $i=$_
    $name=@('contract','isolated-journal-v3')[$i]
    $program=Join-Path $directory ('Release/' + @('IntegrationContract.exe','IntegrationNative.exe')[$i])
    [string[]]$arguments=@()
    if ($i -eq 1) { $arguments=@('--isolated-journal',$testRoot) }
    [ordered]@{name=$name;program=$program;argv=@($arguments);cwd=$directory;status='closed';exit_code=0
        started_utc='2026-11-04T00:00:00Z';ended_utc='2026-11-04T00:00:01Z'
        stdout=(Fact (Join-Path $directory "$name.stdout.bin"));stderr=(Fact (Join-Path $directory "$name.stderr.bin"))
        image=(Fact $program);image_after=(Fact $program)}
})
$valid=Clone ([ordered]@{source_sha=$sha;source_tree=$tree;architecture='arm64';validation_profile='isolated-journal-v3'
    validation_status='passed';tools=$tools;checks=$checks;test_root=$testRoot
    configuration='Release';targets=@('IntegrationApp','IntegrationTask','IntegrationHost','IntegrationClient','IntegrationContract','IntegrationNative')
    pe=@('IntegrationApp.exe','IntegrationTask.dll','IntegrationHost.exe','IntegrationClient.exe' |
        ForEach-Object { [ordered]@{file=$_;bytes=1;sha256=$hash} })})
$tools=$valid.tools
Assert-BuildValidation $valid 'isolated-journal-v3' $sha $tree 'arm64' $directory $tools
$legacy=[ordered]@{source_sha=$sha;architecture='arm64';ctests='passed';pe=@()} |
    ConvertTo-Json | ConvertFrom-Json
Assert-BuildValidation $legacy 'full-suite' $sha $tree 'arm64' $directory @()
$cases=@(
    {param($r) $r.source_sha='3'*40},
    {param($r) $r.source_tree='3'*40},
    {param($r) $r.architecture='x64'},
    {param($r) $r.validation_profile='full-suite'},
    {param($r) $r.validation_status='failed'},
    {param($r) $r.configuration='Debug'},
    {param($r) $r.targets=@('all')},
    {param($r) $r.test_root=Join-Path $ModelRoot 'wrong'},
    {param($r) $r.tools[0].sha256='b'*64},
    {param($r) $r.tools=@()},
    {param($r) $r.pe=@()},
    {param($r) $r.pe[0].file='IntegrationNative.exe'},
    {param($r) $r.pe[0].sha256='bad'},
    {param($r) $r.pe[0].bytes=-1},
    {param($r) $r.checks=@()},
    {param($r) $r.checks[1].argv=@()},
    {param($r) $r.checks[1].exit_code=1},
    {param($r) $r.checks[0].status='started'},
    {param($r) $r.checks[0].stdout.path=Join-Path $ModelRoot 'wrong'},
    {param($r) $r.checks[0].image_after.sha256='b'*64},
    {param($r) $r.checks[0].ended_utc=$null},
    {param($r) $r | Add-Member -NotePropertyName ctests -NotePropertyValue 'passed'}
)
foreach ($change in $cases) {
    $r=Clone $valid
    & $change $r
    $rejected=$false
    try { Assert-BuildValidation $r 'isolated-journal-v3' $sha $tree 'arm64' $directory $tools }
    catch { $rejected=$true }
    Need $rejected 'Malformed/mixed scoped receipt admitted.'
}
foreach ($pair in @(@($valid,'full-suite'),@($legacy,'isolated-journal-v3'))) {
    $rejected=$false
    try { Assert-BuildValidation $pair[0] $pair[1] $sha $tree 'arm64' $directory $tools }
    catch { $rejected=$true }
    Need $rejected 'Profile mismatch admitted.'
}
$rejected=$false
try { Assert-BuildFileBytes $valid.checks[0].stdout } catch { $rejected=$true }
Need $rejected 'Synthetic missing file facts treated as measured bytes.'
Write-Output 'PASS: 2 positive, 25 negative synthetic receipt checks; NOT native/build evidence.'
