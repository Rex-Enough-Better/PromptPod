Add-Type -AssemblyName System.Drawing

$outPath = Join-Path $PSScriptRoot "app.ico"
$sizes = @(16, 24, 32, 48, 64, 256)
$payloads = New-Object System.Collections.Generic.List[byte[]]

function New-CapsulePath([float]$cx, [float]$cy, [float]$len, [float]$thick) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = $thick * 0.5
    $x = $cx - $len * 0.5
    $y = $cy - $r
    $d = $thick
    $path.AddArc($x, $y, $d, $d, 90, 180)
    $path.AddArc($x + $len - $d, $y, $d, $d, 270, 180)
    $path.CloseFigure()
    return $path
}

function New-AiSparkPath([float]$cx, [float]$cy, [float]$outerR, [float]$waistR) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $pts = @(
        (New-Object System.Drawing.PointF($cx, ($cy - $outerR))),
        (New-Object System.Drawing.PointF(($cx + $waistR), ($cy - $waistR))),
        (New-Object System.Drawing.PointF(($cx + $outerR), $cy)),
        (New-Object System.Drawing.PointF(($cx + $waistR), ($cy + $waistR))),
        (New-Object System.Drawing.PointF($cx, ($cy + $outerR))),
        (New-Object System.Drawing.PointF(($cx - $waistR), ($cy + $waistR))),
        (New-Object System.Drawing.PointF(($cx - $outerR), $cy)),
        (New-Object System.Drawing.PointF(($cx - $waistR), ($cy - $waistR)))
    )
    for ($i = 0; $i -lt 4; $i++) {
        $pTip1 = $pts[$i * 2]
        $pMid  = $pts[$i * 2 + 1]
        $pTip2 = $pts[(($i * 2 + 2) % 8)]
        $c1 = New-Object System.Drawing.PointF(($pTip1.X * 0.35 + $pMid.X * 0.65), ($pTip1.Y * 0.35 + $pMid.Y * 0.65))
        $c2 = New-Object System.Drawing.PointF(($pTip2.X * 0.35 + $pMid.X * 0.65), ($pTip2.Y * 0.35 + $pMid.Y * 0.65))
        $path.AddBezier($pTip1, $c1, $c2, $pTip2)
    }
    $path.CloseFigure()
    return $path
}

function Convert-BitmapToIcoDibBytes([System.Drawing.Bitmap]$bmp) {
    $w = $bmp.Width
    $h = $bmp.Height
    $andStride = [int]([Math]::Floor(($w + 31) / 32) * 4)
    $xorBytesLen = $w * $h * 4
    $andBytesLen = $andStride * $h

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)

    # BITMAPINFOHEADER (40 bytes)
    $bw.Write([uint32]40)
    $bw.Write([int32]$w)
    $bw.Write([int32]($h * 2)) # XOR + AND height
    $bw.Write([uint16]1)
    $bw.Write([uint16]32)
    $bw.Write([uint32]0)       # BI_RGB
    $bw.Write([uint32]($xorBytesLen + $andBytesLen))
    $bw.Write([int32]0)
    $bw.Write([int32]0)
    $bw.Write([uint32]0)
    $bw.Write([uint32]0)

    # Bottom-up 32-bit BGRA XOR mask
    $andMask = New-Object byte[]($andBytesLen)
    for ($y = $h - 1; $y -ge 0; $y--) {
        $andRowOffset = ($h - 1 - $y) * $andStride
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $bw.Write([byte]$c.B)
            $bw.Write([byte]$c.G)
            $bw.Write([byte]$c.R)
            $bw.Write([byte]$c.A)
            if ($c.A -eq 0) {
                $byteIdx = $andRowOffset + [int][Math]::Floor($x / 8)
                $bitMask = [byte](0x80 -shr ($x % 8))
                $andMask[$byteIdx] = [byte]($andMask[$byteIdx] -bor $bitMask)
            }
        }
    }

    $bw.Write($andMask)
    $bw.Flush()
    $arr = $ms.ToArray()
    $bw.Dispose()
    $ms.Dispose()
    return ,$arr
}

foreach ($sz in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    $half = $sz * 0.5
    $g.TranslateTransform($half, $half)
    $g.RotateTransform(-36.0)

    $lenRatio   = if ($sz -le 24) { 1.04 } else { 0.98 }
    $thickRatio = if ($sz -le 24) { 0.54 } else { 0.48 }
    $capLen     = $sz * $lenRatio
    $capThick   = $sz * $thickRatio

    if ($sz -ge 24) {
        $shPath = New-CapsulePath 0 ($sz * 0.025) ($capLen + $sz * 0.03) ($capThick + $sz * 0.03)
        $shBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(75, 8, 12, 22))
        $g.FillPath($shBrush, $shPath)
        $shBrush.Dispose(); $shPath.Dispose()
    }

    $outerPath = New-CapsulePath 0 0 $capLen $capThick

    $shellBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF((-$capLen * 0.5), (-$capThick * 0.55))),
        (New-Object System.Drawing.PointF(($capLen * 0.5), ($capThick * 0.55))),
        [System.Drawing.Color]::FromArgb(255, 28, 34, 50),
        [System.Drawing.Color]::FromArgb(255, 10, 12, 19)
    )
    $g.FillPath($shellBrush, $outerPath)

    $oldClip = $g.Clip
    $g.SetClip($outerPath)

    $coreInset = $capThick * 0.16
    $coreLen   = $capLen - $coreInset * 2.0
    $coreThick = $capThick - $coreInset * 2.0

    $bloomPath = New-CapsulePath ($capLen * 0.05) 0 ($coreLen * 0.92) ($coreThick * 1.12)
    $bloomBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF((-$coreLen * 0.48), 0)),
        (New-Object System.Drawing.PointF(($coreLen * 0.52), 0)),
        [System.Drawing.Color]::FromArgb(85, 99, 102, 241),
        [System.Drawing.Color]::FromArgb(135, 45, 212, 191)
    )
    $g.FillPath($bloomBrush, $bloomPath)

    $innerCoreLen   = $coreLen * 0.72
    $innerCoreThick = $coreThick * 0.78
    $coreCenterX    = $capLen * 0.08
    $innerCorePath  = New-CapsulePath $coreCenterX 0 $innerCoreLen $innerCoreThick
    $innerBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF(($coreCenterX - $innerCoreLen * 0.52), 0)),
        (New-Object System.Drawing.PointF(($coreCenterX + $innerCoreLen * 0.52), 0)),
        [System.Drawing.Color]::FromArgb(245, 129, 140, 248),
        [System.Drawing.Color]::FromArgb(255, 45, 212, 191)
    )
    $cb = New-Object System.Drawing.Drawing2D.ColorBlend(3)
    $cb.Colors = @(
        [System.Drawing.Color]::FromArgb(235, 124, 92, 255),
        [System.Drawing.Color]::FromArgb(250, 56, 189, 248),
        [System.Drawing.Color]::FromArgb(255, 94, 234, 212)
    )
    $cb.Positions = @(0.0, 0.52, 1.0)
    $innerBrush.InterpolationColors = $cb
    $g.FillPath($innerBrush, $innerCorePath)

    if ($sz -ge 24) {
        $ringX = -$capLen * 0.30
        $ringPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(150, 110, 168, 255), [Math]::Max(1.0, $sz * 0.026))
        $g.DrawLine($ringPen, $ringX, (-$coreThick * 0.36), $ringX, ($coreThick * 0.36))
        $ringPen.Dispose()
    }

    $g.RotateTransform(36.0)
    $rad = -36.0 * [Math]::PI / 180.0
    $sx = [float]($coreCenterX * [Math]::Cos($rad))
    $sy = [float]($coreCenterX * [Math]::Sin($rad))
    $starOuter = [Math]::Max(2.2, $capThick * 0.29)
    $starWaist = $starOuter * 0.20
    $sparkPath = New-AiSparkPath $sx $sy $starOuter $starWaist
    $sparkBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(250, 248, 253, 255))
    $g.FillPath($sparkBrush, $sparkPath)
    $g.RotateTransform(-36.0)

    $specPath = New-CapsulePath 0 (-$capThick * 0.30) ($capLen * 0.78) ([Math]::Max(1.0, $capThick * 0.16))
    $specBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF(0, (-$capThick * 0.44))),
        (New-Object System.Drawing.PointF(0, (-$capThick * 0.16))),
        [System.Drawing.Color]::FromArgb(85, 255, 255, 255),
        [System.Drawing.Color]::FromArgb(0, 255, 255, 255)
    )
    $g.FillPath($specBrush, $specPath)

    $g.Clip = $oldClip

    $rimWidth = [Math]::Max(1.0, $sz * 0.024)
    $rimPath = New-CapsulePath 0 0 ($capLen - $rimWidth) ($capThick - $rimWidth)
    $rimBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF((-$capLen * 0.5), (-$capThick * 0.5))),
        (New-Object System.Drawing.PointF(($capLen * 0.5), ($capThick * 0.5))),
        [System.Drawing.Color]::FromArgb(195, 129, 140, 248),
        [System.Drawing.Color]::FromArgb(220, 94, 234, 212)
    )
    $rimPen = New-Object System.Drawing.Pen($rimBrush, $rimWidth)
    $g.DrawPath($rimPen, $rimPath)

    $g.ResetTransform()

    # Standard Win32 ICO rule: <= 64px stored as uncompressed 32-bpp ARGB DIB + AND mask; 256px stored as PNG!
    if ($sz -lt 256) {
        $dibBytes = Convert-BitmapToIcoDibBytes $bmp
        $payloads.Add($dibBytes)
    } else {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $payloads.Add($ms.ToArray())
        $ms.Dispose()
    }

    $rimPen.Dispose(); $rimBrush.Dispose(); $rimPath.Dispose()
    $specBrush.Dispose(); $specPath.Dispose()
    $sparkBrush.Dispose(); $sparkPath.Dispose()
    $innerBrush.Dispose(); $innerCorePath.Dispose()
    $bloomBrush.Dispose(); $bloomPath.Dispose()
    $shellBrush.Dispose(); $outerPath.Dispose()
    $oldClip.Dispose()
    $g.Dispose(); $bmp.Dispose()
}

$fs = [System.IO.File]::Open($outPath, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([uint16]0)
$bw.Write([uint16]1)
$bw.Write([uint16]$sizes.Count)

$offset = 6 + (16 * $sizes.Count)
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $sz = $sizes[$i]
    $bytes = $payloads[$i]
    $wByte = if ($sz -ge 256) { 0 } else { [byte]$sz }
    $bw.Write([byte]$wByte)
    $bw.Write([byte]$wByte)
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([uint16]1)
    $bw.Write([uint16]32)
    $bw.Write([uint32]$bytes.Length)
    $bw.Write([uint32]$offset)
    $offset += $bytes.Length
}
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $bw.Write($payloads[$i])
}
$bw.Flush()
$bw.Dispose()
$fs.Dispose()
Write-Output "Generated standard DIB+PNG AI Agent Pod icon: $outPath ($((Get-Item $outPath).Length) bytes)"
