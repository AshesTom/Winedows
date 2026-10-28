<h1 align="center">Winedows · Windows 7</h1>
<p align="center">
  <img src="../../docs/images/screenshot-win7.png" alt="Winedows: Debian 13 の上で動く Windows 7 のシェルと、Linux と Windows のアプリ" width="800" />
</p>
<p align="center">
  <strong>Linux のデスクトップを、Windows 7 のシェルで。</strong>
</p>
<p align="center">
  <a href="../../LICENSE"><img src="https://img.shields.io/badge/license-LGPL--2.1--or--later-blue?style=flat" alt="License: LGPL-2.1-or-later"></a>
  <img src="https://img.shields.io/badge/Debian-13-A81D33?style=flat&logo=debian&logoColor=white" alt="Debian 13">
  <img src="https://img.shields.io/badge/Wine-11.0-8B0000?style=flat" alt="Wine 11.0">
  <img src="https://img.shields.io/badge/status-experimental-orange?style=flat" alt="Status: experimental">
</p>
<p align="center">
  <a href="../../README.md">English</a> ·
  <strong>日本語</strong>
</p>

> [!IMPORTANT]
> **個人の実験的なプロジェクトです。** XP のシェルより新しく、確かめた範囲も狭い試作です。
> 動作を確かめているのは、**新しくインストールした Debian 13** に導入スクリプトで入れた場合だけです。
> 試すときは、仮想マシンや試験用のマシンをおすすめします。

## これは何？

Windows 7 SP1（x64）の `explorer.exe` を、パッチを当てた Wine の上で **Linux のデスクトップシェル**として動かすプロジェクトです。
スタートメニュー、タスクバー、通知領域、デスクトップ、フォルダーウィンドウ、ファイルのダイアログは Windows 7 本物のものです。
Linux のアプリにも Windows 7 の枠が付き、アプリごとにタスクバーにまとまります。
Microsoft のファイルは同梱していません。お手元の ISO から、必要なファイルだけを取り出して使います。

Winedows は [mikuta0407/win4wine](https://github.com/mikuta0407/win4wine) のフォークから始まりました。中の名前
（`win4wine-*`、`~/.config/win4wine` など）はそのままです。

### Windows ごとのブランチ

| ブランチ | シェル |
|---|---|
| [`WinXP`](../../../../tree/WinXP) | Windows XP（Luna かクラシック）、ReactOS のシェル、XP Explorer |
| **`Win7`**（このブランチ） | Windows 7 |

`master` はこの 2 つへの案内だけです。

## 試してみる

1. Windows 7 SP1 x64 の ISO を**ご自身で**用意してください。
2. 新しくインストールした Debian 13 に、ログインして使う利用者として入り、次を実行します。

```sh
git clone -b Win7 https://github.com/AshesTom/Winedows.git ~/winedows
cd ~/winedows
scripts/setup.sh --media /path/to/Windows7SP1x64.iso --greeter
```

3. 再起動すると、Windows 7 のログオン画面からそのまま Windows 7 のシェルに入ります。

| オプション | 意味 |
|---|---|
| `--media ISO` | Windows 7 SP1 x64 の ISO（必須） |
| `--greeter` | ログイン画面を Windows 7 のログオン画面にします（LightDM をディスプレイマネージャにします） |
| `--no-greeter` | 今のログイン画面のままにします（既定）。セッション「Windows 7 Shell (Wine, X11)」を選びます |
| `--dpi N` | 画面の DPI です。96（100%）、120（125%）、または 19〜480 の任意の値 |
| `--wayland` | Wayland のセッション「Windows 7 Shell (Wine, Wayland)」も入れます（パッチを当てた labwc。より実験的です） |
| `--wine-install DIR` | 別のマシンでこのリポジトリのパッチでビルドした Wine を使います |
| `--jobs N` | Wine のビルドの並列数です（既定は CPU の数） |

Wine をソースからビルドするので、4 コアで 1 時間半ほどと、約 40 GB の空きが要ります。終わった手順は飛ばすので、
失敗しても同じコマンドをもう一度実行できます。詳しくは [docs/windows7.md](../../docs/windows7.md) にあります。

### 設定（`~/.config/win4wine/win7-session.json`）

| キー | 既定 | 意味 |
|---|---|---|
| `wine`, `prefix` | （setup.sh が書きます） | 使う Wine と prefix |
| `locale` | `ja_JP.UTF-8`（英語版では `en_US.UTF-8`） | セッションのロケール |
| `input_method` | `fcitx5`（日本語版だけ） | 入力メソッド。省くと起動しません |
| `linux_menu` | `true` | Linux のアプリをスタートメニューに入れます |
| `welcome_screen` | `true` | 「ようこそ」「ログオフしています」の画面を出します |
| `cursor_theme` | `win4wine-win7` | カーソルのテーマ。`null` で使いません |
| `shutdown_command`, `reboot_command` | `systemctl poweroff` / `reboot` | シャットダウンと再起動で実行するコマンド |
| `outputs`, `primary` | （無し） | Wayland のセッションの出力ごとの設定（例: `{"HDMI-A-1": {"scale": 1.5}}`）と、タスクバーを出す出力 |
| `composite` | `true` | Wine の窓の中身を X サーバーに覚えさせます（窓を動かしたときに黒く見えなくなります） |
| `window_snap` | `false` | `true` で Aero スナップ風に、Win+矢印や画面の端へのドラッグで窓を半分・最大化にします |

セッションのログは `~/.local/state/win4wine/` にあります（前回の分は `*.log.1`）。シェルが続けて落ちると、Wine 標準の
explorer と xterm に切り替わります。

### 元に戻す

```sh
greeter/install.sh --remove   # LightDM の元のログイン画面に戻す
sudo rm /usr/share/xsessions/windows7-shell.desktop /usr/local/bin/windows7-shell-session
sudo rm -f /usr/share/wayland-sessions/windows7-shell-wayland.desktop /usr/local/bin/windows7-shell-wayland-session
```

ビルドしたもの、取り出したファイル、prefix はチェックアウトの `.local/` にあります。入れたパッケージは元に戻しません。

## できること

- **Windows 7 のシェル**: スタートメニュー、タスクバー、通知領域、デスクトップ、コンピューターとフォルダーウィンドウ、ライブラリ。
- **Windows 7 のファイルのダイアログ**: Windows のアプリでも使えます（うまく動かないアプリは `prefix/win7.py builtin` で Wine のものに戻せます）。
- **Linux アプリとの統合**: Linux のウィンドウに Windows 7 の枠が付き、タスクバー、Alt+Tab、最小化・最大化が Windows 7 と同じように
  動きます。Linux のアプリはスタートメニューに並びます。
- **Windows 7 のログオン画面**（任意）、「ようこそ」「ログオフしています」の画面、Aero のカーソル、Aero のテーマ、
  Windows 7 と同じフォントと ClearType。
- **日本語版と英語版**: 言語は ISO から判断します。日本語入力は fcitx5 + Mozc です。確かめたのは日本語版と、
  その文字を英語の言語パックのものに入れ替えた英語版です。
- **Windows のアプリ**: WoW64 の Wine 11.0 で、32bit と 64bit のアプリが動きます。

<table>
  <tr>
    <td align="center" width="50%">
      <img src="../../docs/images/screenshot-win7-logon.png" alt="Windows 7 のログオン画面で利用者を選ぶところ" width="400" /><br />
      ログオン画面（<code>--greeter</code>）
    </td>
    <td align="center" width="50%">
      <img src="../../docs/images/screenshot-win7-small.png" alt="小さいアイコンでラベル付きのタスクバーの Windows 7 のシェル" width="400" /><br />
      小さいアイコンでラベル付きのタスクバー
    </td>
  </tr>
</table>

## 制限

- **Aero のガラスは試作です。** 既定の見た目は「Windows 7 ベーシック」（Aero のテーマを、デスクトップの合成なしで描いたもの）です。
  `win7-session.json` に `"aero": true`（または `W7_AERO=1 windows7/7.sh run`）で、タスクバーとスタートメニューがガラスになり、
  配色の色とガラスの反射が付きます（Wine のパッチ 0152）。混ぜてぼかすのは X のコンポジター（picom と
  [windows7/picom.conf](../../windows7/picom.conf)）です。窓の枠のガラス、ライブ サムネイル、Aero Peek はまだです。
- ネットワーク、ヘルプとサポート、コントロール パネルの中身の多くは、Wine にその部品が無いので使えません。
- スタートメニューの「ロック」「スリープ」「休止状態」は何もしません。

## 仕組み

```text
LightDM ─ Windows 7 のログオン画面 (greeter/win7-greeter.py)
   └─ Windows 7 Shell セッション (session/supervisor.py)
        ├─ Wine 11.0 + パッチ (wine/patches/)
        │    └─ Windows 7 の explorer.exe, shell32.dll, explorerframe.dll, …（ISO から取り出したもの。extract/win7.py）
        ├─ bridge-wm: X11 のウィンドウマネージャー + Win32 の代理ウィンドウ (bridge-wm/)
        │    （Wayland のセッションでは、パッチを当てた labwc。wayland/）
        └─ fcitx5 + Mozc, トレイの仲介 (bridge-wm/tray.py)
```

今のデスクトップの窓の中で、入れずに試すには `windows7/7.sh setup ISO`、次に `windows7/7.sh run` です
（Arch / CachyOS。引数なしの `windows7/7.sh` でコマンドと切り替えを出します）。

仕組みや設計の説明は [docs/](../../docs/README.md) にあります。Wine、bridge、セッションは XP のシェルと共通なので、
XP のシェルを例に書いた文書もあります。

## 注意

> [!WARNING]
> - **Windows 7 の ISO はご自身で入手してください。** このリポジトリにも導入スクリプトにも、Microsoft のファイルを取得する処理はありません。
>   ISO と取り出したファイルの扱いは、Microsoft の使用許諾に従ってください。
> - **利用は自己責任でお願いします。** データの損失や不具合について、作者は責任を負いません。
> - Microsoft とも Wine プロジェクトとも関係のない、非公式のプロジェクトです。

## 守っていること

- Windows 由来のバイナリには手を加えません。違いは Wine とその周辺で吸収します。
- Microsoft のバイナリを逆アセンブルしません。挙動は公開 API と、実機の上で動かす自作のプローブで調べています。
- Microsoft のファイル、取り出したもの、Wine の prefix は、Git 管理外の `localres/` と `.local/` にだけ置きます。

## 作った人

- [mikuta0407](https://github.com/mikuta0407) さん: このプロジェクトの元の [win4wine](https://github.com/mikuta0407/win4wine)（Windows 7 のシェルも）。
- [AshesTom](https://github.com/AshesTom) さん: Winedows。

## ライセンス

[LGPL-2.1-or-later](../../LICENSE)（Wine と同じ）です。対象はこのリポジトリのコードとパッチだけです。
ただし `wayland/patches/labwc/` は labwc と同じ GPL-2.0-only、`wayland/patches/wlroots/` は wlroots と同じ MIT です。
Windows 7 のファイルと、スクリーンショットに写っている Windows 7 の画面の著作権は Microsoft にあります。
