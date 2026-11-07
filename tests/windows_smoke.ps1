# Native Win32 smoke: no SendKeys, foreground-focus assumption, or screenshots.
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
# Uses the app's own HWND and synchronous window messages with bounded timeouts.
# Existing saved data is moved aside and restored even on failure. Do not run while
# another Tally Counter instance is open. Intended for the Windows CI runner.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Executable
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class TallySmokeNative {
    public delegate bool EnumWindowProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowProc callback, IntPtr data);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr window, StringBuilder text, int maximum);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string className, string title);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message,
        UIntPtr wParam, IntPtr lParam, uint flags, uint milliseconds, out UIntPtr result);
    public static IntPtr FindProcessWindow(uint process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, data) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner != process) return true;
            var name = new StringBuilder(256);
            GetClassNameW(window, name, name.Capacity);
            if (name.ToString() != "TallyCounterWindow") return true;
            found = window;
            return false;
        }, IntPtr.Zero);
        return found;
    }
    public static void Send(IntPtr window, uint message, ulong wParam, long lParam) {
        UIntPtr result;
        if (SendMessageTimeoutW(window, message, new UIntPtr(wParam), new IntPtr(lParam),
                2 /* SMTO_ABORTIFHUNG */, 3000, out result) == IntPtr.Zero)
            throw new InvalidOperationException("Window message timed out or failed: " + message);
    }
}
'@

$binary = (Resolve-Path -LiteralPath $Executable).Path
if ([TallySmokeNative]::FindWindowW('TallyCounterWindow', $null) -ne [IntPtr]::Zero) {
    throw 'Close the existing Tally Counter instance before running the native smoke test.'
}
$roaming = [Environment]::GetFolderPath([Environment+SpecialFolder]::ApplicationData)
if ([string]::IsNullOrWhiteSpace($roaming)) {
    throw 'The Windows roaming application-data directory is unavailable.'
}
$stateDirectory = Join-Path $roaming 'TallyCounter'
$backup = Join-Path $roaming ('.TallyCounter-smoke-backup-' + [Guid]::NewGuid().ToString('N'))
$hadState = Test-Path -LiteralPath $stateDirectory
$statePrepared = $false
$process = $null
$second = $null
$statePath = Join-Path $stateDirectory 'count.txt'

function Start-Tally {
    $script:process = Start-Process -FilePath $binary -PassThru
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 15) {
        $script:process.Refresh()
        if ($script:process.HasExited) {
            throw "The application exited before creating a native window (code $($script:process.ExitCode))."
        }
        $window = [TallySmokeNative]::FindProcessWindow([uint32]$script:process.Id)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 50
    }
    throw 'The native application window was not created within 15 seconds.'
}

function Assert-Count([int] $Expected) {
    # Messages are synchronous; file writes occur before the handler returns.
    $saved = Get-Content -LiteralPath $statePath -Raw
    if ($saved.Trim() -cne $Expected.ToString([Globalization.CultureInfo]::InvariantCulture)) {
        throw "Expected saved count $Expected; received '$($saved.Trim())'."
    }
}

function Send-Key([IntPtr] $Window, [uint32] $Key) {
    [TallySmokeNative]::Send($Window, 0x0100, $Key, 1)          # WM_KEYDOWN, first press
    [TallySmokeNative]::Send($Window, 0x0101, $Key, 0xC0000001) # WM_KEYUP
}

function Close-Tally([IntPtr] $Window) {
    [TallySmokeNative]::Send($Window, 0x0010, 0, 0) # WM_CLOSE saves state and exits normally.
    if (-not $script:process.WaitForExit(10000)) {
        throw 'The application did not exit after WM_CLOSE.'
    }
    $script:process.Refresh()
    if ($script:process.ExitCode -ne 0) {
        throw "The application returned $($script:process.ExitCode) after WM_CLOSE."
    }
}

try {
    if ($hadState) { Move-Item -LiteralPath $stateDirectory -Destination $backup }
    New-Item -ItemType Directory -Path $stateDirectory | Out-Null
    $statePrepared = $true
    [IO.File]::WriteAllText($statePath, "7`n", [Text.UTF8Encoding]::new($false))
    $window = Start-Tally
    [TallySmokeNative]::Send($window, 0x0100, 0x20, 1)
    Assert-Count 8
    [TallySmokeNative]::Send($window, 0x0100, 0x20, 0x40000001) # Previous-key-state marks repeat.
    [TallySmokeNative]::Send($window, 0x0100, 0x20, 0x40000001)
    Assert-Count 8
    [TallySmokeNative]::Send($window, 0x0101, 0x20, 0xC0000001)
    Send-Key $window 0x20
    Assert-Count 9
    Send-Key $window 0x09 # Tab selects Reset.
    Send-Key $window 0x20
    Assert-Count 10
    Send-Key $window 0x0D # Enter activates the focused Reset.
    Assert-Count 0
    Send-Key $window 0x20
    Assert-Count 1

    $second = Start-Process -FilePath $binary -PassThru
    if (-not $second.WaitForExit(10000)) { throw 'The second instance did not hand off to the existing one.' }
    $second.Refresh()
    $process.Refresh()
    if ($second.ExitCode -ne 0 -or $process.HasExited) { throw 'Single-instance activation failed.' }
    Assert-Count 1
    Close-Tally $window
    $geometry = Get-Content -LiteralPath (Join-Path $stateDirectory 'window.ini') -Raw
    if ($geometry -notmatch '(?m)^version=1\r?$' -or $geometry -notmatch '(?m)^width=\d+\r?$') {
        throw 'Normal close did not produce valid window-state metadata.'
    }
    $window = Start-Tally
    Send-Key $window 0x20
    Assert-Count 2
    Close-Tally $window
    Write-Output 'Win32 smoke passed: launch, restore, repeat suppression, reset-focus input, reset, single instance, normal close, count persistence, and window-state persistence.'
}
finally {
    foreach ($candidate in @($second, $process)) {
        if ($null -ne $candidate) {
            $candidate.Refresh()
            if (-not $candidate.HasExited) {
                $candidate.Kill()
                $candidate.WaitForExit(10000) | Out-Null
            }
            $candidate.Dispose()
        }
    }
    if ($statePrepared -and (Test-Path -LiteralPath $stateDirectory)) {
        Remove-Item -LiteralPath $stateDirectory -Recurse -Force
    }
    if (Test-Path -LiteralPath $backup) {
        Move-Item -LiteralPath $backup -Destination $stateDirectory
    }
}
