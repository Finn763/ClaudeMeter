param(
    [string]$TrafficMonitorDir = "D:\Downloads\TrafficMonitor",
    [switch]$WithStatusLine
)
$ErrorActionPreference = "Stop"
$Root      = Split-Path -Parent $PSScriptRoot
$Collector = Join-Path $Root "collector\cc_usage_collector.py"
$Hook      = Join-Path $Root "collector\statusline_hook.py"
$Dll       = Join-Path $Root "plugin\ClaudeMeter.dll"
$CacheDir  = Join-Path $env:LOCALAPPDATA "ClaudeMeter"

New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null

# Resolve pythonw.exe for the scheduled task (no console flash every 3 min),
# and console python.exe for the one-shot seed (PowerShell & waits for it).
# Skip any virtualenv python (looks for site-packages or Scripts\activate).
function Find-SystemPython {
    $candidates = @(Get-Command python.exe -All -ErrorAction SilentlyContinue)
    foreach ($c in $candidates) {
        $p = $c.Source
        # Skip anything inside a venv (has pyvenv.cfg in parent or grandparent)
        $dir1 = Split-Path $p -Parent
        $dir2 = Split-Path $dir1 -Parent
        if ((Test-Path (Join-Path $dir1 "pyvenv.cfg")) -or
            (Test-Path (Join-Path $dir2 "pyvenv.cfg"))) { continue }
        return $p
    }
    # fallback: first one
    return (Get-Command python.exe).Source
}
function Find-SystemPythonw {
    $pyc = Find-SystemPython
    $dir = Split-Path $pyc -Parent
    $pw = Join-Path $dir "pythonw.exe"
    if (Test-Path $pw) { return $pw }
    return $pyc
}

$pyConsole = Find-SystemPython
$pyw       = Find-SystemPythonw
Write-Host "python (console): $pyConsole"
Write-Host "python (gui/sched): $pyw"

# Seed the cache once — use console python so PowerShell waits for it to exit
# before we check usage.ini (pythonw.exe is GUI-subsystem; & would not wait).
Write-Host "Seeding cache..."
& $pyConsole $Collector
Write-Host "seeded: $(Join-Path $CacheDir 'usage.ini')"

# Scheduled task: run collector every 3 minutes (pythonw = no console window)
$taskName = "ClaudeMeter Collector"
# Use -ErrorAction SilentlyContinue on the pipeline to avoid Stop-mode exceptions
# when schtasks exits non-zero (task not yet registered emits to stdout+non-zero exit).
$taskExists = $false
try {
    $null = schtasks /Query /TN $taskName 2>&1
    $taskExists = ($LASTEXITCODE -eq 0)
} catch { $taskExists = $false }
if ($taskExists) {
    try { $null = schtasks /Delete /TN $taskName /F 2>&1 } catch {}
}
$tr = '"' + $pyw + '" "' + $Collector + '"'
$createOut = schtasks /Create /TN $taskName /TR $tr /SC MINUTE /MO 3 /F 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Warning "schtasks /Create failed (exit $LASTEXITCODE). May need elevation."
    Write-Warning "The DLL and cache are already deployed — manually create the task if needed."
    Write-Warning "Command that failed: schtasks /Create /TN `"$taskName`" /TR `"$tr`" /SC MINUTE /MO 3 /F"
} else {
    Write-Host "scheduled task '$taskName' created (every 3 min, pythonw)"
}

# Copy DLL into TrafficMonitor plugins
if (-not (Test-Path $Dll)) { throw "DLL not built: $Dll  (run plugin\build.bat first)" }
$pluginDir = Join-Path $TrafficMonitorDir "plugins"
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item $Dll (Join-Path $pluginDir "ClaudeMeter.dll") -Force
Write-Host "DLL copied to $pluginDir"

# Optional: official statusLine fallback
if ($WithStatusLine) {
    $settings = Join-Path $env:USERPROFILE ".claude\settings.json"
    if (Test-Path $settings) { $json = Get-Content $settings -Raw | ConvertFrom-Json }
    else { $json = [pscustomobject]@{} }
    $py = (Get-Command python.exe).Source
    $cmd = '"' + $py + '" "' + $Hook + '"'
    $sl = [pscustomobject]@{ type = "command"; command = $cmd; padding = 0 }
    $json | Add-Member -NotePropertyName statusLine -NotePropertyValue $sl -Force
    ($json | ConvertTo-Json -Depth 20) | Set-Content $settings -Encoding UTF8
    Write-Host "statusLine configured in $settings"
}

Write-Host ""
Write-Host "DONE. Restart TrafficMonitor, then right-click -> 显示设置 and enable 'Claude Code 用量'."
