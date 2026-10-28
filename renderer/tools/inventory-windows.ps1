param(
    [Parameter(Mandatory = $true)][string]$InstalledRoot,
    [Parameter(Mandatory = $true)][string]$RuntimeRoot,
    [Parameter(Mandatory = $true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
$InstalledRoot = (Resolve-Path $InstalledRoot).Path
$RuntimeRoot = (Resolve-Path $RuntimeRoot).Path
New-Item -ItemType Directory -Force $OutputRoot | Out-Null
$OutputRoot = (Resolve-Path $OutputRoot).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Cannot locate Visual Studio C++ tools for PE inventory' }
$dumpbin = Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (!$dumpbin) { throw 'dumpbin.exe is required' }

$target = Join-Path $InstalledRoot 'x64-windows'
$queue = [System.Collections.Generic.Queue[string]]::new()
$queue.Enqueue((Join-Path $RuntimeRoot 'pdf-render-worker.exe'))
$seen = @{}
$records = @()
while ($queue.Count) {
    $file = $queue.Dequeue()
    $name = [IO.Path]::GetFileName($file).ToLowerInvariant()
    if ($seen.ContainsKey($name)) { continue }
    $seen[$name] = $true
    $imports = & $dumpbin /nologo /dependents $file
    if ($LASTEXITCODE) { throw "PE import inspection failed: $name" }
    $imports | Set-Content -Encoding utf8 (Join-Path $OutputRoot "$name.imports.txt")
    $dependencies = @($imports | ForEach-Object {
        if ($_ -match '^\s+([a-zA-Z0-9_.-]+\.dll)\s*$') { $Matches[1] }
    } | Sort-Object -Unique)
    foreach ($dll in $dependencies) {
        $local = Join-Path $RuntimeRoot $dll
        $installed = Join-Path "$target\bin" $dll
        $system = Join-Path "$env:SystemRoot\System32" $dll
        if (Test-Path $local) {
            $queue.Enqueue($local)
        } elseif (Test-Path $installed) {
            throw "Required installed DLL missing from app-local runtime: $dll"
        } elseif (!(Test-Path $system) -and $dll -notmatch '^(api|ext)-ms-') {
            throw "Unresolved PE dependency: $dll"
        }
    }
    $owners = @(Get-ChildItem "$InstalledRoot\vcpkg\info\*.list" | Where-Object {
        Select-String -Quiet -SimpleMatch "/bin/$([IO.Path]::GetFileName($file))" $_.FullName
    } | Select-Object -ExpandProperty BaseName)
    $records += [ordered]@{
        file = [IO.Path]::GetFileName($file)
        sha256 = (Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant()
        bytes = (Get-Item $file).Length
        package_file_lists = $owners
        imports = $dependencies
    }
}
[ordered]@{
    schema = '4d-pdf-printer.pe-inventory'
    version = 1
    scope = 'Recursive PE imports only; dynamic loads, fonts/config and redistribution clearance require review'
    files = $records
} | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 "$OutputRoot\runtime-imports.json"

Copy-Item "$InstalledRoot\vcpkg\status" "$OutputRoot\vcpkg-status.txt"
Copy-Item "$InstalledRoot\vcpkg\info" "$OutputRoot\package-file-lists" -Recurse
New-Item -ItemType Directory -Force "$OutputRoot\notices" | Out-Null
foreach ($port in Get-ChildItem "$target\share" -Directory) {
    $evidence = @('copyright', 'vcpkg.spdx.json') | ForEach-Object { Join-Path $port.FullName $_ } |
        Where-Object { Test-Path $_ }
    if ($evidence) {
        $destination = "$OutputRoot\notices\$($port.Name)"
        New-Item -ItemType Directory -Force $destination | Out-Null
        $evidence | Copy-Item -Destination $destination
    }
}
