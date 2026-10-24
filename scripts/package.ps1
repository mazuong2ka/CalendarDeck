param([string]$BuildDirectory = 'build/release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    & (Join-Path $PSScriptRoot 'build.ps1') -BuildDirectory $BuildDirectory
    $candidates = @((Join-Path $BuildDirectory 'Release\CalendarDeck.exe'), (Join-Path $BuildDirectory 'CalendarDeck.exe'))
    $executable = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $executable) { throw 'The built executable was not found.' }
    $versionLine = Get-Content -LiteralPath 'CMakeLists.txt' | Select-String 'project\(CalendarDeck VERSION ([0-9.]+)'
    $version = $versionLine.Matches[0].Groups[1].Value
    $stage = Join-Path $projectRoot 'dist\CalendarDeck'
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Copy-Item -LiteralPath $executable -Destination (Join-Path $stage 'CalendarDeck.exe') -Force
    foreach ($readme in @('README.md','README.vi.md')) { Copy-Item -LiteralPath $readme -Destination $stage -Force }
    Copy-Item -LiteralPath 'CONTRIBUTING.md' -Destination $stage -Force
    New-Item -ItemType Directory -Path (Join-Path $stage 'resources') -Force | Out-Null
    Copy-Item -LiteralPath 'resources\calendar-deck.png' -Destination (Join-Path $stage 'resources') -Force
    Copy-Item -LiteralPath 'docs' -Destination $stage -Recurse -Force
    Copy-Item -LiteralPath 'scripts\create-desktop-shortcut.ps1' -Destination $stage -Force
    $binaryZip = Join-Path $projectRoot "dist\CalendarDeck-v$version-windows-x64.zip"
    Compress-Archive -LiteralPath $stage -DestinationPath $binaryZip -Force

    # Allowlist the source archive: generated folders and personal calendars are
    # excluded regardless of what happens to exist in the working directory.
    Add-Type -AssemblyName System.IO.Compression
    $sourceZip = Join-Path $projectRoot "dist\CalendarDeck-v$version-source.zip"
    $sourceFiles = @('CMakeLists.txt','README.md','README.vi.md','CONTRIBUTING.md','.gitignore','.gitattributes')
    foreach ($folder in @('src','resources','scripts','tests','docs')) {
        $sourceFiles += Get-ChildItem -LiteralPath $folder -Recurse -File | ForEach-Object { $_.FullName.Substring($projectRoot.Length + 1) }
    }
    $stream = [IO.File]::Open($sourceZip, [IO.FileMode]::Create)
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($relative in ($sourceFiles | Sort-Object -Unique)) {
            $entry = $archive.CreateEntry(('CalendarDeck/' + $relative.Replace('\','/')), [IO.Compression.CompressionLevel]::Optimal)
            $entryStream = $entry.Open()
            $inputStream = [IO.File]::OpenRead((Join-Path $projectRoot $relative))
            try { $inputStream.CopyTo($entryStream) } finally { $inputStream.Dispose(); $entryStream.Dispose() }
        }
    } finally { $archive.Dispose(); $stream.Dispose() }
    $sums = foreach ($item in @($binaryZip,$sourceZip)) { (Get-FileHash -LiteralPath $item -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + (Split-Path -Leaf $item) }
    $sums | Set-Content -LiteralPath 'dist\SHA256SUMS.txt' -Encoding ascii
    Write-Output "Ready to run: $stage\CalendarDeck.exe"
    Write-Output "Release archive: $binaryZip"
    Write-Output "Source archive: $sourceZip"
} finally { Pop-Location }
