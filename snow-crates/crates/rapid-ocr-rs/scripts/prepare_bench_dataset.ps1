param(
    [string]$OutputDir = ".bench/images"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$target = Resolve-Path -Path . -ErrorAction SilentlyContinue
if ($null -eq $target) {
    throw "cannot resolve working directory"
}

$absoluteOutput = Join-Path $target $OutputDir
New-Item -ItemType Directory -Path $absoluteOutput -Force | Out-Null

$samples = @(
    @{
        Name = "ch_en_num.jpg"
        Urls = @(
            "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.1.0/resources/test_files/ch_en_num.jpg"
        )
    },
    @{
        Name = "te.png"
        Urls = @(
            "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.1.0/resources/test_files/te.png"
        )
    },
    @{
        Name = "en.png"
        Urls = @(
            "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.1.0/resources/test_files/en.png"
        )
    }
)

foreach ($sample in $samples) {
    $dst = Join-Path $absoluteOutput $sample.Name
    if (Test-Path $dst) {
        Write-Host "skip existing $dst"
        continue
    }
    $downloaded = $false
    foreach ($url in $sample.Urls) {
        try {
            Write-Host "downloading $url -> $dst"
            Invoke-WebRequest -Uri $url -OutFile $dst
            $downloaded = $true
            break
        } catch {
            Write-Warning "download failed from ${url}: $($_.Exception.Message)"
        }
    }

    if ($downloaded) {
        continue
    }

    Write-Warning "all downloads failed for $($sample.Name); generating synthetic image instead"
    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap(320, 64)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::White)
            $font = New-Object System.Drawing.Font("Arial", 20, [System.Drawing.FontStyle]::Bold)
            $brush = [System.Drawing.Brushes]::Black
            $graphics.DrawString("rapidocr bench", $font, $brush, 8, 16)
            $format = if ($sample.Name.ToLower().EndsWith(".jpg")) {
                [System.Drawing.Imaging.ImageFormat]::Jpeg
            } else {
                [System.Drawing.Imaging.ImageFormat]::Png
            }
            $bitmap.Save($dst, $format)
        } finally {
            $graphics.Dispose()
        }
    } finally {
        $bitmap.Dispose()
    }
}

Write-Host "dataset prepared at $absoluteOutput"
