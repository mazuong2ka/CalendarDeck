param([string]$ExecutablePath, [string]$DesktopPath)
$ErrorActionPreference = 'Stop'
if (-not $ExecutablePath) {
    $besideScript = Join-Path $PSScriptRoot 'CalendarDeck.exe'
    $fromProject = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\CalendarDeck\CalendarDeck.exe'
    $ExecutablePath = if (Test-Path -LiteralPath $besideScript) { $besideScript } else { $fromProject }
}
if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) { throw 'CalendarDeck.exe was not found. Build/package the app or pass -ExecutablePath.' }
$resolvedExe = (Resolve-Path -LiteralPath $ExecutablePath).Path
if (-not $DesktopPath) { $DesktopPath = [Environment]::GetFolderPath('Desktop') }
if (-not (Test-Path -LiteralPath $DesktopPath -PathType Container)) { throw 'The desktop directory does not exist.' }
$shortcutPath = Join-Path $DesktopPath 'Calendar Deck.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $resolvedExe
$shortcut.WorkingDirectory = Split-Path -Parent $resolvedExe
$shortcut.IconLocation = "$resolvedExe,0"
$shortcut.Description = 'Calendar Deck - weekly desktop calendar'
$shortcut.Save()
if (-not (Test-Path -LiteralPath $shortcutPath)) { throw 'The shortcut could not be created.' }
Write-Output "Created: $shortcutPath"
