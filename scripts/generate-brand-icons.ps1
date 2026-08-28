# Regenerates the platform icon sets from the canonical branding PNG files.
param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$masterPath = Join-Path $RepositoryRoot 'branding\jxqy-all-in-one-icon-1024.png'
$foregroundPath = Join-Path $RepositoryRoot 'branding\jxqy-all-in-one-foreground-1024.png'

if (!(Test-Path -LiteralPath $masterPath) -or !(Test-Path -LiteralPath $foregroundPath))
{
    throw 'Render the 1024px branding PNG files before generating platform icons.'
}

function New-RoundedRectanglePath
{
    param(
        [float]$Width,
        [float]$Height,
        [float]$Radius
    )

    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $Radius * 2
    $path.AddArc(0, 0, $diameter, $diameter, 180, 90)
    $path.AddArc($Width - $diameter, 0, $diameter, $diameter, 270, 90)
    $path.AddArc($Width - $diameter, $Height - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc(0, $Height - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Save-ResizedPng
{
    param(
        [System.Drawing.Image]$Source,
        [int]$Size,
        [string]$Destination,
        [ValidateSet('Square', 'Rounded', 'Circle')]
        [string]$Shape = 'Square'
    )

    $directory = Split-Path -Parent $Destination
    if (!(Test-Path -LiteralPath $directory))
    {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }

    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $clipPath = $null
    try
    {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

        if ($Shape -eq 'Rounded')
        {
            $clipPath = New-RoundedRectanglePath -Width $Size -Height $Size -Radius ($Size * 0.19)
            $graphics.SetClip($clipPath)
        }
        elseif ($Shape -eq 'Circle')
        {
            $clipPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
            $clipPath.AddEllipse(0, 0, $Size, $Size)
            $graphics.SetClip($clipPath)
        }

        $graphics.DrawImage($Source, 0, 0, $Size, $Size)
        $bitmap.Save($Destination, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally
    {
        if ($clipPath)
        {
            $clipPath.Dispose()
        }
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Write-PngIco
{
    param(
        [string[]]$PngPaths,
        [string]$Destination
    )

    $entries = foreach ($pngPath in $PngPaths)
    {
        $image = [System.Drawing.Image]::FromFile($pngPath)
        try
        {
            [PSCustomObject]@{
                Width = $image.Width
                Height = $image.Height
                Bytes = [System.IO.File]::ReadAllBytes($pngPath)
            }
        }
        finally
        {
            $image.Dispose()
        }
    }

    $stream = [System.IO.File]::Open($Destination, [System.IO.FileMode]::Create)
    $writer = [System.IO.BinaryWriter]::new($stream)
    try
    {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$entries.Count)

        $offset = 6 + (16 * $entries.Count)
        foreach ($entry in $entries)
        {
            $widthByte = if ($entry.Width -ge 256) { 0 } else { $entry.Width }
            $heightByte = if ($entry.Height -ge 256) { 0 } else { $entry.Height }
            $writer.Write([byte]$widthByte)
            $writer.Write([byte]$heightByte)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$entry.Bytes.Length)
            $writer.Write([uint32]$offset)
            $offset += $entry.Bytes.Length
        }

        foreach ($entry in $entries)
        {
            $writer.Write($entry.Bytes)
        }
    }
    finally
    {
        $writer.Dispose()
        $stream.Dispose()
    }
}

$master = [System.Drawing.Image]::FromFile($masterPath)
$foreground = [System.Drawing.Image]::FromFile($foregroundPath)
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('jxqy-brand-icons-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

try
{
    $appleResDirectory = Join-Path $RepositoryRoot 'macos_ios\jxqy\res'
    foreach ($size in @(16, 32, 64, 128, 256, 512, 1024))
    {
        Save-ResizedPng -Source $master -Size $size -Destination (Join-Path $appleResDirectory ("icon{0}.png" -f $size))
    }

    $appIconDirectory = Join-Path $RepositoryRoot 'macos_ios\jxqy\jxqy Shared\Assets.xcassets\AppIcon.appiconset'
    $appIconFiles = @{
        'icon16.png' = 16
        'icon32.png' = 32
        'icon32 1.png' = 32
        'icon64.png' = 64
        'icon128.png' = 128
        'icon256.png' = 256
        'icon256 1.png' = 256
        'icon512.png' = 512
        'icon512 1.png' = 512
        'icon1024.png' = 1024
        'icon1024 1.png' = 1024
    }
    foreach ($fileName in $appIconFiles.Keys)
    {
        Save-ResizedPng -Source $master -Size $appIconFiles[$fileName] -Destination (Join-Path $appIconDirectory $fileName)
    }

    $androidSizes = @{
        'mipmap-mdpi' = @{ Launcher = 48; Foreground = 108 }
        'mipmap-hdpi' = @{ Launcher = 72; Foreground = 162 }
        'mipmap-xhdpi' = @{ Launcher = 96; Foreground = 216 }
        'mipmap-xxhdpi' = @{ Launcher = 144; Foreground = 324 }
        'mipmap-xxxhdpi' = @{ Launcher = 192; Foreground = 432 }
    }
    foreach ($density in $androidSizes.Keys)
    {
        $directory = Join-Path $RepositoryRoot ("android\app\src\main\res\{0}" -f $density)
        $launcherSize = $androidSizes[$density].Launcher
        Save-ResizedPng -Source $master -Size $launcherSize -Destination (Join-Path $directory 'ic_launcher.png') -Shape Rounded
        Save-ResizedPng -Source $master -Size $launcherSize -Destination (Join-Path $directory 'ic_launcher_round.png') -Shape Circle
        Save-ResizedPng -Source $foreground -Size $androidSizes[$density].Foreground -Destination (Join-Path $directory 'ic_launcher_foreground.png')
    }

    $icoPngPaths = foreach ($size in @(16, 24, 32, 48, 64, 128, 256))
    {
        $path = Join-Path $temporaryRoot ("icon-{0}.png" -f $size)
        Save-ResizedPng -Source $master -Size $size -Destination $path -Shape Rounded
        $path
    }
    Write-PngIco -PngPaths $icoPngPaths -Destination (Join-Path $RepositoryRoot 'win\jxqy-all-in-one\jxqy.ico')
}
finally
{
    $master.Dispose()
    $foreground.Dispose()
    if (Test-Path -LiteralPath $temporaryRoot)
    {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Output 'Generated Windows, Android, macOS, and iOS icons.'
