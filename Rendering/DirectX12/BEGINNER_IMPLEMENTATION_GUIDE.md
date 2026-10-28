# DirectX 12・Component指向 初参加ガイド

この文書は、DirectX 12やECSを触ったことがないメンバーが、既存機能を壊さずにゲーム機能を追加するための入口です。

機能別の読み場所、モデル描画の流れ、保存対応を含む追加手順は [実装案内](../Documentation/IMPLEMENTATION_MAP.md) から確認できます。

## 最初に覚える境界

- `Components/`：保存したい状態を書く。HP、移動速度、攻撃力など。
- `Scene/SceneRegistry.h`：EntityとComponentの組み合わせを保持する。
- `Systems/`または機能別System：対象Componentを持つEntityだけを毎フレーム処理する。
- `DirectX12/`：GPU Resource、Descriptor、Command List、Fenceを扱う。通常のゲームルールから直接呼ばない。
- `DirectX12/GltfRenderer.cpp`などの埋込みHLSLと`DirectX12/Shaders/`：GPUで動く処理。シェーダーはフォルダー内のファイルだけとは限りません。C++側のRoot Signatureとレジスタ番号を必ず合わせます。

Entityは番号、Componentはデータ、Systemは処理です。キャラクターごとの巨大な継承クラスを作らず、必要なComponentを組み合わせます。

## 例：回転する障害物を追加する

まず`Components/RotatorComponent.h`へ状態だけを定義します。

```cpp
#pragma once

// 回転速度はScene保存やInspector編集ができるゲーム状態としてComponentへ置く。
struct rotator_component
{
    float degrees_per_second = 90.0f;
};
```

次にSystemから、TransformとRotatorを両方持つEntityだけを更新します。

```cpp
void update_rotators(scene_registry& scene, float elapsed_seconds)
{
    scene.for_each<transform_component, rotator_component>(
        [elapsed_seconds](scene_entity, transform_component& transform,
                          const rotator_component& rotator)
        {
            transform.rotation.y += rotator.degrees_per_second * elapsed_seconds;
        });
}
```

実装時はComponentをSceneの保存／読込み、複製、Undo／Redo、Inspectorにも追加します。どれか一つを忘れると「配置はできるが再起動すると消える」といった不具合になります。

## DirectX 12へ描画機能を追加する場合

1. GPU Resourceの所有者を決める。ローカル変数の`ComPtr`をCommand実行前に破棄しない。
2. CBV／SRV／UAV、RTV、DSVのどのDescriptorが必要か決める。
3. Resource Stateを「書く前」「読む前」にBarrierで遷移する。
4. Command Listへ記録し、Rendererが管理するQueueから実行する。
5. GPUが参照中のResourceやDescriptorはFence完了前に再利用・破棄しない。
6. Mesh ShaderやDXRなどはFeature Supportを確認し、非対応GPU用の通常経路を残す。

`std::atomic`はCPUスレッドの停止フラグや準備完了フラグに使います。GPUの完了待ちは`std::atomic`ではなくD3D12 Fenceを使います。複数の値をまとめて公開する場合はmutexで保護します。

## フレーム資源を直接増やす場合

`GraphicsConfig.h`の`frame_count`とSwapChain Buffer数は同じ値を使います。各フレームのCommandAllocator、CommandList、BackBuffer、Fence値は`FrameResource.h`へまとめています。AllocatorをResetする前に、そのフレームのFenceが完了している必要があります。

## Pull Request前の確認

```powershell
.\Tools\VerifyNativeD3D12.ps1
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' .\JisakuEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' .\JisakuEngine.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

DebugではScene／Inspector／Gizmoを確認し、ReleaseではEditor UIへ依存せずゲーム画面が起動することを確認します。
