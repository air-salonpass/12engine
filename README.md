# 自作エンジン

完成済みの描画エンジンとシェーダープログラミング環境を、1つの Visual Studio ソリューションへ統合した構成です。

## プロジェクト

- `JisakuRendering` (`Rendering/`): Direct3D 11、スプライト、プリミティブ、FBX スキニング、glTF、PBR/IBL、オフスクリーン描画、ブルーム
- `JisakuShaderLab` (`ShaderLab/`): Basic/Lambert/Phong、スプライト、プリミティブ、glTF、ルートモーション、IK、衝突判定、シーン群
- `DirectXTex`: `JisakuShaderLab` が使用するテクスチャライブラリ

教材 PDF、ZIP、インストーラー、古い完成サンプル、`.obj/.pdb/.tlog` などのビルド生成物は統合対象から除外しています。実行に必要なソース、HLSL、モデル、テクスチャ、ヘッダー、ライブラリは含まれています。

## ビルド

Visual Studio 2022 で `JisakuEngine.sln` を開き、`Debug | x64` または `Release | x64` を選択してビルドします。

コマンドラインの場合:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' .\JisakuEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

起動する側を Visual Studio のスタートアッププロジェクトに設定してください。

## 外部要件

`JisakuRendering` の FBX 読み込みには Autodesk FBX SDK 2020.3.1 を使用します。既定の参照先は `C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.1` です。
