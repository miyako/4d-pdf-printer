param(
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Architecture,
    [Parameter(Mandatory = $true)][string]$InstalledRoot,
    [Parameter(Mandatory = $true)][string]$WorkerPath,
    [Parameter(Mandatory = $true)][string]$RuntimeRoot,
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [Parameter(Mandatory = $true)][string]$DumpbinPath,
    [Parameter(Mandatory = $true)][string]$BuildProvenance
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Architecture = $Architecture.ToLowerInvariant()
$InstalledRoot = (Resolve-Path $InstalledRoot).Path
$WorkerPath = (Resolve-Path $WorkerPath).Path
$DumpbinPath = (Resolve-Path $DumpbinPath).Path
$triplet = "$Architecture-windows"
$target = Join-Path $InstalledRoot $triplet
$expectedMachine = if ($Architecture -eq 'arm64') { 0xaa64 } else { 0x8664 }

function Assert-Machine([string]$File) {
    $stream = [IO.File]::OpenRead($File)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE file: $File" }
        $stream.Position = 0x3c
        $offset = $reader.ReadUInt32()
        if ($offset -lt 0x40 -or $offset -gt $stream.Length - 24) { throw "Invalid PE header: $File" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $File" }
        $machine = $reader.ReadUInt16()
        if ($machine -ne $expectedMachine) {
            throw ('Wrong PE machine for {0}: 0x{1:x4}, expected 0x{2:x4}' -f $File, $machine, $expectedMachine)
        }
        return ('0x{0:x4}' -f $machine)
    } finally { $reader.Dispose() }
}

if (Test-Path $RuntimeRoot) {
    if (@(Get-ChildItem -LiteralPath $RuntimeRoot -Force).Count) {
        throw "Runtime destination must be empty (never merge an old package): $RuntimeRoot"
    }
}
Assert-Machine $WorkerPath | Out-Null
New-Item -ItemType Directory -Force $RuntimeRoot, $OutputRoot | Out-Null
$RuntimeRoot = (Resolve-Path $RuntimeRoot).Path
$OutputRoot = (Resolve-Path $OutputRoot).Path
Copy-Item -LiteralPath $WorkerPath -Destination "$RuntimeRoot\pdf-render-worker.exe"

# Index exact release target-bin paths, never host, debug, or tools directories.
$owners = @{}
$packageLists = @{}
foreach ($list in Get-ChildItem "$InstalledRoot\vcpkg\info\*.list") {
    foreach ($line in Get-Content -LiteralPath $list.FullName) {
        if ($line -match "^$([regex]::Escape($triplet))/bin/([^/]+\.dll)$") {
            $dll = $Matches[1].ToLowerInvariant()
            if ($owners.ContainsKey($dll)) { throw "Ambiguous target-bin ownership: $dll" }
            $owners[$dll] = $list.BaseName
            $packageLists[$list.BaseName] = $list.FullName
        }
    }
}
if (!$owners.Count) { throw "No release target-bin package ownership found for $triplet" }

$queue = [Collections.Generic.Queue[string]]::new()
$queue.Enqueue('pdf-render-worker.exe')
$seen = @{}
$usedPackages = @{}
$records = @()
$external = @{}
$windowsDllNames = @(
    'advapi32.dll', 'bcrypt.dll', 'bcryptprimitives.dll', 'cfgmgr32.dll', 'comctl32.dll',
    'comdlg32.dll', 'crypt32.dll', 'd2d1.dll', 'd3d11.dll', 'dwrite.dll', 'dxgi.dll',
    'gdi32.dll', 'imm32.dll', 'iphlpapi.dll', 'kernel32.dll', 'kernelbase.dll',
    'mpr.dll', 'msimg32.dll', 'msvcrt.dll', 'ncrypt.dll', 'netapi32.dll', 'normaliz.dll',
    'ntdll.dll', 'ole32.dll', 'oleaut32.dll', 'powrprof.dll', 'propsys.dll', 'psapi.dll',
    'rpcrt4.dll', 'secur32.dll', 'setupapi.dll', 'shell32.dll', 'shlwapi.dll',
    'user32.dll', 'userenv.dll', 'usp10.dll', 'uxtheme.dll', 'version.dll',
    'windowscodecs.dll', 'winhttp.dll', 'wininet.dll', 'winmm.dll', 'wintrust.dll',
    'ws2_32.dll', 'wtsapi32.dll'
)
while ($queue.Count) {
    $name = $queue.Dequeue().ToLowerInvariant()
    if ($seen.ContainsKey($name)) { continue }
    $seen[$name] = $true
    $file = Join-Path $RuntimeRoot $name
    $machine = Assert-Machine $file
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant()
    $owner = $null
    if ($name -ne 'pdf-render-worker.exe') {
        if (!$owners.ContainsKey($name)) { throw "No exact target-bin package owner: $name" }
        $owner = $owners[$name]
        $usedPackages[$owner] = $true
        $source = Join-Path "$target\bin" $name
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant() -ne $hash) {
            throw "Runtime DLL differs from its owned target-bin file: $name"
        }
    }
    $imports = & $DumpbinPath /nologo /dependents $file
    if ($LASTEXITCODE) { throw "PE import inspection failed: $name" }
    $imports | Set-Content -Encoding utf8 "$OutputRoot\$name.imports.txt"
    # dumpbin /dependents lists both normal and delay-load dependencies.
    $dependencies = @($imports | ForEach-Object {
        if ($_ -match '^\s+([a-zA-Z0-9_.+-]+\.dll)\s*$') { $Matches[1].ToLowerInvariant() }
    } | Sort-Object -Unique)
    if (!$dependencies.Count) { throw "No PE dependencies parsed for $name; refusing an incomplete inventory" }
    $resolutions = @()
    foreach ($dll in $dependencies) {
        $installed = Join-Path "$target\bin" $dll
        $system = Join-Path "$env:SystemRoot\System32" $dll
        $kind = $null
        $classification = $null
        $observedPath = $null
        $basis = $null
        if ($dll -match '^(api|ext)-ms-win-') {
            $kind = 'windows-api-set'
            $classification = 'windows-api-set-contract'
            $basis = 'API-set contract name; supplied by the Windows loader, not an app-local file'
        } elseif ($dll -match '^(msvcp[0-9]+(?:_[a-z0-9_]+)?|vcruntime[0-9]+(?:_[0-9]+)?|concrt[0-9]+|vccorlib[0-9]+|ucrtbase)\.dll$') {
            if (!(Test-Path -LiteralPath $system -PathType Leaf)) { throw "Required external MSVC/UCRT runtime missing: $dll" }
            $kind = 'external-msvc-ucrt'
            $classification = if ($dll -eq 'ucrtbase.dll') { 'windows-ucrt-prerequisite' } else { 'msvc-runtime-prerequisite' }
            $observedPath = $system
            $basis = 'Runtime DLL name and observed System32 file; separate prerequisite, not redistribution clearance'
        } elseif (Test-Path -LiteralPath $installed -PathType Leaf) {
            if (!$owners.ContainsKey($dll)) { throw "Unowned target-bin dependency: $dll" }
            $local = Join-Path $RuntimeRoot $dll
            if (!(Test-Path -LiteralPath $local)) {
                Assert-Machine $installed | Out-Null
                Copy-Item -LiteralPath $installed -Destination $local
            }
            $queue.Enqueue($dll)
            $kind = 'app-local-target-bin'
            $classification = 'third-party-target-bin'
            $observedPath = "$triplet/bin/$dll"
            $basis = 'Exact vcpkg target-bin package ownership, SHA256 and PE machine verified'
        } elseif (Test-Path -LiteralPath $system -PathType Leaf) {
            $kind = 'external-system32'
            $classification = if ($dll -in $windowsDllNames) { 'windows-os-dll-prerequisite' } else { 'unclassified-system32-prerequisite' }
            $observedPath = $system
            $basis = if ($dll -in $windowsDllNames) {
                'Known Windows DLL name and observed System32 file; OS ownership/signature not attested'
            } else {
                'Observed System32 file only; NOT established to be a Windows OS component'
            }
        } else {
            throw "Unresolved PE dependency of ${name}: $dll"
        }
        $resolutions += [ordered]@{
            file = $dll; resolution = $kind; classification = $classification
            observed_path = $observedPath; classification_basis = $basis
        }
        if ($kind -ne 'app-local-target-bin') {
            $external[$dll] = [ordered]@{
                file = $dll; requirement = $kind; classification = $classification
                observed_path = $observedPath; classification_basis = $basis
                os_ownership_attested = $false
            }
        }
    }
    $records += [ordered]@{
        file = $name; machine = $machine; sha256 = $hash; bytes = (Get-Item -LiteralPath $file).Length
        package_file_list = $owner; installed_path = if ($owner) { "$triplet/bin/$name" } else { $null }
        imports = $resolutions
    }
}
foreach ($file in Get-ChildItem -LiteralPath $RuntimeRoot -File) {
    if (!$seen.ContainsKey($file.Name.ToLowerInvariant())) { throw "Uninventoried runtime file: $($file.Name)" }
}

Copy-Item "$InstalledRoot\vcpkg\status" "$OutputRoot\vcpkg-status.txt"
New-Item -ItemType Directory -Force "$OutputRoot\package-file-lists", "$RuntimeRoot\notices" | Out-Null
$licenses = @()
foreach ($port in Get-ChildItem "$target\share" -Directory | Sort-Object Name) {
    $spdxPath = Join-Path $port.FullName 'vcpkg.spdx.json'
    if (Test-Path -LiteralPath $spdxPath) {
        $spdx = Get-Content -Raw -LiteralPath $spdxPath | ConvertFrom-Json
        foreach ($package in $spdx.packages) {
            foreach ($field in @('licenseDeclared', 'licenseConcluded')) {
                $license = $package.PSObject.Properties[$field]
                if ($null -ne $license) {
                    if ($license.Value -match 'AGPL|Affero') { throw "AGPL license in selected target graph: $($port.Name)" }
                    $licenses += [ordered]@{ port = $port.Name; package = $package.name; field = $field; license = $license.Value }
                }
            }
        }
    }
    # Keep notices for the entire target graph, including statically incorporated dependencies.
    foreach ($notice in @('copyright', 'vcpkg.spdx.json')) {
        $source = Join-Path $port.FullName $notice
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $destination = "$RuntimeRoot\notices\$($port.Name)"
            New-Item -ItemType Directory -Force $destination | Out-Null
            Copy-Item -LiteralPath $source -Destination $destination
        }
    }
}
foreach ($owner in $usedPackages.Keys | Sort-Object) {
    if ($owner -notmatch "^([^_]+)_.+_$([regex]::Escape($triplet))$") { throw "Invalid package owner: $owner" }
    $port = $Matches[1]
    $destination = "$RuntimeRoot\notices\$port"
    New-Item -ItemType Directory -Force $destination | Out-Null
    foreach ($notice in @('copyright', 'vcpkg.spdx.json')) {
        $source = "$target\share\$port\$notice"
        if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing $notice for shipped package $owner" }
        Copy-Item -LiteralPath $source -Destination $destination
    }
    Copy-Item -LiteralPath $packageLists[$owner] -Destination "$OutputRoot\package-file-lists"
}
$noticeRecords = @(Get-ChildItem "$RuntimeRoot\notices" -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        file = [IO.Path]::GetRelativePath($RuntimeRoot, $_.FullName).Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$provenance = Get-Content -Raw -LiteralPath $BuildProvenance | ConvertFrom-Json
Copy-Item -LiteralPath $BuildProvenance -Destination "$RuntimeRoot\build-provenance.json"
[ordered]@{
    schema = '4d-pdf-printer.pe-inventory'; version = 2
    delivery = 'INTERNAL TEST DELIVERY ONLY - NOT RELEASE-CLEARED OR APPROVED FOR REDISTRIBUTION'
    architecture = $Architecture; triplet = $triplet; expected_machine = ('0x{0:x4}' -f $expectedMachine)
    scope = 'Recursive normal/delay PE imports and exact target-bin ownership; not an exhaustive dynamic-load/config/font audit'
    artifact_paths = [ordered]@{
        runtime_imports = 'runtime-imports.json'; build_provenance = 'build-provenance.json'
        notices = 'notices/'; native_test_receipt = 'native-test-receipt.json'
    }
    build = $provenance
    external_requirements = @($external.Keys | Sort-Object | ForEach-Object { $external[$_] })
    selected_target_licenses = $licenses
    notices = $noticeRecords
    files = $records
} | ConvertTo-Json -Depth 20 | Set-Content -Encoding utf8 "$RuntimeRoot\runtime-imports.json"
@'
INTERNAL TEST DELIVERY ONLY. NOT RELEASE-CLEARED OR APPROVED FOR REDISTRIBUTION.
This directory contains the worker and its recursive release target-bin DLL
imports, with package notices and provenance. No host tools, test executables,
debug binaries, Windows system DLLs, or MSVC runtime DLLs are included.
runtime-imports.json records imports, classifications, ownership and notice hashes.
build-provenance.json identifies the source, recipes and build toolchain.
System32 presence is not OS ownership/signature attestation; unclassified
System32 dependencies are reported separately for coordinator review.
The matching Windows OS and Microsoft Visual C++ v14 runtime/UCRT are separate
prerequisites. Runner-installed runtimes are not a redistribution grant.
Dynamic loads, installed share/config data and font discovery are NOT exhaustively
covered. Synthetic CI success is not a general packaging or licensing clearance.
Complete LGPL/source/relinking and all other license obligations before any release.
'@ | Set-Content -Encoding utf8 "$RuntimeRoot\INTERNAL-TEST-DELIVERY.txt"
Copy-Item "$RuntimeRoot\runtime-imports.json" "$OutputRoot\runtime-imports.json"
Copy-Item "$RuntimeRoot\notices" "$OutputRoot\notices" -Recurse
