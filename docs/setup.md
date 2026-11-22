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

## Arduino CLIを使う

Windows、macOS、Linuxで、Python 3とArduino CLIを使って書き込めます。Arduino CLIをPATHに追加するか、環境変数 `ARDUINO_CLI` に実行ファイルのパスを指定してください。[Arduino CLIの導入手順](https://arduino.github.io/arduino-cli/1.6/installation/)

次のコマンドはリポジトリのルートで実行します。ボードパッケージとライブラリは `.arduino/`、ビルドしたファームウェアは `build/firmware/` に入ります。`setup` は初回に実行し、ソースを変更したら `build` を再実行します。

### Windows（PowerShell）

Python 3の `py -3` と `arduino-cli` が使える状態で、次を実行します。pySerialは書き込み後の時刻同期、JSON設定、ログの取得に使います。

```powershell
py -3 -m pip install pyserial==3.5
py -3 scripts/board.py setup
py -3 scripts/board.py build
```

StopWatchをダウンロードモードでUSB接続し、`ports` に表示されたポート名で書き込みます。以下の `COM3` は例です。

```powershell
py -3 scripts/board.py ports
py -3 scripts/board.py upload COM3
```

### macOS・Linux（ターミナル）

```sh
python3 scripts/board.py setup
python3 scripts/board.py build
```

StopWatchをダウンロードモードでUSB接続して `python3 scripts/board.py ports` の一覧を確認し、以下のポート名を実際の値に置き換えてください。

```sh
# macOS
python3 scripts/board.py upload /dev/cu.usbmodem101
# Linux
python3 scripts/board.py upload /dev/ttyACM0
```

### 書き込み後の時刻同期

「時刻同期完了」と表示されるまでUSBをつないでおきます。端末が自動で起動しないときは、電源ボタンを短く1回押してください。スクリプトがPCの現地日時を端末に設定し、読み戻した日時を表示します。

ダウンロードモードと通常モードでポート名が異なる場合は、`upload COM3 --sync-port COM7` のように通常モードのポートも指定できます。書き込み完了後に同期だけ失敗したときは、通常モードで `ports` を再実行し、次の手順で時計を合わせます。

## 時計を合わせ直す

本体で合わせるには、待機画面の「現在」をタップします。「年」「月」「日」「時」「分」を選び、「−」「＋」で合わせて「確定」を押すと、秒は00になります。

PCの時計に合わせるには、本体を通常モードの待機画面にしてUSB接続し、実際のポート名で次を実行します。

| OS | コマンド例 |
|---|---|
| Windows | `py -3 scripts/sync-clock.py COM3` |
| macOS | `python3 scripts/sync-clock.py /dev/cu.usbmodem101` |
| Linux | `python3 scripts/sync-clock.py /dev/ttyACM0` |

電源を切った後も内蔵時計はバッテリーを使います。バッテリーが切れて時計が止まったときは、充電後に合わせ直してください。


## USBで起動ログを取る

通常モードで `scripts/monitor-board.py` にポート名を渡すと、30秒間のログを `build/board-monitor.log` に保存します。Windowsでは `py -3 scripts/monitor-board.py COM3`、macOSでは `python3 scripts/monitor-board.py /dev/cu.usbmodem101` のように実行します。

[案内を始める](../README.md#案内を始める)
