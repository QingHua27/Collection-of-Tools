# Opt-in: requires the user's authorization to test live ACE scheduling.
param([string]$Exe = "$PSScriptRoot\..\dist\CppToolbox.exe", [switch]$RestrictedLaunch)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class AceUiTest {
 [StructLayout(LayoutKind.Sequential)] public struct Luid { public uint Low; public int High; }
 [StructLayout(LayoutKind.Sequential)] public struct TokenPrivilege { public uint Count; public Luid Id; public uint Attributes; }
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint access,out IntPtr token);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode)] static extern bool LookupPrivilegeValue(string system,string name,out Luid id);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AdjustTokenPrivileges(IntPtr token,bool all,ref TokenPrivilege value,uint length,out TokenPrivilege old,out uint returned);
 [DllImport("advapi32.dll",EntryPoint="AdjustTokenPrivileges",SetLastError=true)] static extern bool RestoreTokenPrivileges(IntPtr token,bool all,ref TokenPrivilege value,uint length,IntPtr old,IntPtr returned);
 static IntPtr parentToken; static TokenPrivilege oldPrivilege;
 public static void BeginRestrictedLaunch() {
   if(!OpenProcessToken(GetCurrentProcess(),0x28,out parentToken))throw new Exception("parent token unavailable");
   Luid id; LookupPrivilegeValue(null,"SeDebugPrivilege",out id);
   TokenPrivilege value=new TokenPrivilege{Count=1,Id=id,Attributes=0}; uint returned;
   if(!AdjustTokenPrivileges(parentToken,false,ref value,(uint)Marshal.SizeOf<TokenPrivilege>(),out oldPrivilege,out returned)||Marshal.GetLastWin32Error()!=0)throw new Exception("parent debug privilege could not be disabled");
 }
 public static void EndRestrictedLaunch() {
   if(parentToken!=IntPtr.Zero){if(oldPrivilege.Count!=0)RestoreTokenPrivileges(parentToken,false,ref oldPrivilege,0,IntPtr.Zero,IntPtr.Zero);CloseHandle(parentToken);parentToken=IntPtr.Zero;}
 }
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder text,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,StringBuilder text);
 [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
 [DllImport("kernel32.dll")] public static extern uint GetPriorityClass(IntPtr p);
 [DllImport("kernel32.dll")] public static extern bool GetProcessInformation(IntPtr p,int c,out uint value,uint length);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr p);
 [DllImport("ntdll.dll")] public static extern int NtQueryInformationProcess(IntPtr p,int c,out uint value,uint length,out uint returned);
 public static string Text(IntPtr h) { var text=new StringBuilder(32768); SendMessage(h,0xD,(IntPtr)text.Capacity,text); return text.ToString(); }
}
'@
function Command([int]$id) { [void][AceUiTest]::PostMessage($script:window,0x111,[IntPtr]$id,[IntPtr]::Zero) }
function Priorities {
    $result=@{}
    Get-Process -Name 'SGuard64','SGuardSvc64','ACE-Service64','ACE-Service','ACE-Tray' -ErrorAction SilentlyContinue | ForEach-Object {
        $handle=[AceUiTest]::OpenProcess(0x400,$false,$_.Id)
        if($handle -ne [IntPtr]::Zero) {
            [uint32]$io=0; [uint32]$returned=0
            $status=[AceUiTest]::NtQueryInformationProcess($handle,33,[ref]$io,4,[ref]$returned)
            [uint32]$memory=0
            $memoryKnown=[AceUiTest]::GetProcessInformation($handle,0,[ref]$memory,4)
            $result[$_.Id]=@{cpu=[AceUiTest]::GetPriorityClass($handle);io=$io;ioStatus=$status;memory=$memory;memoryKnown=$memoryKnown;name=$_.ProcessName}
            [void][AceUiTest]::CloseHandle($handle)
        }
    }
    return $result
}
try {
    if($RestrictedLaunch) { [AceUiTest]::BeginRestrictedLaunch(); Write-Output 'Launching from administrator token with SeDebugPrivilege disabled.' }
    $process=Start-Process -FilePath $Exe -ArgumentList '--ace-page' -PassThru
} finally { if($RestrictedLaunch) { [AceUiTest]::EndRestrictedLaunch() } }
$window=[IntPtr]::Zero
for($attempt=0;$attempt -lt 25;$attempt++) {
    Start-Sleep -Milliseconds 200; $process.Refresh()
    if($process.HasExited) { throw 'Toolbox already running or failed to initialize; live UI test did not take ownership.' }
    $window=$process.MainWindowHandle
    if($window -ne [IntPtr]::Zero) { break }
}
if($window -eq [IntPtr]::Zero) { throw 'Toolbox window not found' }
$page=[AceUiTest]::GetDlgItem($window,205)
$details=[AceUiTest]::GetDlgItem($page,2502)
Start-Sleep -Milliseconds 3500
$initiallyEnabled=[AceUiTest]::Text([AceUiTest]::GetDlgItem($page,350)).StartsWith('停止')
try {
    if($initiallyEnabled) { Command 350; Start-Sleep -Milliseconds 800 }
    $before=Priorities
    if($before.Count -eq 0) { throw 'No accessible live ACE/SGuard process to test' }
    Write-Output 'BEFORE'
    [AceUiTest]::Text($details)
    Command 350; Start-Sleep -Milliseconds 3500
    Write-Output 'ENABLED'
    $enabled=[AceUiTest]::Text($details); $enabled
    if(!$enabled.Contains('低优先级已确认')) { throw 'UI did not confirm any applied priority' }
    if(!$enabled.Contains('低页优先级已确认')) { throw 'UI did not confirm any memory page priority' }
    $during=Priorities
    $applied=0
    foreach($pidValue in $before.Keys) {
        if($during.ContainsKey($pidValue)) { Write-Output "Independent readback PID=$pidValue CPU=$($during[$pidValue].cpu) IO=$($during[$pidValue].io) MemoryPage=$($during[$pidValue].memory)" }
        if($during.ContainsKey($pidValue) -and $during[$pidValue].cpu -in @(0x4000,0x40) -and $during[$pidValue].ioStatus -ge 0 -and $during[$pidValue].io -le 1 -and $during[$pidValue].memoryKnown -and $during[$pidValue].memory -le 4) { $applied++ }
    }
    if(!$applied) { throw 'Independent API readback did not confirm CPU, I/O and memory page policy' }
    Command 350; Start-Sleep -Milliseconds 1000
    Write-Output 'RESTORED'
    [AceUiTest]::Text($details)
    $after=Priorities
    foreach($pidValue in $before.Keys) {
        if(!$after.ContainsKey($pidValue)) { continue }
        if($before[$pidValue].cpu -ne $after[$pidValue].cpu -or ($before[$pidValue].ioStatus -ge 0 -and $before[$pidValue].io -ne $after[$pidValue].io) -or ($before[$pidValue].memoryKnown -and $before[$pidValue].memory -ne $after[$pidValue].memory)) { throw "Original priority was not restored for PID $pidValue" }
    }
    Write-Output "PASS native ACE UI applied and restored policy; independently verified $applied live processes."
} finally {
    $currentlyEnabled=[AceUiTest]::Text([AceUiTest]::GetDlgItem($page,350)).StartsWith('停止')
    if($currentlyEnabled -ne $initiallyEnabled) { Command 350; Start-Sleep -Milliseconds 700 }
    Command 1203
    if(!$process.WaitForExit(15000)) { throw 'Toolbox did not finish graceful shutdown' }
}
