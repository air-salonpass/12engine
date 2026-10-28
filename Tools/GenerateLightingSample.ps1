# 外部画像を使わない材質球と、照明・影の調整用3D見本を生成する。
# このスクリプトが上書きするのはMaterialStudyと04/05の見本だけ。編集結果は別名で保存する。
$ErrorActionPreference = 'Stop'
$sampleRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'Rendering/resources/samples'
$encoding = [Text.UTF8Encoding]::new($false)
foreach($folder in @('Models','Maps')) { New-Item -ItemType Directory -Path (Join-Path $sampleRoot $folder) -Force | Out-Null }
function Write-SampleJson($path,$value) {
    [IO.File]::WriteAllText((Join-Path $sampleRoot $path),($value | ConvertTo-Json -Depth 40),$encoding)
}

# 球面上の位置を法線にも使う。立方体よりもハイライトの幅と金属反射を比較しやすい。
$rings=24; $segments=48
$vertexCount=($rings+1)*($segments+1)
$positions=[Collections.Generic.List[single]]::new()
$normals=[Collections.Generic.List[single]]::new()
$indices=[Collections.Generic.List[uint16]]::new()
for($ring=0;$ring -le $rings;$ring++) {
    $theta=[Math]::PI*$ring/$rings
    for($segment=0;$segment -le $segments;$segment++) {
        $phi=2*[Math]::PI*$segment/$segments
        $x=[Math]::Sin($theta)*[Math]::Cos($phi)
        $y=[Math]::Cos($theta)
        $z=[Math]::Sin($theta)*[Math]::Sin($phi)
        foreach($v in @($x,($y+1),$z)) { $positions.Add([single]$v) }
        foreach($v in @($x,$y,$z)) { $normals.Add([single]$v) }
    }
}
for($ring=0;$ring -lt $rings;$ring++) {
    for($segment=0;$segment -lt $segments;$segment++) {
        $a=$ring*($segments+1)+$segment; $b=$a+$segments+1
        if($ring -ne 0) { foreach($v in @($a,($a+1),$b)) { $indices.Add([uint16]$v) } }
        if($ring -ne $rings-1) { foreach($v in @(($a+1),($b+1),$b)) { $indices.Add([uint16]$v) } }
    }
}
$stream=[IO.MemoryStream]::new(); $writer=[IO.BinaryWriter]::new($stream)
foreach($v in $positions) { $writer.Write([single]$v) }
foreach($v in $normals) { $writer.Write([single]$v) }
foreach($v in $indices) { $writer.Write([uint16]$v) }
$writer.Flush()
$positionBytes=$vertexCount*12
$materials=@(
    @{name='Warm Ceramic';pbrMetallicRoughness=@{baseColorFactor=@(.7,.64,.52,1);metallicFactor=0;roughnessFactor=.28}},
    @{name='Brushed Silver';pbrMetallicRoughness=@{baseColorFactor=@(.72,.76,.82,1);metallicFactor=1;roughnessFactor=.24}},
    @{name='Polished Copper';pbrMetallicRoughness=@{baseColorFactor=@(.72,.31,.15,1);metallicFactor=1;roughnessFactor=.12}},
    @{name='Rough Stone';pbrMetallicRoughness=@{baseColorFactor=@(.21,.25,.29,1);metallicFactor=0;roughnessFactor=.92}}
)
$meshes=@(); $nodes=@()
for($i=0;$i -lt $materials.Count;$i++) {
    $meshes+=@{name=$materials[$i].name;primitives=@(@{attributes=@{POSITION=0;NORMAL=1};indices=2;material=$i})}
    $nodes+=@{name=$materials[$i].name;mesh=$i;translation=@((-4.2+$i*2.8),0,0)}
}
$model=@{
    asset=@{version='2.0';generator='JisakuEngine procedural material study'}
    scene=0;scenes=@(@{nodes=@(0,1,2,3)});nodes=$nodes;meshes=$meshes;materials=$materials
    buffers=@(@{byteLength=$stream.Length;uri=('data:application/octet-stream;base64,'+[Convert]::ToBase64String($stream.ToArray()))})
    bufferViews=@(
        @{buffer=0;byteOffset=0;byteLength=$positionBytes;target=34962},
        @{buffer=0;byteOffset=$positionBytes;byteLength=$positionBytes;target=34962},
        @{buffer=0;byteOffset=($positionBytes*2);byteLength=($indices.Count*2);target=34963}
    )
    accessors=@(
        @{bufferView=0;componentType=5126;count=$vertexCount;type='VEC3';min=@(-1,0,-1);max=@(1,2,1)},
        @{bufferView=1;componentType=5126;count=$vertexCount;type='VEC3'},
        @{bufferView=2;componentType=5123;count=$indices.Count;type='SCALAR'}
    )
}
Write-SampleJson 'Models/MaterialStudy.gltf' $model
$writer.Dispose();$stream.Dispose()

function New-ModelEntity($name,$asset,$position,$scale) {
    return @{
        name=$name;transform=@{position=$position;rotation=@(0,0,0);scale3=$scale}
        model_renderer=@{asset=$asset;visible=$true}
    }
}
function New-LightScene($objects,$distance,$target) {
    $entities=@(
        @{name='Main Camera';camera=@{target=$target;distance=$distance;fov=42;yaw=14;pitch=18;active=$true}},
        @{name='Directional Light';directional_light=@{
            direction=@(-.45,.8,-.6);enabled=$true;color=@(1,.91,.78);intensity=3.5
            sky_color=@(.32,.48,.72);ground_color=@(.18,.14,.11);ambient_intensity=.65
            shadows=$true;shadow_strength=.9;shadow_distance=22;shadow_bias=.0006
        }},
        @{name='Post Process Volume';post_process=@{exposure=1;ambient_occlusion=$true;ao_strength=.9;ao_radius=.6}},
        @{name='Reflection Settings';reflection_floor=@{
            enabled=$true;reflection_enabled=$true;planar_reflection=$true;strength=.35;height=0
            color=@(.19,.21,.23);ibl_intensity=0;exposure=1;vsync=$true
        }}
    )
    return @{format='JisakuEngineScene';version=1;entities=($entities+@($objects))}
}
$objects=@(
    (New-ModelEntity 'Material Spheres' 'resources/samples/Models/MaterialStudy.gltf' @(0,.35,0) @(1,1,1)),
    (New-ModelEntity 'Display Plinth' 'resources/samples/Models/Block_Gray.gltf' @(0,0,0) @(12,.35,3.5))
)
Write-SampleJson 'Maps/04_Lighting_Materials.jscene' (New-LightScene $objects 9 @(0,1.1,0))

# 交換しやすい確認用モデルだけ既存のサンプルAssetを使う。地形・炎・水への依存は増やさない。
# このAssetの著作権・条件はresources/gltf/DamagedHelmet/README.mdに残す。提出用素材とは別扱い。
$artistObjects=@(
    (New-ModelEntity 'Replaceable Helmet Preview' 'resources/gltf/DamagedHelmet/DamagedHelmet.gltf' @(0,1.9,0) @(1.5,1.5,1.5)),
    (New-ModelEntity 'Artist Preview Plinth' 'resources/samples/Models/Block_Gray.gltf' @(0,0,0) @(4,.45,3))
)
$artistObjects[0].transform.rotation=@(0,180,0)
Write-SampleJson 'Maps/05_Artist_Model_Preview.jscene' (New-LightScene $artistObjects 7 @(0,1.4,0))
Write-Output '照明見本を生成しました（手続き材質球1モデル・マップ2種）。'
