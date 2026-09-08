param(
    [Parameter(Mandatory = $true)][string]$Binary,
    [string]$BaselineBinary,
    [string]$OutputDirectory
)

# Run on Windows with a graphics session. Outputs are review artifacts;
# successful exports alone do not prove that the pages look correct.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$binaryPath = (Resolve-Path -LiteralPath $Binary).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repo ('out/math-fixtures-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw 'Use a new output directory so old pages cannot mask a missing export.'
}
$output = [IO.Directory]::CreateDirectory($OutputDirectory).FullName

function New-PortableRunner([string]$Source, [string]$Name) {
    $folder = [IO.Directory]::CreateDirectory((Join-Path $output $Name)).FullName
    $exe = Join-Path $folder 'tinta.exe'
    Copy-Item -LiteralPath $Source -Destination $exe
    # Isolate settings, recent files and drafts from the user's installation.
    @'
[Settings]
themeIndex=0
followSystemTheme=0
windowWidth=1050
windowHeight=900
windowMaximized=0
hasAskedFileAssociation=1
openInTabs=0
checkUpdates=0
tocPinned=0
browserPinned=0
language=en
'@ | Set-Content -LiteralPath (Join-Path $folder 'settings.ini') -Encoding ASCII
    return $exe
}

function Invoke-Fixture([string]$Exe, [string]$Name, [string]$Destination) {
    $inputPath = Join-Path $PSScriptRoot "fixtures/$Name.md"
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($mode in @('printpages', 'exporthtml')) {
        $target = if ($mode -eq 'printpages') { Join-Path $Destination 'pages' } else { Join-Path $Destination 'document.html' }
        $process = Start-Process -FilePath $Exe -ArgumentList @('"' + $inputPath + '"', "--$mode", '"' + $target + '"') -WindowStyle Hidden -PassThru
        if (!$process.WaitForExit(30000)) {
            Stop-Process -Id $process.Id
            throw "$Name --$mode timed out"
        }
        if ($process.ExitCode -ne 0) { throw "$Name --$mode failed: $($process.ExitCode)" }
    }
    $pages = @(Get-ChildItem -LiteralPath (Join-Path $Destination 'pages') -Filter 'page-*.png')
    if ($pages.Count -eq 0) { throw "$Name produced no pages" }
    foreach ($page in $pages) {
        if ($page.Length -lt 1000) { throw "Suspiciously empty page: $($page.FullName)" }
    }
    $html = Get-Content -LiteralPath (Join-Path $Destination 'document.html') -Raw -Encoding UTF8
    $mathCount = [regex]::Matches($html, 'class="math-(inline|display)"').Count
    $expected = @{ 'math-compatibility' = 15; 'math-mixed-layout' = 25; 'math-inline-stress' = 9; 'markdown-regression-control' = 0 }
    if ($mathCount -ne $expected[$Name]) { throw "$Name exported $mathCount math spans; expected $($expected[$Name])" }
    if ($Name -eq 'math-mixed-layout' -and $html -notmatch '<a [^>]*><span class="math-inline"><svg[^>]*>[\s\S]*?currentColor') {
        throw 'Math links lost their SVG or inherited color'
    }
    if ($Name -ne 'math-compatibility' -and $html -notmatch '<table>') { throw "$Name lost its table" }
    Write-Output "$Name : $($pages.Count) native pages, $mathCount exported equations"
}

$runner = New-PortableRunner $binaryPath 'current'
foreach ($fixture in @('math-compatibility', 'math-mixed-layout', 'math-inline-stress', 'markdown-regression-control')) {
    Invoke-Fixture $runner $fixture (Join-Path $output $fixture)
}
if ($BaselineBinary) {
    $baseline = New-PortableRunner (Resolve-Path -LiteralPath $BaselineBinary).Path 'baseline'
    $control = 'markdown-regression-control'
    $baselineOutput = Join-Path $output 'baseline-control'
    Invoke-Fixture $baseline $control $baselineOutput
    $currentOutput = Join-Path $output $control
    $currentPages = @(Get-ChildItem -LiteralPath (Join-Path $currentOutput 'pages') -Filter 'page-*.png')
    $baselinePages = @(Get-ChildItem -LiteralPath (Join-Path $baselineOutput 'pages') -Filter 'page-*.png')
    if ($currentPages.Count -ne $baselinePages.Count) { throw 'Control pagination changed from baseline' }
    foreach ($page in $currentPages) {
        $reference = Join-Path $baselineOutput ('pages/' + $page.Name)
        if ((Get-FileHash -LiteralPath $page.FullName).Hash -ne (Get-FileHash -LiteralPath $reference).Hash) {
            throw "Control page differs from baseline: $($page.Name); inspect the images"
        }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $currentOutput 'document.html')).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $baselineOutput 'document.html')).Hash) {
        throw 'Control HTML differs from baseline; inspect the exports'
    }
    Write-Output 'Existing Markdown control: native PNGs and HTML are byte-identical to baseline.'
}
Write-Output "Review artifacts: $output"
