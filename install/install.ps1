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
    if (-not (Get-Command python.exe -ErrorAction SilentlyContinue)) {
        throw "Python 3.11+ is required but was not found on PATH."
    }
    return (Get-Command python.exe).Source
}
function Find-SystemPythonw {
    $pyc = Find-SystemPython
    $dir = Split-Path $pyc -Parent
    $pw = Join-Path $dir "pythonw.exe"
    if (Test-Path $pw) { return $pw }
    if (-not (Get-Command python.exe -ErrorAction SilentlyContinue)) {
        throw "Python 3.11+ is required but was not found on PATH."
    }
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
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Seed run exit $LASTEXITCODE. The status bar will show 'CC --' until the next scheduled run succeeds."
} else {
    Write-Host "seeded: $(Join-Path $CacheDir 'usage.ini')"
}

# Scheduled task: run the collector every 3 minutes. Register-ScheduledTask keeps
# Execute and Argument separate, so a python path containing spaces works correctly.
$taskName = "ClaudeMeter Collector"
$action   = New-ScheduledTaskAction -Execute $pyw -Argument ('"' + $Collector + '"')
$trigger  = New-ScheduledTaskTrigger -Once -At (Get-Date) `
    -RepetitionInterval (New-TimeSpan -Minutes 3) `
    -RepetitionDuration (New-TimeSpan -Days 9999)
$taskSettings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -StartWhenAvailable
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $taskSettings -Force | Out-Null
Write-Host "scheduled task '$taskName' created (every 3 min)"

# Copy DLL into TrafficMonitor plugins
if (-not (Test-Path $Dll)) { throw "DLL not built: $Dll  (run plugin\build.bat first)" }
$pluginDir = Join-Path $TrafficMonitorDir "plugins"
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item $Dll (Join-Path $pluginDir "ClaudeMeter.dll") -Force
Write-Host "DLL copied to $pluginDir"
Write-Host "NOTE: restart TrafficMonitor to load the new/updated DLL."

# Optional: official statusLine fallback
if ($WithStatusLine) {
    $settings = Join-Path $env:USERPROFILE ".claude\settings.json"
    if (Test-Path $settings) { $json = Get-Content $settings -Raw | ConvertFrom-Json }
    else { $json = [pscustomobject]@{} }
    $py = $pyConsole
    $cmd = '"' + $py + '" "' + $Hook + '"'
    $sl = [pscustomobject]@{ type = "command"; command = $cmd; padding = 0 }
    $json | Add-Member -NotePropertyName statusLine -NotePropertyValue $sl -Force
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($settings, ($json | ConvertTo-Json -Depth 20), $utf8NoBom)
    Write-Host "statusLine configured in $settings"
}

Write-Host ""
Write-Host "DONE. Restart TrafficMonitor, then right-click -> 显示设置 and enable 'Claude Code 用量'."
