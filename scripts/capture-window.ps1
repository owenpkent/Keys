# Captures a screenshot of the Keys (or Keys Host) standalone for the docs, per the
# CLAUDE.md screenshot contract: launch, PrintWindow(PW_RENDERFULLCONTENT), kill.
# Never SetForegroundWindow/SetCursorPos (Owen may be using the machine), and never
# synthesized clicks - on-screen controls are driven through UI Automation Invoke.
#
# Usage:
#   powershell -File scripts/capture-window.ps1 -ExePath build/Keys_artefacts/Release/Standalone/Keys.exe `
#       -OutPath assets/screenshots/keys.png
#   ... -InvokeButtons "Arp section"     # unfold a section first (UIA Invoke by name)
#   ... -KeepOpen                        # leave the app running (repeat captures)
#   ... -WindowTitle "Keys Host"         # REQUIRED for Keys Host: see the note by $hwnd
#   ... -InvokeButtons "Chord generator window" -WindowTitle "Keys Chord Generator"
#
# A section bar is a juce::Button named "<caption> section", so "Controls section",
# "Arp section", "Pads section" and "Keyboard section" each fold or unfold that section.
# Those four are all there are: Centre and Transcribe were deleted on 2026-07-30.
# The arp starts folded, so most arp shots begin by invoking "Arp section".
#
# The Detach buttons are named per section, because four buttons reading "Detach" are four
# identical accessible names: "Detach Controls", "Detach Arp", "Detach Pads" and
# "Detach Keyboard", each flipping to "Re-dock ..." once the section is out. The button
# says Arp where the window says "Keys Arpeggiator"; invoke the one, shoot the other.
#
# The chord generator is not a section, it is a window, so it has no bar and no Detach. The
# chip that opens it rides the Pads bar and is named "Chord generator window"; invoking it
# a second time only raises the window, it never closes it.
param(
    # Not mandatory: with -ProcessId you are reusing a running instance and there is
    # nothing to launch. -SetValues runs *before* -InvokeButtons, so reaching a control
    # that only exists inside a folded section means two passes: one to unfold it with
    # -InvokeButtons and -KeepOpen, then one with -ProcessId to drive it.
    [string]$ExePath,
    [Parameter(Mandatory = $true)] [string]$OutPath,
    [int]$SettleMs = 2500,
    [string[]]$InvokeButtons = @(),
    # Two combos can collide: the match is on the text a box is showing, and Shape and the strum
    # Dir both read "Up", so it takes the first one and the other has to be set out of the way
    # first. A rotary is not reachable this way at all, which since 2026-07-30 includes the arp
    # rate - drive that with -InvokeButtons "Slower rate" / "Faster rate", and flip its units
    # with "Arp rate mode".
    [string[]]$SetValues = @(),   # ComboBox "CurrentText=NewText" pairs (matched by current value)
    [int]$AfterInvokeMs = 900,
    [switch]$KeepOpen,
    [int]$ProcessId = 0,   # reuse an already-running instance instead of launching
    [string]$WindowTitle   # shoot the window with this exact title, not "the main window"
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class KeysCapture {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    // CharSet.Unicode matters: the default is Ansi, which marshals the StringBuilder as
    // bytes, so the W function's UTF-16 comes back truncated at the first NUL and every
    // title reads as its own first letter ("Keys Host" -> "K").
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowTextW(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr param);
    public delegate bool EnumProc(IntPtr hwnd, IntPtr param);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    public static List<IntPtr> TopLevelWindows(uint wanted) {
        var found = new List<IntPtr>();
        EnumWindows((h, p) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid == wanted && IsWindowVisible(h)) found.Add(h);
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static string WindowTitle(IntPtr h) {
        var sb = new StringBuilder(512);
        GetWindowTextW(h, sb, sb.Capacity);
        return sb.ToString();
    }
}
'@
[KeysCapture]::SetProcessDPIAware() | Out-Null

$launched = $false
if ($ProcessId -ne 0) {
    $proc = Get-Process -Id $ProcessId
} else {
    if (-not $ExePath) { throw "Pass -ExePath to launch, or -ProcessId to reuse a running instance." }
    # Smart App Control blocks the first launch of a freshly linked unsigned exe while its
    # reputation check runs, then allows the same file moments later (see docs/BUILD.md).
    # Absorb that here, the same way run.py does, instead of failing a docs capture.
    for ($attempt = 1; $attempt -le 8; $attempt++) {
        try { $proc = Start-Process -FilePath $ExePath -PassThru; break }
        catch { if ($attempt -eq 8) { throw }; Start-Sleep -Milliseconds 1200 }
    }
    $launched = $true
}

try {
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
    }
    if ($proc.MainWindowHandle -eq [IntPtr]::Zero) { throw "The app never opened a main window." }
    if ($launched) { Start-Sleep -Milliseconds $SettleMs }

    foreach ($pair in $SetValues) {
        $current, $value = $pair -split '=', 2
        $root = [System.Windows.Automation.AutomationElement]::FromHandle($proc.MainWindowHandle)
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::ComboBox)
        $el = $null
        foreach ($c in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)) {
            try {
                if ($c.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value -eq $current) {
                    $el = $c
                    break
                }
            } catch {}
        }
        if ($null -eq $el) { throw "No ComboBox with value '$current' found." }

        # JUCE's combo value is UIA read-only, so drive it the way a user would:
        # open its menu, then invoke the row. The popup is a separate top-level
        # window in the same process.
        $opened = $false
        try { $el.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand(); $opened = $true } catch {}
        if (-not $opened) {
            try { $el.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); $opened = $true } catch {}
        }
        if (-not $opened) { throw "Could not open the ComboBox menu for '$current'." }
        Start-Sleep -Milliseconds 600

        $procCond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $proc.Id)
        $itemCond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty, $value)
        $item = $null
        foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
                     [System.Windows.Automation.TreeScope]::Children, $procCond)) {
            $item = $w.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $itemCond)
            if ($null -ne $item) { break }
        }
        if ($null -eq $item) { throw "Menu item '$value' not found." }
        try { $item.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
        catch { $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select() }
        Start-Sleep -Milliseconds $AfterInvokeMs
    }

    foreach ($name in $InvokeButtons) {
        $root = [System.Windows.Automation.AutomationElement]::FromHandle($proc.MainWindowHandle)
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty, $name)
        $el = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($null -eq $el) { throw "UIA element named '$name' not found." }
        $el.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        Start-Sleep -Milliseconds $AfterInvokeMs
    }

    # Which of the process's windows to shoot. MainWindowHandle is a heuristic, and for Keys
    # Host it picks wrong: the hosted instrument's GUI is a top-level window of the same
    # process, so a capture aimed at "the main window" comes back as a picture of somebody
    # else's synth. Pass -WindowTitle to name the one you actually want. The detached
    # sections have the same problem, and the same answer: "Keys Controls", "Keys Arpeggiator",
    # "Keys Chord Pads", "Keys Keyboard". Those four are every detached section there is, and
    # the chord generator is a fifth window on top of them, "Keys Chord Generator", up whenever
    # the Pads bar's Generator chip is lit.
    $hwnd = $proc.MainWindowHandle
    if ($WindowTitle) {
        $match = [IntPtr]::Zero
        foreach ($h in [KeysCapture]::TopLevelWindows([uint32]$proc.Id)) {
            if ([KeysCapture]::WindowTitle($h) -eq $WindowTitle) { $match = $h; break }
        }
        if ($match -eq [IntPtr]::Zero) {
            $titles = ([KeysCapture]::TopLevelWindows([uint32]$proc.Id) |
                       ForEach-Object { "'" + [KeysCapture]::WindowTitle($_) + "'" }) -join ", "
            throw "No visible window titled '$WindowTitle'. This process has: $titles"
        }
        $hwnd = $match
    }
    $rect = New-Object KeysCapture+RECT
    [KeysCapture]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    if ($w -le 0 -or $h -le 0) { throw "Window rect is empty ($w x $h)." }

    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $gfx.GetHdc()
    $ok = [KeysCapture]::PrintWindow($hwnd, $hdc, 2)  # 2 = PW_RENDERFULLCONTENT
    $gfx.ReleaseHdc($hdc)
    $gfx.Dispose()
    if (-not $ok) { $bmp.Dispose(); throw "PrintWindow failed." }

    $bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "saved $OutPath ($w x $h)"
}
finally {
    if ($launched -and -not $KeepOpen) {
        try { Stop-Process -Id $proc.Id -Force -ErrorAction Stop } catch {}
    }
}
