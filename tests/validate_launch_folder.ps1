# Launch-folder release (#253): a running Tinta must not keep the folder it
# was started in from being renamed or deleted. Explorer starts Tinta with
# the document's folder as the working directory; terminals start it in the
# current folder. Runs a portable copy with isolated settings.
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\issue-253"
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$app = "$Output\app"; $docs = "$Output\docs"; $other = "$Output\other"
New-Item -ItemType Directory -Force $app, $docs, $other | Out-Null
Copy-Item $Binary "$app\tinta.exe"
$settings = "[Settings]`nhasAskedFileAssociation=1`ncheckUpdates=0`nlanguage=en`nopenInTabs=0`n"
Set-Content -Encoding ascii "$app\settings.ini" $settings
Set-Content -Encoding utf8 "$docs\a.md" "# A`n`nLaunch folder fixture"
Set-Content -Encoding utf8 "$docs\syntax.md" "# Legacy syntax.md fallback"
Set-Content -Encoding utf8 "$other\b.md" "# B`n`nHanded to the running window"
$failures = 0

function Free([string]$dir) {
    try { [IO.Directory]::Move($dir, "$dir-moved"); [IO.Directory]::Move("$dir-moved", $dir); return $true }
    catch { return $false }
}
function Launch([string[]]$arguments, [string]$folder) {
    $start = @{ FilePath = "$app\tinta.exe"; WorkingDirectory = $folder; PassThru = $true }
    if ($arguments) { $start.ArgumentList = $arguments }
    $process = Start-Process @start
    for ($i = 0; $i -lt 60 -and $process.MainWindowHandle -eq 0 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100; $process.Refresh()
    }
    Start-Sleep -Milliseconds 800
    $process.Refresh()
    return $process
}
function Expect([string]$name, [bool]$ok, [string]$detail) {
    "{0,-6} {1} ({2})" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail
    if (-not $ok) { $script:failures++ }
}
function Close($process) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }

$p = Launch @("`"$docs\a.md`"") $docs
Expect 'Explorer-style launch leaves the folder free' ((Free $docs) -and $p.MainWindowTitle -like '*a.md*') $p.MainWindowTitle
Close $p

$p = Launch @('a.md') $docs
Expect 'a relative path still opens, folder free' ((Free $docs) -and $p.MainWindowTitle -like '*a.md*') $p.MainWindowTitle
Close $p

$p = Launch @() $docs
Expect 'syntax.md fallback still loads, folder free' ((Free $docs) -and $p.MainWindowTitle -like '*syntax.md*') $p.MainWindowTitle
Close $p

# A relative path handed to the running window resolves in the sender's folder
if (Get-Process tinta -ErrorAction SilentlyContinue | Where-Object { $_.Path -ne "$app\tinta.exe" }) {
    'SKIP   hand-off check: another Tinta window is open'
} else {
    Set-Content -Encoding ascii "$app\settings.ini" ($settings -replace 'openInTabs=0', 'openInTabs=1')
    $first = Launch @("`"$docs\a.md`"") $app
    $second = Start-Process "$app\tinta.exe" -ArgumentList 'b.md' -WorkingDirectory $other -PassThru
    [void]$second.WaitForExit(5000); Start-Sleep -Milliseconds 800; $first.Refresh()
    Expect 'relative hand-off opens b.md in the running window' ($second.HasExited -and $first.MainWindowTitle -like '*b.md*' -and (Free $other)) $first.MainWindowTitle
    Close $first
}
Expect 'all instances closed, both folders free' ((Free $docs) -and (Free $other)) 'after exit'
"Launch folder: $failures failures"
exit $failures
