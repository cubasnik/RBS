param(
    [string]$BaseUrl = 'http://127.0.0.1:8181/api/v1',
    [int]$StartupTimeoutSec = 25,
    [switch]$StopExisting,
    [switch]$KeepLastRunning,
    [ValidateSet('gsm', 'umts', 'lte')]
    [string]$FinalMode,
    [ValidateSet('gsm', 'umts', 'lte')]
    [string]$OnlyMode
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$script:Results = @()
$script:RbsProc = $null
$script:CurrentMode = ''

function Add-Result {
    param(
        [string]$Mode,
        [string]$Step,
        [bool]$Ok,
        [string]$Details = ''
    )

    $script:Results += [PSCustomObject]@{
        Mode = $Mode
        Step = $Step
        Ok = $Ok
        Details = $Details
    }

    $tag = if ($Ok) { 'PASS' } else { 'FAIL' }
    $color = if ($Ok) { 'Green' } else { 'Red' }
    if ([string]::IsNullOrWhiteSpace($Details)) {
        Write-Host "[$tag] $Mode :: $Step" -ForegroundColor $color
    }
    else {
        Write-Host "[$tag] $Mode :: $Step :: $Details" -ForegroundColor $color
    }
}

function Invoke-Api {
    param(
        [string]$Method,
        [string]$Path,
        [object]$Body
    )

    $uri = "$BaseUrl$Path"
    if ($null -eq $Body) {
        return Invoke-RestMethod -Uri $uri -Method $Method -TimeoutSec 5
    }

    $json = if ($Body -is [string]) { $Body } else { $Body | ConvertTo-Json -Compress -Depth 8 }
    return Invoke-RestMethod -Uri $uri -Method $Method -ContentType 'application/json' -Body $json -TimeoutSec 5
}

function Wait-ApiReady {
    $deadline = (Get-Date).AddSeconds($StartupTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $null = Invoke-Api -Method GET -Path '/status' -Body $null
            return $true
        }
        catch {
            Start-Sleep -Milliseconds 500
        }
    }
    return $false
}

function Stop-RbsIfRunning {
    if ($script:RbsProc -and -not $script:RbsProc.HasExited) {
        try {
            Stop-Process -Id $script:RbsProc.Id -Force -ErrorAction SilentlyContinue
        }
        catch { }
    }
    $script:RbsProc = $null
}

function Start-RbsMode {
    param([string]$Mode)

    Stop-RbsIfRunning

    $exe = Join-Path $PSScriptRoot '..\build\Release\rbs_node.exe'
    $exe = [System.IO.Path]::GetFullPath($exe)
    $conf = Join-Path $PSScriptRoot '..\rbs.conf'
    $conf = [System.IO.Path]::GetFullPath($conf)

    if (-not (Test-Path $exe)) {
        throw "rbs_node.exe not found: $exe"
    }
    if (-not (Test-Path $conf)) {
        throw "rbs.conf not found: $conf"
    }

    $script:RbsProc = Start-Process -FilePath $exe -ArgumentList @($conf, $Mode) -PassThru -WindowStyle Minimized
    $script:CurrentMode = $Mode
    if (-not (Wait-ApiReady)) {
        throw "REST API did not become ready for mode '$Mode' in ${StartupTimeoutSec}s"
    }
}

function Assert-LinkPresent {
    param(
        [string]$Mode,
        [string]$LinkName
    )

    try {
        $links = Invoke-Api -Method GET -Path '/links' -Body $null
        $found = $false
        foreach ($lnk in $links) {
            if ($lnk.name -eq $LinkName) {
                $found = $true
                break
            }
        }
        Add-Result -Mode $Mode -Step "links contains '$LinkName'" -Ok $found
    }
    catch {
        Add-Result -Mode $Mode -Step "links contains '$LinkName'" -Ok $false -Details $_.Exception.Message
    }
}

function Run-GsmChecks {
    $mode = 'gsm'
    try {
        Start-RbsMode -Mode $mode
        Add-Result -Mode $mode -Step 'start and status reachable' -Ok $true
    }
    catch {
        Add-Result -Mode $mode -Step 'start and status reachable' -Ok $false -Details $_.Exception.Message
        return
    }

    Assert-LinkPresent -Mode $mode -LinkName 'abis'

    try {
        $resp = Invoke-Api -Method POST -Path '/links/abis/inject' -Body @{ procedure = 'OML:OPSTART' }
        Add-Result -Mode $mode -Step 'inject OML:OPSTART' -Ok $true -Details ($resp | ConvertTo-Json -Compress)
    }
    catch {
        Add-Result -Mode $mode -Step 'inject OML:OPSTART' -Ok $false -Details $_.Exception.Message
    }

    try {
        $trace = Invoke-Api -Method GET -Path '/links/abis/trace?limit=5' -Body $null
        $count = @($trace).Count
        Add-Result -Mode $mode -Step 'trace abis' -Ok ($count -ge 0) -Details "entries=$count"
    }
    catch {
        Add-Result -Mode $mode -Step 'trace abis' -Ok $false -Details $_.Exception.Message
    }

    try {
        $h = Invoke-Api -Method GET -Path '/links/abis/health' -Body $null
        $state = [string]$h.healthStatus
        $ok = $state -ne 'DOWN'
        Add-Result -Mode $mode -Step 'health not DOWN' -Ok $ok -Details "healthStatus=$state"
    }
    catch {
        Add-Result -Mode $mode -Step 'health not DOWN' -Ok $false -Details $_.Exception.Message
    }
}

function Run-UmtsChecks {
    $mode = 'umts'
    try {
        Start-RbsMode -Mode $mode
        Add-Result -Mode $mode -Step 'start and status reachable' -Ok $true
    }
    catch {
        Add-Result -Mode $mode -Step 'start and status reachable' -Ok $false -Details $_.Exception.Message
        return
    }

    Assert-LinkPresent -Mode $mode -LinkName 'iub'

    try {
        $resp = Invoke-Api -Method POST -Path '/links/iub/inject' -Body @{ procedure = 'NBAP:RESET' }
        Add-Result -Mode $mode -Step 'inject NBAP:RESET' -Ok $true -Details ($resp | ConvertTo-Json -Compress)
    }
    catch {
        Add-Result -Mode $mode -Step 'inject NBAP:RESET' -Ok $false -Details $_.Exception.Message
    }

    try {
        $trace = Invoke-Api -Method GET -Path '/links/iub/trace?limit=5' -Body $null
        $count = @($trace).Count
        Add-Result -Mode $mode -Step 'trace iub' -Ok ($count -ge 0) -Details "entries=$count"
    }
    catch {
        Add-Result -Mode $mode -Step 'trace iub' -Ok $false -Details $_.Exception.Message
    }
}

function Run-LteChecks {
    $mode = 'lte'
    try {
        Start-RbsMode -Mode $mode
        Add-Result -Mode $mode -Step 'start and status reachable' -Ok $true
    }
    catch {
        Add-Result -Mode $mode -Step 'start and status reachable' -Ok $false -Details $_.Exception.Message
        return
    }

    Assert-LinkPresent -Mode $mode -LinkName 's1'

    try {
        $resp = Invoke-Api -Method POST -Path '/links/s1/inject' -Body @{ procedure = 'S1AP:S1_SETUP' }
        Add-Result -Mode $mode -Step 'inject S1AP:S1_SETUP' -Ok $true -Details ($resp | ConvertTo-Json -Compress)
    }
    catch {
        Add-Result -Mode $mode -Step 'inject S1AP:S1_SETUP' -Ok $false -Details $_.Exception.Message
    }

    try {
        $trace = Invoke-Api -Method GET -Path '/links/s1/trace?limit=5' -Body $null
        $count = @($trace).Count
        Add-Result -Mode $mode -Step 'trace s1' -Ok ($count -ge 0) -Details "entries=$count"
    }
    catch {
        Add-Result -Mode $mode -Step 'trace s1' -Ok $false -Details $_.Exception.Message
    }

    try {
        $cells = Invoke-Api -Method GET -Path '/lte/cells' -Body $null
        $count = @($cells.cells).Count
        Add-Result -Mode $mode -Step 'lte cells endpoint' -Ok $true -Details "cells=$count"
    }
    catch {
        Add-Result -Mode $mode -Step 'lte cells endpoint' -Ok $false -Details $_.Exception.Message
    }
}

function Run-ModeChecks {
    param([string]$Mode)

    switch ($Mode) {
        'gsm'  { Run-GsmChecks }
        'umts' { Run-UmtsChecks }
        'lte'  { Run-LteChecks }
        default { throw "Unsupported mode '$Mode'" }
    }
}

try {
    if ($StopExisting) {
        Get-Process rbs_node -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }

    if ($OnlyMode -and $FinalMode) {
        throw "Use either -OnlyMode or -FinalMode, not both."
    }

    if (-not [string]::IsNullOrWhiteSpace($OnlyMode)) {
        Run-ModeChecks -Mode $OnlyMode
    }
    elseif ([string]::IsNullOrWhiteSpace($FinalMode)) {
        Run-GsmChecks
        Run-UmtsChecks
        Run-LteChecks
    }
    else {
        $defaultOrder = @('gsm', 'umts', 'lte')
        $orderedModes = @($defaultOrder | Where-Object { $_ -ne $FinalMode }) + @($FinalMode)
        foreach ($m in $orderedModes) {
            Run-ModeChecks -Mode $m
        }
    }
}
finally {
    if (-not $KeepLastRunning) {
        Stop-RbsIfRunning
    }
}

Write-Host ''
Write-Host '===== SUMMARY =====' -ForegroundColor Cyan
$passed = @($script:Results | Where-Object { $_.Ok }).Count
$failed = @($script:Results | Where-Object { -not $_.Ok }).Count
Write-Host "Total: $($script:Results.Count), PASS: $passed, FAIL: $failed"

if ($KeepLastRunning -and $script:RbsProc -and -not $script:RbsProc.HasExited) {
    Write-Host "Leaving rbs_node running: PID=$($script:RbsProc.Id), mode=$script:CurrentMode" -ForegroundColor Yellow
}
elseif ($OnlyMode -and -not $KeepLastRunning) {
    Write-Host "OnlyMode '$OnlyMode' finished and process was stopped. Add -KeepLastRunning to leave it running." -ForegroundColor DarkYellow
}
elseif ($FinalMode -and -not $KeepLastRunning) {
    Write-Host "FinalMode '$FinalMode' was used for order only. Add -KeepLastRunning to leave it running." -ForegroundColor DarkYellow
}

$script:Results | Format-Table -AutoSize

if ($failed -gt 0) {
    exit 1
}
exit 0
