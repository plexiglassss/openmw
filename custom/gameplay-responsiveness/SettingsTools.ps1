# Shared settings editor; Windows PowerShell 5.1 and PowerShell 7.
Set-StrictMode -Version Latest

function Get-BytesSha([byte[]]$Bytes) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}

function Convert-GameplaySettings([byte[]]$Bytes) {
    $encoding = New-Object System.Text.UTF8Encoding($false, $true)
    $bom = $Bytes.Length -ge 3 -and $Bytes[0] -eq 239 -and $Bytes[1] -eq 187 -and $Bytes[2] -eq 191
    $offset = 0
    if ($bom) { $offset = 3 }
    try { $text = $encoding.GetString($Bytes, $offset, $Bytes.Length - $offset) }
    catch { throw 'settings.cfg must be UTF-8. The file was not changed.' }
    if ($text.Contains([char]0)) { throw 'settings.cfg contains NUL bytes. The file was not changed.' }
    $newline = "`r`n"
    $firstNewline = [regex]::Match($text, "`r`n|`n|`r")
    if ($firstNewline.Success) { $newline = $firstNewline.Value }
    $lines = New-Object 'System.Collections.Generic.List[object]'
    foreach ($match in [regex]::Matches($text, '(?<line>[^\r\n]*)(?<end>\r\n|\n|\r|$)')) {
        if ($match.Length -eq 0) { continue }
        $lines.Add(@{ Text = $match.Groups['line'].Value; End = $match.Groups['end'].Value })
    }
    $changes = @(
        @{ Section = 'Shadows'; Key = 'compute scene bounds'; Value = 'bounds' },
        @{ Section = 'Lua'; Key = 'inactive script cache duration'; Value = '30' },
        @{ Section = 'Lua'; Key = 'inactive script cache memory threshold'; Value = '2147483648' }
    )
    foreach ($change in $changes) {
        $sectionStarts = @()
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $sectionMatch = [regex]::Match($lines[$i].Text, '^\s*\[([^\]]+)\]\s*(?:[#;].*)?$')
            if ($sectionMatch.Success -and $sectionMatch.Groups[1].Value -ieq $change.Section) { $sectionStarts += $i }
        }
        if ($sectionStarts.Count -gt 1) { throw ('Duplicate [' + $change.Section + '] sections; the file was not changed.') }
        if ($sectionStarts.Count -eq 0) {
            if ($lines.Count -gt 0) {
                if ($lines[$lines.Count - 1].End -eq '') { $lines[$lines.Count - 1].End = $newline }
                if ($lines[$lines.Count - 1].Text -ne '') { $lines.Add(@{ Text = ''; End = $newline }) }
            }
            $sectionStarts = @($lines.Count)
            $lines.Add(@{ Text = '[' + $change.Section + ']'; End = $newline })
        }
        $start = $sectionStarts[0]
        $end = $lines.Count
        for ($i = $start + 1; $i -lt $lines.Count; $i++) {
            if ($lines[$i].Text -match '^\s*\[') { $end = $i; break }
        }
        $matches = @()
        $pattern = '^(?<prefix>\s*' + [regex]::Escape($change.Key) + '\s*=\s*)(?<value>.*?)(?<suffix>\s*(?:[#;].*)?)$'
        for ($i = $start + 1; $i -lt $end; $i++) {
            $keyMatch = [regex]::Match($lines[$i].Text, $pattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($keyMatch.Success) { $matches += @{ Index = $i; Match = $keyMatch } }
        }
        if ($matches.Count -gt 1) { throw ('Duplicate setting: [' + $change.Section + '] ' + $change.Key + '. The file was not changed.') }
        if ($matches.Count -eq 1) {
            $m = $matches[0]
            $lines[$m.Index].Text = $m.Match.Groups['prefix'].Value + $change.Value + $m.Match.Groups['suffix'].Value
        }
        else {
            if ($end -gt 0 -and $lines[$end - 1].End -eq '') { $lines[$end - 1].End = $newline }
            $lines.Insert($end, @{ Text = $change.Key + ' = ' + $change.Value; End = $newline })
        }
    }
    $builder = New-Object Text.StringBuilder
    foreach ($line in $lines) { [void]$builder.Append($line.Text); [void]$builder.Append($line.End) }
    [byte[]]$result = $encoding.GetBytes($builder.ToString())
    if ($bom) { $result = [byte[]](@(239, 187, 191) + $result) }
    return ,$result
}

function Write-SettingsAtomically([string]$Path, [byte[]]$Bytes, [string]$ExpectedSha) {
    $temporary = $Path + '.omw-' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllBytes($temporary, $Bytes)
        if ((Get-BytesSha ([IO.File]::ReadAllBytes($Path))) -ne $ExpectedSha) { throw 'settings.cfg changed during preparation. Nothing was replaced.' }
        [IO.File]::Replace($temporary, $Path, [NullString]::Value)
    }
    finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue } }
}
