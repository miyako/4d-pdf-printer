# Functions only. The caller supplies the existing guarded readers and raw inventory.
function Get-NumericJournalMappings([object[]]$Mappings,[switch]$AllowPartial) {
    if($Mappings.Count -gt 32){throw 'Journal mapping count exceeds32.'}
    $indexed=[object[]]::new(32)
    $slots=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($mapping in $Mappings){
        if($mapping -isnot [Collections.IDictionary] -or $mapping.Count -ne 4){throw 'Exact journal mapping dictionary required.'}
        foreach($key in $mapping.Keys){
            if($key -isnot [string] -or $key -cnotin @('sequence','slot','bytes','sha256')){throw 'Unexpected journal mapping key.'}
        }
        $number=$mapping['sequence'];$slot=$mapping['slot'];$bytes=$mapping['bytes'];$sha=$mapping['sha256']
        if($number -isnot [int] -or $number -lt 0 -or $number -gt 31){throw 'Journal numeric sequence type/range.'}
        if($slot -isnot [string] -or $slot -cnotmatch '^journal-slot-(0[0-9]|[12][0-9]|3[01])\.bin$'){throw 'Journal slot shape.'}
        if(($bytes -isnot [int] -and $bytes -isnot [long]) -or $bytes -le 0 -or $bytes -gt 8192){throw 'Journal mapping byte bound.'}
        if($sha -isnot [string] -or $sha -cnotmatch '^[0-9a-f]{64}$'){throw 'Journal mapping SHA256.'}
        if($null -ne $indexed[$number]){throw 'Duplicate journal sequence.'}
        if(-not $slots.Add($slot)){throw 'Duplicate journal slot.'}
        $indexed[$number]=$mapping
    }
    $ordered=[Collections.Generic.List[object]]::new()
    for($number=0;$number -lt 32;$number++){
        if($null -ne $indexed[$number]){$ordered.Add($indexed[$number])}
    }
    if(-not $AllowPartial){
        for($number=0;$number -lt $ordered.Count;$number++){
            if($ordered[$number]['sequence'] -ne $number){throw 'Journal numeric sequence gap.'}
        }
    }
    return [pscustomobject]@{records=[object[]]$ordered.ToArray()}
}

function CaptureJournal {
    if($trial.journal_attempted){throw 'Second journal capture forbidden.'};$trial.journal_attempted=$true
    $v=$trial.final_inventory
    $journal=[ordered]@{collection_attempted=$false;activation=$null;records=@();terminal=$null;unresolved=@()
        deferral_return_proved=$false;process_exit_proved=$false;physical_unload_proved=$false}
    if($null -eq $v){Save $journal 'journal-capture.json';throw 'Raw inventory unavailable; no journal content collection.'}
    Save ([ordered]@{cache=$v.rows;terminal_siblings=$v.terminal_siblings;grace_complete=$trial.grace_complete}) 'journal-raw-inventory.json'
    $old=(Baseline).cache.tree
    $reparse=@($v.rows|Where-Object {$_.path -cnotin @($old.path) -and ($_.attributes -band [IO.FileAttributes]::ReparsePoint)})
    if($reparse.Count){
        $journal.unresolved=$reparse;Save $journal 'journal-capture.json'
        throw 'New namespace reparse point; journal content not attempted.'
    }
    if(-not $trial.grace_complete -or $v.metadata_errors.Count){Save $journal 'journal-capture.json';return}
    $journal.collection_attempted=$true
    $ids=@($v.rows|Where-Object {$_.directory -and $_.path -cmatch '^ActivationDiagnostics\\([0-9a-f]{32})$' -and $Matches[1] -cnotin $activations})
    if($ids.Count -ne 1){Save $journal 'journal-capture.json';throw 'Exactly one new activation required; no retry.'}
    $id=$ids[0].path.Split('\')[1];$journal.activation=$id
    $prefix="ActivationDiagnostics\$id\"
    $rows=@($v.rows|Where-Object {$_.path.StartsWith($prefix,[StringComparison]::Ordinal)})
    $slots=@();$total=0L;$index=0;$seen=[bool[]]::new(32)
    try {
        if($rows.Count -gt 64){throw 'New diagnostic entry limit.'}
        foreach($row in $rows){
            $leaf=$row.path.Substring($prefix.Length);$total+=$row.bytes
            if($row.directory -or ($row.attributes -band [IO.FileAttributes]::ReparsePoint) -or $leaf -cnotmatch '^([0-9]|[12][0-9]|3[01])\.json$' -or
                ($row.bytes -isnot [int] -and $row.bytes -isnot [long]) -or
                $row.bytes -le 0 -or $row.bytes -gt 8192 -or $total -gt 512KB){
                $journal.unresolved+=,$row;continue
            }
            $number=[int]$leaf.Split('.')[0]
            if($seen[$number]){throw 'Duplicate journal source sequence.'}
            $seen[$number]=$true
            $slot=('journal-slot-{0:D2}.bin' -f $index);$index++
            $null=CopyRecord (Join-Path $v.root $row.path) $slot 8192
            $entry=[ordered]@{sequence=$number;slot=$slot;bytes=$row.bytes;sha256=(Digest (Join-Path $evidence $slot))}
            $null=Get-NumericJournalMappings @($entry) -AllowPartial
            $slots+=,$entry
        }
        $journal.records=[object[]](Get-NumericJournalMappings $slots -AllowPartial).records
        $expected="DesktopPdfJournal-$id.json"
        foreach($item in $v.terminal_siblings){
            if($item.name -cne $expected -or $item.metadata.directory -or ($item.metadata.attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.metadata.bytes -le 0 -or $item.metadata.bytes -gt 4096){
                $journal.unresolved+=,$item;continue
            }
            if($null -ne $journal.terminal){throw 'Duplicate terminal.'}
            $null=CopyRecord (Join-Path $v.cache $expected) 'journal-terminal.bin' 4096
            $journal.terminal=[ordered]@{slot='journal-terminal.bin';bytes=$item.metadata.bytes;sha256=(Digest (Join-Path $evidence 'journal-terminal.bin'))}
        }
        $null=Get-NumericJournalMappings $slots
        if($journal.unresolved.Count -or $null -eq $journal.terminal){throw 'Unknown/staging/missing journal artifact, metadata retained; no normalization.'}
    } finally {
        $journal.records=[object[]](Get-NumericJournalMappings $slots -AllowPartial).records
        Save $journal 'journal-capture.json'
    }
}
