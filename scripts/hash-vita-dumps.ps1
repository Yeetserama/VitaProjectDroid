# Hash and sanity-check Vita module dumps.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/hash-vita-dumps.ps1
#   powershell -ExecutionPolicy Bypass -File scripts/hash-vita-dumps.ps1 -Path re\dumps\fw365
#   powershell -ExecutionPolicy Bypass -File scripts/hash-vita-dumps.ps1 -Json
#
# The output is a Markdown table suitable for re/notes/manifest.md.
# This script never modifies the dumps.

param(
    [string]$Path = "re/dumps/fw365",
    [switch]$Json
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path)) {
    throw "dump folder not found: $Path"
}

function Get-MagicKind {
    param([string]$File)

    $stream = [System.IO.File]::OpenRead($File)
    try {
        $magic = New-Object byte[] 4
        $read = $stream.Read($magic, 0, 4)
    } finally {
        $stream.Dispose()
    }

    if ($read -lt 4) { return "short" }
    if ($magic[0] -eq 0x7F -and $magic[1] -eq 0x45 -and
        $magic[2] -eq 0x4C -and $magic[3] -eq 0x46) { return "ELF" }
    if ($magic[0] -eq 0x53 -and $magic[1] -eq 0x43 -and
        $magic[2] -eq 0x45 -and $magic[3] -eq 0x00) { return "SELF" }
    return "unknown"
}

$extensions = @(".elf", ".skprx", ".suprx")
$files = Get-ChildItem -LiteralPath $Path -Recurse -File |
    Where-Object { $extensions -contains $_.Extension.ToLowerInvariant() } |
    Sort-Object FullName

if (-not $files) {
    throw "no .elf, .skprx or .suprx files under $Path"
}

$rows = foreach ($file in $files) {
    $relative = $file.FullName.Substring((Resolve-Path -LiteralPath $Path).Path.Length).TrimStart('\', '/')
    [pscustomobject]@{
        File   = $relative -replace '\\', '/'
        Bytes  = $file.Length
        SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        Magic  = Get-MagicKind -File $file.FullName
    }
}

if ($Json) {
    $rows | ConvertTo-Json -Depth 3
    exit 0
}

Write-Output "| File | Bytes | SHA-256 | Magic |"
Write-Output "| --- | ---: | --- | --- |"
foreach ($row in $rows) {
    Write-Output ("| {0} | {1} | {2} | {3} |" -f $row.File, $row.Bytes, $row.SHA256, $row.Magic)
}
