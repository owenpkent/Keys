# Captures a screenshot of the Keys (or Keys Host) standalone for the docs, per the
# CLAUDE.md screenshot contract: launch, PrintWindow(PW_RENDERFULLCONTENT), kill.
# Never SetForegroundWindow/SetCursorPos (Owen may be using the machine), and never
# synthesized clicks - overlay buttons are driven through UI Automation Invoke.
#
# Usage:
#   powershell -File scripts/capture-window.ps1 -ExePath build/Keys_artefacts/Release/Standalone/Keys.exe `
#       -OutPath assets/screenshots/keys.png
#   ... -InvokeButtons Chords            # open an overlay first (UIA Invoke by name)
#   ... -KeepOpen                        # leave the app running (repeat captures)
param(
    # Not mandatory: with -ProcessId you are reusing a running instance and there is
    # nothing to launch. -SetValues runs *before* -InvokeButtons, so reaching a control
    # that only exists inside a view means two passes: one to open it with -KeepOpen,
    # then one with -ProcessId to drive it.
    [string]$ExePath,
    [Parameter(Mandatory = $true)] [string]$OutPath,
    [int]$SettleMs = 2500,
    [string[]]$InvokeButtons = @(),
    [string[]]$SetValues = @(),   # ComboBox "CurrentText=NewText" pairs (matched by current value)
    [int]$AfterInvokeMs = 900,
    [switch]$KeepOpen,
    [int]$ProcessId = 0   # reuse an already-running instance instead of launching
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class KeysCapture {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
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

    $hwnd = $proc.MainWindowHandle
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
