# 操作・当たり判定・Prefab配置を確認できる石造りの3Dコースを生成する。
# 生成対象は06のSceneとStageKitだけ。自分で編集したSceneは別名で保存してから再生成する。
$ErrorActionPreference='Stop'
$root=Join-Path (Split-Path $PSScriptRoot -Parent) 'Rendering'
$samples=Join-Path $root 'resources/samples'
$encoding=[Text.UTF8Encoding]::new($false)
foreach($folder in @('StageKit/Models','StageKit/Prefabs','Maps')) { New-Item -ItemType Directory -Path (Join-Path $samples $folder) -Force | Out-Null }
function Write-StageJson($path,$value) { [IO.File]::WriteAllText((Join-Path $samples $path),($value|ConvertTo-Json -Depth 40),$encoding) }

# 既存の自作生成ジオメトリを複製し、材質だけを用途別にする。外部の地形・画像は使わない。
$source=Join-Path $samples 'Models/Block_Gray.gltf'
if(-not(Test-Path $source)){throw '先にTools/GenerateSamples.ps1で基本見本を生成してください。'}
function New-StageMaterial($name,$color,$metal,$rough,$emission,$stone=$false) {
    $model=Get-Content $source -Raw | ConvertFrom-Json -AsHashtable
    $model.materials=@(@{name=$name;doubleSided=$false;pbrMetallicRoughness=@{baseColorFactor=$color;metallicFactor=$metal;roughnessFactor=$rough};emissiveFactor=$emission;extras=@{jisaku_stone=$stone}})
    $model.nodes[0].name=$name
    Write-StageJson "StageKit/Models/$name.gltf" $model
}
New-StageMaterial 'Limestone' @(.36,.34,.29,1) 0 .85 @(0,0,0) $true
New-StageMaterial 'SandstoneTrim' @(.55,.43,.27,1) .05 .65 @(0,0,0) $true
New-StageMaterial 'WeatheredMetal' @(.11,.18,.17,1) .65 .4 @(0,0,0)
New-StageMaterial 'AmberMarker' @(.8,.45,.06,1) .15 .35 @(.5,.2,.01)
New-StageMaterial 'GreenMarker' @(.08,.5,.22,1) .1 .3 @(.02,.45,.12)
New-StageMaterial 'RedMarker' @(.55,.055,.035,1) 0 .5 @(.45,.015,.005)
New-StageMaterial 'PlayerMarker' @(.12,.36,.62,1) .1 .35 @(0,.025,.06)

function New-Part($name,$material,$position,$size,$kind='static') {
    $entity=[ordered]@{name=$name;transform=@{position=$position;rotation=@(0,0,0);scale3=$size};model_renderer=@{asset="resources/samples/StageKit/Models/$material.gltf";visible=$true};map_object=@{static=($kind -eq 'static' -or $kind -eq 'decor');snap_to_grid=$true;grid_size=.5;layer=0};gameplay_tag='Stage'}
    if($kind -ne 'decor') {
        $entity.collider=@{shape=0;center=@(0,.5,0);half_extents=@(.5,.5,.5);enabled=$true;layer=0;mask=4294967295;trigger=($kind -in 'Checkpoint','Goal','Hazard')}
    }
    if($kind -eq 'static' -or $kind -eq 'dynamic') {
        $entity.rigid_body=@{type=$(if($kind -eq 'dynamic'){2}else{0});mass=2;velocity=@(0,0,0);gravity_scale=1;restitution=.05;friction=.7;enabled=$true}
    }
    if($kind -in 'Checkpoint','Goal','Hazard'){$entity.gameplay_tag=$kind}
    if($kind -eq 'player') {
        $entity.gameplay_tag='Player'
        $entity.character_controller=@{gravity=19.6;jump_speed=7;slope_limit=50;step_height=.45;enabled=$true}
        $entity.playtest_controller=@{move_speed=5;sprint_multiplier=1.8;respawn_height=-6;camera_distance=7;first_person=$true;eye_height=1.55;mouse_sensitivity=.12;field_of_view=75}
    }
    return $entity
}
$parts=[Collections.Generic.List[object]]::new()
function Add-Part($name,$material,$position,$size,$kind='static') {$parts.Add((New-Part $name $material $position $size $kind))}

# 足元0mの下段と、2m高い上段を、狭い橋と段差乗越え確認用の階段でつなぐ。
Add-Part '01_Courtyard_Foundation' 'Limestone' @(0,-.8,-8) @(28,.8,14)
for($x=-3;$x -le 3;$x++){for($z=-3;$z -le -1;$z++){
    Add-Part "01_Paving_${x}_${z}" 'Limestone' @(($x*3.8),0,($z*4+.2)) @(3.72,.04,3.88) 'decor'
}}
Add-Part '01_Back_Wall' 'Limestone' @(0,0,-15) @(28,2.4,.6)
Add-Part '01_West_Wall' 'Limestone' @(-14,0,-8) @(.6,2.4,14)
Add-Part '01_East_Wall' 'Limestone' @(14,0,-8) @(.6,2.4,14)
for($i=-3;$i -le 3;$i++){
    Add-Part "01_Back_Pier_$i" 'SandstoneTrim' @(($i*4),0,-14.7) @(.85,3,.85)
    Add-Part "01_Back_Cap_$i" 'SandstoneTrim' @(($i*4),3,-14.7) @(1.1,.2,1.1) 'decor'
}

# 門は柱と梁を別Entityにし、開口部を単一Box Colliderで塞がない。
foreach($x in @(-2.8,2.8)){
    Add-Part "02_Gate_Column_$x" 'Limestone' @($x,0,-7) @(1.1,3.7,1.1)
    Add-Part "02_Gate_Base_$x" 'SandstoneTrim' @($x,0,-7) @(1.45,.3,1.45)
    Add-Part "02_Gate_Capital_$x" 'SandstoneTrim' @($x,3.5,-7) @(1.5,.4,1.5)
}
Add-Part '02_Gate_Lintel' 'Limestone' @(0,3.9,-7) @(7.5,.75,1.4)
Add-Part '02_Gate_Cornice' 'SandstoneTrim' @(0,4.65,-7) @(7.9,.25,1.7) 'decor'
for($i=0;$i -lt 4;$i++) {
    Add-Part "02_Crate_$i" 'WeatheredMetal' @((-8+($i%2)*1.6),(1.2+[Math]::Floor($i/2)*1.3),(-8+[Math]::Floor($i/2)*1.5)) @(1.1,1.1,1.1) 'dynamic'
}
Add-Part '02_Low_Jump_Obstacle' 'SandstoneTrim' @(5,0,-4) @(4,.65,1)
Add-Part '02_Hazard_Pad' 'RedMarker' @(8,.05,-9) @(2.4,.12,2.4) 'Hazard'
Add-Part '02_Checkpoint_Pad' 'AmberMarker' @(0,.05,-2.2) @(3,.12,1.6) 'Checkpoint'
Add-Part '02_Checkpoint_Beacon' 'AmberMarker' @(-2.3,0,-2.2) @(.25,2.5,.25) 'decor'

Add-Part '03_Bridge_Deck' 'Limestone' @(0,-.45,2) @(4,.45,6)
for($i=0;$i -lt 6;$i++) {Add-Part "03_Bridge_Slab_$i" 'SandstoneTrim' @(0,0,(-.5+$i)) @(3.94,.05,.92) 'decor'}
foreach($x in @(-2.2,2.2)) {
    foreach($z in @(-.8,1.2,3.2,4.8)) {Add-Part "03_Rail_Post_${x}_${z}" 'SandstoneTrim' @($x,-.2,$z) @(.38,1.3,.38)}
    Add-Part "03_Rail_${x}_A" 'WeatheredMetal' @($x,.7,.2) @(.16,.18,2)
    # 後半の手すりに切れ目を残し、落下→Checkpoint復帰を確認できるようにする。
    Add-Part "03_Rail_${x}_B" 'WeatheredMetal' @($x,.7,4) @(.16,.18,1.6)
}
for($i=0;$i -lt 5;$i++) {
    Add-Part "04_Jump_Step_$i" 'Limestone' @(0,0,(5.05+$i*.6)) @(4,(($i+1)*.4),.66)
}
Add-Part '04_Upper_Terrace' 'Limestone' @(0,1.2,11.4) @(18,.8,8.8)
Add-Part '04_Terrace_Back_Wall' 'Limestone' @(0,2,15.6) @(18,1.4,.55)
foreach($x in @(-8.8,8.8)) {
    Add-Part "04_Terrace_Side_$x" 'Limestone' @($x,2,11.4) @(.55,1.1,8.8)
    foreach($z in @(8,12,15)) {
        Add-Part "04_Terrace_Column_${x}_${z}" 'SandstoneTrim' @($x,2,$z) @(.8,2.7,.8)
        Add-Part "04_Terrace_Cap_${x}_${z}" 'SandstoneTrim' @($x,4.7,$z) @(1.1,.2,1.1) 'decor'
    }
}
Add-Part '05_Goal_Pad' 'GreenMarker' @(0,2.02,12.8) @(3,.15,2) 'Goal'
foreach($x in @(-2.3,2.3)) {
    Add-Part "05_Goal_Pillar_$x" 'Limestone' @($x,2,13.2) @(.8,3,.8)
    Add-Part "05_Goal_Light_$x" 'GreenMarker' @($x,4.3,13.2) @(.3,.6,.3) 'decor'
}
Add-Part '05_Goal_Header' 'SandstoneTrim' @(0,5,13.2) @(5.8,.55,1)
Add-Part '06_Player_Tester' 'PlayerMarker' @(0,.15,-11) @(.65,1.7,.65) 'player'

$services=@(
    @{name='Main Camera';camera=@{target=@(0,1,0);distance=42;fov=44;yaw=-28;pitch=54;active=$true}},
    @{name='Directional Light';directional_light=@{direction=@(-.5,.85,-.35);enabled=$true;color=@(1,.9,.73);intensity=3;sky_color=@(.32,.47,.66);ground_color=@(.25,.19,.12);ambient_intensity=.7;shadows=$true;shadow_strength=.85;shadow_distance=48;shadow_bias=.00035}},
    @{name='Reflection Settings';reflection_floor=@{enabled=$false;reflection_enabled=$false;height=-10;color=@(.15,.17,.19);ibl_intensity=0;exposure=1;vsync=$true}},
    @{name='Post Process Volume';post_process=@{exposure=1;ambient_occlusion=$true;ao_strength=.9;ao_radius=.65}}
)
Write-StageJson 'Maps/06_Ruins_Courtyard.jscene' @{format='JisakuEngineScene';version=1;editor=@{start_paused=$true};entities=($services+@($parts.ToArray()))}

function Write-StagePrefab($file,$entity) {Write-StageJson "StageKit/Prefabs/$file.jprefab" @{format='JisakuEnginePrefab';version=1;entity=$entity}}
Write-StagePrefab 'StoneTile_4m' (New-Part 'StoneTile_4m' 'Limestone' @(0,0,0) @(4,.25,4))
Write-StagePrefab 'StoneWall_4m' (New-Part 'StoneWall_4m' 'Limestone' @(0,0,0) @(4,2.4,.6))
Write-StagePrefab 'Column_3m' (New-Part 'Column_3m' 'SandstoneTrim' @(0,0,0) @(.8,3,.8))
Write-StagePrefab 'Bridge_Tile' (New-Part 'Bridge_Tile' 'Limestone' @(0,0,0) @(4,.45,1))
Write-StagePrefab 'Physics_Crate' (New-Part 'Physics_Crate' 'WeatheredMetal' @(0,2,0) @(1.1,1.1,1.1) 'dynamic')
Write-StagePrefab 'Checkpoint' (New-Part 'Checkpoint' 'AmberMarker' @(0,0,0) @(3,.12,1.6) 'Checkpoint')
Write-StagePrefab 'Goal' (New-Part 'Goal' 'GreenMarker' @(0,0,0) @(3,.15,2) 'Goal')
Write-StagePrefab 'Player_Tester' (New-Part 'Player_Tester' 'PlayerMarker' @(0,.15,0) @(.65,1.7,.65) 'player')
Write-Output "3Dステージ生成: $($parts.Count) 部品、7材質モデル、9Prefab。"

Write-StagePrefab 'Player_FirstPerson' (New-Part 'Player_FirstPerson' 'PlayerMarker' @(0,.15,0) @(.65,1.7,.65) 'player')
