param(
    [string]$OutputPath,
    [int]$X = 180,
    [int]$Y = 160,
    [int]$Width = 960,
    [int]$Height = 540,
    [int]$IntervalMs = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

$resolvedOutputPath = if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    Join-Path $env:TEMP "snow_capture_workload_window.json"
} else {
    [System.IO.Path]::GetFullPath($OutputPath)
}

$parentDirectory = Split-Path -Parent $resolvedOutputPath
if ($parentDirectory -and -not (Test-Path $parentDirectory)) {
    New-Item -ItemType Directory -Path $parentDirectory -Force | Out-Null
}
if (Test-Path $resolvedOutputPath) {
    Remove-Item $resolvedOutputPath -Force
}

$window = New-Object System.Windows.Window
$window.Title = "snow-capture-perf-window"
$window.WindowStyle = [System.Windows.WindowStyle]::None
$window.ResizeMode = [System.Windows.ResizeMode]::NoResize
$window.ShowInTaskbar = $false
$window.Topmost = $true
$window.Left = $X
$window.Top = $Y
$window.Width = $Width
$window.Height = $Height
$window.Background = [System.Windows.Media.Brushes]::Black

$canvas = New-Object System.Windows.Controls.Canvas
$canvas.Width = $Width
$canvas.Height = $Height
$window.Content = $canvas

$background = New-Object System.Windows.Shapes.Rectangle
$background.Width = $Width
$background.Height = $Height
$null = $canvas.Children.Add($background)

$movingRect = New-Object System.Windows.Shapes.Rectangle
$movingRect.Width = [Math]::Max(40, [Math]::Floor($Width / 6))
$movingRect.Height = [Math]::Max(40, [Math]::Floor($Height / 6))
$null = $canvas.Children.Add($movingRect)

$movingEllipse = New-Object System.Windows.Shapes.Ellipse
$movingEllipse.Width = [Math]::Max(34, [Math]::Floor($Width / 7))
$movingEllipse.Height = [Math]::Max(34, [Math]::Floor($Height / 7))
$null = $canvas.Children.Add($movingEllipse)

$label = New-Object System.Windows.Controls.TextBlock
$label.FontFamily = New-Object System.Windows.Media.FontFamily("Consolas")
$label.FontSize = 28
$label.Foreground = [System.Windows.Media.Brushes]::White
[System.Windows.Controls.Canvas]::SetLeft($label, 16.0)
[System.Windows.Controls.Canvas]::SetTop($label, 10.0)
$null = $canvas.Children.Add($label)

$timer = New-Object System.Windows.Threading.DispatcherTimer
$timer.Interval = [TimeSpan]::FromMilliseconds([Math]::Max(4, $IntervalMs))

$tick = 0
$timer.Add_Tick({
    $script:tick++
    $t = $script:tick

    $r = [byte](($t * 3) % 255)
    $g = [byte](($t * 5) % 255)
    $b = [byte](($t * 7) % 255)
    $background.Fill = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb($r, $g, $b))

    $rectX = [double](($t * 11) % [Math]::Max(1, [int]($Width - $movingRect.Width)))
    $rectY = [double](($t * 7) % [Math]::Max(1, [int]($Height - $movingRect.Height)))
    [System.Windows.Controls.Canvas]::SetLeft($movingRect, $rectX)
    [System.Windows.Controls.Canvas]::SetTop($movingRect, $rectY)
    $movingRect.Fill = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb([byte](255 - $r), $b, $g))

    $ellipseX = [double](($t * 5) % [Math]::Max(1, [int]($Width - $movingEllipse.Width)))
    $ellipseY = [double](($t * 13) % [Math]::Max(1, [int]($Height - $movingEllipse.Height)))
    [System.Windows.Controls.Canvas]::SetLeft($movingEllipse, $ellipseX)
    [System.Windows.Controls.Canvas]::SetTop($movingEllipse, $ellipseY)
    $movingEllipse.Fill = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.Color]::FromRgb($g, [byte](255 - $b), $r))

    $label.Text = "tick=$t"
})

$window.Add_SourceInitialized({
    $helper = New-Object System.Windows.Interop.WindowInteropHelper($window)
    $reportedWidth = [int][Math]::Round($window.ActualWidth)
    $reportedHeight = [int][Math]::Round($window.ActualHeight)
    if ($reportedWidth -le 0) {
        $reportedWidth = [int][Math]::Round($window.Width)
    }
    if ($reportedHeight -le 0) {
        $reportedHeight = [int][Math]::Round($window.Height)
    }
    $payload = [ordered]@{
        hwnd = $helper.Handle.ToInt64()
        pid = $PID
        x = [int]$window.Left
        y = [int]$window.Top
        width = $reportedWidth
        height = $reportedHeight
        interval_ms = $IntervalMs
        created_utc = [DateTime]::UtcNow.ToString("o")
    } | ConvertTo-Json -Depth 4
    Set-Content -Path $resolvedOutputPath -Value $payload -Encoding UTF8
})

$window.Add_Closed({
    $timer.Stop()
})

$timer.Start()
$app = New-Object System.Windows.Application
$null = $app.Run($window)
