# StopWatchへの書き込み

対象はM5Stack StopWatch（C152）です。Arduino IDEまたはArduino CLIのどちらかを使います。

初回の導入では、パーティション配置が異なるファームウェアの保存データは引き継げません。

**このアプリの記録を残して更新する場合**は、同梱の `partitions.csv` の配置を保ち、以下の手順で書き込んでください。全体画像（`*.merged.bin`）を書き込むと保存記録も消えます。

## Arduino IDEを使う

1. 「追加のボードマネージャのURL」に次のURLを登録します。

   ```text
   https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
   ```

2. ボードマネージャでM5Stack（3.3.7）、ライブラリマネージャでM5Unified（0.2.28）とM5GFX（0.3.0）を導入します。括弧内はビルドを確認したバージョンです。
3. [StopWatchTourGuide.ino](../StopWatchTourGuide/StopWatchTourGuide.ino) を開き、ボードに `M5StopWatch` を選びます。16 MB Flash、8 MB OPI PSRAMの設定で「検証」を実行します。
4. StopWatchをUSBで接続し、接続先のポートを選んで書き込みます。起動後、待機画面で現在日時を確認してください。

## Arduino CLIを使う（Linux）

Bash、Python 3、Arduino CLIを用意し、リポジトリのルートで実行します。CLIがPATHにない場合は、環境変数 `ARDUINO_CLI` に実行ファイルのパスを指定してください。

1. 初回はボードパッケージとライブラリを導入し、ファームウェアをビルドします。依存ファイルは `.arduino/`、ビルド結果は `build/firmware/` に入ります。ソースを変更した後は2行目を再実行します。

   ```sh
   ./scripts/setup-arduino.sh
   ./scripts/build-board.sh
   ```

2. StopWatchをダウンロードモードでUSB接続します。次のポート名を実際の接続先に置き換えて、書き込みます。

   ```sh
   ./scripts/upload-board.sh /dev/ttyACM0
   ```

3. 「時刻同期完了」と表示されるまでUSBをつないでおきます。端末が自動で起動しないときは、電源ボタンを短く1回押してください。スクリプトがPCの現地日時を端末に設定し、読み戻した日時を表示します。

## 時計を合わせ直す

本体で合わせるには、待機画面の「現在」をタップします。「年」「月」「日」「時」「分」を選び、「−」「＋」で合わせて「確定」を押すと、秒は00になります。

LinuxでPCの時計に合わせるには、本体を通常モードの待機画面にしてUSB接続し、次を実行します。

```sh
python3 scripts/sync-clock.py /dev/ttyACM0
```

電源を切った後も内蔵時計はバッテリーを使います。バッテリーが切れて時計が止まったときは、充電後に合わせ直してください。

[案内を始める](../README.md#案内を始める)
