Set-StrictMode -Version Latest

function Write-SmokeNewFile([string]$Path, [byte[]]$Bytes) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.Write($Bytes, 0, $Bytes.Length) } finally { $stream.Dispose() }
}

function Invoke-SmokeBody {
    param([string]$Executable, [string[]]$Arguments, [string]$Evidence,
          [int]$DeadlineMs = 55000, [int]$Limit = 65536)
    $stage = 'validate paths'
    $primary = $null
    $process = [Diagnostics.Process]::new()
    $buffers = @([IO.MemoryStream]::new(), [IO.MemoryStream]::new())
    $exitCode = $null
    $started = $false
    try {
        if (-not [IO.Path]::IsPathFullyQualified($Executable) -or
            -not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Expected an existing absolute executable path.' }
        if (-not [IO.Path]::IsPathFullyQualified($Evidence)) { throw 'Expected an absolute new evidence directory.' }
        if (Test-Path -LiteralPath $Evidence) { throw 'Evidence directory must not already exist.' }
        [IO.Directory]::CreateDirectory($Evidence) | Out-Null
        $process.StartInfo.FileName = $Executable
        foreach ($argument in $Arguments) { $process.StartInfo.ArgumentList.Add($argument) }
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        $process.StartInfo.CreateNoWindow = $true
        $stage = 'Process.Start fixed observer'
        if (-not $process.Start()) { throw 'Process.Start returned false.' }
        $started = $true
        $streams = @($process.StandardOutput.BaseStream, $process.StandardError.BaseStream)
        $chunks = @([byte[]]::new(4096), [byte[]]::new(4096))
        $reads = @($streams[0].ReadAsync($chunks[0], 0, 4096), $streams[1].ReadAsync($chunks[1], 0, 4096))
        $done = @($false, $false)
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $stage = 'bounded raw stdout/stderr/process deadline'
        while (-not ($process.HasExited -and $done[0] -and $done[1])) {
            if ($watch.ElapsedMilliseconds -gt $DeadlineMs) { throw 'Observer overall deadline exceeded; late host launch is not excluded.' }
            for ($i = 0; $i -lt 2; $i++) {
                if (-not $done[$i] -and $reads[$i].IsCompleted) {
                    $count = $reads[$i].GetAwaiter().GetResult()
                    if ($count -eq 0) { $done[$i] = $true; continue }
                    if ($buffers[$i].Length + $count -gt $Limit) { throw "Raw stream $i exceeded $Limit bytes." }
                    $buffers[$i].Write($chunks[$i], 0, $count)
                    $reads[$i] = $streams[$i].ReadAsync($chunks[$i], 0, 4096)
                }
            }
            Start-Sleep -Milliseconds 10
        }
        $process.WaitForExit()
        $exitCode = $process.ExitCode
        if ($exitCode -ne 0) { throw "Observer exit code $exitCode; inspect raw stderr and per-run LocalCache records." }
    } catch {
        $primary = "API/stage=$stage; $($_.Exception.Message)"
        if ($started -and -not $process.HasExited) {
            try { $process.Kill(); $null = $process.WaitForExit(2000) }
            catch { [Console]::Error.WriteLine("Secondary exact observer termination failure: $_") }
        }
    }
    try {
        Write-SmokeNewFile (Join-Path $Evidence 'stdout.txt') $buffers[0].ToArray()
        Write-SmokeNewFile (Join-Path $Evidence 'stderr.txt') $buffers[1].ToArray()
        $report = [ordered]@{ exitCode = $exitCode; primaryError = $primary; pdfProduced = $false
            note = 'Wrapper transport only; activation pass is in native observation.json, not this report.' }
        Write-SmokeNewFile (Join-Path $Evidence 'wrapper.json') `
            ([Text.UTF8Encoding]::new($false).GetBytes(($report | ConvertTo-Json -Depth 4)))
    } catch {
        $secondary = "API/stage=write create-new evidence/report; $($_.Exception.Message)"
        if ($primary) { [Console]::Error.WriteLine($secondary) } else { $primary = $secondary }
    } finally {
        foreach ($buffer in $buffers) { $buffer.Dispose() }
        $process.Dispose()
    }
    if ($primary) { throw $primary }
}
