param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$rendering = Join-Path $repository 'Rendering'

# 旧APIのファイルが再び置かれると、プロジェクトへ未登録でもチームメンバーが
# 誤って利用できてしまう。物理ファイルとビルド設定の両方を検査する。
$forbiddenPaths = @(
    # Bin以下にローカル生成された無視対象の.libが残っていても、ソースや公開Headerがなければ依存ではない。
    'DirectXTK-main\Inc',
    'framebuffer.cpp', 'framebuffer.h',
    'framework.cpp', 'framework.h',
    'gltf_model.cpp', 'gltf_model.h',
    'shader.cpp', 'shader.h',
    'skinned_mesh.cpp', 'skinned_mesh.h',
    'sprite.cpp', 'sprite.h',
    'static_mesh.cpp', 'static_mesh.h',
    'texture.cpp', 'texture.h',
    'imgui\imgui_impl_dx11.cpp', 'imgui\imgui_impl_dx11.h'
)

$errors = [System.Collections.Generic.List[string]]::new()
foreach ($relativePath in $forbiddenPaths) {
    $candidate = Join-Path $rendering $relativePath
    if (Test-Path -LiteralPath $candidate) {
        $errors.Add("旧D3D11資産が残っています: Rendering\$relativePath")
    }
}

$projectPath = Join-Path $rendering '3dgp.vcxproj'
$projectText = Get-Content -LiteralPath $projectPath -Raw
$forbiddenProjectPatterns = @(
    @{ Pattern = '(?i)d3d11\.lib'; Name = 'd3d11.lib' },
    @{ Pattern = '(?i)imgui_impl_dx11'; Name = 'imgui_impl_dx11' },
    @{ Pattern = '(?i)DirectXTK'; Name = 'DirectXTK' }
)
foreach ($entry in $forbiddenProjectPatterns) {
    if ($projectText -match $entry.Pattern) {
        $errors.Add("プロジェクトに旧依存が含まれています: $($entry.Name)")
    }
}

# エンジン本体だけを対象にAPI型を検査する。教材保管用ShaderLibraryと
# 外部ライブラリは実行経路ではないため、この検査対象へ含めない。
$sourceRoots = @('DirectX12', 'Components', 'Scene', 'Threading') |
    ForEach-Object { Join-Path $rendering $_ }
$sourceFiles = foreach ($sourceRoot in $sourceRoots) {
    Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Include *.h,*.cpp
}
$sourceFiles += Get-Item -LiteralPath (Join-Path $rendering 'main.cpp')
$sourceFiles += Get-Item -LiteralPath (Join-Path $rendering 'WindowApplication.h')

$forbiddenApiPattern = '(?i)\bID3D11|\bD3D11_CREATE|\bD3D11CreateDevice|imgui_impl_dx11'
foreach ($file in $sourceFiles) {
    $match = Select-String -LiteralPath $file.FullName -Pattern $forbiddenApiPattern
    if ($match) {
        $relative = [System.IO.Path]::GetRelativePath($repository, $file.FullName)
        $errors.Add("旧D3D11 APIの参照があります: $relative")
    }
}

if ($errors.Count -ne 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'Native DirectX 12検査: OK（旧D3D11実装・リンク依存なし）' -ForegroundColor Green
