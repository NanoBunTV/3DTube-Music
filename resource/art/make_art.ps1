# Generates 3DTube Music's icon (resource/icon.png, 48x48), HOME menu banner (resource/banner.png, 256x128) and a
# large icon preview (resource/art/icon_large.png). Windows only (uses System.Drawing), no other dependencies.
# Usage : powershell -ExecutionPolicy Bypass -File resource\art\make_art.ps1
$outDir = Split-Path -Parent $PSScriptRoot
$artDir = $PSScriptRoot
Add-Type -AssemblyName System.Drawing

function New-NotePath([single]$x, [single]$y, [single]$s) {
	# a beamed eighth-note pair in a 100x100 box, scaled by $s/100 and offset by ($x, $y)
	$p = New-Object System.Drawing.Drawing2D.GraphicsPath
	$k = $s / 100.0
	foreach ($head in @(@(30, 74), @(68, 66))) {
		$e = New-Object System.Drawing.Drawing2D.GraphicsPath
		$e.AddEllipse([single](-13 * $k), [single](-9.5 * $k), [single](26 * $k), [single](19 * $k))
		$m = New-Object System.Drawing.Drawing2D.Matrix
		$m.Translate([single]($x + $head[0] * $k), [single]($y + $head[1] * $k))
		$m.Rotate(-22)
		$e.Transform($m)
		$p.AddPath($e, $false)
	}
	$pt = { param($a, $b) New-Object System.Drawing.PointF([single]($x + $a * $k), [single]($y + $b * $k)) }
	$p.AddPolygon([System.Drawing.PointF[]]@((& $pt 37.5 28), (& $pt 43 27), (& $pt 43 72), (& $pt 37.5 72)))
	$p.AddPolygon([System.Drawing.PointF[]]@((& $pt 75.5 20), (& $pt 81 19), (& $pt 81 64), (& $pt 75.5 64)))
	$p.AddPolygon([System.Drawing.PointF[]]@((& $pt 37.5 26), (& $pt 81 16), (& $pt 81 30), (& $pt 37.5 40)))
	$p.FillMode = [System.Drawing.Drawing2D.FillMode]::Winding
	return $p
}

function Draw-Note($g, [single]$x, [single]$y, [single]$s) {
	# extruded "3D" note : a stack of dark copies toward the bottom right, white face on top
	$depth = [Math]::Max(3, [int]($s * 0.07))
	$dark = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 58, 14, 110))
	for ($i = $depth; $i -ge 1; $i--) {
		$g.FillPath($dark, (New-NotePath ($x + $i * 0.8) ($y + $i) $s))
	}
	$white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
	$g.FillPath($white, (New-NotePath $x $y $s))
}

function New-Canvas([int]$w, [int]$h) {
	$bmp = New-Object System.Drawing.Bitmap $w, $h
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
	$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
	$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
	return @($bmp, $g)
}

function Fill-Gradient($g, [int]$w, [int]$h) {
	$rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
	$brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect,
		[System.Drawing.Color]::FromArgb(255, 108, 43, 217), [System.Drawing.Color]::FromArgb(255, 226, 33, 140), 45.0)
	$g.FillRectangle($brush, $rect)
}

# icon : drawn at 4x, then downscaled (the 3DS icon has no transparency, so it's full-bleed)
$big = 192
$c = New-Canvas $big $big; $bmp = $c[0]; $g = $c[1]
Fill-Gradient $g $big $big
Draw-Note $g ($big * 0.08) ($big * 0.1) ($big * 0.84)
$icon = New-Object System.Drawing.Bitmap 48, 48
$ig = [System.Drawing.Graphics]::FromImage($icon)
$ig.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$ig.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$ig.DrawImage($bmp, 0, 0, 48, 48)
$icon.Save((Join-Path $outDir "icon.png"), [System.Drawing.Imaging.ImageFormat]::Png)
# large preview for the README
$bmp.Save((Join-Path $artDir "icon_large.png"), [System.Drawing.Imaging.ImageFormat]::Png)

# banner : 256x128 (shown on the HOME menu's top screen)
$c = New-Canvas 256 128; $bmp = $c[0]; $g = $c[1]
$g.Clear([System.Drawing.Color]::Transparent)
$card = New-Object System.Drawing.Drawing2D.GraphicsPath
$r = 18; $x0 = 4; $y0 = 18; $w = 248; $h = 92
$card.AddArc($x0, $y0, $r * 2, $r * 2, 180, 90)
$card.AddArc($x0 + $w - $r * 2, $y0, $r * 2, $r * 2, 270, 90)
$card.AddArc($x0 + $w - $r * 2, $y0 + $h - $r * 2, $r * 2, $r * 2, 0, 90)
$card.AddArc($x0, $y0 + $h - $r * 2, $r * 2, $r * 2, 90, 90)
$card.CloseFigure()
$rect = New-Object System.Drawing.Rectangle $x0, $y0, $w, $h
$brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect,
	[System.Drawing.Color]::FromArgb(255, 108, 43, 217), [System.Drawing.Color]::FromArgb(255, 226, 33, 140), 20.0)
$g.FillPath($brush, $card)
Draw-Note $g 10 26 76
$font1 = New-Object System.Drawing.Font("Segoe UI", 27, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$font2 = New-Object System.Drawing.Font("Segoe UI", 23, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
$soft = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(235, 255, 255, 255))
$g.DrawString("3DTube", $font1, $white, 92, 30)
$g.DrawString("Music", $font2, $soft, 93, 64)
$bmp.Save((Join-Path $outDir "banner.png"), [System.Drawing.Imaging.ImageFormat]::Png)
"done"
