# MemoryGuard

空きメモリが減ったときだけ、アロケータ(Scudo)が抱えている未使用ページを OS に返す mod です。
ゲーム本体はフック・パッチしません。署名もバージョン依存もないので、Minecraft が更新されても壊れません。

## 動作
- 有効化から30秒待つ(ワールド読み込み中は何もしない)
- 5秒ごとに /proc/meminfo の MemAvailable を確認
- MemAvailable が MemTotal の15%(下限800MB・上限1.5GB)を下回ったら `mallopt(M_PURGE_ALL)` を実行
  (Pixel 10a 想定の8GB機では約1.2GB)
- 連続実行は30秒に1回まで。バックグラウンドスレッドは低優先度
- ログ: logcat タグ `MemoryGuard`(解放が走ったときだけ出力)

## 限界
- 解放できるのは「アロケータが保持している未使用メモリ」だけです。
  チャンク・テクスチャ・エンティティなど、ゲームが実際に使っているメモリは減りません。
- それが原因の落ち方(巨大ワールド・多数の mod・高い描画距離)には、描画距離・シミュレーション距離を下げるのが確実です。

## ビルド(GitHub Actions)
1. このフォルダを GitHub の新しいリポジトリに push
2. Actions タブ → build の完了を待つ
3. Artifacts の `MemoryGuard-levipack` をダウンロードして解凍し、`MemoryGuard.levipack` を LeviLaunchroid でインポート

ローカルビルド: Android NDK を用意して
`cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release && cmake --build build`

## 調整
閾値・待ち時間は `src/MemoryGuard.cpp` 冒頭の定数です。
