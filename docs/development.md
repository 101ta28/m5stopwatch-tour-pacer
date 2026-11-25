# 開発とテスト

以下のコマンドはリポジトリのルートで実行します。対象ボード向けのビルドと書き込みは[導入手順](setup.md)を参照してください。

## PCで計時と操作をテストする

本体を接続せずに、計時・画面操作・USB設定をテストできます。uvとC++17コンパイラを用意してください。`uv run` はPython 3.10以降を使い、`uv.lock` に記録されたライブラリを `.venv/` に用意します。ファームウェアのビルドに使うESP32用コンパイラは、別途 `uv run scripts/board.py setup` で導入します。[uvの導入手順](https://docs.astral.sh/uv/getting-started/installation/)

| OS | ホストテストの準備 | リポジトリのルートで実行 |
|---|---|---|
| Windows（PowerShell） | MSYS2のUCRT64シェルで `pacman -S mingw-w64-ucrt-x86_64-gcc` を実行し、PowerShellで `$env:Path = "C:\msys64\ucrt64\bin;$env:Path"` を実行 | `uv run scripts/test.py` |
| macOS | `xcode-select --install` でCommand Line Toolsを導入 | `uv run scripts/test.py` |
| Linux | GCCまたはClangを導入 | `uv run scripts/test.py` または `./scripts/test.sh` |

WindowsのテストはMinGW-w64で実行ファイルを作成し、模擬COMポートを通じてPythonの設定コマンドと本体のコマンド処理を接続します。macOSとLinuxでは仮想シリアルポートを使い、C++のテストでAddressSanitizerとUndefinedBehaviorSanitizerを有効にします。コンパイラを変更する場合は環境変数 `CXX` に実行ファイルを指定してください。[MSYS2の導入手順](https://www.msys2.org/) · [AppleのCommand Line Tools](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools/)

## 本体画面をプレビューする

上のホストテストで画面データを作り、`uv run scripts/board.py setup` でM5GFXを導入します。プレビュー用のPillowは、下表の `--group preview` を付けるとuvが導入します。Noto Sans CJK JPのフォントは別途用意してください。フォントが見つからない場合は、`NotoSansCJK-Regular.ttc` のあるフォルダーを環境変数 `NOTO_CJK_DIR` に指定してください。[Noto Sans CJKのTTCファイル](https://github.com/notofonts/noto-cjk/tree/main/Sans/OTC)

| OS | 描画ライブラリの準備 | プレビュー |
|---|---|---|
| Windows | MSYS2のUCRT64シェルで `pacman -S mingw-w64-ucrt-x86_64-SDL2` を実行。ホストテストと同じPATHを使用 | `uv run --group preview scripts/preview.py` |
| macOS | Homebrewで `brew install sdl2-compat` を実行し、`export SDL2_ROOT="$(brew --prefix sdl2-compat)"` を設定 | `uv run --group preview scripts/preview.py` |
| Linux | M5GFXのLinuxフレームバッファ用コードを使用 | `uv run --group preview scripts/preview.py` |

実機と同じ描画コードとフォントで各画面を描画し、文字の重なりや円周での見切れを検査します。一覧は `build/ui-preview.png` に出力します。字形を更新するときは、Regular・Medium・BoldのTTCをそろえ、`uv run --group preview scripts/make-ui-fonts.py` を実行します。

## Wi-Fi設定画面をブラウザーでテストする

Node.js 20以降とnpmを用意します。テストは本体の設定画面をChromiumで開き、設定の取得と保存には模擬データを使います。

```sh
npm install --prefix tests --no-save --package-lock=false playwright@1.63.0
node tests/node_modules/playwright/cli.js install chromium
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

終了日時を指定した場合は開始時に内蔵時計から終了までの秒数を求めます。持ち時間を指定した場合は、その秒数を開始時の残り時間として使います。いずれも単調時計で経過時間を計り、ミリ秒単位で記録します。画面では残り時間を秒単位で切り上げ、所要時間と超過時間は切り捨てます。

[時刻の計算例と振動の条件](../stopwatch-tour-guide-spec.md)

コース設定はNVSの `tour-guide` 名前空間の `course-v1` に保存します。案内の完了記録は、終了時に `record0` と `record1` の古い側へ書き込み、読み戻して検証します。起動時には、CRCが正しい記録のうち最新の有効な世代を使います。
