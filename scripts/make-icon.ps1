$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$assets=Join-Path (Split-Path $PSScriptRoot -Parent) 'assets'
function Rounded([single]$x,[single]$y,[single]$w,[single]$h,[single]$r) {
    $p=[Drawing.Drawing2D.GraphicsPath]::new()
    $d=$r*2
    $p.AddArc($x,$y,$d,$d,180,90); $p.AddArc($x+$w-$d,$y,$d,$d,270,90)
    $p.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90); $p.AddArc($x,$y+$h-$d,$d,$d,90,90)
    $p.CloseFigure(); return ,$p
}
$frames=@()
foreach($size in @(16,20,24,32,48,64,128,256)) {
    $bitmap=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g=[Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.ScaleTransform($size/256.0,$size/256.0)
    $bg=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#2459D3'))
    $white=[Drawing.SolidBrush]::new([Drawing.Color]::White)
    $cyan=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#6EE7D8'))
    $p=Rounded 8 8 240 240 54; $g.FillPath($bg,$p); $p.Dispose()
    $p=Rounded 87 53 82 49 15; $g.FillPath($white,$p); $p.Dispose()
    $p=Rounded 101 66 54 32 4; $g.FillPath($bg,$p); $p.Dispose()
    $p=Rounded 43 87 170 121 23; $g.FillPath($white,$p); $p.Dispose()
    $g.FillRectangle($bg,43,131,170,12)
    $p=Rounded 112 123 32 31 6; $g.FillPath($cyan,$p); $p.Dispose()
    if($size -eq 256) { $bitmap.Save((Join-Path $assets 'toolbox-preview.png'),[Drawing.Imaging.ImageFormat]::Png) }
    $stream=[IO.MemoryStream]::new(); $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
    $frames+=@{size=$size;bytes=$stream.ToArray()}
    $stream.Dispose();$g.Dispose();$bitmap.Dispose();$bg.Dispose();$white.Dispose();$cyan.Dispose()
}
$output=[IO.File]::Create((Join-Path $assets 'toolbox.ico'));$writer=[IO.BinaryWriter]::new($output)
$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$frames.Count)
$offset=6+16*$frames.Count
foreach($frame in $frames) {
    $dimension=if($frame.size -eq 256){0}else{$frame.size}
    $writer.Write([byte]$dimension);$writer.Write([byte]$dimension)
    $writer.Write([byte]0);$writer.Write([byte]0);$writer.Write([uint16]1);$writer.Write([uint16]32)
    $writer.Write([uint32]$frame.bytes.Length);$writer.Write([uint32]$offset)
    $offset+=$frame.bytes.Length
}
foreach($frame in $frames){$writer.Write([byte[]]$frame.bytes)}
$writer.Dispose();$output.Dispose()
