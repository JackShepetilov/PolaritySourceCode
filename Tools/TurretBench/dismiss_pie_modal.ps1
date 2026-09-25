# Clicks through "Blueprint Asset Compilation Errors", the modal the editor raises before the first
# PIE of a session while some Blueprint does not compile (Docs/Gotchas/Python_Editor.md). Slate does
# not expose its buttons to UI Automation, so it is a real click: one into the body for focus, then
# "Play in Editor" (about 517,170 in a 656x192 window, scaled here to the actual size).
# Waits up to -Seconds for the window; no window means nothing to dismiss.
param([int]$Seconds = 12)

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class BenchModal {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static IntPtr Find(string like) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            if (!IsWindowVisible(h)) return true;
            StringBuilder sb = new StringBuilder(512);
            GetWindowText(h, sb, 512);
            if (sb.ToString().IndexOf(like, StringComparison.OrdinalIgnoreCase) >= 0) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static void Click(int x, int y) {
        SetCursorPos(x, y);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }
}
"@

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    $h = [BenchModal]::Find("Compilation Errors")
    if ($h -ne [IntPtr]::Zero) {
        $r = New-Object BenchModal+RECT
        [void][BenchModal]::GetWindowRect($h, [ref]$r)
        $w = $r.Right - $r.Left
        $hh = $r.Bottom - $r.Top
        [void][BenchModal]::SetForegroundWindow($h)
        Start-Sleep -Milliseconds 300
        [BenchModal]::Click($r.Left + [int]($w / 2), $r.Top + [int]($hh * 0.35))
        Start-Sleep -Milliseconds 300
        [BenchModal]::Click($r.Left + [int]($w * 517 / 656), $r.Top + [int]($hh * 170 / 192))
        Start-Sleep -Milliseconds 800
        if ([BenchModal]::Find("Compilation Errors") -eq [IntPtr]::Zero) {
            Write-Output "MODAL DISMISSED (${w}x${hh})"
            exit 0
        }
    }
    Start-Sleep -Milliseconds 500
}
Write-Output "NO MODAL (or it did not close)"
