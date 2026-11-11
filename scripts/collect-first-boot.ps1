<#
.SYNOPSIS
Collect AndroidOnVita first-boot diagnostics over adb.

.DESCRIPTION
Connects to the Vita's adbd over TCP, saves the commands and logs needed to
debug a first boot, and zips the result. Run it immediately after the Vita is
reachable: /proc/uptime is captured so the report shows how long the Linux
kernel had been up when adb answered.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\collect-first-boot.ps1 -VitaIP 192.168.1.50

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\collect-first-boot.ps1 -VitaIP 192.168.1.50 -NoZip
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$VitaIP,

    [string]$OutDir,

    [string]$Adb,

    [switch]$NoZip
)

$ErrorActionPreference = 'Continue'

try {
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
} catch {
    # Older PowerShell hosts may refuse to change the console encoding.
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

if (-not $OutDir) {
    $OutDir = Join-Path $repoRoot "build\first-boot-report\$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

if (-not $Adb) {
    $bundled = Join-Path $repoRoot 'build\tools\platform-tools\adb.exe'
    if (Test-Path -LiteralPath $bundled) {
        $Adb = $bundled
    } else {
        $onPath = Get-Command adb -ErrorAction SilentlyContinue
        if ($onPath) {
            $Adb = $onPath.Source
        } else {
            throw 'adb not found. Pass -Adb <path> or install Android platform-tools.'
        }
    }
}
$Adb = (Resolve-Path -LiteralPath $Adb).Path

function Save-Text {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [AllowEmptyString()][string]$Text
    )
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Invoke-Adb {
    param([Parameter(Mandatory = $true)][string[]]$AdbArgs)
    # Stringify each record separately: Out-String would render stderr as a
    # PowerShell NativeCommandError stack trace instead of the adb message.
    $lines = & $Adb @AdbArgs 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $text = ($lines -join "`r`n")
    if ($text.Length -gt 0) {
        $text += "`r`n"
    }
    return [pscustomobject]@{
        Text = $text
        Code = $code
    }
}

function Add-Note {
    param([string]$Line)
    Write-Host "collect-first-boot: $Line"
}

$serial = "${VitaIP}:5555"
Add-Note "adb:    $Adb"
Add-Note "target: $serial"
Add-Note "output: $OutDir"

$version = Invoke-Adb -AdbArgs @('version')
Save-Text -Path (Join-Path $OutDir 'adb-version.txt') -Text $version.Text

$connect = Invoke-Adb -AdbArgs @('connect', $serial)
Add-Note "connect exit $($connect.Code): $($connect.Text.Trim())"

$state = Invoke-Adb -AdbArgs @('-s', $serial, 'get-state')
Save-Text -Path (Join-Path $OutDir 'adb-state.txt') -Text $state.Text

if ($state.Code -ne 0 -or $state.Text.Trim() -ne 'device') {
    $connectReport = @(
        "adb connect exit: $($connect.Code)"
        $connect.Text.Trim()
        "adb get-state exit: $($state.Code)"
        $state.Text.Trim()
    ) -join "`r`n"
    Save-Text -Path (Join-Path $OutDir 'connect.txt') -Text $connectReport
    Write-Warning "adb cannot reach $serial. Check the Vita IP, Wi-Fi association, and that adbd is running."
    Add-Note "partial report: $OutDir"
    exit 1
}

$commands = @(
    @{ Name = 'getprop.txt';        Args = @('getprop') },
    @{ Name = 'meminfo.txt';        Args = @('cat', '/proc/meminfo') },
    @{ Name = 'uptime.txt';         Args = @('cat', '/proc/uptime') },
    @{ Name = 'dmesg.txt';          Args = @('dmesg') },
    @{ Name = 'logcat.txt';         Args = @('logcat', '-d') },
    @{ Name = 'ip-addr.txt';        Args = @('ip', 'addr') },
    @{ Name = 'ip-route.txt';       Args = @('ip', 'route') },
    @{ Name = 'ifconfig.txt';       Args = @('/sbin/busybox', 'ifconfig') },
    @{ Name = 'route-proc.txt';     Args = @('cat', '/proc/net/route') },
    @{ Name = 'wireless.txt';       Args = @('cat', '/proc/net/wireless') },
    @{ Name = 'mount.txt';          Args = @('mount') },
    @{ Name = 'df.txt';             Args = @('df') },
    @{ Name = 'ps.txt';             Args = @('ps') },
    @{ Name = 'cmdline.txt';        Args = @('cat', '/proc/cmdline') },
    @{ Name = 'devices.txt';        Args = @('ls', '-l', '/dev/binder', '/dev/ashmem', '/dev/sw_sync', '/dev/graphics/fb0') }
)

foreach ($item in $commands) {
    $adbArgs = @('-s', $serial, 'exec-out') + $item.Args
    $result = Invoke-Adb -AdbArgs $adbArgs
    Save-Text -Path (Join-Path $OutDir $item.Name) -Text $result.Text
    Add-Note ("{0,-16} exit {1}" -f $item.Name, $result.Code)
}

# Files on ur0 survive a reboot and are the only diagnostics when adbd or the
# network never start.  They are readable from Android because stage 1 mounts
# ur0 at /mnt/ur0 and that mount survives Android init.
$pulls = @(
    @{ Remote = '/mnt/ur0/android/boot.log';                 Name = 'ur0-boot.log' },
    @{ Remote = '/mnt/ur0/android/vita-net.log';             Name = 'ur0-vita-net.log' },
    @{ Remote = '/mnt/ur0/android/wpa_supplicant.log';       Name = 'ur0-wpa_supplicant.log' },
    @{ Remote = '/mnt/ur0/android/vita-net.prev.log';        Name = 'ur0-vita-net.prev.log' },
    @{ Remote = '/mnt/ur0/android/wpa_supplicant.prev.log';  Name = 'ur0-wpa_supplicant.prev.log' },
    @{ Remote = '/mnt/ur0/android/wpa_supplicant.normal.log'; Name = 'ur0-wpa_supplicant.normal.log' },
    @{ Remote = '/mnt/ur0/android/wpa_supplicant.crash.log';  Name = 'ur0-wpa_supplicant.crash.log' },
    @{ Remote = '/data/misc/wifi/vita-net.log';              Name = 'data-vita-net.log' },
    @{ Remote = '/data/misc/wifi/wpa_supplicant.log';        Name = 'data-wpa_supplicant.log' }
)

foreach ($item in $pulls) {
    $dest = Join-Path $OutDir $item.Name
    if (Test-Path -LiteralPath $dest) {
        Remove-Item -LiteralPath $dest -Force
    }
    $result = Invoke-Adb -AdbArgs @('-s', $serial, 'pull', $item.Remote, $dest)
    if ($result.Code -eq 0 -and (Test-Path -LiteralPath $dest)) {
        Add-Note ("pulled {0}" -f $item.Remote)
    } else {
        if (Test-Path -LiteralPath $dest) {
            Remove-Item -LiteralPath $dest -Force
        }
        Save-Text -Path ($dest + '.missing.txt') -Text ("adb pull failed for {0}`r`n{1}" -f $item.Remote, $result.Text)
        Add-Note ("MISSING {0}" -f $item.Remote)
    }
}

foreach ($name in @('SHA256SUMS', 'build-info.txt', 'README.txt')) {
    $src = Join-Path $repoRoot "build\first-boot-kit\$name"
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination $OutDir -Force
    }
}

$selected = @()
$getpropPath = Join-Path $OutDir 'getprop.txt'
if (Test-Path -LiteralPath $getpropPath) {
    $props = Get-Content -LiteralPath $getpropPath
    $keys = @(
        'ro.build.version.release',
        'ro.product.model',
        'ro.secure',
        'ro.adb.secure',
        'ro.debuggable',
        'ro.boot.selinux',
        'service.adb.tcp.port',
        'ro.boottime.adbd',
        'init.svc.adbd',
        'init.svc.zygote',
        'init.svc.surfaceflinger',
        'init.svc.healthd',
        'init.svc.servicemanager',
        'init.svc.lmkd',
        'init.svc.vold',
        'init.svc.netd'
    )
    foreach ($key in $keys) {
        $pattern = '^\[' + [regex]::Escape($key) + '\]: \[(.*)\]$'
        $match = $props | Where-Object { $_ -match $pattern } | Select-Object -First 1
        if ($match) {
            $selected += $match
        } else {
            $selected += "[$key]: (not set)"
        }
    }
}

$uptime = ''
$uptimePath = Join-Path $OutDir 'uptime.txt'
if (Test-Path -LiteralPath $uptimePath) {
    $uptime = (Get-Content -LiteralPath $uptimePath -Raw).Trim()
}

$summary = @(
    'AndroidOnVita first-boot report'
    '================================'
    ''
    "collected (local): $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
    "vita ip: $VitaIP"
    "serial: $serial"
    "adb: $Adb"
    "adb state: $($state.Text.Trim())"
    "adb connect exit: $($connect.Code)"
    "adb connect output: $($connect.Text.Trim())"
    "linux uptime at collection: $uptime"
    ''
    'selected properties:'
) + $selected + @(
    ''
    'files:'
) + (Get-ChildItem -LiteralPath $OutDir -File |
        Sort-Object Name |
        ForEach-Object { "  {0}  {1} bytes" -f $_.Name, $_.Length })

Save-Text -Path (Join-Path $OutDir 'summary.txt') -Text ($summary -join "`r`n")

if (-not $NoZip) {
    $zip = $OutDir + '.zip'
    if (Test-Path -LiteralPath $zip) {
        Remove-Item -LiteralPath $zip -Force
    }
    Compress-Archive -Path (Join-Path $OutDir '*') -DestinationPath $zip -CompressionLevel Optimal
    Add-Note "zip:    $zip"
}

Add-Note "report: $OutDir"
