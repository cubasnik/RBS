param(
    [string]$ConfigPath = '.\rbs.conf',
    [string]$BaseUrl = 'http://127.0.0.1:8181/api/v1',
    [string]$BuildConfig = 'Release',
    [int]$StartupTimeoutSec = 30,
    [switch]$StopExisting,
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$script:Results = @()
$script:StartedProc = $null
$script:OriginalConfig = $null
$script:ResolvedConfigPath = $null

function Add-Result {
    param(
        [string]$Option,
        [bool]$Ok,
        [string]$Details = ''
    )

    $script:Results += [PSCustomObject]@{
        Option  = $Option
        Ok      = $Ok
        Details = $Details
    }

    $tag = if ($Ok) { 'PASS' } else { 'FAIL' }
    $clr = if ($Ok) { 'Green' } else { 'Red' }
    if ([string]::IsNullOrWhiteSpace($Details)) {
        Write-Host "[$tag] Option $Option" -ForegroundColor $clr
    }
    else {
        Write-Host "[$tag] Option $Option :: $Details" -ForegroundColor $clr
    }
}

function Stop-StartedProcess {
    if ($script:StartedProc -and -not $script:StartedProc.HasExited) {
        try {
            Stop-Process -Id $script:StartedProc.Id -Force -ErrorAction SilentlyContinue
        }
        catch { }
    }
    $script:StartedProc = $null
}

function Wait-ApiReady {
    $deadline = (Get-Date).AddSeconds($StartupTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $null = Invoke-RestMethod -Uri "$BaseUrl/status" -Method GET -TimeoutSec 5
            return $true
        }
        catch {
            Start-Sleep -Milliseconds 500
        }
    }
    return $false
}

function Set-EndcOptionInConfig {
    param([string]$Option)

    $content = Get-Content $script:ResolvedConfigPath -Raw
    $updated = [regex]::Replace(
        $content,
        '(?im)^(\s*option\s*=\s*)(3a|3x|3)\s*(#.*)?$',
    "`$1$Option `$3",
        1
    )

    if ($updated -eq $content) {
        throw "Could not update [endc] option in config: $script:ResolvedConfigPath"
    }

    Set-Content $script:ResolvedConfigPath -Value $updated -NoNewline
}

function Start-RbsAll {
    Stop-StartedProcess

    $exe = Join-Path $PSScriptRoot "..\build\$BuildConfig\rbs_node.exe"
    $exe = [System.IO.Path]::GetFullPath($exe)

    if (-not (Test-Path $exe)) {
        throw "rbs_node.exe not found: $exe"
    }

    $script:StartedProc = Start-Process -FilePath $exe -ArgumentList @($script:ResolvedConfigPath) -PassThru -WindowStyle Minimized
    if (-not (Wait-ApiReady)) {
        throw "REST API was not ready within ${StartupTimeoutSec}s"
    }
}

function Normalize-EndcOption {
    param([object]$Raw)

    if ($null -eq $Raw) {
        return ''
    }

    $s = [string]$Raw
    $s = $s.Trim().ToLowerInvariant()
    $s = $s.Replace('option_', '')
    return $s
}

function Verify-Option {
    param([string]$ExpectedOption)

    try {
        $status = Invoke-RestMethod -Uri "$BaseUrl/status" -Method GET -TimeoutSec 5

        $enabled = $false
        if ($status.PSObject.Properties.Name -contains 'endcEnabled') {
            $enabled = [bool]$status.endcEnabled
        }

        $reported = ''
        if ($status.PSObject.Properties.Name -contains 'endcOption') {
            $reported = Normalize-EndcOption -Raw $status.endcOption
        }

        $rats = @()
        if ($status.PSObject.Properties.Name -contains 'rats') {
            $rats = @($status.rats)
        }

        $hasLte = $rats -contains 'LTE'
        $hasNr = $rats -contains 'NR'

        $ok = $enabled -and ($reported -eq $ExpectedOption) -and $hasLte -and $hasNr
        $details = "enabled=$enabled, option='$reported', rats=[{0}]" -f ($rats -join ',')
        Add-Result -Option $ExpectedOption -Ok $ok -Details $details
    }
    catch {
        Add-Result -Option $ExpectedOption -Ok $false -Details $_.Exception.Message
    }
}

try {
    $script:ResolvedConfigPath = [System.IO.Path]::GetFullPath($ConfigPath)
    if (-not (Test-Path $script:ResolvedConfigPath)) {
        throw "Config not found: $script:ResolvedConfigPath"
    }

    $script:OriginalConfig = Get-Content $script:ResolvedConfigPath -Raw

    if ($StopExisting) {
        Get-Process rbs_node -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }

    foreach ($opt in @('3', '3a', '3x')) {
        Set-EndcOptionInConfig -Option $opt
        Start-RbsAll
        Verify-Option -ExpectedOption $opt
    }
}
finally {
    Stop-StartedProcess

    if ($null -ne $script:OriginalConfig) {
        Set-Content $script:ResolvedConfigPath -Value $script:OriginalConfig -NoNewline
    }

    if ($KeepRunning) {
        try {
            Start-RbsAll
            Write-Host "Leaving rbs_node running with restored config (option from file)." -ForegroundColor Yellow
        }
        catch {
            Write-Host "Could not leave rbs_node running: $($_.Exception.Message)" -ForegroundColor Red
        }
    }
}

Write-Host ''
Write-Host '===== EN-DC OPTION SUMMARY =====' -ForegroundColor Cyan
$pass = @($script:Results | Where-Object { $_.Ok }).Count
$fail = @($script:Results | Where-Object { -not $_.Ok }).Count
Write-Host "Total: $($script:Results.Count), PASS: $pass, FAIL: $fail"
$script:Results | Format-Table -AutoSize

if ($fail -gt 0) { exit 1 }
exit 0
