# Builds res\app.ico as a BMP-format multi-size icon (16 + 32).
# Run from res\:  powershell -ExecutionPolicy Bypass -File make_icon.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-CoinBmpData([int]$size) {
    $bmp = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = [single]$size

    $body = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 0x33, 0x39, 0x44))
    $g.FillEllipse($body, $s * 0.05, $s * 0.05, $s * 0.90, $s * 0.90)

    $ring = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 0xE8, 0xC0, 0x5F), [single]($s * 0.11))
    $r0 = $s * 0.13 + $ring.Width / 2.0
    $g.DrawEllipse($ring, $r0, $r0, $s - 2.0 * $r0, $s - 2.0 * $r0)

    $inner = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 0x18, 0x1D, 0x26))
    $c0 = $s * 0.28
    $g.FillEllipse($inner, $c0, $c0, $s - 2.0 * $c0, $s - 2.0 * $c0)

    $font = [System.Drawing.Font]::new('Segoe UI', [single]($s * 0.5),
                [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = [System.Drawing.StringFormat]::new()
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $tb = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 0xE8, 0xC0, 0x5F))
    $rc = [System.Drawing.RectangleF]::new(0, 0, $s, $s)
    $g.DrawString([string]'G', $font, $tb, $rc, $fmt)

    # Lock bits to get raw BGRA pixels (top-down scan order)
    $rect = [System.Drawing.Rectangle]::new(0, 0, $size, $size)
    $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                         [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rowBytes = $size * 4
    $topPixels = New-Object byte[] ($rowBytes * $size)
    # Copy row by row (Scan0 may have padding between rows)
    for ($y = 0; $y -lt $size; $y++) {
        $src = [IntPtr]::Add($bd.Scan0, $y * $bd.Stride)
        [System.Runtime.InteropServices.Marshal]::Copy($src, $topPixels, $y * $rowBytes, $rowBytes)
    }
    $bmp.UnlockBits($bd)

    # Flip vertically for BMP bottom-up format
    $xorData = New-Object byte[] ($rowBytes * $size)
    for ($y = 0; $y -lt $size; $y++) {
        [System.Buffer]::BlockCopy($topPixels, $y * $rowBytes,
                                   $xorData, ($size - 1 - $y) * $rowBytes, $rowBytes)
    }

    # AND mask: bottom-up, 1 bit per pixel, 1 where alpha == 0 (transparent)
    $andRowBytes = [math]::Ceiling($size / 32) * 4   # DWORD-aligned rows
    $andMask = New-Object byte[] ($andRowBytes * $size)
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $alpha = $topPixels[($y * $size + $x) * 4 + 3]
            if ($alpha -eq 0) {
                $by = $size - 1 - $y   # bottom-up row
                $byteIdx = $by * $andRowBytes + [math]::Floor($x / 8)
                $bitIdx = 7 - ($x % 8)
                $andMask[$byteIdx] = $andMask[$byteIdx] -bor ([byte](1 -shl $bitIdx))
            }
        }
    }

    # BITMAPINFOHEADER (40 bytes) - biHeight = size*2 (XOR + AND)
    $hdr = New-Object byte[] 40
    [BitConverter]::GetBytes([uint32]40).CopyTo($hdr, 0)
    [BitConverter]::GetBytes([int32]$size).CopyTo($hdr, 4)
    [BitConverter]::GetBytes([int32]($size * 2)).CopyTo($hdr, 8)
    [BitConverter]::GetBytes([uint16]1).CopyTo($hdr, 12)
    [BitConverter]::GetBytes([uint16]32).CopyTo($hdr, 14)
    [BitConverter]::GetBytes([uint32]0).CopyTo($hdr, 16)      # BI_RGB
    [BitConverter]::GetBytes([uint32]($size * $size * 4)).CopyTo($hdr, 20)
    # remaining bytes = 0 (biXPelsPerMeter, biClrUsed, etc.)

    # Combine: BITMAPINFOHEADER + XOR (bottom-up) + AND (bottom-up)
    $total = $hdr.Length + $xorData.Length + $andMask.Length
    $result = New-Object byte[] $total
    [System.Buffer]::BlockCopy($hdr,      0, $result, 0,               $hdr.Length)
    [System.Buffer]::BlockCopy($xorData,  0, $result, $hdr.Length,     $xorData.Length)
    [System.Buffer]::BlockCopy($andMask,  0, $result, $hdr.Length + $xorData.Length, $andMask.Length)

    $g.Dispose(); $bmp.Dispose(); $body.Dispose(); $ring.Dispose()
    $inner.Dispose(); $font.Dispose(); $fmt.Dispose(); $tb.Dispose()
    return ,$result
}

$sizes = @(16, 32)
$bmpDataList = @()
foreach ($sz in $sizes) { $bmpDataList += ,(New-CoinBmpData $sz) }

# Build ICO container
$blob = [System.Collections.Generic.List[byte]]::new()

# ICONDIR (all little-endian WORDs)
$blob.AddRange([byte[]](0, 0, 1, 0, $sizes.Count, 0))

# Compute offsets
$offset = 6 + 16 * $sizes.Count
$entries = [System.Collections.Generic.List[byte]]::new()
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $sz = $sizes[$i]
    $data = $bmpDataList[$i]
    $len = $data.Length
    $entries.AddRange([byte[]]@(
        $sz, $sz, 0, 0,          # bWidth, bHeight, bColorCount, bReserved
        1, 0,                     # wPlanes  = 1 (LE: low=1, high=0)
        32, 0,                    # wBitCount = 32 (LE: low=32, high=0)
        ($len -band 0xFF), (($len -shr 8) -band 0xFF),
        (($len -shr 16) -band 0xFF), (($len -shr 24) -band 0xFF),
        ($offset -band 0xFF), (($offset -shr 8) -band 0xFF),
        (($offset -shr 16) -band 0xFF), (($offset -shr 24) -band 0xFF)
    ))
    $offset += $len
}
$blob.AddRange($entries)
for ($i = 0; $i -lt $sizes.Count; $i++) { $blob.AddRange($bmpDataList[$i]) }

$out = Join-Path $PSScriptRoot 'app.ico'
[System.IO.File]::WriteAllBytes($out, $blob.ToArray())
Write-Host "Wrote $out ($($blob.Count) bytes)"