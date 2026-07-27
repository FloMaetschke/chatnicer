<#
.SYNOPSIS
    Bereitet ein ChatNicer-Release vor: Versionsnummer vergeben, CHANGELOG.md
    fortschreiben, docs/index.html nachziehen, Release-Notes schreiben.

.DESCRIPTION
    Aufgerufen wird das Skript vom Release-Job in .github/workflows/build.yml,
    nachdem beide Build-Jobs gruen sind. Es aendert nur Dateien; das Committen,
    Taggen und Veroeffentlichen bleibt im Workflow, damit hier nichts liegt, was
    ohne GitHub-Token ohnehin nicht laufen kann.

    Es liegt bewusst als eigene Datei und nicht inline im YAML: So laesst es sich
    gegen eine Kopie des Repos ausprobieren, ohne einen Push auszuloesen.

.PARAMETER ExePath
    Die gebaute build\ChatNicer.exe. Groesse und SHA256 stammen aus dieser Datei -
    nie aus einem Wert, den jemand von Hand gepflegt hat.

.PARAMETER CompatExePath
    Optional die ChatNicer-compat.exe. Fehlt sie, bleibt die compat-Zeile in der
    Groessentabelle der Landingpage stehen, wie sie ist.

.PARAMETER RepoRoot
    Wurzel des Arbeitsbaums. Standard: zwei Ebenen ueber diesem Skript.

.PARAMETER Repo
    "owner/name" fuer die Links in den Release-Notes und auf der Landingpage.

.PARAMETER CommitSha
    Der Commit, aus dem gebaut wurde. Standard: $env:GITHUB_SHA, sonst HEAD.

.PARAMETER NotesPath
    Wohin die Release-Notes geschrieben werden (Markdown, UTF-8 ohne BOM).

.PARAMETER ReleaseDate
    Datum des Release als "yyyy-MM-dd". Standard: heute. Nur gesetzt, um Tests
    reproduzierbar zu machen.

.PARAMETER DryRun
    Nichts schreiben, nur berichten, was passieren wuerde.

.EXAMPLE
    # Trockenlauf gegen eine Kopie, ohne irgendetwas zu veraendern
    .\Publish-Release.ps1 -ExePath C:\tmp\ChatNicer.exe -RepoRoot C:\tmp\kopie -DryRun
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ExePath,
    [string]$CompatExePath,
    [string]$RepoRoot,
    [string]$Repo = $env:GITHUB_REPOSITORY,
    [string]$CommitSha,
    [string]$NotesPath,
    [string]$ReleaseDate,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
#  Dateien lesen und schreiben
#
#  Windows-Runner checken mit core.autocrlf=true aus, der Arbeitsbaum hier hat
#  LF. Wer die Zeilenenden beim Schreiben nicht so laesst, wie er sie vorgefunden
#  hat, produziert einen Diff ueber die komplette Datei - und der Release-Commit
#  soll nachvollziehbar bleiben.
# ---------------------------------------------------------------------------

$script:Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Read-TextFile {
    param([string]$Path)
    $raw = [System.IO.File]::ReadAllText($Path)
    return [pscustomobject]@{
        Text = $raw -replace "`r`n", "`n"
        Crlf = $raw.Contains("`r`n")
    }
}

function Write-TextFile {
    param([string]$Path, [string]$Text, [bool]$Crlf)
    if ($Crlf) { $Text = $Text -replace "`n", "`r`n" }
    [System.IO.File]::WriteAllText($Path, $Text, $script:Utf8NoBom)
}

function Format-Bytes {
    # 118272 -> "118.272" (deutsche Tausenderpunkte, wie ueberall sonst im Projekt)
    param([long]$Value)
    return ([string]::Format([System.Globalization.CultureInfo]::GetCultureInfo('de-DE'), '{0:N0}', $Value))
}

function Format-Kb {
    param([long]$Value)
    return ('{0} KB' -f [math]::Round($Value / 1024.0, 0, [System.MidpointRounding]::AwayFromZero))
}

function Invoke-Git {
    # Gibt die Ausgabezeilen zurueck, oder nichts, wenn git nicht laeuft. Bewusst
    # ohne throw: Ohne Tags und ohne Historie muss das Skript trotzdem arbeiten.
    #
    # Das lokale ErrorActionPreference ist Pflicht, nicht Geschmack: Windows
    # PowerShell verpackt jede stderr-Zeile eines nativen Programms in einen
    # ErrorRecord, und mit dem 'Stop' von oben wuerde schon ein "not a git
    # repository" das ganze Skript abbrechen - statt in den Fallback zu laufen.
    #
    # Das fuehrende Komma bei jedem return ist ebenfalls Pflicht: Ohne es packt
    # PowerShell ein leeres Array zu $null aus, und der Aufrufer faellt beim
    # naechsten .Count auf die Nase (Set-StrictMode).
    param([string[]]$Arguments)
    $ErrorActionPreference = 'SilentlyContinue'
    # Dasselbe fuer PowerShell 7.3+, das den Exit-Code eines nativen Programms
    # ebenfalls an $ErrorActionPreference haengen kann.
    $PSNativeCommandUseErrorActionPreference = $false
    $out = & git -C $RepoRoot @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) { return , @() }
    if ($null -eq $out) { return , @() }
    return , @($out)
}

# ---------------------------------------------------------------------------
#  Version bestimmen
#
#  Gefragt werden beide Quellen - vorhandene v-Tags und die Abschnitte im
#  CHANGELOG - und die hoechste Nummer gewinnt. Ein von Hand gesetzter Tag und
#  ein von Hand geschriebener Abschnitt sollen beide zaehlen; die Versionen 0.9
#  bis 1.2 etwa existieren nur im CHANGELOG, weil sie aus der Zeit vor der
#  automatischen Veroeffentlichung stammen.
#
#  Hochgezaehlt wird die zweite Stelle (1.3 -> 1.4 -> ... -> 1.10). Einen Sprung
#  auf 2.0 macht man, indem man den Tag v2.0 von Hand setzt: Ab dann zaehlt das
#  Skript von dort weiter.
# ---------------------------------------------------------------------------

function Get-VersionsFromTags {
    # Objekte statt Arrays: Ein Array mit einem einzigen inneren Array wird beim
    # return zu genau diesem inneren Array ausgepackt - eine der zuverlaessigsten
    # Arten, sich in PowerShell einen Fehler einzubauen.
    $versions = @()
    foreach ($tag in (Invoke-Git @('tag', '--list', 'v*'))) {
        if ($tag -match '^v(\d+)\.(\d+)$') {
            $versions += [pscustomobject]@{ Major = [int]$Matches[1]; Minor = [int]$Matches[2] }
        }
    }
    return , $versions
}

function Get-VersionsFromChangelog {
    param([string]$Text)
    $versions = @()
    foreach ($line in ($Text -split "`n")) {
        if ($line -match '^##\s+\[(\d+)\.(\d+)\]') {
            $versions += [pscustomobject]@{ Major = [int]$Matches[1]; Minor = [int]$Matches[2] }
        }
    }
    return , $versions
}

function Get-NextVersion {
    param([string]$ChangelogText)
    $all = @()
    $all += Get-VersionsFromTags
    $all += Get-VersionsFromChangelog -Text $ChangelogText

    if ($all.Count -eq 0) {
        Write-Host 'Weder Tags noch Changelog-Abschnitte gefunden - starte bei 1.0'
        return '1.0'
    }

    $maxMajor = ($all | ForEach-Object { $_.Major } | Measure-Object -Maximum).Maximum
    $maxMinor = ($all | Where-Object { $_.Major -eq $maxMajor } | ForEach-Object { $_.Minor } | Measure-Object -Maximum).Maximum
    return ('{0}.{1}' -f $maxMajor, ($maxMinor + 1))
}

# ---------------------------------------------------------------------------
#  CHANGELOG
# ---------------------------------------------------------------------------

function Get-SectionBounds {
    # Liefert Start- und Endindex (exklusiv) des Abschnitts, dessen Ueberschrift
    # auf $HeadingPattern passt - Ende ist die naechste "## "-Zeile.
    param([string[]]$Lines, [string]$HeadingPattern)
    $start = -1
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match $HeadingPattern) { $start = $i; break }
    }
    if ($start -lt 0) { return $null }
    $end = $Lines.Count
    for ($i = $start + 1; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match '^##\s') { $end = $i; break }
    }
    return [pscustomobject]@{ Start = $start; End = $end }
}

function Test-SectionHasContent {
    # Ein Abschnitt zaehlt als gepflegt, sobald er einen Stichpunkt oder eine
    # Kurzfassung enthaelt. Alles andere (Platzhaltersatz, Leerzeilen) ist leer.
    param([string[]]$Body)
    foreach ($line in $Body) {
        if ($line -match '^\s*-\s+\S') { return $true }
        if ($line -match '^>\s*\*\*Kurzfassung:\*\*\s*\S') { return $true }
    }
    return $false
}

function New-AutoSection {
    # Notnagel: Der Abschnitt "Unveroeffentlicht" wurde nicht gepflegt. Statt den
    # Release zu verweigern (ein Tippfehler-Fix im README soll nicht blockieren)
    # entsteht er aus den Commit-Titeln seit dem letzten Release-Tag.
    param([string]$PreviousTag)

    # Kein @(...) um Invoke-Git: Die Funktion gibt ihr Array per "return ,"
    # unversehrt zurueck: ein @() drumherum wuerde es ein zweites Mal einpacken.
    # --reverse: aelteste Aenderung zuerst, wie man einen Changelog liest.
    $range = if ($PreviousTag) { "$PreviousTag..HEAD" } else { 'HEAD' }
    $subjects = Invoke-Git @('log', $range, '--no-merges', '--reverse', '--pretty=format:%s')
    $subjects = @($subjects | Where-Object { $_ -and $_.Trim() } | Select-Object -First 20)
    if ($subjects.Count -eq 0) { $subjects = @('Ohne Changelog-Eintrag veroeffentlicht') }

    $summary = $subjects[0]
    if ($subjects.Count -gt 1) { $summary = ($subjects | Select-Object -First 3) -join '; ' }

    $out = @()
    $out += "> **Kurzfassung:** $summary"
    $out += ''
    $out += '### Geändert'
    $out += ''
    foreach ($s in $subjects) { $out += "- $s" }
    return , $out
}

function Get-ChangelogVersions {
    # Alle Versionsabschnitte fuer die Landingpage: Nummer, Datum, Stichpunkte.
    param([string]$Text)

    $lines = $Text -split "`n"
    $result = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # Bewusst -match und kein "-notmatch ... continue": -notmatch fuellt
        # $Matches nicht, die Gruppen kaemen dann vom letzten geglueckten -match
        # irgendwo anders im Skript.
        if ($lines[$i] -match '^##\s+\[(\d+)\.(\d+)\]\s*[–-]\s*(\d{4}-\d{2}-\d{2})') {
            $version = '{0}.{1}' -f $Matches[1], $Matches[2]
            $date = $Matches[3]
        }
        else { continue }

        $end = $lines.Count
        for ($j = $i + 1; $j -lt $lines.Count; $j++) {
            if ($lines[$j] -match '^##\s') { $end = $j; break }
        }
        $body = @($lines[($i + 1)..($end - 1)])

        $result += [pscustomobject]@{
            Version = $version
            Date    = $date
            Points  = Get-SummaryPoints -Body $body
        }
        $i = $end - 1
    }
    return , $result
}

function Get-SummaryPoints {
    # Bevorzugt die Zeile "> **Kurzfassung:** a; b; c". Fehlt sie, entstehen die
    # Punkte aus den ersten drei Stichpunkten, gekuerzt auf den ersten Satz -
    # brauchbar, aber sichtbar zweite Wahl. Deshalb steht die Kurzfassung als
    # Konvention im CHANGELOG-Kopf.
    param([string[]]$Body)

    $quote = @()
    foreach ($line in $Body) {
        if ($line -match '^>\s?(.*)$') { $quote += $Matches[1]; continue }
        if ($quote.Count -gt 0) { break }
    }
    $summary = ($quote -join ' ').Trim()
    if ($summary -match '^\*\*Kurzfassung:\*\*\s*(.+)$') {
        $points = @()
        foreach ($part in ($Matches[1] -split ';')) {
            $p = $part.Trim()
            if ($p) { $points += $p }
        }
        if ($points.Count -gt 0) { return , $points }
    }

    # Fallback: Stichpunkte einsammeln, Fortsetzungszeilen anhaengen.
    $bullets = @()
    $current = $null
    foreach ($line in $Body) {
        if ($line -match '^-\s+(.*)$') {
            if ($current) { $bullets += $current }
            $current = $Matches[1].Trim()
        }
        elseif ($current -and $line -match '^\s+\S') {
            $current = ($current + ' ' + $line.Trim())
        }
        elseif ($current -and $line -match '^\s*$') {
            $bullets += $current
            $current = $null
        }
    }
    if ($current) { $bullets += $current }

    $points = @()
    foreach ($b in ($bullets | Select-Object -First 3)) {
        $t = $b -replace '\s+', ' '
        # Bis zum ersten Satzende, aber nicht mitten in einer Abkuerzung wie "z. B."
        if ($t -match '^(.{20,}?[.!?])\s') { $t = $Matches[1] }
        if ($t.Length -gt 130) { $t = $t.Substring(0, 127).TrimEnd() + '…' }
        $points += ($t -replace '[.]$', '')
    }
    if ($points.Count -eq 0) { $points = @('Siehe CHANGELOG.md') }
    return , $points
}

# ---------------------------------------------------------------------------
#  Landingpage
# ---------------------------------------------------------------------------

function Set-Marker {
    # Ersetzt den Text zwischen <!--cn:NAME--> und <!--/cn:NAME-->, an jeder
    # Stelle im Dokument. Bewusst ueber IndexOf statt Regex: Der Wert darf
    # beliebige Zeichen enthalten, ohne dass ein $ oder \ zur Falle wird.
    param([string]$Html, [string]$Name, [string]$Value)

    $open = "<!--cn:$Name-->"
    $close = "<!--/cn:$Name-->"
    $count = 0
    $pos = 0
    while ($true) {
        $i = $Html.IndexOf($open, $pos)
        if ($i -lt 0) { break }
        $j = $Html.IndexOf($close, $i + $open.Length)
        if ($j -lt 0) { throw "Marker cn:$Name ist geoeffnet, aber nicht geschlossen." }
        $Html = $Html.Substring(0, $i + $open.Length) + $Value + $Html.Substring($j)
        $pos = $i + $open.Length + $Value.Length + $close.Length
        $count++
    }
    if ($count -eq 0) { throw "Marker cn:$Name fehlt in docs/index.html." }
    Write-Host ("  cn:{0} -> {1} Stelle(n)" -f $Name, $count)
    return $Html
}

function Set-KbInMetaTags {
    # Die drei description-Metatags nennen die Groesse im Fliesstext. In einem
    # Attribut kann kein Kommentar stehen, deshalb wird hier nur die Zahl in
    # genau diesen Zeilen getauscht - der Text drumherum bleibt im HTML.
    param([string]$Html, [string]$Kb)

    $needles = @('name="description"', 'property="og:description"', 'name="twitter:description"')
    $pattern = '\b\d{1,4}(?:[.,]\d+)?\s?KB\b'
    $lines = $Html -split "`n"
    $hits = 0
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $matched = $false
        foreach ($n in $needles) { if ($lines[$i].Contains($n)) { $matched = $true; break } }
        if (-not $matched) { continue }
        # Gezaehlt wird der Treffer, nicht die Aenderung: Sonst meldet das Skript
        # "keine KB-Angabe gefunden", sobald der Wert schon stimmt.
        if ([regex]::IsMatch($lines[$i], $pattern)) {
            $hits++
            $lines[$i] = [regex]::Replace($lines[$i], $pattern, $Kb)
        }
    }
    if ($hits -eq 0) { throw 'Keine KB-Angabe in den description-Metatags gefunden.' }
    Write-Host ("  KB in Metatags -> {0} Stelle(n)" -f $hits)
    return ($lines -join "`n")
}

function Set-DownloadLinks {
    # Die Knoepfe zeigen auf die konkrete Version, nicht auf /releases/latest:
    # Daneben steht eine Pruefsumme, und die gehoert zu genau einer Datei.
    param([string]$Html, [string]$Version)

    $pattern = 'releases/(?:latest/)?download/(?:v[0-9]+\.[0-9]+/)?ChatNicer\.exe'
    $replacement = "releases/download/v$Version/ChatNicer.exe"
    $count = ([regex]::Matches($Html, $pattern)).Count
    if ($count -eq 0) { throw 'Kein Download-Link auf ChatNicer.exe in docs/index.html gefunden.' }
    Write-Host ("  Download-Links -> {0} Stelle(n)" -f $count)
    return [regex]::Replace($Html, $pattern, $replacement)
}

function ConvertTo-HtmlInline {
    # Der kleine gemeinsame Nenner von Markdown, der in Kurzfassungen vorkommt.
    # Escapen zuerst: Was danach an Tags entsteht, entsteht hier und nirgends
    # sonst - Commit-Titel landen ueber den Notnagel-Pfad ebenfalls in dieser
    # Funktion, und die schreibt der Kollege, nicht dieses Skript.
    param([string]$Text)

    $t = $Text -replace '&', '&amp;' -replace '<', '&lt;' -replace '>', '&gt;' -replace '"', '&quot;'
    $t = [regex]::Replace($t, '\[([^\]]+)\]\([^)]*\)', '$1')
    $t = [regex]::Replace($t, '`([^`]+)`', '<code class="i">$1</code>')
    $t = [regex]::Replace($t, '\*\*([^*]+)\*\*', '<strong>$1</strong>')
    return $t
}

function New-ReleaseListHtml {
    param($Versions)

    if ($Versions.Count -eq 0) { throw 'Keine Versionsabschnitte im CHANGELOG gefunden.' }

    $lines = @()
    $recent = @($Versions | Select-Object -First 3)
    $older = @($Versions | Select-Object -Skip 3)

    $lines += '<ul class="rel">'
    foreach ($v in $recent) { $lines += (New-ReleaseItemHtml -Version $v) }
    $lines += '</ul>'

    if ($older.Count -gt 0) {
        $lines += '<details>'
        $lines += '<summary>Ältere Versionen</summary>'
        $lines += '<div class="det-body">'
        $lines += '<ul class="rel" style="margin:0">'
        $first = $true
        foreach ($v in $older) {
            $style = if ($first) { ' style="border-top:0; padding-top:0"' } else { '' }
            $lines += (New-ReleaseItemHtml -Version $v -LiAttributes $style)
            $first = $false
        }
        $lines += '</ul>'
        $lines += '</div>'
        $lines += '</details>'
    }
    return ($lines -join "`n")
}

function New-ReleaseItemHtml {
    param($Version, [string]$LiAttributes = '')

    $d = [datetime]::ParseExact($Version.Date, 'yyyy-MM-dd', [System.Globalization.CultureInfo]::InvariantCulture)
    $shown = $d.ToString('dd.MM.yyyy')
    $items = ($Version.Points | ForEach-Object { '<li>' + (ConvertTo-HtmlInline $_) + '</li>' }) -join ''
    return @(
        "<li$LiAttributes>",
        ('<h3><span class="v">v{0}</span> <time datetime="{1}">{2}</time></h3>' -f $Version.Version, $Version.Date, $shown),
        "<ul>$items</ul>",
        '</li>'
    ) -join "`n"
}

# ===========================================================================
#  Ablauf
# ===========================================================================

if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path }
$RepoRoot = (Resolve-Path $RepoRoot).Path
if (-not $Repo) { $Repo = 'FloMaetschke/chatnicer' }
if (-not $ReleaseDate) { $ReleaseDate = (Get-Date).ToString('yyyy-MM-dd') }
if (-not $CommitSha) {
    $head = Invoke-Git @('rev-parse', 'HEAD')
    $CommitSha = if ($head.Count -gt 0) { $head[0] } else { '' }
}
if (-not $NotesPath) { $NotesPath = Join-Path $RepoRoot 'release-notes.md' }

if (-not (Test-Path -LiteralPath $ExePath)) { throw "EXE nicht gefunden: $ExePath" }

$changelogPath = Join-Path $RepoRoot 'CHANGELOG.md'
$docsPath = Join-Path $RepoRoot 'docs\index.html'
foreach ($p in @($changelogPath, $docsPath)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "Datei nicht gefunden: $p" }
}

$exe = Get-Item -LiteralPath $ExePath
$size = [long]$exe.Length
$sha256 = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash
$compatSize = $null
if ($CompatExePath -and (Test-Path -LiteralPath $CompatExePath)) {
    $compatSize = [long](Get-Item -LiteralPath $CompatExePath).Length
}

# --- Version ---------------------------------------------------------------

$changelog = Read-TextFile -Path $changelogPath
$version = Get-NextVersion -ChangelogText $changelog.Text
$tag = "v$version"
Write-Host "Version: $version"

$existing = Invoke-Git @('tag', '--list', $tag)
if ($existing.Count -gt 0) { throw "Der Tag $tag existiert bereits - Abbruch statt Ueberschreiben." }

# Letzter Release-Tag, fuer den Notnagel und den Vergleichslink.
$previousTag = $null
foreach ($t in (Invoke-Git @('tag', '--list', 'v*', '--sort=-v:refname'))) {
    if ($t -match '^v\d+\.\d+$') { $previousTag = $t; break }
}

# --- CHANGELOG fortschreiben ----------------------------------------------

$lines = @($changelog.Text -split "`n")
$bounds = Get-SectionBounds -Lines $lines -HeadingPattern '^##\s+\[Unver'
if ($null -eq $bounds) { throw 'Im CHANGELOG.md fehlt der Abschnitt "## [Unveröffentlicht]".' }

$body = @()
if ($bounds.End -gt ($bounds.Start + 1)) {
    $body = @($lines[($bounds.Start + 1)..($bounds.End - 1)])
}
# Leerzeilen am Rand weg, damit der Abstand unabhaengig vom Vorzustand stimmt.
while ($body.Count -gt 0 -and $body[0].Trim() -eq '') { $body = @($body[1..($body.Count - 1)]) }
while ($body.Count -gt 0 -and $body[-1].Trim() -eq '') { $body = @($body[0..($body.Count - 2)]) }

$autofilled = $false
if (-not (Test-SectionHasContent -Body $body)) {
    $autofilled = $true
    $body = New-AutoSection -PreviousTag $previousTag
    Write-Host '::warning::Der Abschnitt "Unveröffentlicht" im CHANGELOG.md war leer - der Eintrag wurde aus den Commit-Titeln erzeugt.'
}

$placeholder = @(
    '## [Unveröffentlicht]',
    '',
    ('_Noch nichts eingetragen. Wer etwas ändert, schreibt es hierhin – der nächste Push auf `main` macht daraus Version {0}._' -f ('{0}.{1}' -f $version.Split('.')[0], ([int]$version.Split('.')[1] + 1))),
    ''
)

$newSection = @(
    ('## [{0}] – {1}' -f $version, $ReleaseDate),
    ''
) + $body + @('')

$rebuilt = @()
if ($bounds.Start -gt 0) { $rebuilt += $lines[0..($bounds.Start - 1)] }
$rebuilt += $placeholder
$rebuilt += $newSection
if ($bounds.End -lt $lines.Count) { $rebuilt += $lines[$bounds.End..($lines.Count - 1)] }

$changelogText = ($rebuilt -join "`n")
$changelogText = ($changelogText -replace "`n{3,}", "`n`n")
if (-not $changelogText.EndsWith("`n")) { $changelogText += "`n" }

# --- Landingpage -----------------------------------------------------------

Write-Host 'docs/index.html:'
$docs = Read-TextFile -Path $docsPath
$html = $docs.Text

$versions = Get-ChangelogVersions -Text $changelogText
if ($versions.Count -eq 0 -or $versions[0].Version -ne $version) {
    $found = ($versions | ForEach-Object { $_.Version }) -join ', '
    throw ("Der neue Abschnitt [$version] wurde im erzeugten CHANGELOG nicht als erster wiedergefunden. " +
        "Gefunden wurde: [$found]. Erwartet wird eine Ueberschrift der Form '## [$version] – JJJJ-MM-TT'.")
}

$html = Set-Marker -Html $html -Name 'version' -Value $version
$html = Set-Marker -Html $html -Name 'size-bytes' -Value (Format-Bytes $size)
$html = Set-Marker -Html $html -Name 'size-kb' -Value (Format-Kb $size)
$html = Set-Marker -Html $html -Name 'sha256' -Value $sha256
$html = Set-Marker -Html $html -Name 'releases' -Value ("`n" + (New-ReleaseListHtml -Versions $versions) + "`n")
if ($null -ne $compatSize) {
    $html = Set-Marker -Html $html -Name 'size-compat' -Value (Format-Bytes $compatSize)
}
else {
    Write-Host '  compat-EXE nicht uebergeben - Groessenzeile bleibt unveraendert'
}
$html = Set-KbInMetaTags -Html $html -Kb (Format-Kb $size)
$html = Set-DownloadLinks -Html $html -Version $version

# --- Release-Notes ---------------------------------------------------------

$short = if ($CommitSha.Length -ge 7) { $CommitSha.Substring(0, 7) } else { $CommitSha }
$subjectLines = Invoke-Git @('log', '-1', '--pretty=format:%s', $CommitSha)
$subject = if ($subjectLines.Count -gt 0) { $subjectLines[0] } else { '(kein Commit-Titel)' }
$subject = $subject -replace '\|', '\|'

$notesBody = @($body | Where-Object { $_ -notmatch '^>\s*\*\*Kurzfassung:\*\*' })
while ($notesBody.Count -gt 0 -and $notesBody[0].Trim() -eq '') { $notesBody = @($notesBody[1..($notesBody.Count - 1)]) }

$notes = @()
$notes += ('Automatisch veröffentlicht vom Stand auf `main`.')
$notes += ''
$notes += $notesBody
$notes += ''
$notes += '| | |'
$notes += '| --- | --- |'
$notes += ("| Version | {0} |" -f $version)
$notes += ("| Commit | [{0}](https://github.com/{1}/commit/{2}) – {3} |" -f $short, $Repo, $CommitSha, $subject)
$notes += ("| Größe | {0} Bytes |" -f (Format-Bytes $size))
if ($previousTag) {
    $notes += ("| Vergleich | [{0}…{1}](https://github.com/{2}/compare/{0}...{1}) |" -f $previousTag, $tag, $Repo)
}
$notes += ''
$notes += '**SHA256 (ChatNicer.exe)**'
$notes += ''
$notes += '```'
$notes += $sha256
$notes += '```'
$notes += ''
$notes += 'Nachrechnen: `Get-FileHash .\ChatNicer.exe -Algorithm SHA256`'
$notes += ''
$notes += ('Die EXE ist unsigniert – beim ersten Start meldet sich SmartScreen (*Weitere Informationen* → *Trotzdem ausführen*). Hintergrund und die compat-Variante für Systeme ohne aktuelle UCRT: siehe [README](https://github.com/{0}#erster-start--windows-warnungen).' -f $Repo)
$notes += ''
$notes += ('Alle Änderungen: [CHANGELOG.md](https://github.com/{0}/blob/{1}/CHANGELOG.md)' -f $Repo, $tag)
$notesText = ($notes -join "`n") + "`n"

# --- Schreiben -------------------------------------------------------------

if ($DryRun) {
    Write-Host ''
    Write-Host '--- Trockenlauf, es wurde nichts geschrieben ---'
    Write-Host ("Version      : {0} (Tag {1})" -f $version, $tag)
    Write-Host ("Groesse      : {0} Bytes ({1})" -f (Format-Bytes $size), (Format-Kb $size))
    Write-Host ("SHA256       : {0}" -f $sha256)
    Write-Host ("Changelog    : {0}" -f $(if ($autofilled) { 'aus Commit-Titeln erzeugt' } else { 'gepflegt' }))
    Write-Host ("Versionen    : {0}" -f (($versions | ForEach-Object { $_.Version }) -join ', '))
}
else {
    Write-TextFile -Path $changelogPath -Text $changelogText -Crlf $changelog.Crlf
    Write-TextFile -Path $docsPath -Text $html -Crlf $docs.Crlf
    Write-TextFile -Path $NotesPath -Text $notesText -Crlf $false
    Write-Host "Geschrieben: CHANGELOG.md, docs/index.html, $NotesPath"
}

# --- Ergebnis an den Workflow ---------------------------------------------

if ($env:GITHUB_OUTPUT) {
    $out = @(
        "version=$version",
        "tag=$tag",
        "sha256=$sha256",
        "size=$size",
        "notes_path=$NotesPath",
        ("changelog_autofilled=" + $autofilled.ToString().ToLowerInvariant())
    )
    Add-Content -Path $env:GITHUB_OUTPUT -Value ($out -join "`n") -Encoding utf8
}

if ($env:GITHUB_STEP_SUMMARY) {
    $sum = @(
        "### Release $tag",
        '',
        '| | |',
        '| --- | --- |',
        "| Version | $version |",
        ("| Größe | {0} Bytes |" -f (Format-Bytes $size)),
        "| SHA256 | ``$sha256`` |",
        ("| Changelog | {0} |" -f $(if ($autofilled) { '**aus Commit-Titeln erzeugt** – bitte künftig vorher pflegen' } else { 'gepflegt' }))
    )
    Add-Content -Path $env:GITHUB_STEP_SUMMARY -Value ($sum -join "`n") -Encoding utf8
}

# Ohne das erbt der Step den Exit-Code des letzten nativen Aufrufs. Ein "git tag
# --list" in einem frisch geklonten Repo ohne Tags liefert 128, und der Job waere
# rot, obwohl alles geschrieben wurde.
exit 0
