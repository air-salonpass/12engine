# 自作エンジン

完成済みの描画エンジンとシェーダーを土台にした Direct3D 11 エンジンです。元教材フォルダーは変更せず、このディレクトリへ組み込んだコピーだけを編集します。

## 現在の機能

- FBX スキニング・アニメーション
- glTF / GLB、PBR マテリアル、IBL
- HDR、ブルーム、トーンマッピング
- スプライト、プリミティブ、OBJ
- ゲーム画面内モデルビューア
  - 任意の `.gltf` / `.glb` をファイルダイアログから読み込み
  - アニメーションクリップ選択、再生、一時停止、ループ、速度、時間スクラブ
  - PBR マテリアル値、ノード数、メッシュ数、シーンノード一覧
  - 位置・回転・スケール編集
  - 原点グリッドとモデル境界ボックス
  - 右ドラッグでオービット、中ドラッグで平行移動、ホイールでズーム、`F` でモデルへフォーカス

シェーダープログラミング側から再利用する HLSL は `Rendering/ShaderLibrary/` に保存しています。ゲープロ側の C++、シーン、衝突判定、DirectXTex プロジェクトはビルド対象から削除済みです。

## ビルド

Visual Studio 2022 で `JisakuEngine.sln` を開き、`Debug | x64` または `Release | x64` を選択してビルドします。

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' .\JisakuEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

実行時の作業ディレクトリは `Rendering` にしてください。

## 外部要件

FBX 読み込みには Autodesk FBX SDK 2020.3.1 を使用します。既定の参照先は `C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.1` です。
