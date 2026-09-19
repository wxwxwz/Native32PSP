param([string]$Source = "$PSScriptRoot/../assets/logo-source.png")
Add-Type -AssemblyName System.Drawing
$project = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$inputImage = [Drawing.Bitmap]::new([IO.Path]::GetFullPath($Source))
try {
    # Remove transparent outer padding, preserving every visible source pixel.
    $left = $inputImage.Width; $top = $inputImage.Height; $right = -1; $bottom = -1
    for ($y = 0; $y -lt $inputImage.Height; $y++) {
        for ($x = 0; $x -lt $inputImage.Width; $x++) {
            if ($inputImage.GetPixel($x,$y).A -gt 0) {
                $left = [Math]::Min($left,$x); $right = [Math]::Max($right,$x)
                $top = [Math]::Min($top,$y); $bottom = [Math]::Max($bottom,$y)
            }
        }
    }
    if ($right -lt $left) { throw 'Source logo is fully transparent' }
    $crop = [Drawing.Rectangle]::new($left,$top,$right-$left+1,$bottom-$top+1)
    foreach ($item in @(@{Name='ICON0';Width=144;Height=80}, @{Name='menu-logo';Width=128;Height=48})) {
        $canvas = [Drawing.Bitmap]::new($item.Width,$item.Height,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($canvas)
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $ratio = [Math]::Min(($item.Width-4)/$crop.Width,($item.Height-4)/$crop.Height)
            $w = [int][Math]::Round($crop.Width*$ratio); $h = [int][Math]::Round($crop.Height*$ratio)
            $dest = [Drawing.Rectangle]::new([int](($item.Width-$w)/2),[int](($item.Height-$h)/2),$w,$h)
            $graphics.DrawImage($inputImage,$dest,$crop,[Drawing.GraphicsUnit]::Pixel)
            $canvas.Save("$project/assets/$($item.Name).png",[Drawing.Imaging.ImageFormat]::Png)
            if ($item.Name -eq 'menu-logo') {
                $text = [Text.StringBuilder]::new()
                [void]$text.Append("#include `"platform/logo_rgba.h`"`nnamespace n32 {`nconst unsigned int kLogoWidth = 128;`nconst unsigned int kLogoHeight = 48;`nconst u32 kLogoPixels[] = {`n")
                for ($y=0; $y -lt $canvas.Height; $y++) {
                    for ($x=0; $x -lt $canvas.Width; $x++) {
                        $pixel = $canvas.GetPixel($x,$y)
                        [void]$text.Append(('0x{0:X2}{1:X2}{2:X2}{3:X2},' -f $pixel.A,$pixel.R,$pixel.G,$pixel.B))
                    }
                    [void]$text.Append("`n")
                }
                [void]$text.Append("};`n}`n")
                [IO.File]::WriteAllText("$project/src/platform/logo_rgba.cpp",$text.ToString())
            }
        } finally { $graphics.Dispose(); $canvas.Dispose() }
    }
} finally { $inputImage.Dispose() }
