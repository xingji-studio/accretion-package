param([ValidateSet('all', 'frontend', 'backend')][string]$Mode = 'all')
$ErrorActionPreference = 'Stop'
$apRoot = $PSScriptRoot
$apBin = Join-Path $apRoot 'bin'
$apFailed = $false
if (-not [Environment]::Is64BitOperatingSystem) { Write-Host 'This package requires x64 Windows.'; exit 1 }
$apFileList = Join-Path $apRoot $(if ($Mode -eq 'backend') { 'runtime-backend.txt' } else { 'runtime-files.txt' })
if (-not (Test-Path -LiteralPath $apFileList)) { Write-Host "MISSING FILE: $apFileList"; exit 1 }
foreach ($apFile in (Get-Content -LiteralPath $apFileList -Encoding UTF8)) {
    if ($apFile -and -not (Test-Path -LiteralPath (Join-Path $apRoot $apFile) -PathType Leaf)) {
        Write-Host "MISSING PACKAGE DEPENDENCY: $apFile"; $apFailed = $true
    }
}

# Inspect PE imports without running application code or requiring SDK tools/Python.
function Get-PeImports([string]$Path) {
    $data = [System.IO.File]::ReadAllBytes($Path)
    if ($data.Length -lt 64 -or $data[0] -ne 77 -or $data[1] -ne 90) { return @() }
    $pe = [BitConverter]::ToUInt32($data, 60)
    $optional = $pe + 24
    $magic = [BitConverter]::ToUInt16($data, $optional)
    $directory = $optional + $(if ($magic -eq 523) { 112 } elseif ($magic -eq 267) { 96 } else { throw "Invalid PE: $Path" })
    $count = [BitConverter]::ToUInt16($data, $pe + 6)
    $sections = $optional + [BitConverter]::ToUInt16($data, $pe + 20)
    function Convert-Rva([uint32]$Rva) {
        for ($i = 0; $i -lt $count; $i++) {
            $at = $sections + 40 * $i
            $address = [BitConverter]::ToUInt32($data, $at + 12)
            $size = [BitConverter]::ToUInt32($data, $at + 16)
            if ($Rva -ge $address -and $Rva -lt $address + $size) {
                return [BitConverter]::ToUInt32($data, $at + 20) + $Rva - $address
            }
        }
        throw "Invalid import address: $Path"
    }
    $rva = [BitConverter]::ToUInt32($data, $directory + 8)
    if ($rva -eq 0) { return @() }
    $at = Convert-Rva $rva
    $names = @()
    for ($i = 0; $i -lt 4096; $i++) {
        $nameRva = [BitConverter]::ToUInt32($data, $at + 20 * $i + 12)
        if ($nameRva -eq 0) { return $names }
        $start = Convert-Rva $nameRva
        $end = $start
        while ($end -lt $data.Length -and $data[$end] -ne 0 -and $end - $start -lt 512) { $end++ }
        if ($end -ge $data.Length -or $end - $start -ge 512) { throw "Invalid DLL name: $Path" }
        $names += [System.Text.Encoding]::ASCII.GetString($data, $start, $end - $start)
    }
    throw "Unterminated import table: $Path"
}

$required = @(Join-Path $apBin 'apack.exe')
$workbench = Join-Path $apBin 'apack-workbench.exe'
if ($Mode -ne 'backend' -and (Test-Path -LiteralPath $workbench)) {
    $required += $workbench
    $qwindows = Join-Path $apRoot 'plugins\platforms\qwindows.dll'
    if (-not (Test-Path -LiteralPath $qwindows)) { $qwindows = Join-Path $apRoot 'plugins\platforms\qwindowsd.dll' }
    $required += $qwindows
    if (-not (Test-Path -LiteralPath (Join-Path $apBin 'qt.conf'))) {
        Write-Host 'MISSING FILE: bin/qt.conf'; $apFailed = $true
    }
} elseif ($Mode -ne 'backend') { Write-Host 'Frontend: built-in TUI (bin/apack.exe wizard).' }
if ($Mode -eq 'all') {
    Get-ChildItem -LiteralPath (Join-Path $apBin 'ap-stubs') -Filter 'ap-setup-windows-x64-*.exe' | ForEach-Object {
        $required += $_.FullName
    }
}

$queue = New-Object 'System.Collections.Generic.Queue[string]'
$seen = @{}
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { Write-Host "MISSING FILE: $path"; $apFailed = $true }
    else { $queue.Enqueue($path) }
}
$systemDir = [Environment]::SystemDirectory
while ($queue.Count -gt 0) {
    $path = $queue.Dequeue()
    if ($seen.ContainsKey($path)) { continue }
    $seen[$path] = $true
    try {
        foreach ($dll in (Get-PeImports $path)) {
            if ($dll.StartsWith('api-ms-', [StringComparison]::OrdinalIgnoreCase) -or
                $dll.StartsWith('ext-ms-', [StringComparison]::OrdinalIgnoreCase)) { continue }
            $local = Join-Path $apBin $dll
            $adjacent = Join-Path (Split-Path -Parent $path) $dll
            if (Test-Path -LiteralPath $local) { $queue.Enqueue($local) }
            elseif (Test-Path -LiteralPath $adjacent) { $queue.Enqueue($adjacent) }
            elseif (-not (Test-Path -LiteralPath (Join-Path $systemDir $dll))) {
                Write-Host "MISSING DLL: $dll (required by $([IO.Path]::GetFileName($path)))"
                $apFailed = $true
            }
        }
        Write-Host "OK: $path"
    } catch { Write-Host "DEPENDENCY CHECK FAILED: $path -- $_"; $apFailed = $true }
}
if ($apFailed) {
    Write-Host 'Restore missing package files or install the named system runtime.'
    Get-ChildItem -LiteralPath $apRoot -Filter 'vc_redist*.exe' -Recurse | ForEach-Object {
        Write-Host "Included Microsoft runtime installer: $($_.FullName)"
    }
    exit 1
}
Write-Host 'Runtime dependency check passed. No Python, CMake or Qt SDK is required.'
exit 0
