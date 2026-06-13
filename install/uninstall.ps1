param([string]$TrafficMonitorDir = "D:\Downloads\TrafficMonitor")
schtasks /Delete /TN "ClaudeMeter Collector" /F 2>$null | Out-Null
Remove-Item (Join-Path $TrafficMonitorDir "plugins\ClaudeMeter.dll") -Force -ErrorAction SilentlyContinue
$settings = Join-Path $env:USERPROFILE ".claude\settings.json"
if (Test-Path $settings) {
    $json = Get-Content $settings -Raw | ConvertFrom-Json
    if ($json.PSObject.Properties.Name -contains 'statusLine') {
        $json.PSObject.Properties.Remove('statusLine')
        $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
        [System.IO.File]::WriteAllText($settings, ($json | ConvertTo-Json -Depth 20), $utf8NoBom)
    }
}
Remove-Item (Join-Path $env:LOCALAPPDATA "ClaudeMeter") -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "ClaudeMeter uninstalled."
