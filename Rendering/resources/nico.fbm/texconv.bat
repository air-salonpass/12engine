@echo off
REM UNIT31: nico用PNGをBC7形式のDDSへ圧縮し、全ミップマップを生成する。
REM -f BC7_UNORM : 色・質感画像をBC7_UNORM形式で圧縮する。
REM -m 0         : 1x1までの全ミップマップを自動生成する。
REM -y           : 既存のDDSを確認なしで上書きする。
REM -ft dds      : 出力ファイル形式をDDSにする。
.\texconv.exe -f BC7_UNORM -m 0 -y -ft dds *.png
