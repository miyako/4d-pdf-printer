param(
    [Parameter(Mandatory=$true)][string]$Fixture,
    [Parameter(Mandatory=$true)][string]$FrozenOperator,
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][ValidateSet('ps7-model','native-windows-ps51')][string]$Boundary,
    [ValidateSet('x64','ARM64')][string]$Architecture,
    [Parameter(Mandatory=$true)][string]$InputSha256,
    [ValidateSet('positive','gap','duplicate','reparse','staging','copy-fault')][string]$Case='positive',
    [switch]$PolicyChecksOnly
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
function Assert-FixtureRuntime($Facts,[string]$Mode,[string]$Target) {
    if($Facts.windows -isnot [bool] -or $Facts.major -isnot [int] -or $Facts.minor -isnot [int] -or
        $Facts.pointer_bytes -isnot [int]){throw 'Observed runtime facts must have exact primitive types.'}
    if($Mode -ceq 'ps7-model'){
        if($Facts.edition -cne 'Core' -or $Facts.major -lt 7 -or $Target){
            throw 'PS7/model requires Core7+ and no native architecture claim.'
        }
    } elseif($Mode -ceq 'native-windows-ps51'){
        if($Facts.windows -ne $true){throw 'Native boundary requires Windows.'}
        if($Facts.edition -cne 'Desktop' -or $Facts.major -ne 5 -or $Facts.minor -ne 1){
            throw 'Native boundary requires Windows PowerShell Desktop5.1.'
        }
        if($Facts.pointer_bytes -ne 8){throw 'Native boundary requires64-bit process.'}
        if($Target -cnotin @('x64','ARM64')){throw 'Explicit native target required.'}
        if($Facts.os_arch -ine $Target -or $Facts.process_arch -ine $Target){
            throw 'Native OS/process target mismatch; no emulation fallback.'
        }
        $machine=if($Target -ceq 'ARM64'){0xaa64}else{0x8664}
        if($Facts.image_machine -isnot [int] -or $Facts.image_machine -ne $machine){throw 'Native interpreter PE mismatch.'}
        if($Facts.elevated -isnot [bool] -or $Facts.elevated){throw 'Unelevated native context required.'}
    } else {throw 'Explicit fixture boundary required.'}
}
function InterpreterMachine([string]$Path) {
    $bytes=[IO.File]::ReadAllBytes($Path)
    if($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes,0) -ne 0x5a4d){throw 'Interpreter DOS header.'}
    $offset=[BitConverter]::ToInt32($bytes,0x3c)
    if($offset -lt 64 -or $offset -gt $bytes.Length-6 -or [BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550){
        throw 'Interpreter PE header.'
    }
    [int][BitConverter]::ToUInt16($bytes,$offset+4)
}
$runtime=[ordered]@{windows=([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT)
    edition=$PSVersionTable.PSEdition;major=$PSVersionTable.PSVersion.Major;minor=$PSVersionTable.PSVersion.Minor
    version=$PSVersionTable.PSVersion.ToString();pointer_bytes=[IntPtr]::Size
    os_arch=$null;process_arch=$null;image_machine=$null;elevated=$null
    image=[Diagnostics.Process]::GetCurrentProcess().MainModule.FileName}
if($PolicyChecksOnly){
    Assert-FixtureRuntime $runtime $Boundary $Architecture
    if($Boundary -cne 'ps7-model'){throw 'Policy models only run in explicit PS7/model mode.'}
    $base=@{windows=$true;edition='Desktop';major=5;minor=1;pointer_bytes=8;os_arch='ARM64'
        process_arch='ARM64';image_machine=0xaa64;elevated=$false}
    Assert-FixtureRuntime $base 'native-windows-ps51' 'ARM64'
    $checks=1
    foreach($pair in @(@('windows',$false),@('edition','Core'),@('major',7),@('minor',0),
        @('pointer_bytes',4),@('os_arch','x64'),@('process_arch','x64'),@('image_machine',0x8664),
        @('elevated',$true),@('elevated',$null),@('windows','true'),@('major','5'))){
        $bad=@{};foreach($key in $base.Keys){$bad[$key]=$base[$key]};$bad[$pair[0]]=$pair[1]
        $rejected=$false
        try{Assert-FixtureRuntime $bad 'native-windows-ps51' 'ARM64'}catch{$rejected=$true}
        if(-not $rejected){throw 'Policy model admitted invalid runtime.'};$checks++
    }
    $x64=@{};foreach($key in $base.Keys){$x64[$key]=$base[$key]}
    $x64.os_arch='x64';$x64.process_arch='x64';$x64.image_machine=0x8664
    Assert-FixtureRuntime $x64 'native-windows-ps51' 'x64';$checks++
    foreach($selection in @(@('native-windows-ps51',''),@('unknown','ARM64'),@('ps7-model','ARM64'))){
        $rejected=$false
        try{Assert-FixtureRuntime $base $selection[0] $selection[1]}catch{$rejected=$true}
        if(-not $rejected){throw 'Policy model admitted invalid explicit selection.'};$checks++
    }
    [ordered]@{checks=$checks;kind='PURE policy models, not native runtime evidence'
        native_windows_executed=$false;native_ps51_executed=$false}|ConvertTo-Json -Compress
    return
}
if($Boundary -ceq 'native-windows-ps51'){
    if(-not $runtime.windows -or $runtime.edition -cne 'Desktop' -or $runtime.major -ne 5 -or
        $runtime.minor -ne 1 -or $runtime.pointer_bytes -ne 8){throw 'Native Windows Desktop5.1/64-bit required before native probes.'}
    $runtime.os_arch=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    $runtime.process_arch=[Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    $runtime.os_arch=$runtime.os_arch.Replace('X64','x64')
    $runtime.process_arch=$runtime.process_arch.Replace('X64','x64')
    $runtime.image_machine=InterpreterMachine $runtime.image
    $runtime.elevated=[Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
Assert-FixtureRuntime $runtime $Boundary $Architecture
$runtime.image_sha256=(Get-FileHash -LiteralPath $runtime.image -Algorithm SHA256).Hash.ToLowerInvariant()
$Fixture=[IO.Path]::GetFullPath($Fixture);$Output=[IO.Path]::GetFullPath($Output)
$run=[IO.Path]::GetDirectoryName($Fixture)
if([IO.Path]::GetFileName($Fixture) -cne 'input' -or [IO.Path]::GetDirectoryName($Output) -cne $run -or
    [IO.Path]::GetFileName($Output) -cne $Case -or $run -imatch '(^|[\\/])Packages([\\/]|$)'){
    throw 'Only exact owned synthetic input/case siblings outside package state.'
}
if(Test-Path -LiteralPath $Output){throw 'Fresh fixture output required.'}
if((Get-FileHash -LiteralPath $FrozenOperator -Algorithm SHA256).Hash.ToLowerInvariant() -cne
    '3dc3e83da6f8f36d7d70cf2c7105fffe9cb1ed48591607f7d64b3581fe3e8822'){throw 'Frozen operator source pin changed.'}
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($FrozenOperator,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Frozen source parse failure.'}
$names=@('Item','File','Same','RecordError','CloseRead','OpenShared','Metadata','OpenEvidence',
    'PublishEvidence','Save','Baseline','Staging','BytesDigest','NoControlRead','ContentRole','ReadBytes','Digest','CopyRecord')
$imports=[Collections.Generic.List[object]]::new()
foreach($name in $names){
    $found=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst]},$true)|
        Where-Object {$_.Name -ceq $name})
    if($found.Count -ne 1){throw "Exact generic function missing/ambiguous: $name"}
    $definition=$found[0].Extent.Text
    $hash=[Security.Cryptography.SHA256]::Create()
    try{$pin=[BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($definition))).Replace('-','').ToLowerInvariant()}
    finally{$hash.Dispose()}
    $imports.Add([ordered]@{name=$name;source_sha256=$pin;start_line=$found[0].Extent.StartLineNumber
        end_line=$found[0].Extent.EndLineNumber;alias=$(if($name -ceq 'ReadBytes'){'ProductionReadBytes'}else{$name})})
    if($name -ceq 'ReadBytes'){$definition=$definition.Replace('function ReadBytes(', 'function ProductionReadBytes(')}
    . ([scriptblock]::Create($definition))
}
. (Join-Path $PSScriptRoot '../tools/JournalCapture.ps1')
$readEntries=[Collections.Generic.List[string]]::new()
function ReadBytes([string]$Path,[long]$Maximum=256MB) {
    $readEntries.Add($Path)
    if($Case -ceq 'copy-fault' -and [IO.Path]::GetFileName($Path) -ceq '1.json'){throw 'Injected guarded read failure.'}
    ProductionReadBytes $Path $Maximum
}
$null=Item $Fixture;$null=Item $run
$null=New-Item -ItemType Directory -Path $Output
$evidence=$Output;$parent=$Fixture;$utf8=[Text.UTF8Encoding]::new($false,$true)
$control=@{errors=[Collections.Generic.List[object]]::new();first=$null;phase='fixture';slot=0}
$capturePolicy=[pscustomobject]@{root=(Join-Path $Fixture 'NO-RETAINED-ROLE')}
$leasePolicy=[pscustomobject]@{root=(Join-Path $Fixture 'NO-LEASE-ROLE')}
$archivePolicy=[pscustomobject]@{root_name='NO-ARCHIVE';relative='source.oxps'}
$inputPath=Join-Path $run 'fixture-input.json'
$null=File $inputPath 1MB
if($InputSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $InputSha256){
    throw 'Exact bounded fixture control input required.'
}
# The pinned control input is not a journal content read or a strict JSON decoder.
$context=ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($inputPath))
if($context.schema -cne '4d-pdf-printer.collector-test-input' -or $context.version -ne 1 -or
    $context.fixture -cne $Fixture -or $context.boundary -cne $Boundary){throw 'Synthetic fixture input binding.'}
Save ([ordered]@{runtime=$runtime;boundary=$Boundary;input_sha256=$InputSha256;functions=$imports.ToArray()
    metadata=$(if($Boundary -ceq 'ps7-model'){'MODELED Windows metadata'}else{'observed owned Windows files'})
    read_entries_mean='ReadBytes entry attempts, NOT OS handle opens'
    producer_identity_grace_history='SYNTHETIC; no real job/CompleteJob/PSA/Host'} ) 'fixture-provenance.json'
if($Boundary -ceq 'ps7-model'){
    $v=$context.model_inventory
} else {
    $root=Join-Path $Fixture 'DesktopPdf';$rows=[Collections.Generic.List[object]]::new()
    $directories=[Collections.Generic.List[string]]::new()
    function WalkFixture([string]$Directory,[int]$Depth) {
        if($Depth -gt 6){throw 'Fixture directory depth bound.'}
        $null=Item $Directory
        foreach($item in @(Get-ChildItem -LiteralPath $Directory -Force -ErrorAction Stop)){
            if($rows.Count -ge 128){throw 'Fixture item bound.'}
            $m=Metadata $item.FullName
            $relative=$item.FullName.Substring($root.Length+1)
            $rows.Add([ordered]@{path=$relative;directory=$m.directory;bytes=$m.bytes
                attributes=$m.attributes;write_ticks=$m.write_ticks})
            if($m.directory){$directories.Add($relative);WalkFixture $item.FullName ($Depth+1)}
        }
    }
    WalkFixture $root 0
    $byPath=@{};foreach($row in $rows){$byPath[$row.path]=$row}
    $ordered=[string[]]@($byPath.Keys);[Array]::Sort($ordered,[StringComparer]::Ordinal)
    $terminal="DesktopPdfJournal-$($context.activation).json"
    $v=[pscustomobject]@{root=$root;cache=$Fixture;rows=@(foreach($name in $ordered){$byPath[$name]})
        metadata_errors=@();enumerated_directories=$directories.ToArray();transactions=@()
        terminal_siblings=@([ordered]@{name=$terminal;metadata=(Metadata (Join-Path $Fixture $terminal))})}
}
if($v.root -cne (Join-Path $Fixture 'DesktopPdf') -or $v.cache -cne $Fixture){throw 'Owned raw roots required.'}
Same @($context.expected_paths|Sort-Object) @($v.rows.path|Sort-Object) 'Synthetic tree set differs.'
Save $v 'input-raw-inventory.json'
$trial=@{journal_attempted=$false;grace_complete=$true;final_inventory=$v
    baseline=[pscustomobject]@{cache=[pscustomobject]@{tree=@([pscustomobject]@{path='old';directory=$true})}}}
$activations=@('e'*32)
$prefix='ActivationDiagnostics\'+$context.activation+'\'
if($Case -ceq 'gap'){$v.rows=@($v.rows|Where-Object {$_.path -cne ($prefix+'1.json')})}
if($Case -ceq 'duplicate'){$v.rows+=,@($v.rows|Where-Object {$_.path -ceq ($prefix+'0.json')})[0]}
if($Case -ceq 'reparse'){@($v.rows|Where-Object {$_.path -ceq ('ActivationDiagnostics\'+$context.activation)})[0].attributes=1040}
if($Case -ceq 'staging'){
    $v.rows+=,[pscustomobject]@{path=($prefix+'pending-never-open');directory=$false;attributes=32;bytes=1}
}
Save $v 'final-raw-inventory.json'
$failure=$null
try{CaptureJournal}catch{$failure=$_}
Save ([ordered]@{case=$Case;error=$(if($null -eq $failure){$null}else{$failure.ToString()})
    error_detail=$(if($null -eq $failure){$null}else{$failure.Exception.ToString()})
    error_stack=$(if($null -eq $failure){$null}else{$failure.ScriptStackTrace})
    read_entries=[object[]]$readEntries.ToArray();runtime=$runtime;boundary=$Boundary
    raw_before_journal_content=$true
    injection=$(if($Case -ceq 'positive'){'none'}else{'synthetic case fault;NOT physical reparse/share-denial'})
    input_metadata_observed_native=($Boundary -ceq 'native-windows-ps51')
    derived_metadata=$(if($Case -ceq 'positive'){'unchanged input'}else{'injected case input;not an observed physical fault'})
    producer_grace_history='synthetic'
    physical_handle_open_count=$null;physical_stop_proved=$false}) 'fixture-result.json'
if($Case -ceq 'positive' -and $null -ne $failure){throw $failure}
if($Case -cne 'positive' -and $null -eq $failure){throw 'Expected capture rejection.'}
if($Case -ceq 'reparse' -and $readEntries.Count -ne 0){throw 'Reparse case invoked guarded content reader.'}
if(@($readEntries|Where-Object {$_ -like '*pending-never-open*'}).Count){throw 'Staging case invoked guarded content reader.'}
