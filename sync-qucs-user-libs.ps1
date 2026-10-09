# Sync Qucs User Libraries before launch.
# - Always refresh IHP PDK *.lib into QucsWorkspace\user_lib and .qucs\user_lib
# - Copy optional QUCS_EXTRA_USER_LIBS dirs
# - From LibMan CORE_PRIMITIVE_LIBS_FILE / opened ROOM view: emit stub *.lib
#   for design libraries so they appear under User Libraries (symbols resolve via ROOM)

param(
    [string]$OpenedView = "",
    [string]$PdkRoot = "",
    [string]$Pdk = "",
    [string]$PrimitiveLibsFile = ""
)

$ErrorActionPreference = "Continue"
$logPath = Join-Path $env:TEMP "sync-qucs-user-libs.log"
function Log([string]$msg) {
    $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $msg
    Add-Content -LiteralPath $logPath -Value $line -ErrorAction SilentlyContinue
}

Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
Log "start OpenedView=$OpenedView PrimitiveLibsFile=$PrimitiveLibsFile"

if (-not $PdkRoot) { $PdkRoot = $env:PDK_ROOT }
if (-not $Pdk) { $Pdk = $(if ($env:PDK) { $env:PDK } else { "ihp-sg13g2" }) }
if (-not $PrimitiveLibsFile) { $PrimitiveLibsFile = $env:CORE_PRIMITIVE_LIBS_FILE }

$userLibDirs = @(
    (Join-Path $env:USERPROFILE "QucsWorkspace\user_lib"),
    (Join-Path $env:USERPROFILE ".qucs\user_lib")
)
foreach ($d in $userLibDirs) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

function Copy-QucsLibsFrom([string]$srcDir) {
    if (-not $srcDir -or -not (Test-Path -LiteralPath $srcDir)) { return 0 }
    $n = 0
    Get-ChildItem -LiteralPath $srcDir -Filter "*.lib" -File -ErrorAction SilentlyContinue | ForEach-Object {
        foreach ($dst in $userLibDirs) {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $dst $_.Name) -Force
        }
        $n++
    }
    return $n
}

function Write-Utf8NoBom([string]$path, [string]$text) {
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($path, $text, $utf8)
}

# --- IHP PDK (same source as before, always refresh) ---
if ($PdkRoot) {
    $ihpSrc = Join-Path $PdkRoot "$Pdk\libs.tech\qucs-s\user_lib"
    $n = Copy-QucsLibsFrom $ihpSrc
    Log "IHP copy from $ihpSrc -> $n libs"
}

# --- Extra directories (semicolon-separated) ---
if ($env:QUCS_EXTRA_USER_LIBS) {
    foreach ($p in ($env:QUCS_EXTRA_USER_LIBS -split ";")) {
        $p = $p.Trim()
        if ($p) { [void](Copy-QucsLibsFrom $p) }
    }
}

# Tech / PDK names that must NOT become separate design stubs
$skipLibs = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
@(
    "analogLib", "commonLib", "sg13g2_pr", "ihp_sg13g2",
    "IHP_PDK_basic_components", "IHP_PDK_nonlinear_components", "IHP_PDK_stdcells"
) | ForEach-Object { [void]$skipLibs.Add($_) }

# Collect .symbol.room paths -> group by design library
$byLib = @{}

function Add-Symbol([string]$symPath) {
    if (-not $symPath) { return }
    $symPath = $symPath.Trim().Trim('"')
    if (-not (Test-Path -LiteralPath $symPath)) {
        Log "missing symbol: $symPath"
        return
    }
    if ($symPath -notmatch '\.symbol\.room$') { return }

    $fi = Get-Item -LiteralPath $symPath
    $cellDir = $fi.Directory
    if (-not $cellDir -or -not $cellDir.Parent) { return }

    $libName = $cellDir.Parent.Name
    if ($skipLibs.Contains($libName)) { return }

    $cellName = $fi.Name
    if ($cellName -match '^(.*)\.symbol\.room$') {
        $cellName = $Matches[1]
    }
    if (-not $cellName) { return }

    if (-not $byLib.ContainsKey($libName)) {
        $byLib[$libName] = @{}
    }
    $byLib[$libName][$cellName] = $fi.FullName
    Log "add $libName/$cellName"
}

function Add-SymbolsUnderLibRoot([string]$libRoot) {
    if (-not $libRoot -or -not (Test-Path -LiteralPath $libRoot)) { return }
    Get-ChildItem -LiteralPath $libRoot -Recurse -Filter "*.symbol.room" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Add-Symbol $_.FullName }
}

# LibMan primitive list (param first, then env)
if ($PrimitiveLibsFile -and (Test-Path -LiteralPath $PrimitiveLibsFile)) {
    Log "reading PrimitiveLibsFile=$PrimitiveLibsFile"
    Get-Content -LiteralPath $PrimitiveLibsFile | ForEach-Object { Add-Symbol $_ }
} else {
    Log "no PrimitiveLibsFile (arg/env)"
}
if ($env:CORE_PRIMITIVE_LIBS) {
    foreach ($p in ($env:CORE_PRIMITIVE_LIBS -split ";")) { Add-Symbol $p }
}
if ($env:CORE_PRIMITIVE_LIB) {
    Add-Symbol $env:CORE_PRIMITIVE_LIB
}

# Design libs named in LIBMAN_TECH_LIBRARY (first entries are often the design lib)
if ($env:LIBMAN_TECH_LIBRARY -and $OpenedView) {
    $ovDir = Split-Path -Parent $OpenedView.Trim().Trim('"')
    # Walk up looking for each tech name as a sibling/parent folder
    foreach ($name in ($env:LIBMAN_TECH_LIBRARY -split "[;:]")) {
        $name = $name.Trim()
        if (-not $name -or $skipLibs.Contains($name)) { continue }
        $walk = Get-Item -LiteralPath $ovDir -ErrorAction SilentlyContinue
        for ($i = 0; $i -lt 6 -and $null -ne $walk; $i++) {
            $candidate = Join-Path $walk.FullName $name
            if (Test-Path -LiteralPath $candidate) {
                Log "LIBMAN_TECH_LIBRARY scan $candidate"
                Add-SymbolsUnderLibRoot $candidate
                break
            }
            $walk = $walk.Parent
        }
    }
}

# Opened ROOM view: copy nearby *.lib and harvest sibling cells in that design lib
if ($OpenedView) {
    $OpenedView = $OpenedView.Trim().Trim('"')
}
if ($OpenedView -and (Test-Path -LiteralPath $OpenedView)) {
    Log "OpenedView ok: $OpenedView"
    $ov = Get-Item -LiteralPath $OpenedView
    $walk = $ov.Directory
    for ($i = 0; $i -lt 8 -and $null -ne $walk; $i++) {
        foreach ($rel in @(
            "user_lib",
            "qucs-s\user_lib",
            "libs.tech\qucs-s\user_lib"
        )) {
            [void](Copy-QucsLibsFrom (Join-Path $walk.FullName $rel))
        }
        $walk = $walk.Parent
    }

    # .../<lib>/<cell>/<cell>.(schematic|symbol|emmodel).room
    if ($ov.FullName -match '[\\/]([^\\/]+)[\\/]([^\\/]+)[\\/][^\\/]+\.(schematic|symbol|emmodel)\.room$') {
        $libName = $Matches[1]
        $libRoot = $ov.Directory.Parent
        if ($libRoot -and $libRoot.Name -eq $libName -and -not $skipLibs.Contains($libName)) {
            Log "OpenedView harvest libRoot=$($libRoot.FullName)"
            Add-SymbolsUnderLibRoot $libRoot.FullName
        }
    }
} elseif ($OpenedView) {
    Log "OpenedView missing: $OpenedView"
}

# Emit stub Qucs libraries for design cells (panel listing; geometry from ROOM at place/load)
foreach ($libName in @($byLib.Keys)) {
    $cells = $byLib[$libName]
    if ($cells.Count -lt 1) { continue }

    $lines = New-Object System.Collections.Generic.List[string]
    [void]$lines.Add("<Qucs Library 2.1.0 `"$libName`">")
    [void]$lines.Add("")
    foreach ($cellName in ($cells.Keys | Sort-Object)) {
        [void]$lines.Add("<Component $cellName>")
        [void]$lines.Add("  <Description>")
        [void]$lines.Add("LibMan ROOM cell ($libName/$cellName) - symbol resolved from .symbol.room")
        [void]$lines.Add("  </Description>")
        [void]$lines.Add("  <Model>")
        [void]$lines.Add(".Def:${libName}_${cellName}")
        [void]$lines.Add(".Def:End")
        [void]$lines.Add("  </Model>")
        [void]$lines.Add("  <Spice>")
        [void]$lines.Add(".SUBCKT ${libName}_${cellName}")
        [void]$lines.Add(".ENDS")
        [void]$lines.Add("  </Spice>")
        [void]$lines.Add("  <VerilogModel>  </VerilogModel>")
        [void]$lines.Add("  <VHDLModel>  </VHDLModel>")
        [void]$lines.Add("  <Symbol>")
        [void]$lines.Add("    <Rectangle -30 -30 60 60 #000080 1 1 #e8e8e8 1 0>")
        [void]$lines.Add("    <.ID 35 -16 $cellName>")
        [void]$lines.Add("  </Symbol>")
        [void]$lines.Add("</Component>")
        [void]$lines.Add("")
    }
    $text = ($lines -join "`n") + "`n"
    foreach ($dst in $userLibDirs) {
        $out = Join-Path $dst "$libName.lib"
        try {
            Write-Utf8NoBom $out $text
            Log "wrote $out ($($cells.Count) cells)"
        } catch {
            Log "FAILED write $out : $_"
        }
    }
}

Log "done libs=$($byLib.Keys -join ',')"
exit 0
