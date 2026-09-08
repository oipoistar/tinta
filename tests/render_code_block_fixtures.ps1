param(
    [string]$Binary = 'build/Release/tinta.exe',
    [string]$Output = 'out/code-block-spacing',
    [string]$BaselineOutput = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskOutput = [IO.Path]::GetFullPath((Join-Path $repo $Output))
New-Item -ItemType Directory -Force "$taskOutput/runner" | Out-Null
$taskExe = Join-Path $taskOutput 'runner/tinta.exe'
Copy-Item -LiteralPath (Join-Path $repo $Binary) -Destination $taskExe -Force
@'
[Settings]
themeIndex=0
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=0
checkUpdates=0
language=en
'@ | Set-Content -LiteralPath "$taskOutput/runner/settings.ini"

foreach ($name in @('code-block-one-line', 'code-block-spacing', 'markdown-regression-control')) {
    $source = Join-Path $PSScriptRoot "fixtures/$name.md"
    $destination = Join-Path $taskOutput $name
    New-Item -ItemType Directory -Force $destination | Out-Null
    foreach ($mode in @('printpages', 'exporthtml', 'exportdocx')) {
        $target = switch ($mode) {
            'printpages' { "$destination/pages" }
            'exporthtml' { "$destination/document.html" }
            'exportdocx' { "$destination/document.docx" }
        }
        $process = Start-Process -FilePath $taskExe -ArgumentList @('"' + $source + '"', "--$mode", '"' + $target + '"') -WindowStyle Hidden -PassThru
        if (!$process.WaitForExit(30000)) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
            throw "$name $mode timed out"
        }
        if ($process.ExitCode -ne 0) { throw "$name $mode failed: $($process.ExitCode)" }
    }
    $html = Get-Content -LiteralPath "$destination/document.html" -Raw -Encoding UTF8
    $codeCount = [regex]::Matches($html, '<pre><code').Count
    $expectedCode = @{ 'code-block-one-line'=1; 'code-block-spacing'=9; 'markdown-regression-control'=1 }
    if ($codeCount -ne $expectedCode[$name]) { throw "$name lost code blocks" }
    if ($name -eq 'code-block-spacing') {
        $equations = [regex]::Matches($html, 'class="math-(inline|display)"').Count
        if ($equations -ne 5 -or $html -notmatch '<table>' -or $html -notmatch '<h1') {
            throw "$name lost surrounding headings, table or math"
        }
    }
    $pages = @(Get-ChildItem -LiteralPath "$destination/pages" -Filter 'page-*.png')
    if (!$pages.Count) { throw "$name produced no native pages" }
    if ($BaselineOutput) {
        $baselineHtml = Join-Path $repo "$BaselineOutput/$name/document.html"
        if ((Get-FileHash -LiteralPath $baselineHtml).Hash -ne (Get-FileHash -LiteralPath "$destination/document.html").Hash) {
            throw "$name HTML changed; the fix should affect only native layout"
        }
    }
    "$name : $($pages.Count) native pages, $codeCount code blocks, HTML and DOCX exported"
}
