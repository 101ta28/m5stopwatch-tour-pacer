# 開発とテスト

以下のコマンドはリポジトリのルートで実行します。対象ボード向けのビルドと書き込みは[導入手順](setup.md)を参照してください。

## PCで計時と操作をテストする

Linux、Bash、C++17コンパイラ、Python 3を使います。C++テストはAddressSanitizerとUndefinedBehaviorSanitizerを有効にして実行します。USBのテストでは、仮想シリアルポートを通して時刻同期とJSON設定のスクリプトを本体のコマンド処理に接続します。

```sh
./scripts/test.sh
```

リーク検査は既定で無効です。対応環境では `ASAN_OPTIONS=detect_leaks=1 ./scripts/test.sh` で有効にできます。テストの対象と実機での確認項目は[検証記録](../VERIFICATION.md)にあります。

## 本体画面をプレビューする

Linuxで上のテストを実行してから、Pillow、Noto Sans CJK、ボードのセットアップで取得したM5GFXを使って画面を描画します。

```sh
python3 scripts/preview.py
```

実機と同じ描画コードとフォントで9画面を描画し、文字の重なりや円周での見切れを検査します。一覧は `build/ui-preview.png` に出力します。フォントの字形を更新するときは `python3 scripts/make-ui-fonts.py` で再生成します。

## Wi-Fi設定画面をブラウザーでテストする

Node.js 20以降とnpmを用意します。テストは本体の設定画面をChromiumで開き、設定の取得と保存には模擬データを使います。

```sh
npm install --prefix tests --no-save --package-lock=false playwright@1.63.0
tests/node_modules/.bin/playwright install chromium
node tests/course-page.cjs
```

導入済みのChromiumを使う場合は2行目を省き、3行目の実行時に環境変数 `CHROMIUM_PATH` で実行ファイルを指定します。

## 機能ごとの実装

| 機能 | 主なファイル |
|---|---|
| 計時・区間切り替え・ボタン判定・振動パターン | [Tour.cpp](../StopWatchTourGuide/src/Tour.cpp) |
| 時計・入力・振動モーターとの接続 | [Device.cpp](../StopWatchTourGuide/src/Device.cpp) |
| 本体の画面と操作 | [App.cpp](../StopWatchTourGuide/src/App.cpp)、[WatchUi.cpp](../StopWatchTourGuide/src/WatchUi.cpp) |
| コースの初期値とWi-Fi経由の編集 | [CourseConfig.h](../StopWatchTourGuide/CourseConfig.h)、[WifiCourseEditor.cpp](../StopWatchTourGuide/src/WifiCourseEditor.cpp)、[CoursePage.h](../StopWatchTourGuide/src/CoursePage.h) |
| 完了記録の保存と復元 | [RecordStore.cpp](../StopWatchTourGuide/src/RecordStore.cpp) |

開始時に内蔵時計から終了までの秒数を求め、その後は単調時計で経過時間を計ります。記録の単位はミリ秒です。画面では残り時間を秒単位で切り上げ、所要時間と超過時間は切り捨てます。  
[時刻の計算例と振動の条件](../stopwatch-tour-guide-spec.md)

コース設定はNVSの `tour-guide` 名前空間の `course-v1` に保存します。案内の完了記録は、終了時に `record0` と `record1` の古い側へ書き込み、読み戻して検証します。起動時には、CRCが正しい記録のうち最新の有効な世代を使います。
