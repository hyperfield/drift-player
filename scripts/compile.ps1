[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$BuildType,
    [string]$Generator,
    [string]$QtPath,
    [string]$MpvPath,
    [string]$MsysRoot,
    [string]$MsysSubdir,
    [string]$CMake = "cmake",
    [switch]$NoBuild,
    [string]$Target,
    [int]$Parallel,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraConfigureArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Info {
    param([string]$Message)
    Write-Host "[compile.ps1] $Message"
}

function Resolve-QtCandidate {
    param([string]$Candidate)
    if (-not $Candidate) { return $null }

    $expanded = [Environment]::ExpandEnvironmentVariables($Candidate)
    if ($expanded.StartsWith("~")) {
        $expanded = Join-Path ([Environment]::GetFolderPath("UserProfile")) $expanded.Substring(1)
    }

    try {
        $paths = Resolve-Path -Path $expanded -ErrorAction Stop
    } catch {
        return $null
    }

    foreach ($pathInfo in $paths) {
        $path = $pathInfo.Path
        if (Test-Path $path -PathType Leaf) {
            if (Split-Path $path -Leaf -ErrorAction SilentlyContinue -eq "Qt6Config.cmake") {
                return (Split-Path $path -Parent)
            }
        } else {
            $candidates = @(
                Join-Path $path "Qt6Config.cmake",
                Join-Path $path "lib\cmake\Qt6\Qt6Config.cmake",
                Join-Path $path "share\cmake\Qt6\Qt6Config.cmake"
            )
            foreach ($candidate in $candidates) {
                if (Test-Path $candidate -PathType Leaf) {
                    return (Split-Path $candidate -Parent)
                }
            }
        }
    }

    return $null
}

function Resolve-QtDir {
    param([string]$Override)

    if ($Override) {
        $qt = Resolve-QtCandidate $Override
        if (-not $qt) {
            throw "Qt6Config.cmake not found under '$Override'."
        }
        return $qt
    }

    $envCandidates = @(
        [Environment]::GetEnvironmentVariable("Qt6_DIR"),
        [Environment]::GetEnvironmentVariable("QT6_DIR"),
        [Environment]::GetEnvironmentVariable("Qt_DIR"),
        [Environment]::GetEnvironmentVariable("QT_DIR"),
        [Environment]::GetEnvironmentVariable("QT_ROOT")
    )
    foreach ($candidate in $envCandidates) {
        $qt = Resolve-QtCandidate $candidate
        if ($qt) {
            Write-Info "Found Qt 6 via environment variable: $qt"
            return $qt
        }
    }

    $common = @(
        "C:\Qt",
        "C:\Program Files\Qt",
        (Join-Path ([Environment]::GetFolderPath("UserProfile")) "Qt")
    )
    foreach ($root in $common) {
        $qt = Resolve-QtCandidate $root
        if ($qt) {
            Write-Info "Found Qt 6 under '$root': $qt"
            return $qt
        }
    }

    return $null
}

function Resolve-MsysPrefix {
    param(
        [string]$RootOverride,
        [string]$PrefixOverride
    )

    $roots = @()
    if ($RootOverride) { $roots += $RootOverride }
    $envRoot = [Environment]::GetEnvironmentVariable("MSYS2_ROOT")
    if ($envRoot) { $roots += $envRoot }
    $roots += "C:\msys64","D:\msys64"
    $roots = $roots | Where-Object { $_ } | Select-Object -Unique

    foreach ($root in $roots) {
        try {
            $resolvedRoot = (Resolve-Path $root -ErrorAction Stop).Path
        } catch {
            continue
        }

        if ($PrefixOverride) {
            $prefix = Join-Path $resolvedRoot $PrefixOverride
            if (Test-Path $prefix -PathType Container) {
                return @{
                    Root = $resolvedRoot
                    Prefix = $prefix
                    Subdir = $PrefixOverride
                }
            }
            continue
        }

        foreach ($candidate in @("ucrt64","mingw64","clang64")) {
            $prefix = Join-Path $resolvedRoot $candidate
            if (Test-Path $prefix -PathType Container) {
                return @{
                    Root = $resolvedRoot
                    Prefix = $prefix
                    Subdir = $candidate
                }
            }
        }
    }

    return $null
}

function Stage-MpvPrefix {
    param(
        [string]$SourcePrefix,
        [string]$StageDir
    )

    if (-not (Test-Path $SourcePrefix -PathType Container)) {
        throw "MSYS2 prefix '$SourcePrefix' was not found."
    }

    if (Test-Path $StageDir -PathType Container) {
        Remove-Item -Path $StageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $StageDir | Out-Null

    $binSource = Join-Path $SourcePrefix "bin"
    $libSource = Join-Path $SourcePrefix "lib"
    $includeSource = Join-Path $SourcePrefix "include\mpv"

    if (-not (Test-Path (Join-Path $includeSource "client.h") -PathType Leaf)) {
        throw "libmpv headers not found in '$includeSource'. Install the MSYS2 mpv package for this prefix."
    }

    Write-Info "Staging libmpv from $SourcePrefix into $StageDir"
    New-Item -ItemType Directory -Path (Join-Path $StageDir "include") | Out-Null
    Copy-Item -Path $includeSource -Destination (Join-Path $StageDir "include") -Recurse -Force

    if (Test-Path $libSource -PathType Container) {
        Copy-Item -Path (Join-Path $libSource "mpv.lib") -Destination (Join-Path $StageDir "lib\mpv.lib") -Force -ErrorAction SilentlyContinue
    }

    New-Item -ItemType Directory -Path (Join-Path $StageDir "bin") | Out-Null
    if (Test-Path $binSource -PathType Container) {
        Copy-Item -Path (Join-Path $binSource "*.dll") -Destination (Join-Path $StageDir "bin") -Force
    }

    Set-Content -Path (Join-Path $StageDir ".origin") -Value $SourcePrefix -Encoding UTF8
}

function Ensure-CMake {
    param([string]$Command)
    $tool = Get-Command -Name $Command -ErrorAction SilentlyContinue
    if (-not $tool) {
        throw "Unable to locate '$Command' in PATH."
    }
    return $tool.Path
}

function Find-WindeployQt {
    param([string]$QtDir)
    if (-not $QtDir) { return $null }

    $shareDir = Split-Path $QtDir -Parent
    $prefix = Split-Path $shareDir -Parent

    $candidates = @(
        (Join-Path $prefix "bin\windeployqt.exe"),
        (Join-Path $prefix "tools\Qt6\bin\windeployqt.exe"),
        (Join-Path $prefix "tools\qt6-tools\bin\windeployqt.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate -PathType Leaf) {
            return (Resolve-Path $candidate).Path
        }
    }

    try {
        $found = Get-ChildItem -Path $prefix -Filter "windeployqt.exe" -Recurse -ErrorAction Stop | Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    } catch {}

    return $null
}

function Copy-QtFallback {
    param(
        [string]$QtDir,
        [string]$OutputDir
    )

    if (-not $QtDir) { return }

    $shareDir = Split-Path $QtDir -Parent
    $prefix = Split-Path $shareDir -Parent

    $binDirs = @(
        (Join-Path $prefix "bin"),
        (Join-Path $prefix "..\..\bin")
    ) | Where-Object { Test-Path $_ -PathType Container } | Select-Object -Unique

    foreach ($binDir in $binDirs) {
        Copy-Item -Path (Join-Path $binDir "Qt6*.dll") -Destination $OutputDir -Force -ErrorAction SilentlyContinue
    }

    $pluginDirs = @(
        (Join-Path $prefix "plugins"),
        (Join-Path $shareDir "plugins")
    ) | Where-Object { Test-Path $_ -PathType Container } | Select-Object -Unique

    foreach ($pluginDir in $pluginDirs) {
        Copy-Item -Path $pluginDir -Destination (Join-Path $OutputDir "plugins") -Recurse -Force
        break
    }

    $supportPatterns = @(
        "double-conversion*.dll",
        "pcre2-16*.dll",
        "pcre2-8*.dll",
        "zlib1*.dll"
    )

    foreach ($pattern in $supportPatterns) {
        foreach ($binDir in $binDirs) {
            $match = Get-ChildItem -Path $binDir -Filter $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($match) {
                Copy-Item -Path $match.FullName -Destination (Join-Path $OutputDir $match.Name) -Force
                break
            }
        }
    }
}

function Copy-MpvRuntime {
    param(
        [string]$StageDir,
        [string]$OutputDir
    )

    if (-not $StageDir) { return }

    $binDir = Join-Path $StageDir "bin"
    if (-not (Test-Path $binDir -PathType Container)) { return }

    Copy-Item -Path (Join-Path $binDir "*.dll") -Destination $OutputDir -Force -ErrorAction SilentlyContinue

    $originPath = $null
    $originFile = Join-Path $StageDir ".origin"
    if (Test-Path $originFile -PathType Leaf) {
        try {
            $originPath = (Get-Content $originFile -Raw -ErrorAction Stop).Trim()
        } catch {}
    }

    if ($originPath) {
        $originBin = Join-Path $originPath "bin"
        if (Test-Path $originBin -PathType Container) {
            Get-ChildItem -Path $originBin -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
                $target = Join-Path $OutputDir $_.Name
                if (-not (Test-Path $target -PathType Leaf)) {
                    Copy-Item -Path $_.FullName -Destination $target -Force
                }
            }
        }
    }

    $aliasMap = @{
        "mpv.dll"          = "libmpv-2.dll"
        "libpng16.dll"     = "libpng16-16.dll"
        "libharfbuzz.dll"  = "libharfbuzz-0.dll"
        "harfbuzz.dll"     = "libharfbuzz-0.dll"
        "libfreetype.dll"  = "libfreetype-6.dll"
        "freetype.dll"     = "libfreetype-6.dll"
        "zstd.dll"         = "libzstd.dll"
        "gio-2.0-0.dll"    = "libgio-2.0-0.dll"
        "gmodule-2.0-0.dll"= "libgmodule-2.0-0.dll"
        "gdk_pixbuf-2.0-0.dll" = "libgdk_pixbuf-2.0-0.dll"
    }

    foreach ($alias in $aliasMap.GetEnumerator()) {
        $source = Join-Path $OutputDir $alias.Value
        $dest = Join-Path $OutputDir $alias.Key
        if ((Test-Path $source -PathType Leaf) -and -not (Test-Path $dest -PathType Leaf)) {
            Copy-Item -Path $source -Destination $dest -Force
        }
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")

$cmakeExe = Ensure-CMake $CMake
Write-Info "Using cmake at: $cmakeExe"

$qtDir = Resolve-QtDir $QtPath
if ($qtDir) {
    Write-Info "Qt 6 directory: $qtDir"
} else {
    Write-Info "Qt 6 not found automatically; CMake must resolve it via its own hints."
}

$mpvStage = $null
if ($MpvPath) {
    $mpvStage = Resolve-Path $MpvPath -ErrorAction Stop
} else {
    $msysInfo = Resolve-MsysPrefix -RootOverride $MsysRoot -PrefixOverride $MsysSubdir
    if (-not $msysInfo) {
        throw "Unable to locate MSYS2 prefix. Provide -MpvPath or point to MSYS2 using -MsysRoot/-MsysSubdir."
    }
    $mpvStage = Join-Path $repoRoot.Path "build\mpv-stage"
    Stage-MpvPrefix -SourcePrefix $msysInfo.Prefix -StageDir $mpvStage
}

$buildDirPath = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot.Path $BuildDir }
New-Item -ItemType Directory -Path $buildDirPath -Force | Out-Null

$configureArgs = @("-S", $repoRoot.Path, "-B", $buildDirPath)
if ($Generator) { $configureArgs += @("-G", $Generator) }
if ($BuildType) { $configureArgs += @("-DCMAKE_BUILD_TYPE=$BuildType") }
if ($qtDir) { $configureArgs += @("-DQt6_DIR=$($qtDir -replace '\\', '/')") }
if ($mpvStage) { $configureArgs += @("-DMPV_ROOT=$($mpvStage -replace '\\', '/')") }
if ($ExtraConfigureArgs) { $configureArgs += $ExtraConfigureArgs }

Write-Info ("Configuring with: {0} {1}" -f $cmakeExe, ($configureArgs -join " "))
& $cmakeExe @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "cmake configure step failed with exit code $LASTEXITCODE"
}

if ($NoBuild) {
    Write-Info "Skipping build (--NoBuild)."
    exit 0
}

$buildArgs = @("--build", $buildDirPath)
if ($Config) { $buildArgs += @("--config", $Config) }
if ($Target) { $buildArgs += @("--target", $Target) }
if ($Parallel) { $buildArgs += @("--parallel", $Parallel) }

Write-Info ("Building with: {0} {1}" -f $cmakeExe, ($buildArgs -join " "))
& $cmakeExe @buildArgs
if ($LASTEXITCODE -ne 0) {
    throw "cmake build step failed with exit code $LASTEXITCODE"
}

$executables = @(
    (Join-Path $buildDirPath "driftplayer.exe"),
    (Join-Path $buildDirPath "drift_dialog_helper.exe")
)

$windeploy = Find-WindeployQt $qtDir
if ($windeploy) {
    foreach ($exe in $executables) {
        if (Test-Path $exe -PathType Leaf) {
            & $windeploy "--release" "--no-compiler-runtime" "--dir" $buildDirPath $exe | Out-Null
        }
    }
} elseif ($qtDir) {
    Write-Info "windeployqt not found; copying Qt runtime manually."
    Copy-QtFallback -QtDir $qtDir -OutputDir $buildDirPath
}

Copy-MpvRuntime -StageDir $mpvStage -OutputDir $buildDirPath

Write-Info "Build completed successfully."
