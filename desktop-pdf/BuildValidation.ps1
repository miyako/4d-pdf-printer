# Function-only receipt checks. Importing this file never builds or runs a test.
function Get-ScopedUtcTicks($Time) {
    if ($Time -isnot [string] -or
        $Time -cnotmatch '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]{1,7})?Z$') {
        throw 'Scoped timestamps must be literal UTC strings with zero to seven fractional digits.'
    }
    [string[]]$formats=@("yyyy-MM-dd'T'HH:mm:ss'Z'", "yyyy-MM-dd'T'HH:mm:ss.FFFFFFF'Z'")
    $parsed=[DateTimeOffset]::MinValue
    if (-not [DateTimeOffset]::TryParseExact($Time,$formats,[Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::AssumeUniversal,[ref]$parsed)) {
        throw 'Invalid scoped UTC calendar/time.'
    }
    return $parsed.UtcDateTime.Ticks
}
function ConvertFrom-ScopedBuildReceipt([string]$Json) {
    $document=[System.Text.Json.JsonDocument]::Parse($Json,[System.Text.Json.JsonDocumentOptions]::new())
    try {
        if ($document.RootElement.ValueKind -ne [System.Text.Json.JsonValueKind]::Object) { throw 'Scoped receipt object required.' }
        $checks=$null
        foreach ($property in $document.RootElement.EnumerateObject()) {
            if ($property.Name -ceq 'checks') {
                if ($null -ne $checks) { throw 'Duplicate scoped checks property.' }
                $checks=$property.Value
            }
        }
        if ($null -eq $checks -or $checks.ValueKind -ne [System.Text.Json.JsonValueKind]::Array) { throw 'Scoped check array required.' }
        $times=[Collections.Generic.List[object]]::new()
        foreach ($check in $checks.EnumerateArray()) {
            if ($check.ValueKind -ne [System.Text.Json.JsonValueKind]::Object) { throw 'Scoped check object required.' }
            $start=$null; $end=$null
            foreach ($property in $check.EnumerateObject()) {
                if ($property.Name -cin @('started_utc','ended_utc')) {
                    if ($property.Value.ValueKind -ne [System.Text.Json.JsonValueKind]::String) { throw 'Nonstring scoped timestamp token.' }
                    if ($property.Name -ceq 'started_utc') {
                        if ($null -ne $start) { throw 'Duplicate started_utc token.' }
                        $start=$property.Value.GetString()
                    } else {
                        if ($null -ne $end) { throw 'Duplicate ended_utc token.' }
                        $end=$property.Value.GetString()
                    }
                }
            }
            if ((Get-ScopedUtcTicks $start) -gt (Get-ScopedUtcTicks $end)) { throw 'Reversed scoped check timestamps.' }
            $times.Add([string[]]@($start,$end))
        }
        $value=ConvertFrom-Json -InputObject $Json -ErrorAction Stop
        if ($value.checks -isnot [array] -or $value.checks.Count -ne $times.Count) { throw 'Scoped check transport mismatch.' }
        for ($i=0; $i -lt $times.Count; ++$i) {
            # Only these two tokens bypass ConvertFrom-Json's implicit DateTime conversion.
            $value.checks[$i].started_utc=$times[$i][0]
            $value.checks[$i].ended_utc=$times[$i][1]
        }
        return $value
    } finally { $document.Dispose() }
}
function Assert-BuildKeys($Value, [string[]]$Keys) {
    if ($null -eq $Value) { throw 'Missing build receipt object.' }
    $actual = if ($Value -is [Collections.IDictionary]) { @($Value.Keys) } else { @($Value.PSObject.Properties.Name) }
    if ((($actual | Sort-Object) -join '|') -cne (($Keys | Sort-Object) -join '|')) { throw 'Unexpected build receipt shape.' }
}
function Assert-BuildHash($Value) {
    if ($Value -isnot [string] -or $Value -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid build byte hash.' }
}
function Assert-BuildFileFact($Value) {
    Assert-BuildKeys $Value @('path','bytes','sha256')
    Assert-BuildHash $Value.sha256
    if ($Value.path -isnot [string] -or -not [IO.Path]::IsPathFullyQualified($Value.path) -or
        ($Value.bytes -isnot [int] -and $Value.bytes -isnot [long]) -or $Value.bytes -lt 0) {
        throw 'Invalid build file facts.'
    }
}
function Assert-BuildFileBytes($Value) {
    Assert-BuildFileFact $Value
    $item = Get-Item -LiteralPath $Value.path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $item.Length -ne $Value.bytes -or (Digest $Value.path) -cne $Value.sha256) {
        throw 'Scoped build file changed.'
    }
}
function Get-ScopedBuildTools([string]$Directory, [string]$CMake, [string]$Vs, [string]$Sdk, [string]$Arch) {
    $records = @(Get-ChildItem -LiteralPath (Join-Path $Directory 'CMakeFiles') -Directory |
        ForEach-Object { Join-Path $_.FullName 'CMakeCXXCompiler.cmake' } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($records.Count -ne 1) { throw 'One scoped compiler selection record required.' }
    $text = [IO.File]::ReadAllText($records[0])
    if ($text -cnotmatch 'set\(CMAKE_CXX_COMPILER "([^"]+)"\)') { throw 'Missing scoped compiler.' }
    $compiler = $Matches[1].Replace('/','\')
    $versionFile = Join-Path $Vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
    $version = [IO.File]::ReadAllText($versionFile).Trim()
    $expected = Join-Path $Vs "VC/Tools/MSVC/$version/bin/Host$Arch/$Arch/cl.exe"
    if ($compiler -ine $expected -or $text -notmatch "set\(CMAKE_CXX_COMPILER_ARCHITECTURE_ID $Arch\)") {
        throw 'Unexpected scoped native compiler selection.'
    }
    $paths = @($CMake, $compiler, $records[0], $versionFile,
        (Join-Path $Sdk 'Include/10.0.26100.0/um/Windows.h'),
        (Join-Path $Sdk 'Include/10.0.26100.0/cppwinrt/winrt/Windows.Graphics.Printing.Workflow.h'),
        (Join-Path $Sdk "Lib/10.0.26100.0/um/$Arch/windowsapp.lib"),
        (Join-Path $Sdk "Lib/10.0.26100.0/um/$Arch/OneCore.lib"))
    @($paths | ForEach-Object {
        [ordered]@{ path=$_;bytes=(Get-Item -LiteralPath $_ -ErrorAction Stop).Length;sha256=(Digest $_) }
    })
}
function Assert-BuildValidation($Build, [string]$Profile, [string]$Sha, [string]$Tree,
    [string]$Arch, [string]$Directory, $Tools) {
    if ($Build.source_sha -cne $Sha -or $Build.architecture -cne $Arch) { throw 'Build source/architecture mismatch.' }
    if ($Profile -ceq 'full-suite') {
        if (($Build.PSObject.Properties.Name -contains 'validation_profile') -and $Build.validation_profile -cne 'full-suite') {
            throw 'Named scoped receipt is not a full-suite receipt.'
        }
        if ($Build.ctests -cne 'passed') { throw 'Successful original full-suite receipt required.' }
        return
    }
    if ($Profile -cne 'isolated-journal-v3') { throw 'Unknown validation profile.' }
    Assert-BuildKeys $Build @('source_sha','source_tree','architecture','validation_profile','validation_status','configuration','targets','tools','pe','checks','test_root')
    if ($Build.source_tree -cne $Tree -or $Build.validation_profile -cne $Profile -or
        $Build.validation_status -cne 'passed' -or
        $Build.test_root -cne (Join-Path $Directory 'isolated-journal-v3-files')) {
        throw 'Scoped source/profile/root/status mismatch.'
    }
    if ($Build.configuration -cne 'Release' -or $Build.targets -isnot [array] -or
        ($Build.targets -join '|') -cne 'IntegrationApp|IntegrationTask|IntegrationHost|IntegrationClient|IntegrationContract|IntegrationNative') {
        throw 'Exact scoped target/configuration list required.'
    }
    if ($Build.tools -isnot [array] -or $Build.tools.Count -ne 8 -or
        ($Build.tools | ConvertTo-Json -Depth 6 -Compress) -cne ($Tools | ConvertTo-Json -Depth 6 -Compress)) {
        throw 'Scoped compiler/SDK/tool facts mismatch.'
    }
    foreach ($tool in $Build.tools) { Assert-BuildFileFact $tool }
    $payload = @('IntegrationApp.exe','IntegrationTask.dll','IntegrationHost.exe','IntegrationClient.exe')
    if ($Build.pe -isnot [array] -or $Build.pe.Count -ne 4 -or
        (@($Build.pe | ForEach-Object file | Sort-Object) -join '|') -cne (@($payload | Sort-Object) -join '|')) {
        throw 'Exact four scoped payloads required.'
    }
    foreach ($pe in $Build.pe) {
        Assert-BuildKeys $pe @('file','bytes','sha256')
        Assert-BuildHash $pe.sha256
        if (($pe.bytes -isnot [int] -and $pe.bytes -isnot [long]) -or $pe.bytes -le 0) { throw 'Invalid scoped payload byte count.' }
    }
    if ($Build.checks -isnot [array] -or $Build.checks.Count -ne 2) { throw 'Exact scoped check list required.' }
    for ($i=0; $i -lt 2; ++$i) {
        $check = $Build.checks[$i]
        Assert-BuildKeys $check @('name','program','argv','cwd','status','exit_code','started_utc','ended_utc','stdout','stderr','image','image_after')
        $name = @('contract','isolated-journal-v3')[$i]
        $exe = @('IntegrationContract.exe','IntegrationNative.exe')[$i]
        [string[]]$expectedArgs = @()
        if ($i -eq 1) { $expectedArgs = @('--isolated-journal',$Build.test_root) }
        if ($check.name -cne $name -or $check.program -cne (Join-Path $Directory "Release/$exe") -or
            $check.cwd -cne $Directory -or $check.status -cne 'closed' -or
            ($check.exit_code -isnot [int] -and $check.exit_code -isnot [long]) -or $check.exit_code -ne 0 -or
            $check.argv -isnot [array] -or ($check.argv -join '|') -cne ($expectedArgs -join '|') -or
            $check.argv.Count -ne $expectedArgs.Count) { throw 'Wrong/missing/failed scoped check invocation.' }
        Assert-BuildFileFact $check.image
        Assert-BuildFileFact $check.image_after
        if ($check.image.path -cne $check.program -or
            ($check.image | ConvertTo-Json -Compress) -cne ($check.image_after | ConvertTo-Json -Compress)) {
            throw 'Scoped test image changed.'
        }
        if ((Get-ScopedUtcTicks $check.started_utc) -gt (Get-ScopedUtcTicks $check.ended_utc)) {
            throw 'Reversed scoped check timestamps.'
        }
        foreach ($channel in @('stdout','stderr')) {
            Assert-BuildFileFact $check.$channel
            if ($check.$channel.path -cne (Join-Path $Directory "$name.$channel.bin")) { throw 'Wrong scoped log binding.' }
        }
    }
}
function Invoke-ScopedBuildChecks([string]$Directory) {
    $testRoot = Join-Path $Directory 'isolated-journal-v3-files'
    $record = Join-Path $Directory 'scoped-checks.json'
    if (Test-Path -LiteralPath $testRoot) { throw 'Fresh retained private test root required; never reset/retry.' }
    if (Test-Path -LiteralPath $record) { throw 'Scoped checks already attempted; no rerun.' }
    $checks = [Collections.Generic.List[object]]::new()
    foreach ($name in @('contract','isolated-journal-v3')) {
        $program = Join-Path $Directory ('Release/' + $(if ($name -ceq 'contract') {'IntegrationContract.exe'} else {'IntegrationNative.exe'}))
        [string[]]$arguments = @()
        if ($name -ceq 'isolated-journal-v3') { $arguments = @('--isolated-journal',$testRoot) }
        $stdout=Join-Path $Directory "$name.stdout.bin"; $stderr=Join-Path $Directory "$name.stderr.bin"
        if ((Test-Path -LiteralPath $stdout) -or (Test-Path -LiteralPath $stderr)) { throw 'Scoped log collision; no overwrite/rerun.' }
        $row=[ordered]@{name=$name;program=$program;argv=@($arguments);cwd=$Directory;status='started';exit_code=$null
            started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;stdout=$null;stderr=$null
            image=[ordered]@{path=$program;bytes=(Get-Item -LiteralPath $program).Length;sha256=(Digest $program)}
            image_after=$null}
        $checks.Add($row)
        $checks.ToArray() | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $record -Encoding utf8NoBOM
        Push-Location -LiteralPath $Directory
        try {
            & $program @arguments 1> $stdout 2> $stderr
            $exit=$LASTEXITCODE
        } finally { Pop-Location }
        $row.status='closed'; $row.exit_code=$exit; $row.ended_utc=[DateTime]::UtcNow.ToString('o')
        $row.image_after=[ordered]@{path=$program;bytes=(Get-Item -LiteralPath $program).Length;sha256=(Digest $program)}
        foreach ($pair in @(@('stdout',$stdout),@('stderr',$stderr))) {
            $row[$pair[0]]=[ordered]@{path=$pair[1];bytes=(Get-Item -LiteralPath $pair[1]).Length;sha256=(Digest $pair[1])}
        }
        $checks.ToArray() | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $record -Encoding utf8NoBOM
        if ($exit -ne 0) { throw "Scoped $name failed ($exit); retain root/logs, no repair/rerun/kill/cleanup." }
        if (($row.image | ConvertTo-Json -Compress) -cne ($row.image_after | ConvertTo-Json -Compress)) {
            throw 'Scoped test image changed during execution.'
        }
        if ($name -ceq 'isolated-journal-v3' -and [IO.File]::ReadAllText($stdout).Trim() -cne
            'Isolated native journal/deferral fixtures passed; retained test root; NOT PSA/printing/SystemPolicy proof.') {
            throw 'Scoped native success marker missing.'
        }
    }
    return $checks.ToArray()
}
