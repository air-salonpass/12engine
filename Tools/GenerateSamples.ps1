# 外部モデル・画像を使わず、操作練習用の3D形状とScene/Prefabを再生成する。
# 実行するとresources/samples内の同名見本を上書きする。編集した見本は別名で保存すること。
$ErrorActionPreference = 'Stop'
$sampleRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'Rendering/resources/samples'
$encoding = [Text.UTF8Encoding]::new($false)
foreach ($folder in @('Models', 'Prefabs', 'Maps')) {
    New-Item -ItemType Directory -Path (Join-Path $sampleRoot $folder) -Force | Out-Null
}
function Write-JsonFile($relativePath, $value) {
    [IO.File]::WriteAllText((Join-Path $sampleRoot $relativePath), ($value | ConvertTo-Json -Depth 40), $encoding)
}

# 各面を独立した4頂点にすることで、角で法線を共有せず平らな箱として照明を計算する。
# 原点は底面中央。配置ツールで床へ置いたときに、半分埋まらない寸法にそろえる。
$faces = @(
    @{ n=@(0,0,1); p=@(@(-.5,0,.5),@(.5,0,.5),@(.5,1,.5),@(-.5,1,.5)) },
    @{ n=@(0,0,-1); p=@(@(.5,0,-.5),@(-.5,0,-.5),@(-.5,1,-.5),@(.5,1,-.5)) },
    @{ n=@(1,0,0); p=@(@(.5,0,.5),@(.5,0,-.5),@(.5,1,-.5),@(.5,1,.5)) },
    @{ n=@(-1,0,0); p=@(@(-.5,0,-.5),@(-.5,0,.5),@(-.5,1,.5),@(-.5,1,-.5)) },
    @{ n=@(0,1,0); p=@(@(-.5,1,.5),@(.5,1,.5),@(.5,1,-.5),@(-.5,1,-.5)) },
    @{ n=@(0,-1,0); p=@(@(-.5,0,-.5),@(.5,0,-.5),@(.5,0,.5),@(-.5,0,.5)) }
)
function Write-BoxModel($name, $color, $animated = $false) {
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    foreach ($face in $faces) { foreach ($point in $face.p) { foreach ($v in $point) { $writer.Write([single]$v) } } }
    foreach ($face in $faces) { for ($i=0; $i -lt 4; $i++) { foreach ($v in $face.n) { $writer.Write([single]$v) } } }
    for ($face=0; $face -lt 6; $face++) { foreach ($v in @(0,1,2,0,2,3)) { $writer.Write([uint16]($face*4+$v)) } }
    $views = @(
        @{buffer=0;byteOffset=0;byteLength=288;target=34962},
        @{buffer=0;byteOffset=288;byteLength=288;target=34962},
        @{buffer=0;byteOffset=576;byteLength=72;target=34963}
    )
    $accessors = @(
        @{bufferView=0;componentType=5126;count=24;type='VEC3';min=@(-.5,0,-.5);max=@(.5,1,.5)},
        @{bufferView=1;componentType=5126;count=24;type='VEC3'},
        @{bufferView=2;componentType=5123;count=36;type='SCALAR'}
    )
    $model = [ordered]@{
        asset=@{version='2.0';generator='JisakuEngine sample geometry'}
        scene=0;scenes=@(@{nodes=@(0)});nodes=@(@{mesh=0;name=$name})
        meshes=@(@{primitives=@(@{attributes=@{POSITION=0;NORMAL=1};indices=2;material=0})})
        materials=@(@{name=$name;doubleSided=$true;pbrMetallicRoughness=@{baseColorFactor=$color;metallicFactor=0;roughnessFactor=.8}})
    }
    if ($animated) {
        # ファイル内のノードを上下させる。Entityの位置やColliderは動かない、見た目用のアニメーション。
        foreach ($v in @(0,1,2, 0,0,0, 0,.6,0, 0,0,0)) { $writer.Write([single]$v) }
        $views += @{buffer=0;byteOffset=648;byteLength=12}, @{buffer=0;byteOffset=660;byteLength=36}
        $accessors += @{bufferView=3;componentType=5126;count=3;type='SCALAR';min=@(0);max=@(2)}, @{bufferView=4;componentType=5126;count=3;type='VEC3'}
        $model.animations=@(@{name='Float';samplers=@(@{input=3;output=4;interpolation='LINEAR'});channels=@(@{sampler=0;target=@{node=0;path='translation'}})})
    }
    $writer.Flush()
    $model.bufferViews=$views; $model.accessors=$accessors
    $model.buffers=@(@{byteLength=$stream.Length;uri=('data:application/octet-stream;base64,'+[Convert]::ToBase64String($stream.ToArray()))})
    Write-JsonFile "Models/$name.gltf" $model
    $writer.Dispose(); $stream.Dispose()
}
Write-BoxModel 'Block_Gray' @(.48,.55,.64,1)
Write-BoxModel 'Block_Orange' @(.95,.36,.08,1)
Write-BoxModel 'Block_Green' @(.08,.65,.3,1)
Write-BoxModel 'Block_Blue' @(.08,.35,.95,1)
Write-BoxModel 'Block_Yellow' @(.95,.75,.08,1)
Write-BoxModel 'FloatingMarker' @(.7,.15,.85,1) $true

function New-SampleEntity($name, $model, $position, $scale, $kind='static') {
    $entity = [ordered]@{
        name=$name
        transform=@{position=$position;rotation=@(0,0,0);scale3=$scale}
        model_renderer=@{asset="resources/samples/Models/$model.gltf";visible=$true}
        gameplay_tag='Stage'
        map_object=@{static=($kind -eq 'static');snap_to_grid=$true;grid_size=1;layer=0}
    }
    if ($kind -ne 'decoration') {
        $entity.collider=@{shape=0;center=@(0,.5,0);half_extents=@(.5,.5,.5);layer=0;mask=4294967295;trigger=($kind -eq 'trigger');enabled=$true}
    }
    if ($kind -eq 'static' -or $kind -eq 'dynamic') {
        $entity.rigid_body=@{type=$(if($kind -eq 'static'){0}else{2});mass=1;velocity=@(0,0,0);gravity_scale=1;restitution=.2;friction=.6;enabled=$true}
    }
    if ($kind -eq 'trigger') { $entity.gameplay_tag='Goal' }
    if ($kind -eq 'dynamic') { $entity.gameplay_tag='Crate' }
    return $entity
}
function Write-Prefab($file, $entity) { Write-JsonFile "Prefabs/$file.jprefab" @{format='JisakuEnginePrefab';version=1;entity=$entity} }
Write-Prefab 'Floor_4m' (New-SampleEntity 'Floor_4m' 'Block_Gray' @(0,0,0) @(4,.25,4))
Write-Prefab 'Wall_4m' (New-SampleEntity 'Wall_4m' 'Block_Blue' @(0,0,0) @(4,2,.25))
Write-Prefab 'Crate_Dynamic' (New-SampleEntity 'Crate_Dynamic' 'Block_Orange' @(0,3,0) @(1,1,1) 'dynamic')
Write-Prefab 'Goal_Trigger' (New-SampleEntity 'Goal_Trigger' 'Block_Green' @(0,0,0) @(2,.15,2) 'trigger')
Write-Prefab 'Floating_Marker' (New-SampleEntity 'Floating_Marker' 'FloatingMarker' @(0,1,0) @(.5,.5,.5) 'decoration')
$character = New-SampleEntity 'Controller_Standing' 'Block_Yellow' @(0,3,0) @(.7,1.8,.7) 'controller'
$character.gameplay_tag='Player'
$character.character_controller=@{gravity=19.6;jump_speed=7;enabled=$true}
Write-Prefab 'Controller_Standing' $character

function Write-Scene($file, $objects, $target, $distance) {
    $services = @(
        @{name='Main Camera';camera=@{target=$target;distance=$distance;fov=55;yaw=25;pitch=35;active=$true}},
        @{name='Directional Light';directional_light=@{direction=@(.35,.8,-.45);enabled=$true}},
        @{name='Reflection Settings';reflection_floor=@{enabled=$true;reflection_enabled=$false;height=-.3;color=@(.08,.1,.14);ibl_intensity=0;exposure=1.15;vsync=$true}}
    )
    Write-JsonFile "Maps/$file.jscene" @{format='JisakuEngineScene';version=1;entities=($services+@($objects))}
}
$map = @()
for($z=-1;$z -le 1;$z++){for($x=-1;$x -le 1;$x++){
    $map += New-SampleEntity "Floor_${x}_${z}" 'Block_Gray' @(($x*4),0,($z*4)) @(4,.25,4)
}}
$map += New-SampleEntity 'Back_Wall' 'Block_Blue' @(0,.25,6) @(12,2,.25)
$map += New-SampleEntity 'Side_Wall' 'Block_Blue' @(-6,.25,0) @(.25,2,12)
for($i=0;$i -lt 3;$i++) { $map += New-SampleEntity "Step_$i" 'Block_Gray' @((-3+$i*1.5),.25,$i) @(1.5,(($i+1)*.5),2) }
$map += New-SampleEntity 'Goal_Trigger' 'Block_Green' @(3,.25,3) @(2,.15,2) 'trigger'
$map += New-SampleEntity 'Floating_Goal_Marker' 'FloatingMarker' @(3,1,3) @(.4,.4,.4) 'decoration'
$map[-1].animation=@{clip=0;time=0;playing=$true;loop=$true;speed=1}
Write-Scene '01_Map_Placement' $map @(0,.7,0) 18

$physics = @((New-SampleEntity 'Physics_Floor' 'Block_Gray' @(0,0,0) @(12,.5,10)))
for($i=0;$i -lt 3;$i++) { $physics += New-SampleEntity "Falling_Crate_$i" 'Block_Orange' @((-3+$i*2),(3+$i),0) @(1,1,1) 'dynamic' }
$physics += $character
$physics += New-SampleEntity 'Trigger_Zone' 'Block_Green' @(-3,.5,0) @(2,.2,2) 'trigger'
Write-Scene '02_Physics_And_Trigger' $physics @(0,1.5,0) 16

$display = @((New-SampleEntity 'Display_Floor' 'Block_Gray' @(0,0,0) @(10,.25,6)))
for($i=0;$i -lt 3;$i++) {
    $item = New-SampleEntity "Animated_Marker_$i" 'FloatingMarker' @((-3+$i*3),1,0) @(.8,.8,.8) 'decoration'
    $item.animation=@{clip=0;time=$i*.6;playing=$true;loop=$true;speed=($i+1)*.5}
    $display += $item
}
Write-Scene '03_Shared_Animation' $display @(0,1,0) 12
Write-Output '3D見本を生成しました（6モデル・6Prefab・3マップ）。'
