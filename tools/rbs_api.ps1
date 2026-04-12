param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$CommandOrUrl,

    [Parameter(Position = 1)]
    [string]$Arg2,

    [Parameter(Position = 2)]
    [string]$Arg3,

    [Parameter(Position = 3)]
    [string]$Arg4
)

$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Stop'

function Enable-AnsiOutput {
    if (-not $IsWindows) { return }

    if ($PSStyle) {
        $PSStyle.OutputRendering = 'Ansi'
    }

    # Enable VT100 processing for classic Windows console hosts.
    $signature = @"
using System;
using System.Runtime.InteropServices;
public static class WinConsole {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr GetStdHandle(int nStdHandle);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetConsoleMode(IntPtr hConsoleHandle, out uint lpMode);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetConsoleMode(IntPtr hConsoleHandle, uint dwMode);
}
"@

    if (-not ([System.Management.Automation.PSTypeName]'WinConsole').Type) {
        Add-Type -TypeDefinition $signature | Out-Null
    }

    $STD_OUTPUT_HANDLE = -11
    $ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
    $ENABLE_PROCESSED_OUTPUT = 0x0001

    $hOut = [WinConsole]::GetStdHandle($STD_OUTPUT_HANDLE)
    if ($hOut -eq [IntPtr]::Zero) { return }

    [uint32]$mode = 0
    if ([WinConsole]::GetConsoleMode($hOut, [ref]$mode)) {
        $newMode = $mode -bor $ENABLE_VIRTUAL_TERMINAL_PROCESSING -bor $ENABLE_PROCESSED_OUTPUT
        [void][WinConsole]::SetConsoleMode($hOut, $newMode)
    }
}

Enable-AnsiOutput

$ESC = [char]27
$CLR_RESET = "$ESC[0m"
$CLR_DIM   = "$ESC[38;5;245m"
$CLR_KEY   = "$ESC[38;5;153m"
$CLR_STR   = "$ESC[38;5;221m"
$CLR_NUM   = "$ESC[38;5;114m"
$CLR_BOOL  = "$ESC[38;5;213m"
$CLR_ERR   = "$ESC[38;5;203m"
$CLR_HDR   = "$ESC[38;5;81m"

$BaseUrl = 'http://127.0.0.1:8080/api/v1'
$Methods = @('GET', 'POST', 'PUT', 'DELETE', 'PATCH', 'HEAD', 'OPTIONS')

function Resolve-ProcedureAlias {
    param(
        [string]$Link,
        [string]$Name
    )

    $n = $Name.ToLowerInvariant()
    switch ("$Link`:$n") {
        's1:setup'    { return 'S1AP:S1_SETUP' }
        's1:reset'    { return 'S1AP:RESET' }
        'abis:opstart'{ return 'OML:OPSTART' }
        'iub:reset'   { return 'NBAP:RESET' }
        default       { return $Name }
    }
}

$Url = ''
$Method = 'GET'
$Body = ''

if ($CommandOrUrl -match '^https?://') {
    $Url = $CommandOrUrl
    if (-not [string]::IsNullOrWhiteSpace($Arg2)) {
        if ($Methods -contains $Arg2.ToUpperInvariant()) {
            $Method = $Arg2.ToUpperInvariant()
            $Body = $Arg3
        }
        else {
            throw "Unknown HTTP method '$Arg2'. Allowed: $($Methods -join ', ')"
        }
    }
}
else {
    $cmd = $CommandOrUrl.ToLowerInvariant()
    switch ($cmd) {
        'links' {
            $Url = "$BaseUrl/links"
            $Method = 'GET'
        }
        'status' {
            $Url = "$BaseUrl/status"
            $Method = 'GET'
        }
        'inject-list' {
            if ([string]::IsNullOrWhiteSpace($Arg2)) {
                throw "Usage: .\\tools\\rbs_api.ps1 inject-list <abis|iub|s1>"
            }
            $link = $Arg2.ToLowerInvariant()
            $Url = "$BaseUrl/links/$link/inject"
            $Method = 'GET'
        }
        'trace' {
            if ([string]::IsNullOrWhiteSpace($Arg2)) {
                throw "Usage: .\\tools\\rbs_api.ps1 trace <abis|iub|s1> [limit]"
            }
            $link = $Arg2.ToLowerInvariant()
            $limit = if ([string]::IsNullOrWhiteSpace($Arg3)) { '10' } else { $Arg3 }
            $Url = "$BaseUrl/links/$link/trace?limit=$limit"
            $Method = 'GET'
        }
        'trace-all' {
            $limit = if ([string]::IsNullOrWhiteSpace($Arg2)) { '10' } else { $Arg2 }
            Write-Host "$CLR_HDR--- abis ---$CLR_RESET"
            & $PSCommandPath trace abis $limit
            Write-Host "$CLR_HDR--- iub ---$CLR_RESET"
            & $PSCommandPath trace iub $limit
            Write-Host "$CLR_HDR--- s1 ---$CLR_RESET"
            & $PSCommandPath trace s1 $limit
            exit 0
        }
        'inject' {
            if ([string]::IsNullOrWhiteSpace($Arg2) -or [string]::IsNullOrWhiteSpace($Arg3)) {
                throw "Usage: .\\tools\\rbs_api.ps1 inject <abis|iub|s1> <procedure|alias>"
            }
            $link = $Arg2.ToLowerInvariant()
            $proc = Resolve-ProcedureAlias -Link $link -Name $Arg3
            $Url = "$BaseUrl/links/$link/inject"
            $Method = 'POST'
            $Body = (@{ procedure = $proc } | ConvertTo-Json -Compress)
        }
        'block' {
            if ([string]::IsNullOrWhiteSpace($Arg2) -or [string]::IsNullOrWhiteSpace($Arg3)) {
                throw "Usage: .\\tools\\rbs_api.ps1 block <abis|iub|s1> <type>"
            }
            $link = $Arg2.ToLowerInvariant()
            $Url = "$BaseUrl/links/$link/block"
            $Method = 'POST'
            $Body = (@{ type = $Arg3 } | ConvertTo-Json -Compress)
        }
        'unblock' {
            if ([string]::IsNullOrWhiteSpace($Arg2) -or [string]::IsNullOrWhiteSpace($Arg3)) {
                throw "Usage: .\\tools\\rbs_api.ps1 unblock <abis|iub|s1> <type>"
            }
            $link = $Arg2.ToLowerInvariant()
            $Url = "$BaseUrl/links/$link/unblock"
            $Method = 'POST'
            $Body = (@{ type = $Arg3 } | ConvertTo-Json -Compress)
        }
        default {
            throw "Unknown command '$CommandOrUrl'. Use URL mode or aliases: status | links | inject-list | trace | trace-all | inject | block | unblock"
        }
    }
}

function Format-JsonColor {
    param([string]$JsonText)

    $formatted = $JsonText

    # Keys: "key":
    $formatted = [regex]::Replace(
        $formatted,
        '"([^"\\]*(?:\\.[^"\\]*)*)"(?=\s*:)',
        { param($m) "$CLR_KEY$($m.Value)$CLR_RESET" }
    )

    # String values: : "value"
    $formatted = [regex]::Replace(
        $formatted,
        '(?<=:\s)"([^"\\]*(?:\\.[^"\\]*)*)"',
        { param($m) "$CLR_STR$($m.Value)$CLR_RESET" }
    )

    # Numbers: : 123 or : 12.34
    $formatted = [regex]::Replace(
        $formatted,
        '(?<=:\s)-?\d+(?:\.\d+)?',
        { param($m) "$CLR_NUM$($m.Value)$CLR_RESET" }
    )

    # Booleans
    $formatted = [regex]::Replace(
        $formatted,
        '(?<=:\s)(true|false)\b',
        { param($m) "$CLR_BOOL$($m.Value)$CLR_RESET" },
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
    )

    # Nulls
    $formatted = [regex]::Replace(
        $formatted,
        '(?<=:\s)null\b',
        { param($m) "$CLR_DIM$($m.Value)$CLR_RESET" },
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
    )

    return $formatted
}

try {
    if ([string]::IsNullOrEmpty($Body)) {
        $response = Invoke-RestMethod -Uri $Url -Method $Method
    }
    else {
        $response = Invoke-RestMethod -Uri $Url -Method $Method -ContentType 'application/json' -Body $Body
    }

    # Unwrap PowerShell array envelope: { "value": [...], "Count": N }
    $normalized = $response
    if ($response -is [System.Collections.IDictionary] -and
        $response.Contains('value') -and
        $response.Contains('Count') -and
        $response.Keys.Count -eq 2) {
        $normalized = $response['value']
    }

    $json = $normalized | ConvertTo-Json -Depth 12

    Write-Host "$CLR_HDR$Method $Url$CLR_RESET"
    Write-Host (Format-JsonColor -JsonText $json)
}
catch {
    Write-Host "$CLR_HDR$Method $Url$CLR_RESET"

    if ($_.ErrorDetails -and $_.ErrorDetails.Message) {
        $message = $_.ErrorDetails.Message
        try {
            $obj = $message | ConvertFrom-Json -ErrorAction Stop
            $json = $obj | ConvertTo-Json -Depth 12
            Write-Host (Format-JsonColor -JsonText $json)
        }
        catch {
            Write-Host "$CLR_ERR$message$CLR_RESET"
        }
        exit 1
    }

    Write-Host "$CLR_ERR$($_.Exception.Message)$CLR_RESET"
    exit 1
}
