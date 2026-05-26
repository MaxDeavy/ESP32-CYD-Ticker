$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$envFile = if ($args.Count -gt 0) { $args[0] } else { Join-Path $PSScriptRoot "..\deploy.env" }
$vars = @{}
Get-Content $envFile | ForEach-Object {
    if ($_ -match '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$' -and -not $_.TrimStart().StartsWith("#")) {
        $vars[$Matches[1]] = $Matches[2]
    }
}
if (-not $vars["HOST_PORT"]) { $vars["HOST_PORT"] = "4546" }
$c = (Get-Content nginx-ticker.conf.template -Raw).Replace('${SERVER_NAME}', $vars["SERVER_NAME"]).Replace('${HOST_PORT}', $vars["HOST_PORT"])
Set-Content nginx-ticker.conf $c -NoNewline -Encoding utf8
Write-Host "OK: nginx-ticker.conf ($($vars['SERVER_NAME']):$($vars['HOST_PORT']))"
