<#
.SYNOPSIS
  Run every probe exe on this Windows host and collect the recordings ("recs") in one output directory.
.DESCRIPTION
  Each <name>.exe in -Dir is run with stdout -> <Out>\<name>.rec and stderr -> <name>.err, bounded by -TimeoutSec.
  Writes _env.rec (host/OS/DPI facts, tag RUNALL), _status.rec (one RC line per exe) and MANIFEST.txt (sha256 of exes and recs).
  The directory can then be fed to tools/compare_metrics.py. Works on Windows PowerShell 5.1 and PowerShell 7.
  Probes must not require input; they are killed (and the whole tree is not left running) after the timeout.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\run_all.ps1 -Dir build -Out recs\this-host
  powershell -ExecutionPolicy Bypass -File scripts\run_all.ps1 -Dir build,tools\probes -Include tm_*,np_* -TimeoutSec 120
#>
param(
  [string[]]$Dir = @("$PSScriptRoot\..\build"),
  [string]$Out = "",
  [string[]]$Include = @("*"),
  [string[]]$Exclude = @(),
  [int]$TimeoutSec = 300,
  [switch]$Destructive,
  [string[]]$ProbeArgs = @()
)
$ErrorActionPreference = "Continue"
$host_ = $env:COMPUTERNAME
$os = [Environment]::OSVersion.Version
if (-not $Out) { $Out = Join-Path (Join-Path $PSScriptRoot "..\recs") ("{0}-{1}-{2}" -f $host_, $os.Build, (Get-Date -Format "yyyyMMdd-HHmmss")) }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path

function Sha($p) { (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLower() }

# ---- environment record (tag RUNALL is ignored by compare_metrics.py)
$dpi = "?"
try {
  Add-Type -AssemblyName System.Drawing -ErrorAction Stop
  $g = [System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero); $dpi = "{0}x{1}" -f $g.DpiX, $g.DpiY; $g.Dispose()
} catch { }
$env_ = @()
$env_ += "RUNALL host=$host_ user=$env:USERNAME os=$([Environment]::OSVersion.VersionString) build=$($os.Build) arch=$env:PROCESSOR_ARCHITECTURE ps=$($PSVersionTable.PSVersion) dpi=$dpi"
try { $ci = Get-CimInstance Win32_OperatingSystem; $env_ += "RUNALL caption=$($ci.Caption) version=$($ci.Version) ubr=$((Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').UBR)" } catch { }
try { $fc = (Get-ChildItem "$env:WINDIR\Fonts" -ErrorAction Stop | Measure-Object).Count; $env_ += "RUNALL fonts_dir_files=$fc" } catch { }
$env_ | Set-Content -Encoding ASCII (Join-Path $Out "_env.rec")

$exes = @()
foreach ($d in $Dir) {
  if (Test-Path $d) { $exes += Get-ChildItem -LiteralPath $d -Filter *.exe -File | Sort-Object Name }
  else { Write-Warning "no such dir $d" }
}
$status = New-Object System.Collections.ArrayList
$man = New-Object System.Collections.ArrayList
$nfail = 0
foreach ($e in $exes) {
  $n = [IO.Path]::GetFileNameWithoutExtension($e.Name)
  if (-not ($Include | Where-Object { $n -like $_ })) { continue }
  if ($Exclude | Where-Object { $n -like $_ }) { continue }
  if ($n -like "dx_*" -and -not $Destructive) { [void]$status.Add("RUNALL rc name=$n rc=skipped reason=destructive"); continue }
  $recf = Join-Path $Out "$n.rec"; $errf = Join-Path $Out "$n.err"
  $t0 = Get-Date
  Write-Host "=== $n"
  $a = @{ FilePath = $e.FullName; RedirectStandardOutput = $recf; RedirectStandardError = $errf; PassThru = $true; NoNewWindow = $true; WorkingDirectory = $Out }
  if ($ProbeArgs.Count) { $a.ArgumentList = $ProbeArgs }
  $p = Start-Process @a
  $null = $p.Handle   # keep the handle: without it PS 5.1 returns a null ExitCode for redirected Start-Process
  $rc = "timeout"
  if ($p.WaitForExit($TimeoutSec * 1000)) { $p.WaitForExit(); $rc = $p.ExitCode } else { try { $p.Kill() } catch { } ; $nfail++ }
  if ($rc -ne 0 -and $rc -ne "timeout") { $nfail++ }
  $sec = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
  $lines = 0; if (Test-Path $recf) { $lines = @(Get-Content -LiteralPath $recf).Count }
  [void]$status.Add("RUNALL rc name=$n rc=$rc seconds=$sec lines=$lines exe_sha256=$(Sha $e.FullName)")
  Write-Host ("    rc={0} {1}s {2} lines" -f $rc, $sec, $lines)
}
$status | Set-Content -Encoding ASCII (Join-Path $Out "_status.rec")
Get-ChildItem -LiteralPath $Out -File | Where-Object { $_.Name -ne "MANIFEST.txt" } | Sort-Object Name |
  ForEach-Object { "{0}  {1,9}  {2}" -f (Sha $_.FullName), $_.Length, $_.Name } | Set-Content -Encoding ASCII (Join-Path $Out "MANIFEST.txt")
Write-Host "recs in $Out ($($status.Count) probes, $nfail failed/timeout)"
if ($nfail) { exit 1 } else { exit 0 }
