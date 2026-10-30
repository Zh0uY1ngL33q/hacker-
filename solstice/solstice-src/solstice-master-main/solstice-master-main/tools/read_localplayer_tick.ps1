param(
    [string]$LogPath = "$env:LOCALAPPDATA\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\RoamingState\Solstice\solstice.log",
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LocalPlayerReadOnly {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int id);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, UIntPtr size, out UIntPtr read);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool CloseHandle(IntPtr handle);
}
'@
function Read-Pointer([IntPtr]$Handle, [UInt64]$Address) {
    $buffer = New-Object byte[] 8
    $count = [UIntPtr]::Zero
    if (-not [LocalPlayerReadOnly]::ReadProcessMemory($Handle, [IntPtr][Int64]$Address, $buffer, [UIntPtr]::new([UInt64]8), [ref]$count) -or $count.ToUInt64() -ne 8) {
        throw "ReadProcessMemory failed at 0x$($Address.ToString('X')); Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    return [BitConverter]::ToUInt64($buffer, 0)
}
if ([IntPtr]::Size -ne 8) { throw 'Run 64-bit PowerShell.' }
if ($SelfTest) {
    $memory = [Runtime.InteropServices.Marshal]::AllocHGlobal(8)
    $handle = [LocalPlayerReadOnly]::OpenProcess(0x410, $false, $PID)
    try {
        if ($handle -eq [IntPtr]::Zero) { throw 'Cannot open the current process for self-test.' }
        [Runtime.InteropServices.Marshal]::WriteInt64($memory, 0x12345678)
        if ((Read-Pointer $handle ([UInt64]$memory.ToInt64())) -ne 0x12345678) { throw 'Self-test mismatch.' }
        Write-Output 'Self-test passed: read-only pointer reader.'
    } finally {
        if ($handle -ne [IntPtr]::Zero) { [void][LocalPlayerReadOnly]::CloseHandle($handle) }
        [Runtime.InteropServices.Marshal]::FreeHGlobal($memory)
    }
    exit 0
}
$log = Get-Content -LiteralPath $LogPath -Raw
$hostMatches = [regex]::Matches($log, 'host process=(\d+)')
$playerMatches = [regex]::Matches($log, 'localplayer addr\s*@\s*0x([0-9a-fA-F]+)')
if (!$hostMatches.Count -or !$playerMatches.Count) { throw 'Current log lacks host PID or localplayer address; enter a world and inject before running.' }
$gamePid = [int]$hostMatches[$hostMatches.Count - 1].Groups[1].Value
$player = [Convert]::ToUInt64($playerMatches[$playerMatches.Count - 1].Groups[1].Value, 16)
$process = Get-Process -Id $gamePid
if ($process.ProcessName -ne 'Minecraft.Windows') { throw "Log PID $gamePid is not Minecraft.Windows; do not use a stale log." }
if ($process.StartTime -gt (Get-Item -LiteralPath $LogPath).LastWriteTime) { throw 'Log predates the current process; inject again and use the current log.' }
$module = $process.MainModule
if ([IO.Path]::GetFileName($module.FileName) -ine 'Minecraft.Windows.exe') { throw 'Unexpected main module.' }
$base = [UInt64]$module.BaseAddress.ToInt64()
$handle = [LocalPlayerReadOnly]::OpenProcess(0x410, $false, $gamePid)
if ($handle -eq [IntPtr]::Zero) { throw "OpenProcess failed; Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
try {
    $table = Read-Pointer $handle $player
    $target = Read-Pointer $handle ($table + 29 * 8)
    $end = $base + [UInt64]$module.ModuleMemorySize
    $tableRva = if ($table -ge $base -and $table -lt $end) { '0x{0:X}' -f ($table - $base) } else { 'outside main image' }
    $targetRva = if ($target -ge $base -and $target -lt $end) { '0x{0:X}' -f ($target - $base) } else { 'outside main image' }
    Write-Output ("PID={0} LocalPlayer=0x{1:X} vtable=0x{2:X} (RVA {3}) slot29=0x{4:X} (RVA {5})" -f $gamePid,$player,$table,$tableRva,$target,$targetRva)
    Write-Output 'Read-only snapshot; a stale object may yield misleading values. No game memory was changed.'
} finally { [void][LocalPlayerReadOnly]::CloseHandle($handle) }
