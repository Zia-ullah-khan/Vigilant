$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERROR: Please run PowerShell as Administrator to install Vigilant." -ForegroundColor Red
    exit
}

Clear-Host
$ascii = @"
    ██╗   ██╗██╗ ██████╗ ██╗██╗      █████╗ ███╗   ██╗████████╗
    ██║   ██║██║██╔════╝ ██║██║     ██╔══██╗████╗  ██║╚══██╔══╝
    ██║   ██║██║██║  ███╗██║██║     ███████║██╔██╗ ██║   ██║   
    ╚██╗ ██╔╝██║██║   ██║██║██║     ██╔══██║██║╚██╗██║   ██║   
     ╚████╔╝ ██║╚██████╔╝██║███████╗██║  ██║██║ ╚████║   ██║   
      ╚═══╝  ╚═╝ ╚═════╝ ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝   ╚═╝   
"@

Write-Host $ascii -ForegroundColor Cyan
Write-Host "             >> Ultra-Light Service Manager <<" -ForegroundColor Blue
Write-Host ""

$url = "https://github.com/Zia-ullah-khan/Vigilant/releases/latest/download/vigilant-windows-x64.exe"
$installDir = "$env:ProgramFiles\Vigilant"
$dest = "$installDir\vigilant.exe"

Write-Host "[1/3] Preparing directory: $installDir" -ForegroundColor Blue
if (!(Test-Path $installDir)) {
    New-Item -ItemType Directory -Force -Path $installDir | Out-Null
}

Write-Host "[2/3] Downloading binary..." -ForegroundColor Blue
try {
    Invoke-WebRequest -Uri $url -OutFile $dest -ErrorAction Stop
} catch {
    Write-Host "FAILED: Could not download the binary. Check your internet or the URL." -ForegroundColor Red
    exit
}

Write-Host "[3/3] Adding to System PATH..." -ForegroundColor Blue
$oldPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($oldPath -notlike "*$installDir*") {
    $newPath = "$oldPath;$installDir"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "Machine")
    Write-Host "PATH updated successfully." -ForegroundColor Gray
}

Write-Host ""
Write-Host "✔ INSTALLATION COMPLETE" -ForegroundColor Green
Write-Host "----------------------------------------"
Write-Host "Please RESTART your terminal/PowerShell window."
Write-Host "Then type: vigilant -h"