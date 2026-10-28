# オンライン通信とロールバックの組み込み

## 現在使える範囲（2026-10-27）

専用ゲームサーバーPCを追加せず、2人の参加PCが入力を交換するP2Pを目指す実装です。
通信と同期をゲームから独立させ、同じ入力で同じ結果になる他ゲームにも接続口を使えるようにしています。
UDPを使うだけで入力遅延や同期ずれがなくなるわけではありません。

| 機能 | 状態 |
| --- | --- |
| Windows IPv4の非ブロッキングUDP | 実装済み。既知IPへの直接送信 |
| 入力のフレーム番号、再送、連続受信番号による確認 | 実装済み |
| ゲームとセッションの番号による取り違え防止 | 実装済み。認証・暗号化とは別 |
| 未受信入力の予測、State保存・復元、再計算 | 実装済み。最大予測は既定8フレーム |
| 確定Stateの不一致検出 | 実装済み。不一致なら進行停止 |
| 別プロセスの自動操作サンプル | 実UDPで確認済み。3D表示や実機操作は含まない |
| 2週間制作のState符号化と入力適用 | 実装済み。実GLBで8場面の再計算を確認 |
| 2週間制作のPeerChannel/RollbackSession接続 | 同一プロセスの2世界・仮想回線で同期を確認 |
| 2週間制作の通常対戦画面から部屋作成/参加/切断 | 実装済み。既知IPv4への直接UDP |
| NativePreviewのPlay、Inspectorから接続 | 未接続 |
| 実ゲーム本体の別PC LAN対戦、インターネット対戦 | 未検証 |
| 部屋一覧、招待、NAT越え、中継、認証、暗号化 | 未実装 |
| 3人以上、サーバー権威型の同期 | 未実装。この同期器は2人用 |

仮想回線の成功と、別PCの対戦が完成したことは区別してください。
実際の3Dゲームの再計算負荷も、小さい共通サンプルの負荷とは別に測ります。

## 処理を探す入口

| ファイル | 役割と呼出し元 |
| --- | --- |
| Networking/UdpTransport.h と .cpp | ソケットの作成・破棄・送受信。NetworkDemo/NetworkTestsから呼ぶ |
| Networking/FrameHistory.h | 固定容量の循環履歴。フレーム番号を照合して上書き後の古い参照を拒否 |
| Networking/RollbackSession.h | 入力予測、保存、復元、再計算。ゲームのコールバックを呼ぶ |
| Networking/PeerChannel.h | 入力パケットの符号化・検査・再送・ACK・チェックサム比較。通信APIとは独立 |
| Networking/RoomHandshake.h | 内容の互換性比較、開始Stateの分割転送、ACK、再戦の同意。送信元固定とタイムアウトは呼出し側 |
| Networking/StateArchive.h | Stateをフィールドごとにbytesへ読み書きする共通部品 |
| Networking/ReplayEffects.h | 再計算中の音・振動等の外部出力を抑止するscope |
| Tools/NetworkSampleGame.h | 入力2バイト・State34バイトの小さい使用例 |
| Tools/NetworkDemo/NetworkDemo.cpp | ソケット→PeerChannel→RollbackSession→ゲームの呼出し順 |
| Tools/NetworkTests/NetworkTests.cpp | 欠落・重複・逆順・履歴循環・不一致・実UDP・Archiveと接続口の検証 |
| Tools/RunNetworkDemo.ps1 | 同じPCで2つのサンプルを起動し、終了と最終ハッシュを比較 |

NetworkingのパスはRendering配下です。

## ビルドして別プロセスで試す

Visual Studio 2022のDeveloper PowerShellで、リポジトリ直下から実行します。

```powershell
msbuild Tools/NetworkTests/NetworkTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
msbuild Tools/NetworkDemo/NetworkDemo.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
./Build/Networking/Debug/NetworkTests.exe
./Tools/RunNetworkDemo.ps1 -Configuration Debug -Frames 180
```

Releaseでも構成名を変更して実行します。
RunNetworkDemoは空きポートとセッション番号を作り、起動した2つの子プロセスだけを管理します。
SYNC OKと同じhash、終了コード0が両側で出れば、その試験の最終状態が一致しています。
ログはBuild/Networking/構成名配下です。
自動操作サンプルなので、キャラクターを実際に操作する試験ではありません。

既知のLAN IPへ送る例です。bindはそのPC自身のアドレス、peerは相手のアドレスです。
同じgame/セッション、異なるplayer、対応する送受信ポートを指定します。

```powershell
./Build/Networking/Release/NetworkDemo.exe --player 0 --bind 0.0.0.0 --peer 192.168.1.20 --local-port 42000 --peer-port 42001 --session 12345 --frames 300
./Build/Networking/Release/NetworkDemo.exe --player 1 --bind 0.0.0.0 --peer 192.168.1.10 --local-port 42001 --peer-port 42000 --session 12345 --frames 300
```

上のIPは例です。現在のPCや相手の実IPを表す値ではありません。
LANの別PCでの動作、学校のネットワーク条件やファイアウォールの許可は別途確認します。
実装はポート開放やファイアウォール設定を自動変更しません。

## ゲーム側が用意する処理

rollback_callbacksには次を設定します。

- save: 今の試合Stateをbytesへ書く。
- load: 保存済みStateを戻す。全部読み終えて検査してから現在の世界へ反映する。
- step: P1/P2順の入力を使い、固定1フレームだけ進める。replay=trueの出力重複をゲーム側で抑止する。
- predict_remote: 任意。押した瞬間や方向イベントを含む場合は、次の予測へ長押しだけを引き継ぐ。
- state_checksum: 任意。表示設定も保存するゲームは、保存Stateから戦闘だけの比較値を取り出す。

predict_remote未指定なら従来どおり直前入力を繰り返します。戻り値の長さがinput_sizeと違う場合は停止します。
state_checksum未指定なら保存bytes全体のFNVを使います。確定する前のStateにはチェックサムを求めません。
カメラや端末情報を除く場合も、ゲーム結果に関わる値を誤って除かないようテストしてください。

Stateには座標だけでなく、前回入力、コマンド猶予、技タイマー、乱数生成器の現在状態、Entity生成番号、将来の動作へ影響する骨姿勢等も含めます。
GPUリソース、ソケット、OS機器オブジェクト、C++ポインター、構造体のpaddingをそのまま保存しません。
同じ入力で同じ結果になる更新が前提です。Componentがあるだけでは自動同期されません。
他ゲームでは保存項目と入力形式を、そのゲームのデータに合わせて定義します。

## 更新と通信の順序

1. ソケットから受信できるものを上限付きで読む。設定した相手の送信元アドレス/ポートだけをPeerChannelへ渡す。
2. PeerChannelへパケットを渡す。過去の入力が予測と違えば、保存Stateから再計算する。
3. can_advanceを確認してから、その固定フレームのローカル入力を消費してadvanceする。
4. outgoingを作り、相手へ送る。未確認の古い入力も再送する。
5. 描画、終了判定、接続待ちのタイムアウト等を呼出し側で進める。

予測上限まで入力が届かなければ戦闘更新を待ちます。描画や受信も一緒に止めないでください。
入力を先に消費してからcan_advanceで失敗すると押下を失うため、順序を守ります。
最小化や停止中の入力、切断、待ち時間、ネットワーク状態表示はアプリ側の責務です。
送信元確認やセッション番号は認証の代用ではありません。
再計算・送受信は同じスレッドから呼ぶ前提で、任意のスレッドから同時に操作する設計ではありません。

## 上限とデータの検査

入力は1〜64バイト、履歴は256フレーム、最大予測は既定8フレームです。
StateはRollbackSessionで既定1MiB/回です。大きいStateを設定すれば、メモリとコピー時間も増えます。
パケットは明示的なビッグエンディアンの形式で、最大1200バイト、入力記録は最大16件です。
magic、形式番号、長さ、プレイヤー、入力サイズ、試合番号、ゲーム番号、未来すぎる入力やACK等を検査します。
同じフレームの確定入力を後から変えることも拒否します。
FNVは同期の診断用です。改ざん防止、認証、暗号化の機能ではありません。

StateArchiveの上限はデータ4MiB、配列/文字列16384要素、復号中の動的割当合計32MiBです。
整数64bit、float32bit、bool、文字列、配列、vectorを決めた順序で扱い、独自型はstate_fieldsで列挙します。
切れた入力、余った末尾、不正bool、範囲外整数、非有限floatを拒否します。
試合のCodecはこの共通部品に加え、Entity参照や技の種別、骨階層、Assetとの一致等を検査します。

## 再計算中の外部出力

replay_effect_gateを使い、再計算する範囲でauto replay = gate.suppress()を保持します。
音や振動の入口はgate.suppressed()なら要求を出さずに戻ります。ゲーム状態の更新自体は止めません。
入れ子と例外でも抑止状態を元へ戻します。gateのdepthをゲームStateへ保存しません。
2週間制作ではGameAudioのtick/play/loop全てへ接続し、直接の命中・着地音も覆っています。
replay_effect_gate単独では再計算中の要求を破棄します。オンラインの2週間制作はConfirmedEffects.hを併用し、フレームごとに要求を記録・訂正で置換し、入力確定後に一度だけ出力します。

## 2週間制作の実装

ゲーム固有の処理は2週間制作だけにあります。共通エンジンへ格闘ゲームの項目を追加していません。

- CombatFrameInput.h: 64バイトの固定フレーム入力。方向列18件まで、最終長押しと短押しを分ける。
- CombatCheckpoint.h: 試合CPU状態のメモリ上の保持。
- CombatStateFields.h/CombatStateCodec.h: 23 Component、Resource、runtimeを項目ごとに符号化・検査。
- CombatCheckpoint.cpp: bytes取得/復元、今のOS入力採取を残すload、実ゲームのコールバック接続と自動検証。
- Documentation/COMBAT_STATE_SERIALIZATION_GUIDE.md: 保存項目の追加、比較範囲、確認手順、現在の制約。

--rollback-smoke-testで実GLBの8場面をbytesから復元して120更新ずつ比較します。
同じAssetを使う2つのCPU世界へPeerChannel/RollbackSessionを接続し、欠落・待ち・重複・逆順配送のある仮想回線で360更新の通常再生と一致することも確認します。
2人目の画角・表示名・モデル可視状態が異なっても、戦闘チェックサムは一致するように分けています。
OnlineBattle.cppとRoomHandshake.hで、通常画面の部屋作成/参加/切断と実時間UDPを接続しています。同一PCの2プロセスで開始State転送と360更新の一致を確認しています。音イベントの確定出力と部屋内再戦も実装済みです。別PCのLAN実機確認は残っています。
仮想回線の検証tickは、実際の回線遅延のミリ秒値を測ったものではありません。

## Editor、保存、検証の範囲

共通部品に新しいComponent/Inspector項目はありません。
既存のScene/Prefab保存、複製、Undoは変更しません。試合StateはEditorの保存形式やUndoの代用品ではありません。
エンジンのPlayからオンライン接続する導線もまだありません。

NetworkTestsは履歴循環、不正パケット、予測上限、状態復元、前回入力と乱数、20%欠落設定・順序逆転・重複を含む1200更新、故意の不一致を検証します。
同じPCの実UDPで300更新を進め、通常再生と比較します。
Archiveの異常系、外部出力scopeの入れ子/例外、独自チェックサムの確定時だけの呼出しも確認します。
RunNetworkDemoは別プロセスの実UDP確認です。
実ゲームの操作感、別PC、WAN、認証/中継、長時間対戦の確認は別途必要です。描画と通信を同時に動かす2プロセス自動検証も追加しています。


2026-10-27追記：2週間制作のラウンド準備・暗転待機を固定60Hzの更新へ移しました。Frontendの描画は待機数を変更しません。KOから次ラウンドへ描画なしで180更新を再実行比較し、仮想回線の360更新では開始演出と戦闘を含めて照合します。ゲームState形式はversion=2です。エンジン共通APIの変更はありません。通常対戦画面と実UDPへの接続は下記の追記で対応済みです。別PCのLAN確認は残っています。


## 2026-10-27 通常対戦画面とUDPへの接続

Networking/RoomHandshake.hは最大1MiBの開始Stateを1024バイトずつACK付きで送ります。ゲーム番号・試合番号・参加nonceを検査し、完了後はPeerChannelで入力だけを交換します。送信元IP/ポートの固定とタイムアウトはゲーム側の責任です。認証やNAT越えは含みません。
2週間制作のVERSUS → LAN接続から、部屋作成・参加・切断ができます。ホストは1P、参加側は2Pです。開始Stateはホストのものへ合わせ、操作機器は各PCで選びます。Escメニュー中も試合は進み、自分の操作は中立になります。切断は開始前のオフライン状態へ戻します。
共通部品とNetworkTestsはチーム用・個人用にも反映しています。ゲーム固有のUI・呼出しは2週間制作だけです。Scene/Prefab保存、Inspector、複製/Undo、NativePreviewのPlayには未接続です。
2週間制作側のRendering/Documentation/ONLINE_BATTLE_GUIDE.mdに操作、呼出し順、制約、他ゲームへの導入、Debug/Release検証をまとめています。同一PCの2プロセスで実UDPと描画を併用した360更新を検証します。別PCの操作感やWAN対応は未検証です。


## 2026-10-27 互換性・確定イベント・部屋内再戦

RoomHandshakeの制御形式はversion=2、ヘッダー70バイト、最大1094バイトです。State形式・実行コード・素材の3指紋を比較し、不一致時はStateを確保せず拒否理由を返します。ContentFingerprint.hはファイルを一定サイズで読み、相対名順で内容を集約する部品です。FNVは配布物の取り違え検出で、署名ではありません。比較するファイル群はゲーム側が決めます。
再戦はrequest_rematchで双方の同意を集め、ホストがrestartで試合番号を1増やしてStateを再転送します。古い番号のパケットを拒否します。ゲーム側は同期器と確定イベント履歴を作り直し、受信タイムアウトの起点も更新します。
ConfirmedEffects.hはフレームごとの外部出力要求を保持し、再計算で置換、確定後にcommit_throughで一度出力します。2週間制作のGameAudioはone-shotとパリィ等のループ要求を接続しています。BGM/Voice管理は描画ごとの実時間です。入力へ固定の追加待機は入れませんが、音は入力確定を待ちます。オフラインの単純再実行は従来の抑止scopeのままです。
共通部品・テスト・Visual Studio登録はチームと個人にも反映済みです。Inspector、Scene/Prefab保存、複製/Undo、Editor Playからの接続は未実装です。振動・通知を扱う場合はゲーム側で記録と確定出力を接続します。
