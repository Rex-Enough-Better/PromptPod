param(
    [switch]$Bootstrap
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# 0. Deterministic version-sync gate (VERSION == src/version.h numeric+string macros == src/app.manifest)
$verFile = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()
if ($verFile -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
    throw "Invalid VERSION format '$verFile' (expected X.Y.Z)."
}
$vMaj, $vMin, $vPat = $Matches[1], $Matches[2], $Matches[3]
$verHdr = Get-Content (Join-Path $root "src\version.h") -Raw
$verMan = Get-Content (Join-Path $root "src\app.manifest") -Raw
if ($verHdr -notmatch ('#define\s+APP_VER_MAJOR\s+' + $vMaj + '\b') -or
    $verHdr -notmatch ('#define\s+APP_VER_MINOR\s+' + $vMin + '\b') -or
    $verHdr -notmatch ('#define\s+APP_VER_PATCH\s+' + $vPat + '\b') -or
    $verHdr -notmatch [regex]::Escape("`"${verFile}`"") -or
    $verHdr -notmatch [regex]::Escape("L`"${verFile}`"")) {
    throw "Version mismatch: VERSION is '$verFile' but src\version.h macros do not match."
}
if ($verMan -notmatch [regex]::Escape("version=`"${verFile}.0`"")) {
    throw "Version mismatch: VERSION is '$verFile' but src\app.manifest does not have version=`"${verFile}.0`"."
}

# 1. Locate MinGW-w64 toolchain (PATH first, then user-level persistent cache, then project-local .tmp/toolchain)
$userCacheDir = Join-Path $env:LOCALAPPDATA "PromptPod-Toolchain"
$userCacheBin = Join-Path $userCacheDir "w64devkit\bin"
$localBin = Join-Path $root ".tmp\toolchain\w64devkit\bin"
$gppCmd = Get-Command "g++.exe" -ErrorAction SilentlyContinue
if (-not $gppCmd) {
    foreach ($candidate in @($userCacheBin, $localBin)) {
        if (Test-Path (Join-Path $candidate "g++.exe")) {
            $env:PATH = "$candidate;$env:PATH"
            $gppCmd = Get-Command "g++.exe" -ErrorAction SilentlyContinue
            break
        }
    }
}

if (-not $gppCmd) {
    if ($Bootstrap) {
        Write-Host "[PromptPod] Bootstrapping portable w64devkit into $userCacheDir (16-stream parallel)..."
        $dlStage = Join-Path $userCacheDir ("dl_" + [Guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $dlStage | Out-Null
        $url = "https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe"
        $sfx = Join-Path $dlStage "w64devkit.7z.exe"
        $total = 68801240
        $parts = 16
        $chunk = [Math]::Ceiling($total / $parts)

        try {
            0..($parts - 1) | ForEach-Object -Parallel {
                $idx = $_
                $start = $idx * $using:chunk
                $end = [Math]::Min($using:total - 1, ($idx + 1) * $using:chunk - 1)
                $expectedLen = $end - $start + 1
                $partFile = Join-Path $using:dlStage ("part_{0:D2}.bin" -f $idx)
                $ok = $false
                for ($attempt = 1; $attempt -le 5; $attempt++) {
                    $code = & curl.exe -L -s --max-time 25 --retry 2 -w "%{http_code}" -r "$start-$end" -o $partFile $using:url
                    if ($LASTEXITCODE -eq 0 -and $code -eq "206" -and (Test-Path $partFile) -and ((Get-Item $partFile).Length -eq $expectedLen)) {
                        $ok = $true
                        break
                    }
                    Remove-Item $partFile -Force -ErrorAction SilentlyContinue
                }
                if (-not $ok) { throw "Failed to download w64devkit part $idx (bytes $start-$end)" }
            } -ThrottleLimit 16

            $outStream = [System.IO.File]::Create($sfx)
            try {
                for ($i = 0; $i -lt $parts; $i++) {
                    $pf = Join-Path $dlStage ("part_{0:D2}.bin" -f $i)
                    $bytes = [System.IO.File]::ReadAllBytes($pf)
                    $outStream.Write($bytes, 0, $bytes.Length)
                }
            } finally {
                $outStream.Close()
            }
            if ((Get-Item $sfx).Length -ne $total) {
                throw "Downloaded w64devkit SFX size mismatch: $((Get-Item $sfx).Length) != $total"
            }
            & $sfx -y "-o$userCacheDir" | Out-Null
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $userCacheBin "g++.exe"))) {
                throw "w64devkit extraction failed."
            }
            $env:PATH = "$userCacheBin;$env:PATH"
        } finally {
            Remove-Item -Recurse -Force $dlStage -ErrorAction SilentlyContinue
        }
    } else {
        throw "g++.exe not found. Run .\build.ps1 -Bootstrap to fetch portable w64devkit."
    }
}

# 2. Ensure icon resource exists and is up to date with generate_icon.ps1
$icoPath = Join-Path $root "assets\app.ico"
$icoGen  = Join-Path $root "assets\generate_icon.ps1"
if ((-not (Test-Path $icoPath)) -or ((Get-Item $icoGen).LastWriteTimeUtc -gt (Get-Item $icoPath).LastWriteTimeUtc)) {
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $icoGen
    if ($LASTEXITCODE -ne 0) { throw "Icon generation failed with code $LASTEXITCODE" }
}

# 3. Prepare output & temporary build folders
$distDir = Join-Path $root "dist"
$tmpBuild = Join-Path $root ".tmp\build"
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
New-Item -ItemType Directory -Force -Path $tmpBuild | Out-Null

$resObj = Join-Path $tmpBuild "app.res"
$stageExe = Join-Path $tmpBuild "PromptPod.exe"
$outExe = Join-Path $distDir "PromptPod.exe"

# Detect current Win32 desktop name (so sandboxed non-'Default' shells don't kill/relaunch onto an invisible desktop)
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class PpDesktopHelper {
    [DllImport("user32.dll")] public static extern IntPtr GetThreadDesktop(uint dwThreadId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool GetUserObjectInformationW(IntPtr hObj, int nIndex, StringBuilder pvInfo, int nLength, out int lpnLengthNeeded);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, IntPtr title);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
    public static string GetDesktopName() {
        IntPtr hDesk = GetThreadDesktop(GetCurrentThreadId());
        var sb = new StringBuilder(256);
        int needed = 0;
        GetUserObjectInformationW(hDesk, 2, sb, 256, out needed);
        return sb.ToString();
    }
}
"@ -ErrorAction SilentlyContinue
$curDesktop = [PpDesktopHelper]::GetDesktopName()
$isInteractiveDefaultDesktop = ($curDesktop -ieq "Default")

# 4. Compile Win32 resources (UTF-8 codepage 65001) & standalone C++17 binary into staging path first
Push-Location (Join-Path $root "src")
try {
    & windres.exe -c 65001 -O coff "app.rc" -o $resObj
    if ($LASTEXITCODE -ne 0) { throw "windres failed with code $LASTEXITCODE" }

    & g++.exe `
        -std=c++17 -Os -s -DNDEBUG -fno-ident -Wall -Wextra -Wno-missing-field-initializers `
        -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE `
        -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections "-Wl,--gc-sections" `
        -static -static-libgcc -static-libstdc++ `
        -municode -mwindows `
        "main.cpp" $resObj `
        -o $stageExe `
        -ld2d1 -ldwrite -ldwmapi -lole32 -lshell32 -lcomdlg32 -luuid -lgdi32 -luser32 -ladvapi32 -lwinhttp -lbcrypt
    if ($LASTEXITCODE -ne 0) { throw "g++ compilation failed with code $LASTEXITCODE" }
} finally {
    Pop-Location
}

# 5. Hard gates on staged binary: size (< 600 KB), PE version, and strict Windows native DLL whitelist
$sizeBytes = (Get-Item $stageExe).Length
$sizeKB = [Math]::Round($sizeBytes / 1024.0, 1)
if ($sizeBytes -ge 614400) {
    throw "Size gate failed: staged binary is $sizeKB KB ($sizeBytes bytes), exceeding 600 KB limit!"
}

$fvi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($stageExe)
if ($fvi.ProductVersion -ne $verFile -or $fvi.FileVersion -ne $verFile) {
    throw "PE version mismatch: expected '$verFile', got FileVersion='$($fvi.FileVersion)' ProductVersion='$($fvi.ProductVersion)'."
}

$objOut = & objdump.exe -p $stageExe
if ($LASTEXITCODE -ne 0 -or -not $objOut) {
    throw "objdump -p failed on staged binary."
}
$dllImports = @($objOut | Select-String "DLL Name:" | ForEach-Object { ($_.Line -replace ".*DLL Name:\s*", "").Trim() })
if ($dllImports.Count -eq 0) {
    throw "Import table audit failed: zero DLL imports parsed from objdump."
}
$allowedSystemDlls = @(
    "kernel32.dll", "msvcrt.dll", "user32.dll", "gdi32.dll",
    "shell32.dll", "ole32.dll", "d2d1.dll", "dwrite.dll",
    "dwmapi.dll", "advapi32.dll", "comdlg32.dll",
    "winhttp.dll", "bcrypt.dll"
)
Write-Host "[PromptPod] Linked system DLLs: $($dllImports -join ', ')"
foreach ($dll in $dllImports) {
    if ($allowedSystemDlls -notcontains $dll.ToLowerInvariant()) {
        throw "Forbidden non-whitelisted DLL dependency detected: $dll"
    }
}

# 6. Run deterministic headless self-test on staged binary (with safe quoting & timeout)
$selfTestDir = Join-Path $tmpBuild "selftest"
New-Item -ItemType Directory -Force -Path $selfTestDir | Out-Null

# 6a. Verify --self-test rejects empty directory argument cleanly (must return non-zero without touching user data)
$pEmpty = Start-Process -FilePath $stageExe -ArgumentList '--self-test ""' -PassThru
if (-not $pEmpty.WaitForExit(10000)) {
    $pEmpty.Kill()
    throw "Self-test empty-arg check timed out!"
}
if ($pEmpty.ExitCode -eq 0) {
    throw "Self-test empty-arg guard FAILED: returned 0 instead of rejecting empty path!"
}

# 6b. Run full deterministic headless self-test
$quotedDir = '"' + $selfTestDir + '"'
$proc = Start-Process -FilePath $stageExe -ArgumentList "--self-test $quotedDir" -PassThru
if (-not $proc.WaitForExit(30000)) {
    $proc.Kill()
    throw "Headless self-test timed out after 30s!"
}
if ($proc.ExitCode -ne 0) {
    throw "Self-test failed with exit code $($proc.ExitCode)"
}
Write-Host "[PromptPod] Headless self-test PASSED (exit code 0, PE ProductName='$($fvi.ProductName)', v$verFile)."

# 7. Atomically update dist\PromptPod.exe (only stop running instance when on interactive Default desktop;
#    if running inside an isolated sandbox desktop, rename the old running binary so user's live UI stays alive!)
$wasRunning = $false
$oldBackupExe = Join-Path $tmpBuild "PromptPod.running.old.exe"
Get-ChildItem -Path (Join-Path $root ".tmp") -Filter "*.running.old.exe" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
$runningProcs = Get-Process -Name "PromptPod" -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $outExe }
if ($runningProcs) {
    $wasRunning = $true
    if ($isInteractiveDefaultDesktop) {
        $trayHwnd = [PpDesktopHelper]::FindWindowW("PromptPod_TrayHost", [IntPtr]::Zero)
        if ($trayHwnd -ne [IntPtr]::Zero) {
            [PpDesktopHelper]::PostMessageW($trayHwnd, 0x0010 <# WM_CLOSE #>, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        }
        Start-Sleep -Milliseconds 300
        Get-Process -Name "PromptPod" -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $outExe } | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 150
    } else {
        Move-Item -Path $outExe -Destination $oldBackupExe -Force -ErrorAction SilentlyContinue
    }
}
Copy-Item -Path $stageExe -Destination $outExe -Force
Write-Host "[PromptPod] Built: $outExe ($sizeKB KB / $sizeBytes bytes)"

# 8. Clean temporary build artifacts & flush Windows Explorer icon cache for updated exe
Remove-Item -Recurse -Force $tmpBuild -ErrorAction SilentlyContinue

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class PpShellIconRefresh {
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern void SHChangeNotify(int wEventId, uint uFlags, string dwItem1, string dwItem2);
    [DllImport("shell32.dll")]
    public static extern void SHChangeNotify(int wEventId, uint uFlags, IntPtr dwItem1, IntPtr dwItem2);
}
"@ -ErrorAction SilentlyContinue
[PpShellIconRefresh]::SHChangeNotify(0x00002000 <# SHCNE_UPDATEITEM #>, 0x0005 <# SHCNF_PATHW #>, $outExe, $null)
[PpShellIconRefresh]::SHChangeNotify(0x08000000 <# SHCNE_ASSOCCHANGED #>, 0x0000 <# SHCNF_IDLIST #>, [IntPtr]::Zero, [IntPtr]::Zero)
$ie4 = Join-Path $env:SystemRoot "System32\ie4uinit.exe"
if (Test-Path $ie4) { Start-Process -FilePath $ie4 -ArgumentList "-show" -WindowStyle Hidden }

if ($wasRunning) {
    if ($isInteractiveDefaultDesktop) {
        Start-Process -FilePath $outExe
        Write-Host "[PromptPod] Relaunched updated instance on Default desktop: $outExe"
    } else {
        Write-Host "[PromptPod] NOTE: Current shell is inside isolated desktop '$curDesktop' (not 'Default'). Please launch $outExe from your normal Windows desktop/Explorer so its UI renders on your visible screen."
    }
}
