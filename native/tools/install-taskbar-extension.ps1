param(
    [Parameter(Mandatory=$true)][string]$InstallerPath,
    [Parameter(Mandatory=$true)][string]$ModLibraryPath,
    [Parameter(Mandatory=$true)][string]$SevenZipPath,
    [string]$ModSourcePath
)
$ErrorActionPreference = 'Stop'
if ((Get-AuthenticodeSignature -LiteralPath $InstallerPath).Status -ne 'Valid') {
    throw 'Windhawk installer signature verification failed.'
}
$extensionRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'WinCal\TaskbarExtension'
$extracted = Join-Path $env:TEMP ('WinCal-Windhawk-' + [guid]::NewGuid().ToString('N'))
& $SevenZipPath x $InstallerPath ('-o' + $extracted) -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract Windhawk installer.' }
New-Item -ItemType Directory -Force -Path $extensionRoot | Out-Null
$engineRoot = Join-Path $extensionRoot 'Engine\1.7.3'
New-Item -ItemType Directory -Force -Path $engineRoot | Out-Null
foreach ($architecture in @('32','64','arm64')) {
    $sourceFolder = Join-Path $extracted ('Engine\$R1\' + $architecture)
    $destinationFolder = Join-Path $engineRoot $architecture
    New-Item -ItemType Directory -Force -Path $destinationFolder | Out-Null
    foreach ($sourceFile in Get-ChildItem -LiteralPath $sourceFolder -File) {
        $destinationFile = Join-Path $destinationFolder $sourceFile.Name
        if ((Test-Path -LiteralPath $destinationFile) -and
            ((Get-FileHash -LiteralPath $sourceFile.FullName).Hash -eq (Get-FileHash -LiteralPath $destinationFile).Hash)) { continue }
        Copy-Item -LiteralPath $sourceFile.FullName -Destination $destinationFile -Force
    }
}
Copy-Item -LiteralPath (Join-Path $extracted 'AppData') -Destination $extensionRoot -Recurse -Force
@'
[Storage]
AppDataPath=..\..\AppData\Engine
Portable=1
'@ | Set-Content -Encoding Unicode -LiteralPath (Join-Path $engineRoot 'engine.ini')
$modRoot = Join-Path $extensionRoot 'AppData\Engine\Mods'
$mod64 = Join-Path $modRoot '64'
New-Item -ItemType Directory -Force -Path $mod64 | Out-Null
Copy-Item -LiteralPath $ModLibraryPath -Destination (Join-Path $mod64 'WinCalTaskbarClock.dll') -Force
$config = @'
[Mod]
LibraryFileName=WinCalTaskbarClock.dll
Disabled=1
LoggingEnabled=0
DebugLoggingEnabled=0
Include=explorer.exe
Architecture=x86-64
Version=1.8.1
SettingsChangeTime=1

[Settings]
ShowSeconds=1
TimeFormat=HH':'mm':'ss
DateFormat=yyyy'年'M'月'd'日'
DateLocale=zh-CN
WeekdayFormat=dddd
TopLine=%web1% %weekday% %time%（%web4%）
BottomLine=%web2% %date% %web3%
MiddleLine=-
TooltipLine=WinCal 天气与农历
TooltipLineMode=append
Width=300
Height=48
MaxWidth=0
TextSpacing=0
WebContentsUpdateInterval=1
DataCollection.UpdateInterval=1
TimeStyle.FontSize=12
DateStyle.FontSize=12
TimeStyle.TextAlignment=Left
DateStyle.TextAlignment=Left
TimeStyle.FontFamily=Microsoft YaHei UI
DateStyle.FontFamily=Microsoft YaHei UI
'@
$tags = @('weather','city','lunar','hour')
for ($index=0; $index -lt $tags.Count; $index++) {
    $prefix = 'WebContentsItems[' + $index + ']'
    $tag = $tags[$index]
    $config += "`r`n$prefix.Url=http://127.0.0.1:17864/wincal`r`n$prefix.BlockStart=`r`n$prefix.Start=<$tag>`r`n$prefix.End=</$tag>`r`n$prefix.MaxLength=100`r`n$prefix.ContentMode=plainText`r`n"
}
$config | Set-Content -Encoding Unicode -LiteralPath (Join-Path $modRoot 'taskbar-clock-customization.ini')
@'
Windhawk 1.7.3 by Michael Maltsev: https://windhawk.net/
Taskbar Clock Customization 1.8.1 by m417z, GPL-3.0:
https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-clock-customization.wh.cpp
WinCal builds the clock module from the included GPL-3.0 source. Its own
WinCalTaskbarHost.exe manages the engine; no Windhawk manager is started.
Weather and lunar date come from WinCal's loopback-only endpoint.
'@ | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $extensionRoot 'NOTICE.txt')
if ($ModSourcePath) { Copy-Item -LiteralPath $ModSourcePath -Destination $extensionRoot -Force }
Write-Output ('Prepared taskbar extension: ' + $extensionRoot)
Write-Output ('Installer extraction retained at: ' + $extracted)
