# Shared manifest, payload, and SDK operations; importing this file performs no packaging.
Set-StrictMode -Version Latest
$script:SnowMsixRepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'snow-shot-ocr-release-runtime.ps1')

function ConvertTo-SnowMsixVersion {
    param([Parameter(Mandatory)][string]$Version)
    if ($Version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:\.0)?$') {
        throw 'MSIX requires a stable three-part version, optionally followed by revision .0.'
    }
    $parts = $Version.Split('.')
    foreach ($part in $parts[0..2]) {
        $number = 0
        if (-not [int]::TryParse($part, [ref]$number) -or $number -gt 65535) {
            throw 'MSIX version components must be in the range 0..65535.'
        }
    }
    if ([int]$parts[0] -eq 0) { throw 'Microsoft Store requires a nonzero major version.' }
    return "$($parts[0]).$($parts[1]).$($parts[2]).0"
}

function Read-SnowMsixIdentity {
    param([string]$Path = (Join-Path $script:SnowMsixRepoRoot 'snow_shot/packaging/msix/store-identity.json'))
    $identity = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    foreach ($name in @('Name', 'Publisher', 'PublisherDisplayName', 'PackageFamilyName', 'StoreId')) {
        if (-not $identity.PSObject.Properties[$name] -or
            [string]::IsNullOrWhiteSpace($identity.$name)) { throw "Missing Store identity field: $name" }
    }
    if ($identity.Name -cnotmatch '^[A-Za-z0-9.-]{3,50}$' -or
        $identity.Publisher -notmatch '^CN=\S' -or $identity.Publisher.Length -gt 8192 -or
        $identity.PublisherDisplayName.Length -gt 256 -or
        $identity.PackageFamilyName -cnotmatch ('^' + [regex]::Escape($identity.Name) + '_[a-z0-9]{13}$') -or
        $identity.StoreId -cnotmatch '^[A-Z0-9]{12}$') { throw 'Invalid Microsoft Store identity.' }
    return $identity
}

function Get-SnowMsixFileExtensions {
    param([switch]$ImagesOnly)
    $headers = @('snow_shot/include/snow_shot/image/supportedimagefiles.h')
    if (-not $ImagesOnly) { $headers += 'snow_shot/include/snow_shot/app/supportedpinfiles.h' }
    $extensions = @(foreach ($header in $headers) {
        $source = Get-Content -LiteralPath (Join-Path $script:SnowMsixRepoRoot $header) -Raw
        foreach ($match in [regex]::Matches($source, '\{"([a-z0-9]+)",\s*(?:ImageFileCodec|PinDocumentFormat)::')) {
            '.' + $match.Groups[1].Value
        }
    })
    if ($extensions.Count -lt 2 -or @($extensions | Sort-Object -Unique).Count -ne $extensions.Count) {
        throw 'The supported pin-file extension declarations could not be read uniquely.'
    }
    return $extensions
}

function Assert-SnowMsixChildPath {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Root)
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "MSIX work path must be inside $Root`: $full"
    }
    $ancestor = $full
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "MSIX work paths may not traverse links: $ancestor"
            }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    return $full
}

function Reset-SnowMsixDirectory {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Root)
    $full = Assert-SnowMsixChildPath -Path $Path -Root $Root
    if (Test-Path -LiteralPath $full) { Remove-Item -LiteralPath $full -Recurse -Force }
    $null = New-Item -ItemType Directory -Path $full -Force
}

function Get-SnowMsixInventory {
    param([Parameter(Mandatory)][string]$Root)
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push([IO.Path]::GetFullPath($Root))
    $files = [Collections.Generic.List[object]]::new()
    while ($pending.Count) {
        $directory = $pending.Pop()
        if ((Get-Item -LiteralPath $directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'MSIX payload may not traverse links.'
        }
        foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
            # Inspect each directory before descending; never enumerate through a junction.
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'MSIX payload may not contain links.' }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) } else { $files.Add($item) }
        }
    }
    return @($files | Sort-Object FullName | ForEach-Object {
        [ordered]@{ Path = [IO.Path]::GetRelativePath($Root, $_.FullName).Replace('\', '/');
            Bytes = $_.Length; Sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}

function Test-SnowMsixPeImage {
    param([Parameter(Mandatory)][string]$Path)
    # Read only DOS/PE header fields, even for large model files. A file's suffix
    # is not evidence that it is data: packaged executable code may be renamed.
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { return $false }
        $stream.Position = 0x3c
        $offset = $reader.ReadUInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 24) { return $false }
        $stream.Position = $offset
        return $reader.ReadUInt32() -eq 0x4550
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Assert-SnowMsixAuditedStage {
    param([Parameter(Mandatory)][string]$Stage, [Parameter(Mandatory)][string]$AuditPath,
        [Parameter(Mandatory)][ValidateSet('x64')][string]$Architecture)
    $audit = Get-Content -LiteralPath $AuditPath -Raw | ConvertFrom-Json
    if ($audit.SchemaVersion -ne 1 -or $audit.DistributionChannel -cne 'store' -or
        $audit.Architecture -cne $Architecture -or -not $audit.StaticCrt -or -not $audit.StaticQt) {
        throw 'MSIX requires the audited static Store stage for the selected architecture.'
    }
    $null = ConvertTo-SnowMsixVersion $audit.PackageVersion
    $actual = @(Get-SnowMsixInventory -Root $Stage)
    $expected = @($audit.InstallFiles | ForEach-Object {
        [ordered]@{ Path = $_.Path; Bytes = $_.Bytes; Sha256 = $_.Sha256 }
    })
    if (($actual | ConvertTo-Json -Depth 4 -Compress) -cne ($expected | ConvertTo-Json -Depth 4 -Compress)) {
        throw 'Store staging files changed after the release audit.'
    }
    $binaryPaths = [Collections.Generic.List[string]]::new()
    foreach ($file in $actual) {
        $forbidden = $file.Path -match '(?i)(?:^|/)(?:__data_directory|snow-shot-installation\.json)$|\.(?:pdb|ilk|iobj|ipdb)$'
        $forbidden = $forbidden -or $file.Path -match '(?i)^bin/(?:[^/]*(?:updater|uninstall|test|benchmark|snow_shot_mini)[^/]*\.(?:exe|dll)|(?:tests?|test-bin|benchmarks?)/)'
        if ($forbidden) {
            throw "Forbidden Store payload: $($file.Path)"
        }
        $path = Join-Path $Stage $file.Path
        $extension = [IO.Path]::GetExtension($file.Path)
        $isPe = Test-SnowMsixPeImage -Path $path
        $binaryExtension = $extension -in @('.exe', '.dll', '.com', '.scr', '.cpl', '.ocx', '.sys', '.drv', '.efi')
        if ($isPe) {
            Assert-SnowPeArchitecture -Path $path -Architecture $Architecture
            $binaryPaths.Add($file.Path)
        } elseif ($binaryExtension) {
            throw "Invalid or unsupported executable in Store payload: $($file.Path)"
        }
        $license = $file.Path.StartsWith('share/snow-shot/licenses/', [StringComparison]::OrdinalIgnoreCase)
        if ($license -and ($file.Path -cne 'share/snow-shot/licenses/LICENSES.txt' -or $file.Bytes -eq 0)) {
            throw 'Store releases must contain one nonempty consolidated LICENSES.txt.'
        }
        if (-not $license -and $extension -in @('.msi', '.msp', '.mst', '.msix', '.appx', '.msixbundle', '.appxbundle', '.appinstaller', '.lnk', '.url',
                '.nsi', '.nsh', '.cmd', '.bat', '.ps1', '.psm1', '.psd1', '.js', '.jse', '.vbs', '.vbe', '.wsf', '.wsh',
                '.hta', '.py', '.pyc', '.pyo', '.sh', '.bash', '.zsh')) {
            throw "Installer or script artifact in Store payload: $($file.Path)"
        }
    }
    foreach ($required in @('bin/snow_shot.exe', 'bin/snow-shot-mcp.exe', 'bin/crashpad_handler.exe',
            'bin/SnowShotExplorerCommand.dll',
            'bin/audios/camera_shutter.mp3', 'bin/assets/ocr/asset-manifest.json',
            'bin/assets/qrcode/detect.prototxt', 'bin/assets/qrcode/detect.caffemodel',
            'bin/assets/qrcode/sr.prototxt', 'bin/assets/qrcode/sr.caffemodel',
            'share/snow-shot/licenses/LICENSES.txt')) {
        if ($required -cnotin $actual.Path) { throw "Missing Store payload: $required" }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $Stage 'bin/assets/ocr/asset-manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 2 -or $manifest.runtime.platform -cne "windows-$Architecture" -or
        $manifest.default_model -cne 'small') { throw 'Store OCR identity does not match the target.' }
    $runtimeRoot = Join-Path $Stage "bin/assets/ocr/runtimes/$($manifest.runtime.version)/windows-$Architecture"
    $expectedBinaries = @('bin/snow_shot.exe', 'bin/snow-shot-mcp.exe', 'bin/crashpad_handler.exe',
        'bin/SnowShotExplorerCommand.dll',
        "bin/assets/ocr/runtimes/$($manifest.runtime.version)/windows-$Architecture/snow-ocr-process-$($manifest.runtime.version)-windows-$Architecture.exe")
    if ((@($binaryPaths | Sort-Object) -join ',') -cne (@($expectedBinaries | Sort-Object) -join ',')) {
        throw 'Store payload executable inventory differs from the static release contract.'
    }
    $model = @($manifest.models | Where-Object { $_.type -ceq 'small' })
    if ($model.Count -ne 1) { throw 'Store OCR requires exactly one default Small model descriptor.' }
    foreach ($component in @(
        @{ Root = $runtimeRoot; Files = $manifest.runtime.files; Id = $manifest.runtime.version },
        @{ Root = (Join-Path $Stage "bin/assets/ocr/models/$($model[0].id)"); Files = $model[0].files; Id = $model[0].id }
    )) {
        $completePath = Join-Path $component.Root '.complete.json'
        if (-not (Test-Path -LiteralPath $completePath -PathType Leaf)) { throw 'Bundled OCR component is incomplete.' }
        $complete = Get-Content -LiteralPath $completePath -Raw | ConvertFrom-Json
        if ($complete.schema -ne 1 -or $complete.component -cne $component.Id) { throw 'Bundled OCR completion marker differs from the pinned component.' }
        foreach ($file in $component.Files) {
            if ($file.name -cnotmatch '^[A-Za-z0-9_.-]+$') { throw 'Unsafe OCR asset filename.' }
            $path = Join-Path $component.Root $file.name
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
                (Get-Item -LiteralPath $path).Length -ne $file.size -or
                (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $file.sha256) {
                throw "Missing or altered bundled OCR asset: $path"
            }
        }
    }
    return $audit
}

function Write-SnowMsixManifest {
    param([Parameter(Mandatory)][string]$Stage, [Parameter(Mandatory)]$Identity,
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][ValidateSet('x64')][string]$Architecture)
    $sourceRoot = Join-Path $script:SnowMsixRepoRoot 'snow_shot/packaging/msix'
    $values = [ordered]@{ Name = $Identity.Name; Publisher = $Identity.Publisher;
        PublisherDisplayName = $Identity.PublisherDisplayName; Version = (ConvertTo-SnowMsixVersion $Version);
        Architecture = $Architecture;
        MinVersion = '10.0.19041.0' }
    $text = Get-Content -LiteralPath (Join-Path $sourceRoot 'AppxManifest.xml.in') -Raw
    foreach ($key in $values.Keys) {
        $text = $text.Replace("@$key@", [Security.SecurityElement]::Escape([string]$values[$key]))
    }
    $extensions = @(Get-SnowMsixFileExtensions)
    $types = @($extensions | ForEach-Object { "              <uap:FileType>$_</uap:FileType>" })
    $text = $text.Replace('@FileTypes@', ($types -join "`n"))
    $images = @(Get-SnowMsixFileExtensions -ImagesOnly)
    $pinActions = @('Pin', 'Annotate')
    $imageActions = $pinActions + @('Stitch', 'Split', 'Edit', 'Compress')
    $menuTypes = foreach ($extension in $extensions) {
        '            <desktop5:ItemType Type="{0}">' -f $extension
        $actions = if ($images -contains $extension) { $imageActions } else { $pinActions }
        for ($index = 0; $index -lt $actions.Count; $index++) {
            '              <desktop5:Verb Id="{0}" Clsid="3F1E90A2-7D4B-4F21-9D8A-6B0C5E2F70A{1}" />' -f $actions[$index], $index
        }
        '            </desktop5:ItemType>'
    }
    $text = $text.Replace('@ContextMenuItemTypes@', ($menuTypes -join "`n"))
    if ($text -match '@[A-Za-z]+@') { throw 'An MSIX manifest placeholder was not resolved.' }
    $null = [xml]$text
    [IO.File]::WriteAllText((Join-Path $Stage 'AppxManifest.xml'), $text, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'Strings') -Destination $Stage -Recurse -Force
    $null = New-Item -ItemType Directory -Path (Join-Path $Stage 'Assets') -Force
    Copy-Item -Path (Join-Path $sourceRoot 'Assets/*.png') -Destination (Join-Path $Stage 'Assets') -Force
}

function Invoke-SnowMsixTool {
    param([Parameter(Mandatory)][string]$Tool, [Parameter(Mandatory)][string[]]$Arguments)
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Command $Tool -ErrorAction Stop).Source)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    if ([IO.Path]::GetFileName($start.FileName) -ieq 'makepri.exe') {
        $start.StandardOutputEncoding = [Text.Encoding]::Unicode
        $start.StandardErrorEncoding = [Text.Encoding]::Unicode
    }
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        $null = $process.Start()
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $output = $outputTask.GetAwaiter().GetResult()
        $errors = $errorTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "MSIX tool failed ($($process.ExitCode)): $Tool`n$output`n$errors" }
        Write-Verbose ($output + $errors)
    } finally { $process.Dispose() }
}

function New-SnowMsixPackage {
    param([Parameter(Mandatory)][string]$Stage, [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string]$WorkDirectory,
        [string]$MakeAppx = 'makeappx.exe', [string]$MakePri = 'makepri.exe')
    $config = Join-Path $WorkDirectory 'priconfig.xml'
    Invoke-SnowMsixTool $MakePri @('createconfig', '/cf', $config, '/dq', 'en-US', '/o')
    # The package contains all languages/scales. Auto-splitting requires
    # separate resource packages, so keep all candidates in this one resources.pri.
    $configuration = [xml](Get-Content -LiteralPath $config -Raw)
    $null = $configuration.resources.RemoveChild($configuration.resources.packaging)
    $configuration.Save($config)
    $resourceRoot = Join-Path $WorkDirectory ('resources-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $resourceRoot
    foreach ($entry in @('AppxManifest.xml', 'Assets', 'Strings')) {
        Copy-Item -LiteralPath (Join-Path $Stage $entry) -Destination $resourceRoot -Recurse -Force
    }
    Invoke-SnowMsixTool $MakePri @('new', '/pr', $resourceRoot, '/cf', $config, '/of', (Join-Path $Stage 'resources.pri'), '/o')
    # MakeAppx validates the manifest and payload. Never pass /nv.
    Invoke-SnowMsixTool $MakeAppx @('pack', '/d', $Stage, '/p', $Destination, '/o')
}

function Get-SnowMsixPackageIdentity {
    param([Parameter(Mandatory)][string]$Path)
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entry = $archive.GetEntry('AppxManifest.xml')
        if ($null -eq $entry) { throw 'MSIX input has no AppxManifest.xml.' }
        $reader = [IO.StreamReader]::new($entry.Open())
        try { $manifest = [xml]$reader.ReadToEnd() } finally { $reader.Dispose() }
        return $manifest.Package.Identity
    } finally { $archive.Dispose() }
}

function Set-SnowMsixTestSignature {
    param([Parameter(Mandatory)][string]$Package, [Parameter(Mandatory)][string]$CertificateThumbprint,
        [Parameter(Mandatory)][string]$Publisher, [string]$SignTool = 'signtool.exe')
    if ($CertificateThumbprint -cnotmatch '^[A-Fa-f0-9]{40}$') { throw 'Use an explicit certificate SHA-1 thumbprint.' }
    $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint" -ErrorAction Stop
    if (-not $certificate.HasPrivateKey -or $certificate.Subject -cne $Publisher) {
        throw 'The test signing certificate must have a private key and match the manifest Publisher exactly.'
    }
    Invoke-SnowMsixTool $SignTool @('sign', '/fd', 'SHA256', '/sha1', $CertificateThumbprint, $Package)
    Invoke-SnowMsixTool $SignTool @('verify', '/pa', '/v', $Package)
}
