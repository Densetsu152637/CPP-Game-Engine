param([string]$Engine = 'build/debug-vk0/bin/CPPGameEngine.exe')
$ErrorActionPreference = 'Stop'
$enginePath = (Resolve-Path -LiteralPath $Engine).Path
$repo = Split-Path -Parent $PSScriptRoot
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ('cpp-engine-cli-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
function Invoke-Result([string[]]$Arguments, [int]$Expected = 0) {
    $err = Join-Path $scratch 'stderr.txt'
    # Windows PowerShell represents native stderr as nonterminating errors.
    # Capture it without turning expected nonzero CLI results into PS exceptions.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $text = (& $enginePath @Arguments 2> $err) -join "`n" }
    finally { $ErrorActionPreference = $savedPreference }
    if ($LASTEXITCODE -ne $Expected) { throw "Exit $LASTEXITCODE, expected ${Expected}: $text $(Get-Content $err -Raw)" }
    $result = $text | ConvertFrom-Json
    if ($result.schema -ne 1 -or $result.ok -ne ($Expected -eq 0)) { throw "Invalid result envelope: $text" }
    return $result
}
$template = Join-Path $repo 'examples/first-project'
$project = Join-Path $scratch 'project'
$manifest = Join-Path $project 'project.json'
$initialized = Invoke-Result @('project', 'init', $template, $project)
if (!(Test-Path -LiteralPath $initialized.result.manifest -PathType Leaf)) { throw 'Initialization result must identify the manifest file' }
Invoke-Result @('project', 'validate', $manifest, '--format', 'json') | Out-Null
$scene = Invoke-Result @('scene', 'inspect', (Join-Path $project 'scenes/main.scene.json'), '--project-root', $project)
if (!$scene.result.revision) { throw 'Scene inspection lacks a revision' }
Invoke-Result @('project', 'assets', $manifest) | Out-Null
$runArgs = @('run', $manifest, '--headless', '--ticks', '3', '--input', 'move_right:0')
$first = Invoke-Result $runArgs
$second = Invoke-Result $runArgs
if (($first.result | ConvertTo-Json -Depth 20 -Compress) -ne ($second.result | ConvertTo-Json -Depth 20 -Compress)) { throw 'Normalized runs differ' }
if ($first.result.scene.entities[0].components.Transform.position[0] -ne 1) { throw 'Named input did not move authored player' }
$bad = Invoke-Result @('run', $manifest, '--ticks', '0') 2
if ($bad.result.diagnostics[0].code -ne 'command.arguments') { throw 'Argument diagnostic changed' }
$unknown = Invoke-Result @('run', $manifest, '--headless', '--input', 'typo:0') 2
if ($unknown.result.diagnostics[0].code -ne 'command.input.unknown' -or $unknown.result.diagnostics[0].path -ne '--input') { throw 'Unknown input diagnostic changed' }
$missing = Invoke-Result @('project', 'validate', (Join-Path $scratch 'missing.json')) 1
if (!$missing.result.diagnostics[0].code) { throw 'Missing diagnostic code' }
$scriptPath = Join-Path $project 'scripts/player.lua'
$originalScript = Get-Content -LiteralPath $scriptPath -Raw
try {
    Set-Content -LiteralPath $scriptPath -Value ("print('print log'); io.write('io log\n'); " + $originalScript) -Encoding ascii
    Invoke-Result $runArgs | Out-Null
    Set-Content -LiteralPath $scriptPath -Value 'return { invalid lua' -Encoding ascii
    $invalid = Invoke-Result @('project', 'validate', $manifest) 1
    if (!$invalid.result.diagnostics[0].code) { throw 'Missing script diagnostic' }
} finally { Set-Content -LiteralPath $scriptPath -Value $originalScript -Encoding ascii }
$package = Join-Path $scratch 'package'
Invoke-Result @('project', 'package', $manifest, $package, '--runtime', $enginePath) | Out-Null
Push-Location $scratch
try {
    $result = (& (Join-Path $package 'run.cmd') --headless --ticks 3 --input move_right:0) | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or !$result.ok -or $result.result.scene.entities[0].components.Transform.position[0] -ne 1) { throw 'Packaged runtime failed outside repository working directory' }
} finally { Pop-Location }
Write-Output '[PASS] CLI JSON, diagnostics, deterministic input, initialization, and standalone package checks'
