<p align="center">
  <img src="docs/images/screenshot.png" alt="win4wine: Debian 13 の上で動く Windows XP のシェルと、Linux と Windows のアプリ" width="800" />
</p>
<p align="center">
  <strong>Linux のデスクトップを、Windows XP のシェルで。</strong>
</p>
<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-LGPL--2.1--or--later-blue?style=flat" alt="License: LGPL-2.1-or-later"></a>
  <img src="https://img.shields.io/badge/Debian-13-A81D33?style=flat&logo=debian&logoColor=white" alt="Debian 13">
  <img src="https://img.shields.io/badge/Wine-11.0-8B0000?style=flat" alt="Wine 11.0">
  <img src="https://img.shields.io/badge/status-experimental-orange?style=flat" alt="Status: experimental">
</p>
<p align="center">
  <strong>日本語</strong> ·
  <a href=".github/readme/README.en.md">English</a>
</p>

> [!IMPORTANT]
> **個人の実験的なプロジェクトです。** 日常の作業環境として使えるほど完成してはいません。
> 動作を確かめているのは、**新しくインストールした Debian 13** に導入スクリプトで入れた場合だけです。
> すでに使っている環境（ほかのデスクトップ環境やディスプレイマネージャが入っているものなど）への導入は試していません。
> 試すときは、仮想マシンや試験用のマシンをおすすめします。

## これは何？

Windows XP の `explorer.exe` を、パッチを当てた Wine の上で **Linux のデスクトップシェル**として動かすプロジェクトです。
タスクバー、スタートメニュー、デスクトップ、フォルダウィンドウは XP 本物のものです。Linux のアプリも、XP の枠とタスクバーで扱えます。

Microsoft のファイルは同梱していません。お手元の XP の ISO から、必要なファイルだけを取り出して使います。

XP の ISO が無い方は、XP のシェルの代わりに **[ReactOS](https://reactos.org/) のシェル**（Windows 2000 風のクラシックの見た目）でも
試せます。ReactOS は自由なソフトウェアなので、導入スクリプトが公式のリリースから取得します（[docs/reactos.md](docs/reactos.md)）。

**Windows 7 のシェル**は [`master` ブランチ](../../tree/master)にあります。このブランチ（`WinXP`）は XP（と ReactOS）のシェルと、
Linux のデスクトップの窓の中で XP を動かす **XP Explorer**（[xp-explorer/](xp-explorer/README.md)）です。

## できること

- **XP のシェル**: Luna（またはクラシック）のタスクバー、スタートメニュー、デスクトップ、マイ コンピュータ、フォルダウィンドウ。
- **Linux アプリとの統合**: Linux のウィンドウに XP の枠が付き、タスクバー、Alt+Tab、最小化・最大化が XP と同じように動きます。
  Linux のアプリは「すべてのプログラム」に並びます。
- **通知領域**: Linux のトレイアイコン（StatusNotifierItem と XEmbed）が XP の通知領域に出ます。
  XP 自身のネットワーク・バッテリー・音量のアイコンと、そのフライアウト（音量、電源メーター、ワイヤレス ネットワーク接続の状態）、XP のボリューム コントロールも
  出ます。NetworkManager と PipeWire / PulseAudio の状態から作ります（XP Explorer から。[xp-explorer/](xp-explorer/README.md)）。
  ワイヤレス ネットワークの一覧から、初めてのネットワークにも XP のネットワーク キーの画面で接続できます。メニューや画面の文字は ISO の XP のファイルから読みます。
- **XP Explorer からの追加**: フォルダの左のタスク ペイン、画面のプロパティ（Windows XP とクラシックのテーマ、スタイルと配色。
  配色は ISO の HIVEDEF.INF と themeui.dll から作ります）、画像の縮小版とプレビュー、XP のサウンド（起動、ログオン、ログオフも）、
  ネットワーク接続、Internet Explorer 6、検索コンパニオン（犬の Rover）、Windows Media Player 9、スタートメニューの左の列（インターネット、電子メール、
  よく使うプログラム）、ユーザー アカウント（「画像の変更」で XP の 23 枚の画像から選ぶと、スタートメニューと Linux のユーザーの画像も
  変わります）、XP のネットワーク接続のフォルダ（Linux の有線・無線のアダプタと状態。通知領域のネットワークのアイコンから開きます）と
  XP の「ワイヤレス ネットワークの選択」（NetworkManager の Wi-Fi の一覧と接続）、プログラムの追加と削除（スタートメニューの
  「プログラムのアクセスと既定の設定」も。Linux のアプリも並び、削除はパッケージ マネージャーで、既定のプログラムは Linux の既定に
  なります）、セキュリティ センター（自動更新、インターネット オプション）、ヘルプとサポート センター（XP のヘルプのトピックと
  そのサービス。スタートメニューの「ヘルプとサポート」から開きます）、XP のファイルの種類（HIVECLS.INF の名前とアイコン、
  .zip の圧縮フォルダ、Windows Installer パッケージ、Windows Media Player の音楽と動画の種類）。
  どれもお手元の ISO のファイルで動きます（`prefix/extras.py`）。
- **XP 自身のプログラムと「すべてのプログラム」**: XP の電卓、メモ帳、ペイント、ワードパッド、コマンド プロンプト、タスク マネージャ、
  レジストリ エディタ、文字コード表、サウンド レコーダー、ボリューム コントロール、拡大鏡、スクリーン キーボード、ハイパーターミナル、
  リモート デスクトップ接続、ゲーム（フリーセル、ハート、マインスイーパ、ピンボール、ソリティア、スパイダ ソリティア）、
  Windows Messenger が Wine のものの代わりになります。「すべてのプログラム」は ISO のセットアップの INF からその言語で作る XP 自身のもので
  （上に Windows Update、Windows カタログ、プログラムのアクセスと既定の設定、その下にアクセサリ、ゲーム、スタートアップ、XP のプログラム）、
  Linux のアプリはその中の「Linux アプリケーション」のフォルダにあります（`prefix/startmenu.py`）。
- **キーボードの配列**: 最初は Linux のシステムの配列（`localectl`）です。コントロール パネルの「地域と言語のオプション」→
  「言語」→「詳細」（XP 自身のもの）で入力言語を足したり既定を変えたりすると、セッションの配列（Wayland では labwc、
  X11 では setxkbmap）がすぐに従います。2 つ以上なら XP と同じ左 Alt+Shift で切り替わります（`session/keyboard.py`）。
- **日本語入力**: fcitx5 + Mozc です。Ctrl+Space（または全角/半角）で切り替えます。GTK、Qt、Wine のアプリで使えます。
- **日本語版と英語版の XP**: どちらの ISO でも動きます。言語は ISO から自動で判断し、字体（英語版は Tahoma など）、
  ロケール、ようこそ画面をその XP に合わせます（英語版では日本語入力は設定しません）。
- **Windows のアプリ**: WoW64 の Wine 11.0 で、32bit と 64bit のアプリが動きます。端末で `wine setup.exe` と打つと、
  インストーラーが作るショートカットは XP のスタートメニューに入ります。
- **XP のようこそ画面**: LightDM の greeter として、XP のようこそ画面とログオフの画面を出せます（任意）。
  シェルが起動するまでは XP と同じ大きな「ようこそ」の画面を出します。
- **XP のキー操作**: Windows キー、Ctrl+Esc、Win+R、Win+E が使えます。
- **クイック起動**: タスクバーの右クリック →「ツール バー」→「クイック起動」で出せます（XP と ReactOS。Windows 7 は実機と同じく
  「新規ツール バー」でクイック起動のフォルダを選びます）。スタートメニューから Linux のアプリのショートカットも置けます。
  「デスクトップの表示」も実機と同じく、もう一度押すと窓が元に戻ります。
- **画面の拡大（DPI）**: XP と同じ「DPI 設定」で、文字や部品を大きくできます（96・120・144 など）。
- **落ちても戻れる**: XP のシェルが繰り返し落ちると、Wine 標準の explorer と xterm に切り替わります。
- **Wayland のセッション（任意・X11 のセッション以上に実験的）**: X11 のウィンドウマネージャーの代わりに、パッチを当てた labwc で
  動かすセッションです（XP と ReactOS のシェル）。モニターごとの倍率や 125%・150% などの倍率が使え、シェルはぼけずに描かれます。
  Wayland のアプリにもシェルの枠が付きます。作ったばかりで、確かめた範囲は X11 のセッションより狭く、既知の制限もあります（[docs/wayland.md](docs/wayland.md)）。
  ふだん試すなら X11 のセッションをおすすめします。
- **ReactOS のシェル（任意）**: XP の ISO の代わりに ReactOS 0.4.16 の explorer で動かすセッションです。日本語と英語に対応します。
  XP のシェルと並べて入れられます。
- **XP Explorer（任意）**: セッションを置き換えず、KDE などの今のデスクトップの上で、Wine の仮想デスクトップの窓の中に
  XP のシェルを開きます（[xp-explorer/](xp-explorer/README.md)）。

<table>
  <tr>
    <td align="center" width="50%">
      <img src="docs/images/screenshot-classic.png" alt="クラシックのテーマの XP のシェルと Linux のアプリ" width="400" /><br />
      XP のクラシック（<code>--theme classic</code>）
    </td>
    <td align="center" width="50%">
      <img src="docs/images/screenshot-reactos.png" alt="ReactOS のシェルと Linux のアプリ" width="400" /><br />
      ReactOS のシェル（<code>--shell reactos</code>。XP の ISO は要りません）
    </td>
  </tr>
</table>

## 試してみる

1. Windows XP Professional SP3（x86）の ISO（日本語版か英語版）を**ご自身で**用意してください。
2. 新しくインストールした Debian 13 に、ログインして使う利用者として入り、次を実行します。

```sh
git clone https://github.com/mikuta0407/win4wine.git ~/win4wine
cd ~/win4wine
scripts/setup.sh --media /path/to/WindowsXPSP3.iso --greeter
```

3. 再起動すると、XP のようこそ画面からそのまま XP のシェルに入ります。

XP の ISO が無いときは、ReactOS のシェルで試せます（ISO は要りません）。

```sh
scripts/setup.sh --shell reactos --lang ja   # 英語なら --lang en
```

ログイン画面でセッション「ReactOS Shell (Wine, X11)」を選びます。

詳しい手順とオプションは 🔗 [導入手順（INSTALL.md）](INSTALL.md) にあります。Wine をソースからビルドするので、4 コアで 1 時間半ほどかかります。
テーマ（XP のクラシックなど）と文字の大きさは、後から変えられます（[見た目と文字の大きさを変える](INSTALL.md#見た目と文字の大きさを変える)）。

## 注意

> [!WARNING]
> - **Windows XP（と Windows 7）の ISO はご自身で入手してください。** このリポジトリにも導入スクリプトにも、Microsoft のファイルを
>   取得する処理はありません。ISO と取り出したファイルの扱いは、Microsoft の使用許諾に従ってください。
> - **利用は自己責任でお願いします。** データの損失や不具合について、作者は責任を負いません。
> - Microsoft とも Wine プロジェクトとも関係のない、非公式のプロジェクトです。
> - 確かめているのは日本語版と英語版の XP SP3（x86）だけです。ほかの言語の XP も英語版と同じ設定で入れるので動くかもしれませんが、
>   一切試していません（`setup.sh` は警告を出して続けます）。

## 仕組み

```text
LightDM ─ XP のようこそ画面 (greeter/)
   └─ XP Shell セッション (session/supervisor.py)
        ├─ Wine 11.0 + 182 本のパッチ (wine/patches/)
        │    └─ XP の explorer.exe, shell32.dll, …（ISO から取り出したもの。ReactOS や Windows 7 のシェルではそれらのもの）
        ├─ bridge-wm: X11 のウィンドウマネージャー + Win32 の代理ウィンドウ (bridge-wm/)
        │    （Wayland のセッションでは、ウィンドウマネージャーの代わりにパッチを当てた labwc。wayland/）
        └─ fcitx5 + Mozc, トレイの仲介 (bridge-wm/tray.py)
```

仕組みや設計の詳しい説明は [docs/](docs/README.md) にまとめています。

## 似たプロジェクト

- [loss32](https://loss32.org/): Wine の上の Win32 のソフトウェアだけでデスクトップを作る Linux ディストリビューションの計画です。
  ReactOS の部品も使います。win4wine が XP 本物のシェルを Debian の上で動かすのに対し、loss32 は自由なソフトウェアだけで作ります。

## 守っていること

- Windows 由来のバイナリには手を加えません。違いは Wine とその周辺で吸収します。
- Microsoft のバイナリを逆アセンブルしません。挙動は公開 API と、実際の XP の上で動かす自作のプローブで調べています。
- Microsoft のファイル、取り出したもの、Wine の prefix は、Git 管理外の `localres/` と `.local/` にだけ置きます。

## 貢献してくださった方

- [AshesTom](https://github.com/AshesTom) さん（[フォーク](https://github.com/AshesTom/win4wine)）: XP の画面のプロパティ、
  サウンド、キーボードの配列、通知領域の XP のアイコン、スタートメニューの左の列、Internet Explorer 6 と Windows Media Player 9、
  起動中のようこそ画面、タイトルバーのアニメーションと小さいアイコン、Arch Linux への対応、`xp.sh` など。

## ライセンス

[LGPL-2.1-or-later](LICENSE)（Wine と同じ）です。対象はこのリポジトリのコードとパッチだけです。
ただし `wayland/patches/labwc/` は labwc と同じ GPL-2.0-only、`wayland/patches/wlroots/` は wlroots と同じ MIT です。
Windows XP と Windows 7 のファイルと、スクリーンショットに写っている XP と Windows 7 の画面の著作権は Microsoft にあります。
ReactOS のシェルで使う ReactOS のファイルは、ReactOS のライセンス（GPL-2.0-or-later、DLL の多くは LGPL-2.1-or-later）に従います。
