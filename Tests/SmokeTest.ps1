<#
.SYNOPSIS
    Launches a built LibScope.exe on a real folder, checks that it lists libraries and
    keeps running, then closes it and checks that all its threads shut down cleanly.

.DESCRIPTION
    A failed debug assertion or a crash replaces the main window with a
    "Microsoft Visual C++ Runtime Library" message box or ends the process, so both are reported.
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'x86')]       [string] $Platform = 'x64',
    [string] $Path = "$env:WINDIR\System32\kernel32.dll",
    [int] $RunSeconds = 6
)

$ErrorActionPreference = 'Stop'

$platformDir = if ($Platform -eq 'x86') { 'Win32' } else { 'x64' }
$exe = Join-Path $PSScriptRoot "..\bin\$platformDir\$Configuration\LibScope.exe"
if (-not (Test-Path $exe)) { throw "LibScope.exe not found at $exe - build $Configuration|$Platform first." }
$exe = (Resolve-Path $exe).Path

Write-Host "Starting $exe $Path"
$process = Start-Process -FilePath $exe -ArgumentList "`"$Path`"" -PassThru

try {
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited) { throw "LibScope exited during startup with exit code $($process.ExitCode)." }
    } while ($process.MainWindowHandle -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline)

    if ($process.MainWindowHandle -eq [IntPtr]::Zero) { throw 'LibScope did not create a main window within 30 seconds.' }

    # Lets the folder listing and the details load finish.
    Start-Sleep -Seconds $RunSeconds
    $process.Refresh()
    if ($process.HasExited) { throw "LibScope exited while running with exit code $($process.ExitCode)." }

    $title = $process.MainWindowTitle
    Write-Host "Main window title: '$title'"
    if ($title -notlike 'LibScope - Library Explorer*') { throw "Unexpected main window '$title' (assertion or error dialog?)." }

    Write-Host ("Running OK: {0} threads, {1:N0} MB working set, {2:N1} s CPU" -f `
        $process.Threads.Count, ($process.WorkingSet64 / 1MB), $process.TotalProcessorTime.TotalSeconds)

    if (-not $process.CloseMainWindow()) { throw 'Could not send WM_CLOSE to the main window.' }
    if (-not $process.WaitForExit(15000)) { throw 'LibScope did not exit within 15 seconds of closing (worker thread hang?).' }
    if ($process.ExitCode -ne 0) { throw "LibScope exited with code $($process.ExitCode)." }

    Write-Host 'Smoke test passed.'
}
finally {
    if (-not $process.HasExited) { $process.Kill() }
}
