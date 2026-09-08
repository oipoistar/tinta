param(
    [string]$Binary = 'build/Release/tinta.exe',
    [string]$Output = 'out/blockquote-spacing/exports'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskOutput = Join-Path $repo $Output
$taskBinary = if ([IO.Path]::IsPathRooted($Binary)) { $Binary } else { Join-Path $repo $Binary }
New-Item -ItemType Directory -Force "$taskOutput/runner" | Out-Null
$taskExe = Join-Path $taskOutput 'runner/tinta.exe'
Copy-Item -LiteralPath $taskBinary -Destination $taskExe -Force
@'
[Settings]
themeIndex=0
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=0
checkUpdates=0
language=en
'@ | Set-Content -LiteralPath "$taskOutput/runner/settings.ini"

foreach ($name in @('blockquote-one-line', 'blockquote-spacing', 'inline-code-color', 'markdown-regression-control')) {
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
        if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $target)) { throw "$name $mode failed" }
    }
    $html = Get-Content -LiteralPath "$destination/document.html" -Raw -Encoding UTF8
    if ($html -notmatch '<blockquote') { throw "$name lost its quotes" }
    if ($name -eq 'blockquote-spacing' -and ($html -notmatch '<table>' -or $html -notmatch '<h1' -or
        [regex]::Matches($html, '<pre><code').Count -ne 2 -or
        [regex]::Matches($html, 'class="math-(inline|display)"').Count -ne 5)) {
        throw "$name lost mixed content"
    }
    $pages = @(Get-ChildItem -LiteralPath "$destination/pages" -Filter 'page-*.png')
    if (!$pages.Count) { throw "$name produced no print pages" }
    "$name : $($pages.Count) native pages, HTML and DOCX exported"
}
