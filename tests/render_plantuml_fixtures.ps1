# Real-tool render/export harness for the PlantUML fixtures. Mirrors
# tests/render_math_fixtures.ps1 in structure and conventions.
#
# Requirements:
#   - A built app: -TintaExe (default build\Release\tinta.exe under the repo root).
#   - -PlantumlPath: an absolute plantuml.exe path, an absolute plantuml.jar
#     path (java must be on PATH), or the pair string 'java|<jar>'. The SCRIPT
#     translates the pair form into the bare jar path; the app stores only a
#     resolved tool path in settings.ini and has no env-var configuration.
#   - Committed fixtures tests\fixtures\plantuml-diagrams.md and
#     plantuml-standalone.puml (never modified; -Fixture may point at a scratch
#     copy to prove the gates discriminate).
#
# Invocation (PowerShell 7 with an absolute path - never bare pwsh):
#   & 'C:\Program Files\PowerShell\7\pwsh.exe' -NoProfile -File tests\render_plantuml_fixtures.ps1
#       -PlantumlPath 'C:\tools\plantuml.jar' [-TintaExe <exe>] [-ThemeIndex 0] [-Fixture <path>] [-BaselineBinary <exe>]
#
# Everything lands in a fresh timestamped out\plantuml-fixtures-<yyyyMMdd-HHmmss-fff>;
# reuse is refused and NOTHING is written outside it. Each run uses a portable
# copy of tinta.exe with its own settings.ini so the user's %APPDATA%\Tinta is
# never touched. The app is single-instance and a GUI subsystem binary, so
# launches are strictly sequential with Start-Process -Wait.
#
# Exit codes: 0 all gates pass, 1 a gate failed (artifacts kept for inspection),
# 2 a prerequisite is missing (checked first, before any output is written).
#
# Note: printTheme() forces the light Paper print palette, so print pages are
# identical across themeIndex values by design; -ThemeIndex only varies the
# on-screen preview and the HTML export look. That is not a regression.
param(
    [Parameter(Mandatory = $true)][string]$PlantumlPath,
    [string]$TintaExe = 'build\Release\tinta.exe',
    [int]$ThemeIndex = 0,
    [string]$Fixture,
    [string]$BaselineBinary
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$repo = Split-Path $PSScriptRoot -Parent
if (!$Fixture) { $Fixture = Join-Path $PSScriptRoot 'fixtures\plantuml-diagrams.md' }
$standaloneFixture = Join-Path $PSScriptRoot 'fixtures\plantuml-standalone.puml'

function Resolve-PlantumlTool([string]$Requested) {
    # Accepts plantuml.exe, plantuml.jar or the pair string 'java|<jar>';
    # the pair is translated to the jar path here because the app has no
    # env-var configuration and stores just the tool path.
    $path = $Requested
    if ($path -match '^java\|(.+)$') { $path = $Matches[1] }
    $path = $path.Trim().Trim('"')
    if (!$path) { return $null }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    if ($path -match '\.jar$' -and -not (Get-Command java -ErrorAction SilentlyContinue)) {
        # A jar is only usable through a java launcher on PATH.
        return $null
    }
    return (Resolve-Path -LiteralPath $path).Path
}

# --- Prerequisites FIRST: one clear error, exit 2, zero output written. ---
$tool = Resolve-PlantumlTool $PlantumlPath
if ($null -eq $tool) {
    Write-Output ('PREREQUISITE NOT MET: -PlantumlPath ' + $PlantumlPath + ' does not resolve to a usable PlantUML tool. Pass an absolute plantuml.exe path, an absolute .jar path (with java on PATH), or java|<jar>. Nothing was written.')
    exit 2
}
$tintaPath = if ([IO.Path]::IsPathRooted($TintaExe)) { $TintaExe } else { Join-Path $repo $TintaExe }
if (-not (Test-Path -LiteralPath $tintaPath -PathType Leaf)) {
    Write-Output ('PREREQUISITE NOT MET: tinta.exe not found at ' + $tintaPath + '. Build it first. Nothing was written.')
    exit 2
}
$tintaPath = (Resolve-Path -LiteralPath $tintaPath).Path
if (-not (Test-Path -LiteralPath $Fixture -PathType Leaf)) {
    Write-Output ('PREREQUISITE NOT MET: fixture not found at ' + $Fixture + '. Nothing was written.')
    exit 2
}
$Fixture = (Resolve-Path -LiteralPath $Fixture).Path
if (-not (Test-Path -LiteralPath $standaloneFixture -PathType Leaf)) {
    Write-Output ('PREREQUISITE NOT MET: standalone fixture not found at ' + $standaloneFixture + '. Nothing was written.')
    exit 2
}
if ($BaselineBinary -and -not (Test-Path -LiteralPath $BaselineBinary -PathType Leaf)) {
    Write-Output ('PREREQUISITE NOT MET: baseline binary not found at ' + $BaselineBinary + '. Nothing was written.')
    exit 2
}

$output = Join-Path $repo ('out\plantuml-fixtures-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
if (Test-Path -LiteralPath $output) {
    throw 'Use a new output directory so old pages cannot mask a missing export.'
}
[IO.Directory]::CreateDirectory($output) | Out-Null

function Fail([string]$Message) {
    Write-Output ('GATE FAILED: ' + $Message)
    Write-Output ('Review artifacts: ' + $output)
    exit 1
}

function New-PortableRunner([string]$Name, [string]$ToolPath) {
    $folder = [IO.Directory]::CreateDirectory((Join-Path $output $Name)).FullName
    $exe = Join-Path $folder 'tinta.exe'
    Copy-Item -LiteralPath $tintaPath -Destination $exe
    # Isolate settings, recent files and drafts from the user's installation;
    # plantumlPath carries the resolved tool (empty = tool unset, which must
    # make diagrams fall back to readable source).
    $ini = @(
        '[Settings]'
        "themeIndex=$ThemeIndex"
        'followSystemTheme=0'
        'windowWidth=1050'
        'windowHeight=900'
        'windowMaximized=0'
        'hasAskedFileAssociation=1'
        'openInTabs=0'
        'checkUpdates=0'
        'tocPinned=0'
        'browserPinned=0'
        'language=en'
        "plantumlPath=$ToolPath"
    ) -join [Environment]::NewLine
    Set-Content -LiteralPath (Join-Path $folder 'settings.ini') -Value $ini -Encoding ASCII
    return $exe
}

function Invoke-Tinta([string]$Exe, [string]$InputFile, [string[]]$Flags) {
    # GUI subsystem binary + single instance: -Wait, never overlapping.
    $procArgs = @(('"' + $InputFile + '"')) + @($Flags | ForEach-Object { '"' + $_ + '"' })
    $process = Start-Process -FilePath $Exe -ArgumentList $procArgs -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        Fail ('tinta ' + $Flags[0] + ' on ' + (Split-Path $InputFile -Leaf) + ' exited ' + $process.ExitCode)
    }
}

function Get-Pages([string]$Dir) {
    # page-N.png (1-based); sort numerically so same-index comparisons are stable.
    @(Get-ChildItem -LiteralPath $Dir -Filter 'page-*.png' |
        Sort-Object { [int]($_.BaseName -replace '\D', '') })
}

function Count-Needle([string]$Text, [string]$Needle) {
    return ([regex]::Matches($Text, [regex]::Escape($Needle))).Count
}

function Get-DistinctColorCount([string]$PngPath) {
    # Probe for gate d: sample every 2nd pixel of the whole page (a superset
    # of the diagram region, layout-independent). A rendered diagram carries
    # many fill/stroke hues; a source-fallback text page stays far below the
    # threshold even with grayscale antialiasing.
    $bitmap = New-Object System.Drawing.Bitmap $PngPath
    try {
        $seen = New-Object 'System.Collections.Generic.HashSet[int]'
        for ($y = 1; $y -lt $bitmap.Height; $y += 2) {
            for ($x = 1; $x -lt $bitmap.Width; $x += 2) {
                $c = $bitmap.GetPixel($x, $y)
                [void]$seen.Add(($c.R -shl 16) -bor ($c.G -shl 8) -bor $c.B)
            }
        }
        return $seen.Count
    } finally {
        $bitmap.Dispose()
    }
}

# --- Runs (fixtures are read-only; the app creates the print dir itself) ---
$runner = New-PortableRunner 'portable' $tool
Invoke-Tinta $runner $Fixture @('--printpages', (Join-Path $output 'pages'))
Invoke-Tinta $runner $Fixture @('--exporthtml', (Join-Path $output 'diagrams.html'))
Invoke-Tinta $runner $standaloneFixture @('--exporthtml', (Join-Path $output 'standalone.html'))

# Print control: same fixture with the tool cleared must fall back to source.
$noToolRunner = New-PortableRunner 'portable-notool' ''
Invoke-Tinta $noToolRunner $Fixture @('--printpages', (Join-Path $output 'pages-notool'))

# --- Gate a: real print pages exist and carry content ---
$pages = Get-Pages (Join-Path $output 'pages')
if ($pages.Count -eq 0) { Fail 'gate a: printpages produced zero pages (never silently pass with no diagrams)' }
$thin = @($pages | Where-Object { $_.Length -lt 1000 })
if ($thin.Count -gt 0) {
    Fail ('gate a: ' + $thin.Count + ' of ' + $pages.Count + ' pages below 1000 bytes: ' + (($thin | ForEach-Object Name) -join ', '))
}
Write-Output ('GATE a PASS: ' + $pages.Count + ' print pages, every PNG >= 1000 bytes')

# --- Gate b: HTML export exact counts on the fixture ---
$diagramsHtml = Join-Path $output 'diagrams.html'
$html = Get-Content -LiteralPath $diagramsHtml -Raw -Encoding UTF8
$divs = Count-Needle $html '<div class="diagram">'
$precise = Count-Needle $html '<code class="language-plantuml'
$naive = Count-Needle $html 'language-plantuml'
if ($divs -ne 4) { Fail ('gate b: ' + $divs + ' diagram divs in ' + $diagramsHtml + '; expected 4 (3 plantuml + mermaid control)') }
if ($precise -ne 2) { Fail ('gate b: ' + $precise + ' precise language-plantuml code blocks; expected 2 (anchorless + invalid fallbacks)') }
# The fence prose no longer contains the literal, so every naive
# 'language-plantuml' occurrence must be one of the precise fallback
# elements: the two counts must match exactly (drift means the prose
# grew a literal back or the exporter changed its markup).
if ($naive -ne $precise) {
    foreach ($m in [regex]::Matches($html, 'language-plantuml')) {
        $from = [Math]::Max(0, $m.Index - 60)
        $len = [Math]::Min(130, $html.Length - $from)
        Write-Output ('  naive occurrence @ ' + $m.Index + ': ...' + ($html.Substring($from, $len) -replace '\s+', ' ') + '...')
    }
    Fail ('gate b: naive language-plantuml count ' + $naive + ' != precise count ' + $precise + ' (fence prose must not contain the literal; contexts above)')
}
Write-Output ('GATE b PASS: ' + $divs + ' diagram divs, ' + $precise + ' precise fallback blocks, ' + $naive + ' naive language-plantuml occurrences')

# --- Gate c: standalone .puml proven ---
$standaloneHtml = Get-Content -LiteralPath (Join-Path $output 'standalone.html') -Raw -Encoding UTF8
$standaloneDivs = Count-Needle $standaloneHtml '<div class="diagram">'
if ($standaloneDivs -ne 1) { Fail ('gate c: ' + $standaloneDivs + ' diagram divs in the standalone .puml export; expected exactly 1') }
Write-Output 'GATE c PASS: standalone .puml exported 1 diagram div'

# --- Gate d: print discrimination vs the tool-cleared control ---
$noToolPages = Get-Pages (Join-Path $output 'pages-notool')
if ($noToolPages.Count -eq 0) { Fail 'gate d: control print run produced zero pages' }
$compared = [Math]::Min($pages.Count, $noToolPages.Count)
$differing = @()
$discriminated = $null
for ($i = 0; $i -lt $compared; $i++) {
    $toolHash = (Get-FileHash -LiteralPath $pages[$i].FullName).Hash
    $noToolHash = (Get-FileHash -LiteralPath $noToolPages[$i].FullName).Hash
    if ($toolHash -ne $noToolHash) {
        $differing += @{ Index = $i; Tool = $toolHash; NoTool = $noToolHash }
    }
}
# Probe colors only on differing pages; count every difference first so
# the printed total is not short-circuited by the first success.
foreach ($d in $differing) {
    $colors = Get-DistinctColorCount $pages[$d.Index].FullName
    Write-Output ('  page ' + $pages[$d.Index].Name + ': tool hash ' + $d.Tool.Substring(0, 16) + '... vs no-tool ' + $d.NoTool.Substring(0, 16) + '...; tool page has ' + $colors + ' distinct RGB colors')
    if ($colors -gt 16) {
        $discriminated = @{ Name = $pages[$d.Index].Name; ToolHash = $d.Tool; NoToolHash = $d.NoTool; Colors = $colors }
        break
    }
}
if ($null -eq $discriminated) {
    Fail ('gate d: no page (of ' + $compared + ' compared) both differs in SHA256 and shows >16 distinct colors; differing pages: ' + $differing.Count)
}
Write-Output ('GATE d PASS: ' + $discriminated.Name + ' differs from the same-index source-fallback page (' +
    $discriminated.ToolHash.Substring(0, 16) + '... vs ' + $discriminated.NoToolHash.Substring(0, 16) + '...) and has ' +
    $discriminated.Colors + ' distinct RGB colors (> 16, not source text); ' + $differing.Count + ' of ' + $compared + ' compared pages differ in total')

# --- Gate e (optional): baseline binary hash control ---
if ($BaselineBinary) {
    $baselineExe = New-PortableRunner 'portable-baseline' $tool
    Invoke-Tinta $baselineExe $Fixture @('--printpages', (Join-Path $output 'pages-baseline'))
    $baselinePages = Get-Pages (Join-Path $output 'pages-baseline')
    if ($baselinePages.Count -ne $pages.Count) {
        Fail ('gate e: baseline printed ' + $baselinePages.Count + ' pages, current printed ' + $pages.Count)
    }
    for ($i = 0; $i -lt $pages.Count; $i++) {
        $currentHash = (Get-FileHash -LiteralPath $pages[$i].FullName).Hash
        $baseHash = (Get-FileHash -LiteralPath $baselinePages[$i].FullName).Hash
        if ($currentHash -ne $baseHash) {
            Fail ('gate e: page ' + $pages[$i].Name + ' differs from the baseline binary; inspect the images')
        }
    }
    Write-Output ('GATE e PASS: ' + $pages.Count + ' print pages byte-identical to the baseline binary')
}

Write-Output ('ALL GATES PASS: ' + $pages.Count + ' pages, ' + $divs + ' diagram divs, ' + $precise + ' precise fallbacks (' + $naive + ' naive), ' + $standaloneDivs + ' standalone div, gate d discriminated on ' + $discriminated.Name)
Write-Output ('Review artifacts: ' + $output)
