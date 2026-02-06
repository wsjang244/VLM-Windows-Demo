<#  Download-Hef.ps1
    Downloads a HEF file via curl.exe and verifies MD5.

    - Prints stage-by-stage status messages.
    - Pauses at the end (success or failure) until a key is pressed.
#>

[CmdletBinding()]
param(
  [string]$Url = "https://dev-public.hailo.ai/v5.2.0/blob/Qwen2-VL-2B-Instruct.hef",
  [string]$OutFile = (Join-Path (Get-Location) (Split-Path $Url -Leaf)),
  [string]$ExpectedMd5 = "BF1E5C174F17B609E05364B9EDE8E5C6",
  [int]$Retries = 5,
  [int]$RetryDelaySec = 2,
  [switch]$Force,

  # Safety check: this particular HEF is ~2.18GB; adjust if you use another file.
  [long]$MinBytes = 2000000000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Pause-AtEnd([string]$Message = "Press any key to exit...") {
  Write-Host ""
  Write-Host $Message
  try {
    # Works in the usual PowerShell host
    [void][System.Console]::ReadKey($true)
  } catch {
    # Fallback (rare hosts)
    Read-Host | Out-Null
  }
}

function Log([string]$msg) {
  $ts = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
  Write-Host "[$ts] $msg"
}

function Normalize-Md5([string]$s) {
  if ([string]::IsNullOrWhiteSpace($s)) { return "" }
  return ($s.Trim().Trim('"').ToLower())
}

function Get-FileMd5([string]$Path) {
  return (Get-FileHash -Algorithm MD5 -Path $Path).Hash.ToLower()
}

function Get-CurlHeaders([string]$u) {
  $out = & curl.exe -sS -I -L -f $u 2>$null
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($out)) { return @() }
  return $out -split "`r?`n"
}

function TryGet-Md5FromHeaders([string[]]$headers) {
  foreach ($line in $headers) {
    if ($line -match '^(?i)ETag:\s*(.+)\s*$') {
      $etag = $Matches[1].Trim()
      # Drop weak validator prefix if present: W/"..."
      $etag = $etag -replace '^(?i)W/',''
      $etag = $etag.Trim().Trim('"').ToLower()
      if ($etag -match '^[0-9a-f]{32}$') { return $etag }
    }
  }
  return ""
}

function TryGet-Md5FromSidecar([string]$sideUrl) {
  try {
    $text = & curl.exe -sS -L -f $sideUrl 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($text)) { return "" }
    if ($text -match '([0-9a-fA-F]{32})') { return (Normalize-Md5 $Matches[1]) }
    return ""
  } catch { return "" }
}

$exitCode = 1

try {
  Log "Stage 0: Environment checks"

  if (-not (Get-Command curl.exe -ErrorAction SilentlyContinue)) {
    throw "curl.exe was not found. Please use the built-in Windows curl.exe."
  }

  try {
    # PowerShell 5.1 sometimes needs explicit TLS 1.2
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  } catch { }

  Log "  URL     : $Url"
  Log "  OutFile : $OutFile"

  # --- Decide expected MD5 ---
  Log "Stage 1: Determine expected MD5 (parameter -> .md5/.md5sum -> ETag)"

  $expected = Normalize-Md5 $ExpectedMd5
  if (-not [string]::IsNullOrEmpty($expected)) {
    Log "  Using ExpectedMd5 from parameter: $expected"
  }

  if ([string]::IsNullOrEmpty($expected)) {
    Log "  Trying sidecar checksum: $($Url).md5"
    $expected = TryGet-Md5FromSidecar ($Url + ".md5")
    if (-not [string]::IsNullOrEmpty($expected)) {
      Log "  Found expected MD5 via .md5: $expected"
    }
  }

  if ([string]::IsNullOrEmpty($expected)) {
    Log "  Trying sidecar checksum: $($Url).md5sum"
    $expected = TryGet-Md5FromSidecar ($Url + ".md5sum")
    if (-not [string]::IsNullOrEmpty($expected)) {
      Log "  Found expected MD5 via .md5sum: $expected"
    }
  }

  if ([string]::IsNullOrEmpty($expected)) {
    Log "  Trying to infer from HTTP headers (ETag)"
    $hdr = Get-CurlHeaders $Url
    $expected = TryGet-Md5FromHeaders $hdr
    if (-not [string]::IsNullOrEmpty($expected)) {
      Log "  Found expected MD5 via ETag: $expected"
    }
  }

  if ([string]::IsNullOrEmpty($expected)) {
    Log "  Expected MD5 could not be determined automatically."
  }

  # --- If the file already exists and -Force is not set, validate first ---
  Log "Stage 2: Check existing file (if any)"

  if ((Test-Path $OutFile) -and (-not $Force)) {
    $len0 = (Get-Item $OutFile).Length
    Log "  Existing file detected. Size: $len0 bytes"

    if ($len0 -ge $MinBytes) {
      Log "  Calculating MD5 for existing file..."
      $md50 = Get-FileMd5 $OutFile
      Log "  Existing MD5: $md50"

      if (-not [string]::IsNullOrEmpty($expected) -and $md50 -eq $expected) {
        Log "Stage 2 result: OK (existing file MD5 matches expected)."
        $exitCode = 0
        throw [System.Exception]::new("__SUCCESS_EARLY_EXIT__")
      } else {
        Log "  Existing file does not match expected MD5 (or expected MD5 unknown). Will re-download."
      }
    } else {
      Log "  Existing file is too small (possible HTML/error page). Will re-download."
    }
  } else {
    Log "  No existing file to validate (or -Force is set)."
  }

  # --- Download to .part and resume if possible ---
  Log "Stage 3: Download (to .part, resume if possible)"

  $part = "$OutFile.part"
  if ($Force) {
    Log "  -Force specified. Removing existing output/partial files."
    Remove-Item -Force -ErrorAction SilentlyContinue $OutFile, $part | Out-Null
  }

  $dir = Split-Path -Parent $OutFile
  if ($dir -and -not (Test-Path $dir)) {
    Log "  Creating directory: $dir"
    New-Item -ItemType Directory -Path $dir | Out-Null
  }

  $curlArgs = @("-fL", "--retry", "$Retries", "--retry-delay", "$RetryDelaySec")

  if (Test-Path $part) {
    Log "  Partial file exists. Resuming download: $part"
    $curlArgs += @("-C", "-")
  } else {
    Log "  Starting fresh download."
  }

  $curlArgs += @("-o", $part, $Url)

  Log "  Running: curl.exe $($curlArgs -join ' ')"
  & curl.exe @curlArgs
  if ($LASTEXITCODE -ne 0) { throw "curl.exe download failed. exit=$LASTEXITCODE" }

  Log "  Download finished. Renaming .part -> final file"
  Move-Item -Force $part $OutFile

  # --- Post-download checks ---
  Log "Stage 4: Validate size and compute MD5"

  $len = (Get-Item $OutFile).Length
  Log "  Downloaded size: $len bytes"

  if ($len -lt $MinBytes) {
    throw "Downloaded file is smaller than expected: $len bytes (threshold $MinBytes). Possible HTML/error page."
  }

  Log "  Calculating MD5 for downloaded file..."
  $actualMd5 = Get-FileMd5 $OutFile
  Log "  Downloaded MD5: $actualMd5"

  # --- MD5 verification ---
  Log "Stage 5: MD5 verification"
  if (-not [string]::IsNullOrEmpty($expected)) {
    Log "  Expected MD5: $expected"
    if ($actualMd5 -ne $expected) {
      throw "MD5 mismatch: expected=$expected actual=$actualMd5"
    }
    Log "Stage 5 result: OK (MD5 match)."
    $exitCode = 0
  } else {
    Log "Stage 5 result: SKIPPED (no expected MD5 available)."
    Log "  Please compare this MD5 with the official value: $actualMd5"
    $exitCode = 2
  }

} catch {
  if ($_.Exception.Message -eq "__SUCCESS_EARLY_EXIT__") {
    # Already set exitCode=0 above
  } else {
    Write-Host ""
    Write-Host "ERROR:"
    Write-Host "  $($_.Exception.Message)"
    $exitCode = 1
  }
} finally {
  Write-Host ""
  Write-Host "ExitCode: $exitCode"
  Pause-AtEnd "Press any key to exit..."
  exit $exitCode
}
