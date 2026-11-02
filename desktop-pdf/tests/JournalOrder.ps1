$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../tools/JournalCapture.ps1')
$checks=0
function Entry([object]$Number,[string]$Slot='') {
    if(-not $Slot){$Slot=('journal-slot-{0:D2}.bin' -f [int]$Number)}
    [ordered]@{sequence=$Number;slot=$Slot;bytes=8192;sha256=('a'*64)}
}
function Reject([scriptblock]$Action) {
    try { & $Action | Out-Null } catch { $script:checks++;return }
    throw 'Expected numeric-mapping rejection.'
}
$inputRows=@(0..31 | ForEach-Object {Entry ([int]$_)})
$lexical=@($inputRows | Sort-Object { [string]$_['sequence'] })
$ordered=(Get-NumericJournalMappings $lexical).records
for($i=0;$i -lt 32;$i++){
    if($ordered[$i]['sequence'] -ne $i -or $ordered[$i]['slot'] -cne $inputRows[$i]['slot']){
        throw 'Numeric map lost source/slot association.'
    }
    $checks++
}
foreach($bad in @($true,'1',1.0,[long]1,-1,32)){
    Reject {Get-NumericJournalMappings @((Entry $bad 'journal-slot-00.bin'))}
}
foreach($field in @('slot','bytes','sha256','sequence')){
    $row=Entry 0;$row.Remove($field)
    Reject {Get-NumericJournalMappings @($row)}
}
foreach($bad in @('journal-slot-32.bin','../slot','journal-slot-0.bin','JOURNAL-slot-00.bin')){
    $row=Entry 0;$row['slot']=$bad
    Reject {Get-NumericJournalMappings @($row)}
}
foreach($bad in @(0,8193,$true,'10',1.5)){
    $row=Entry 0;$row['bytes']=$bad
    Reject {Get-NumericJournalMappings @($row)}
}
foreach($bad in @(('A'*64),('a'*63),$null)){
    $row=Entry 0;$row['sha256']=$bad
    Reject {Get-NumericJournalMappings @($row)}
}
Reject {Get-NumericJournalMappings @((Entry 0),(Entry 0 'journal-slot-01.bin'))}
Reject {Get-NumericJournalMappings @((Entry 0),(Entry 1 'journal-slot-00.bin'))}
Reject {Get-NumericJournalMappings @((Entry 0),(Entry 2))}
Reject {Get-NumericJournalMappings @((Entry 0),([pscustomobject](Entry 1)))}
$partial=(Get-NumericJournalMappings @((Entry 2),(Entry 0)) -AllowPartial).records
if($partial.Count -ne 2 -or $partial[0]['sequence'] -ne 0 -or $partial[1]['sequence'] -ne 2){
    throw 'Partial preservation must stay numeric without pretending gaps passed.'
}
$checks++
Write-Output "PASS $checks production numeric-map checks; local PowerShell only, NOT native PS5.1."
