param(
    [int]$DurationSeconds = 60,
    [int]$Step = 2,
    [int]$IntervalMs = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -Namespace Win32 -Name NativeMethods -MemberDefinition @"
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetCursorPos(out POINT lpPoint);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetCursorPos(int X, int Y);
public struct POINT {
    public int X;
    public int Y;
}
"@

$pt = New-Object Win32.NativeMethods+POINT
if (-not [Win32.NativeMethods]::GetCursorPos([ref]$pt)) {
    throw "GetCursorPos failed"
}
$startX = $pt.X
$startY = $pt.Y

try {
    $deadline = (Get-Date).AddSeconds($DurationSeconds)
    $direction = 1
    while ((Get-Date) -lt $deadline) {
        $x = $startX + ($direction * $Step)
        if (-not [Win32.NativeMethods]::SetCursorPos($x, $startY)) {
            throw "SetCursorPos failed while moving cursor"
        }
        Start-Sleep -Milliseconds $IntervalMs
        $direction = -$direction
    }
}
finally {
    [Win32.NativeMethods]::SetCursorPos($startX, $startY) | Out-Null
}
