param(
    [string]$Binary = 'build/Release/tinta.exe',
    [string]$Output = 'out/issue-207/fixed'
)
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskOutput = Join-Path $taskRepo $Output
$taskBinary = if ([IO.Path]::IsPathRooted($Binary)) { $Binary } else { Join-Path $taskRepo $Binary }
foreach ($taskTheme in @(0, 5)) {
    $taskThemeOutput = Join-Path $taskOutput "theme-$taskTheme"
    New-Item -ItemType Directory -Force "$taskThemeOutput/runner" | Out-Null
    $taskExe = Join-Path $taskThemeOutput 'runner/tinta.exe'
    Copy-Item -LiteralPath $taskBinary -Destination $taskExe -Force
    @"
[Settings]
themeIndex=$taskTheme
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=0
checkUpdates=0
language=en
"@ | Set-Content -LiteralPath "$taskThemeOutput/runner/settings.ini"
    foreach ($taskName in @('syntax-languages', 'syntax-multiline', 'syntax-legacy-control', 'markdown-regression-control')) {
        $taskSource = Join-Path $PSScriptRoot "fixtures/$taskName.md"
        $taskBefore = (Get-FileHash -LiteralPath $taskSource).Hash
        $taskDest = Join-Path $taskThemeOutput $taskName
        New-Item -ItemType Directory -Force $taskDest | Out-Null
        foreach ($taskMode in @('printpages', 'exporthtml', 'exportdocx')) {
            $taskTarget = switch ($taskMode) {
                'printpages' { "$taskDest/pages" }
                'exporthtml' { "$taskDest/document.html" }
                'exportdocx' { "$taskDest/document.docx" }
            }
            $taskProcess = Start-Process -FilePath $taskExe -ArgumentList @(('"'+$taskSource+'"'), "--$taskMode", ('"'+$taskTarget+'"')) -WindowStyle Hidden -PassThru
            if (!$taskProcess.WaitForExit(30000)) {
                Stop-Process -Id $taskProcess.Id -Force
                $taskProcess.WaitForExit()
                throw "$taskName $taskMode timed out"
            }
            if ($taskProcess.ExitCode -ne 0 -or !(Test-Path -LiteralPath $taskTarget)) { throw "$taskName $taskMode failed" }
        }
        $taskHtml = Get-Content -LiteralPath "$taskDest/document.html" -Raw -Encoding UTF8
        foreach ($taskElement in @('<blockquote', '<table>', '<h1', '<pre', '<strong>', '<em>')) {
            if (!$taskHtml.Contains($taskElement)) { throw "$taskName lost $taskElement in HTML export" }
        }
        if ((Get-FileHash -LiteralPath $taskSource).Hash -ne $taskBefore) { throw "$taskName source changed" }
        $taskPages = @(Get-ChildItem -LiteralPath "$taskDest/pages" -Filter 'page-*.png')
        if (!$taskPages.Count) { throw "$taskName produced no print pages" }
        "theme $taskTheme, $taskName : $($taskPages.Count) native pages, HTML and DOCX"
    }
}
