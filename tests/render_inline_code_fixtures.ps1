param(
    [string]$Binary = 'build/Release/tinta.exe',
    [string]$Output = 'out/inline-code-colors'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskOutput = Join-Path $repo $Output
$taskBinary = if ([IO.Path]::IsPathRooted($Binary)) { $Binary } else { Join-Path $repo $Binary }
foreach ($variant in @(@('builtin',0), @('legacy',10), @('separate',11), @('dark',12))) {
    $variantDir = Join-Path $taskOutput $variant[0]
    New-Item -ItemType Directory -Force "$variantDir/runner" | Out-Null
    $exe = Join-Path $variantDir 'runner/tinta.exe'
    Copy-Item -LiteralPath $taskBinary -Destination $exe -Force
    Copy-Item -LiteralPath "$PSScriptRoot/fixtures/inline-code-themes.ini" -Destination "$variantDir/runner/themes.ini" -Force
    @"
[Settings]
themeIndex=$($variant[1])
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=0
checkUpdates=0
language=en
"@ | Set-Content -LiteralPath "$variantDir/runner/settings.ini"
    $fixtures = @('inline-code-color')
    if ($variant[0] -eq 'builtin') { $fixtures += 'markdown-regression-control' }
    foreach ($name in $fixtures) {
        $source = Join-Path $PSScriptRoot "fixtures/$name.md"
        $destination = Join-Path $variantDir $name
        New-Item -ItemType Directory -Force $destination | Out-Null
        foreach ($mode in @('printpages','exporthtml','exportdocx')) {
            $target = switch ($mode) {
                'printpages' { "$destination/pages" }
                'exporthtml' { "$destination/document.html" }
                'exportdocx' { "$destination/document.docx" }
            }
            $process = Start-Process -FilePath $exe -ArgumentList @('"' + $source + '"', "--$mode", '"' + $target + '"') -WindowStyle Hidden -PassThru
            if (!$process.WaitForExit(30000)) {
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit()
                throw "$name $mode timed out"
            }
            if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $target)) {
                throw "$name $mode failed"
            }
        }
        $html = Get-Content -LiteralPath "$destination/document.html" -Raw -Encoding UTF8
        $equations = [regex]::Matches($html, 'class="math-(inline|display)"').Count
        $expectedMath = if ($name -eq 'inline-code-color') { 5 } else { 0 }
        $expectedBlocks = if ($name -eq 'inline-code-color') { 3 } else { 1 }
        if ($equations -ne $expectedMath -or [regex]::Matches($html,'<pre><code').Count -ne $expectedBlocks -or
            $html -notmatch '<table>' -or $html -notmatch '<h1') { throw "$name lost mixed content" }
        $pages = @(Get-ChildItem -LiteralPath "$destination/pages" -Filter 'page-*.png')
        if (!$pages.Count) { throw "$name produced no print pages" }
        "$($variant[0]) / $name : $($pages.Count) native pages, HTML and DOCX exported"
    }
}
