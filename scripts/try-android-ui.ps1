<#
.SYNOPSIS
Start the stock Android framework on the Vita and capture what happens.

.DESCRIPTION
The headless image stops zygote and SurfaceFlinger at boot. Setting the
vita.ui property starts them without rebooting. This script clears logcat,
sets the property, waits, then saves the service state, logcat, dmesg and the
EGL/HWC module listing to build\ui-test\<timestamp>. It does not modify the
boot images or any file on the Vita.

If SurfaceFlinger hits its fatal "couldn't create EGLContext" check, Android
init may reboot the Vita after repeated crashes; the property is not
persistent, so the next boot is headless again.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\try-android-ui.ps1 -VitaIP 192.168.1.50

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\try-android-ui.ps1 -VitaIP 192.168.1.50 -WaitSeconds 25
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$VitaIP,

    [int]$WaitSeconds = 15,

    [string]$OutDir,

    [string]$Adb
)

$ErrorActionPreference = 'Continue'

try {
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
} catch {
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

if (-not $OutDir) {
    $OutDir = Join-Path $repoRoot "build\ui-test\$stamp"
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
    Write-Host "try-android-ui: $Line"
}

$serial = "${VitaIP}:5555"
Add-Note "adb:    $Adb"
Add-Note "target: $serial"
Add-Note "output: $OutDir"

$connect = Invoke-Adb -AdbArgs @('connect', $serial)
Add-Note "connect exit $($connect.Code): $($connect.Text.Trim())"

$state = Invoke-Adb -AdbArgs @('-s', $serial, 'get-state')
if ($state.Code -ne 0 -or $state.Text.Trim() -ne 'device') {
    Save-Text -Path (Join-Path $OutDir 'connect.txt') -Text ($connect.Text + $state.Text)
    Write-Warning "adb cannot reach $serial."
    exit 1
}

$pre = @(
    @{ Name = 'egl-modules.txt';   Args = @('ls', '-l', '/system/lib/egl', '/system/lib/hw') },
    @{ Name = 'egl-cfg.txt';       Args = @('cat', '/system/lib/egl/egl.cfg') },
    @{ Name = 'pre-getprop.txt';   Args = @('getprop') },
    @{ Name = 'pre-ps.txt';        Args = @('ps') },
    @{ Name = 'pre-id.txt';        Args = @('id') },
    @{ Name = 'pre-fb0.txt';       Args = @('ls', '-l', '/dev/graphics/fb0') },
    @{ Name = 'fb0-virtual.txt';   Args = @('cat', '/sys/class/graphics/fb0/virtual_size') },
    @{ Name = 'fb0-bpp.txt';       Args = @('cat', '/sys/class/graphics/fb0/bits_per_pixel') },
    @{ Name = 'pre-meminfo.txt';   Args = @('cat', '/proc/meminfo') },
    @{ Name = 'pre-uptime.txt';    Args = @('cat', '/proc/uptime') }
)
foreach ($item in $pre) {
    $result = Invoke-Adb -AdbArgs (@('-s', $serial, 'exec-out') + $item.Args)
    Save-Text -Path (Join-Path $OutDir $item.Name) -Text $result.Text
}

# A clean buffer makes the first fatal line easy to find.
$null = Invoke-Adb -AdbArgs @('-s', $serial, 'logcat', '-b', 'all', '-c')

$set = Invoke-Adb -AdbArgs @('-s', $serial, 'shell', 'setprop', 'vita.ui', '1')
Add-Note "setprop vita.ui=1 exit $($set.Code)"
if ($set.Code -ne 0) {
    Save-Text -Path (Join-Path $OutDir 'setprop.txt') -Text $set.Text
    Write-Warning "Could not set vita.ui; is adbd still up?"
    exit 1
}

Add-Note "waiting $WaitSeconds s for zygote/SurfaceFlinger"
Start-Sleep -Seconds $WaitSeconds

$post = @(
    @{ Name = 'post-getprop.txt';  Args = @('getprop') },
    @{ Name = 'post-ps.txt';       Args = @('ps') },
    @{ Name = 'post-meminfo.txt';  Args = @('cat', '/proc/meminfo') },
    @{ Name = 'dmesg.txt';         Args = @('dmesg') },
    @{ Name = 'logcat.txt';        Args = @('logcat', '-d', '-b', 'all', '-v', 'threadtime') },
    @{ Name = 'dumpsys-sf.txt';    Args = @('dumpsys', 'SurfaceFlinger') }
)
foreach ($item in $post) {
    $result = Invoke-Adb -AdbArgs (@('-s', $serial, 'exec-out') + $item.Args)
    Save-Text -Path (Join-Path $OutDir $item.Name) -Text $result.Text
    Add-Note ("{0,-20} exit {1}" -f $item.Name, $result.Code)
}

# Snapshot the visible framebuffer.  Run dd on the device so the transfer is
# binary-safe (adb shell translates LF to CRLF; exec-out and pull do not).
# 960 * 544 * 4 = 2088960 bytes = 510 blocks of 4096.
$fbRemote = '/data/local/tmp/ui-fb0.raw'
$fbDump = Invoke-Adb -AdbArgs @('-s', $serial, 'shell', 'dd', 'if=/dev/graphics/fb0', "of=$fbRemote", 'bs=4096', 'count=510')
$fbLocal = Join-Path $OutDir 'fb0.raw'
$fbPull = Invoke-Adb -AdbArgs @('-s', $serial, 'pull', $fbRemote, $fbLocal)
if ($fbPull.Code -eq 0 -and (Test-Path -LiteralPath $fbLocal) -and (Get-Item -LiteralPath $fbLocal).Length -eq 2088960) {
    Add-Note ("fb0.raw              {0} bytes" -f (Get-Item -LiteralPath $fbLocal).Length)
} else {
    Add-Note 'fb0.raw              not captured'
    Save-Text -Path (Join-Path $OutDir 'fb0-error.txt') -Text ($fbDump.Text + $fbPull.Text)
}
$null = Invoke-Adb -AdbArgs @('-s', $serial, 'shell', 'rm', '-f', $fbRemote)

# Pull the synced ur0 snapshot too; it may be the only copy if the console
# reboots before the next adb command.
$bootLog = Join-Path $OutDir 'ur0-boot.log'
$null = Invoke-Adb -AdbArgs @('-s', $serial, 'pull', '/mnt/ur0/android/boot.log', $bootLog)

$props = @(Get-Content -LiteralPath (Join-Path $OutDir 'post-getprop.txt') -ErrorAction SilentlyContinue)
$selected = @()
foreach ($key in @(
        'init.svc.zygote', 'init.svc.surfaceflinger', 'init.svc.bootanim',
        'sys.boot_completed', 'vita.ui', 'ro.hardware',
        'ro.kernel.qemu', 'ro.kernel.qemu.gles', 'ro.sf.lcd_density',
        'dalvik.vm.heapsize', 'dalvik.vm.heapgrowthlimit')) {
    $pattern = '^\[' + [regex]::Escape($key) + '\]: \[(.*)\]$'
    $match = $props | Where-Object { $_ -match $pattern } | Select-Object -First 1
    if ($match) { $selected += $match } else { $selected += "[$key]: (not set)" }
}
Save-Text -Path (Join-Path $OutDir 'summary.txt') -Text (($selected -join "`r`n") + "`r`n")

$logcatPath = Join-Path $OutDir 'logcat.txt'
$interesting = @()
if (Test-Path -LiteralPath $logcatPath) {
    $interesting = Select-String -Path $logcatPath -Pattern `
        'FATAL|Fatal signal|ANR|system_server|SystemServer|ActivityManager|Launcher|Zygote|zygote|ExceptionInInitializer|Error preloading|Watchdog|Out of memory|OutOfMemory|lowmemorykiller|surfaceflinger|SurfaceFlinger|EGL|egl|qemu_pipe|goldfish|gralloc|hwcomposer|HWComposer' `
        -CaseSensitive:$false | ForEach-Object { $_.Line } | Select-Object -Last 60
}
Save-Text -Path (Join-Path $OutDir 'interesting.txt') -Text (($interesting -join "`r`n") + "`r`n")

Write-Host ''
Write-Host '--- service state ---'
$selected | ForEach-Object { Write-Host $_ }
Write-Host ''
Write-Host '--- first interesting log lines ---'
$interesting | Select-Object -First 40 | ForEach-Object { Write-Host $_ }
Write-Host ''
Add-Note "report: $OutDir"
