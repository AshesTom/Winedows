# 仕組み

win4wine がどの部品でできていて、どうつながっているかの説明です。なぜそうしたかは [design.md](design.md) にあります。

## 全体の流れ

```text
ディスプレイマネージャ（LightDM）
 └─ session/supervisor.py ……………… セッションの起動と見張り
      ├─ bridge-wm/wm.py ……………… X11 のウィンドウマネージャー（WM）
      ├─ bridge-wm/tray.py …………… Linux のトレイアイコンの仲介
      ├─ fcitx5 …………………………… 日本語入力
      ├─ proxy.exe（Wine の上）……… Linux のウィンドウの代理と、XP の枠
      └─ xp-explorer.exe（Wine の上）… XP のシェル（ISO から取り出した explorer.exe）
                    ↑ ↓  IPC ディレクトリのファイル（WM ⇄ proxy ⇄ tray.py）
```

準備の段階では、次のものを作ります。

| 部品 | 役割 |
|---|---|
| `extract/extract.py` | ISO（または XP の Windows ディレクトリ）から、必要なファイルだけを `.local/assets` に取り出します。ネットワークは使いません。XP の言語（日本語か英語）は `extract/media.py` が shell32.dll のリソースの言語から判断し、英語版では英語版にだけある字体も取り出します。ほかの言語の XP は英語版として扱います（試していません）。 |
| `wine/build.sh` | Wine 11.0 を取得し、`wine/patches/` を当ててビルドします（[patches.md](patches.md)）。 |
| `prefix/create.py`、`prefix/stage.py` | XP 用の Wine の prefix を作り、XP の DLL と COM の登録、レジストリの設定（registry profile）を入れます。設定は XP の言語に合わせます（日本語版は MS UI Gothic の字体の profile とコンソールの MS ゴシック、英語版はクラシックの Tahoma、MS Shell Dlg の Microsoft Sans Serif、コンソールの Terminal）。言語は `xp-shell.json` の `language` に記録します。 |
| `bridge-wm/build.sh` | proxy.exe などの Win32 の補助プログラムをビルドします（`.local/bridge`）。 |
| `prefix/extras.py` | XP のセッションの prefix に、[AshesTom さんのフォーク](https://github.com/AshesTom/win4wine)の XP Explorer から入れた追加（通知領域の XP のアイコン、画面のプロパティ、サウンド、Internet Explorer 6、Windows Media Player 9 など）を、`xp-explorer/scripts/` のコードで入れます。画面のプロパティの配色（New Schemes）は、XP が最初のログオンで作るものを `prefix/xp_schemes.py` が ISO の HIVEDEF.INF と themeui.dll から作ります。変わったときは `VERSION` を上げ、setup.sh がもう一度入れます。 |

## XP のシェルの動かし方

- XP の `explorer.exe` は `C:\xp\xp-explorer.exe` という名前で置き、Wine 自身の explorer（デスクトップの管理）とは別のプロセスにします。
- XP の shell32・browseui・shdocvw・shlwapi・comctl32 などは、AppDefaults で **xp-explorer.exe のプロセスにだけ** XP のもの（native）を
  使わせます。ほかの Wine のアプリは Wine の DLL のままです。
- Wine のパッチの多くは、環境変数 `WIN4WINE_EXTERNAL_SHELL` があるときだけ効きます（opt-in。[patches.md](patches.md)）。
- prefix は 64-bit（WoW64）で、XP の DLL は `syswow64` に置きます。XP のシェルのための COM の登録は 32-bit の側にだけ書きます。
- ReactOS のシェル（`C:\ros\ros-explorer.exe`）と Windows 7 のシェル（`C:\w7\w7-explorer.exe`、64bit）も同じ形で、専用の prefix で
  動かします（[reactos.md](reactos.md)、windows7.md（`master` ブランチ））。session.json の `shell` で選び、supervisor はその prefix の
  `stage`（`prefix/reactos.py`、`prefix/win7.py`）を使います。Windows 7 のシェルでは `WIN4WINE_SHELL=win7` も設定し、proxy は
  Linux の窓に `WM_CLASS` ごとの AppUserModelID を付けます（Windows 7 のタスクバーはこれでボタンをまとめます）。スタートメニューに
  あるアプリなら、ジャンプリストの題とアイコン、固定したボタンのコマンドも付けます（`linux_apps.py` の `relaunch.tsv`）。
  Wayland のセッションでは labwc が WM の代わりに `classes.tsv`（窓ごとの app_id か `WM_CLASS`）を書きます。

## WM（`bridge-wm/wm.py`）

X11 のウィンドウマネージャーです。枠を自分では描かず（reparent しません）、次のことをします。

- Wine の X11 ドライバが付ける `_WIN4WINE_CLASS` と `_WIN4WINE_HWND`（パッチ 0025）で、XP のデスクトップ（Progman）、タスクバー
  （Shell_TrayWnd）、スタートメニュー（DV2ControlHost）、proxy の枠、普通の Wine の窓、Linux の窓を見分けます。
- デスクトップを一番下、タスクバーとスタートメニューを上に置きます。一番上の階層の WorkerW はデスクトップとして扱いません。
  XP と Windows 7 の「デスクトップの表示」は、窓を最小化したうえでデスクトップのアイコンを WorkerW に移し、それを窓の上に
  上げるためです（実機で確認）。Win32 の最前面の窓（`_NET_WM_STATE_ABOVE`）と、
  それが持つ窓はタスクバーより上に置きます。
- タスクバーの位置から作業領域（`_NET_WORKAREA`）を作ります。
- クリックでウィンドウを有効にします。デスクトップとタスクバーをクリックすると、そこにキーボードを渡します。
  上げた窓が持つ窓（`WM_TRANSIENT_FOR`。モーダルのダイアログなど）は、Windows と同じくその上に保ちます。
- Linux の窓の枠（proxy の窓）をその窓のすぐ下に置き、一緒に動かします（下の「Linux の窓の XP の枠」）。
- Alt+Tab、Windows キー、Ctrl+Esc を受け取り、XP のシェルに渡します（下の「キー操作」）。
- `Gtk/DecorationLayout` を `:` にする XSETTINGS を出し、GTK のヘッダーバーの閉じるボタンなどを隠します（ほかの settings
  daemon が無いときだけ）。
- Wine の窓（proxy の枠を除く）を Composite で自動リダイレクトし、中身を X サーバーに覚えさせます（`session.json` の
  `composite`、既定は `true`）。窓を動かしたり閉じたりして下の Wine の窓が見えたとき、X サーバーがその中身をそのまま出すので、
  その窓のプロセスが描き直すまで黒く見えることがありません（Wine は自分の X の窓の背景を黒にしています）。Linux の窓は自分で
  素早く描き直し、Linux の窓と枠は大きさがよく変わる（リダイレクトした窓は大きさが変わるたびに画像を作り直す）ので、
  リダイレクトしません。ただし枠は、動いている間だけリダイレクトします（大きさが変わったら戻します）。ドラッグの 1 歩ごとに
  先に動いた Linux の窓が枠の進む側の縁を隠し、枠が動くときにその縁を写せず、Wine が描き直すまで（約 5 ms）黒く見えていた
  ためです（縁の太い Windows 7 で目立ちました）。影や透明などの効果はありません。

WM は周期的には起きません。X のイベント、IPC ディレクトリの inotify、シグナルで起き、内容が変わったファイルだけを書きます。
窓の位置や題名は X のイベントから覚えておき、X への問い合わせを減らしています。窓のドラッグでは 1 歩ごとに X のイベントが
来ますが、Wine の窓の移動ではファイルの見直しをせず、Linux の窓の位置だけが変わったときは `geometry.tsv` だけを書き直します。

Wine の窓には枠を付けず、代理も作りません。Wine の最大化の大きさは Wine 自身が決めます（パッチ 0031）。

## proxy（`bridge-wm/proxy.c`）

Wine の上で動く Win32 のプロセスです。Linux の窓 1 つごとに、`Win4WineProxy` クラスの Win32 の窓（代理）を作ります。

- XP のタスクバーは、代理を普通の Win32 の窓として扱います（題名、最小化の状態、アイコン、shell hook の通知）。
- 代理の窓は `WS_OVERLAPPEDWINDOW` を持ち、Wine が XP（Luna）の枠を描きます。WM はこの窓を Linux の窓のすぐ下に重ねるので、
  Linux の窓に XP の枠が付いて見えます。
- 枠のボタンやタスクバーでの操作（最小化、最大化、閉じる、移動）は、コマンドのファイルで WM に伝えます。
- shell hook（HSHELL_WINDOWCREATED など）は、Wine のパッチ 0026 が記録した登録先に proxy が送ります。
- 周期的には起きません。IPC ディレクトリの変更通知と、Win32 の WinEvent（前面化、最小化、窓の生成・破棄、題名の変更）で起きます。
- 起動直後に 1 度だけ、タスクバーに今の大きさの `WM_SIZE` を送り、XP のシェル自身の配置で子の大きさを整えさせます。

## IPC（WM・proxy・tray.py の間のファイル）

セッションごとに、本人だけが読める（0700）ディレクトリを `$XDG_RUNTIME_DIR` の下に作ります。ファイルは同じディレクトリで
一時ファイルを書いてから rename するので、読み手は書きかけのファイルを見ません。どれも内容が変わったときだけ書きます。

| ファイル | 書く側 → 読む側 | 中身 |
|---|---|---|
| `windows.tsv` | WM → proxy | Linux の窓の一覧。1 行に `XID(16進) TAB 最小化(0/1) TAB 有効(0/1) TAB 題名` |
| `geometry.tsv` | WM → proxy | `XID x y 幅 高さ 最大化(0/1) 作業領域x 作業領域y 作業領域幅 作業領域高さ`（Linux の窓の中身の位置） |
| `frames.tsv` | proxy → WM | `HWND XID 左 右 上 下 最大化 最小化 枠の幅x 枠の幅y`（枠の余白の大きさ） |
| `PID-番号.cmd` | proxy → WM | `activate`・`minimize`・`close`・`maximize`・`restore` と XID、または `geometry XID x y 幅 高さ`、`switch XID`。WM はプロセスごとに番号の順に読みます |
| `switch.tsv` | WM → proxy | Alt+Tab の状態。`番号 Tab の回数 状態`（状態は o=表示中、c=確定、x=取り消し） |
| `shellkeys.tsv` | WM → proxy | Linux の窓の上で押された Windows キーと Ctrl+Esc。`番号 windows VK...` または `番号 ctrl-escape` |
| `icon-XID.argb` | WM → proxy | Linux の窓のアイコン（下の「アイコン」） |
| `tray.tsv`、`tray-ID.argb`、`menu-ID.tsv` | tray.py → proxy | トレイアイコン（下の「トレイアイコン」） |
| `*.tray` | proxy → tray.py | トレイアイコンのクリックとメニューの選択 |
| `proxy.lock` | proxy | proxy が動いている間、先頭 1 バイトを LockFile で押さえます。WM は F_GETLK で proxy の生存を確かめます |

## Linux の窓の XP の枠

1. WM が `windows.tsv` に Linux の窓を書くと、proxy が代理の窓を作ります。
2. proxy は、代理の窓の枠の余白（公開 API の GetWindowRect・GetClientRect で測った値）を `frames.tsv` に書きます。
3. WM は `_WIN4WINE_HWND` と `frames.tsv` の HWND で、枠の X の窓と Linux の窓を組にし、枠を Linux の窓のすぐ下に重ねます。
   余白は `_NET_FRAME_EXTENTS` にも出します。
4. 枠のタイトルバーや角を動かすと、Win32 の普通の処理（Wine の移動のループ）で枠が動きます。WM は、枠の移動の要求を受けた
   その処理の中で、余白から求めた位置と大きさに Linux の窓も動かします（Linux の窓を先に動かすので、枠の中の空いた部分は
   見えません）。proxy はループの間は `geometry` コマンドを書かず、終わったときに 1 度だけ書きます。WM がまだ合わせられない
   とき（ドラッグの初めに最大化していた窓）の分です。WM が既に合わせた位置の、遅れて届いたコマンドは使いません。
   Linux の窓が自分で動いたとき（GTK のヘッダーバーのドラッグなど）も、WM がその要求を受けた処理の中で枠を動かします。
   Wine は X の窓の新しい位置を、ほかの WM が動かしたときと同じく受け取ります。そのあと WM が `geometry.tsv` を書き、
   proxy が枠の位置を確かめます。

- 初めて枠を付けるとき、位置の指定が無い窓は XP と同じ重ね配置の次の位置に置きます。位置の指定がある窓も、タイトルバーが
  作業領域の外に出ないように直します。
- 最大化では、proxy が `ShowWindow(SW_MAXIMIZE)` で代理を本当に最大化し、WM が Linux の窓を「作業領域から枠の余白を
  除いた大きさ」にします（XP と同じく、外側の枠は画面の外に出ます）。
- 最小化では枠と窓を一緒に隠します。
- 枠は、Linux の窓が X のキーボードを持っている間（`windows.tsv` の「有効」）だけ、アクティブの見た目にします。proxy は
  `WM_NCACTIVATE` で枠の見た目を直接切り替えます。Windows と同じく Wine は、最後の入力を受けていないプロセスの
  SetForegroundWindow を断り、入力は Linux の窓に届くので、代理は Win32 の前面にはなれないことが多いためです。
  Luna の枠も、XP と同じく前面の窓ではなく `WM_NCACTIVATE` の状態で描きます（パッチ 0069）。
- transient（ダイアログ）の Linux の窓には枠を付けず、タスクバーにも出しません。作業領域より大きいもの（小さい画面での GTK の
  ファイルを選ぶダイアログ）は作業領域に収まるように縮め、位置を指定しないもの（GTK 4）は親の窓の中央に置きます。

## アイコン

- Linux の窓のアイコンは `_NET_WM_ICON`（無ければ `WM_HINTS` の pixmap）から読み、`icon-XID.argb` に書きます。
  形式は `W4I1`、幅と高さ（32bit、little endian）、straight ARGB の画素の並びです。proxy はこれで HICON を作ります。
- XP のタスクバーは、ほかのプロセスの窓のアイコンを `WM_GETICON` と `GetClassLongPtr` で読みます。Wine はプロセスをまたぐ
  読み出しを許さないので、パッチ 0028 が opt-in のときだけ、アイコンの画素を wineserver に登録して読めるようにします。

## キー操作

- **Alt+Tab**: WM が Alt を押している間キーボードをつかみ、`switch.tsv` を書きます。proxy が XP の切り替えの窓を描き、
  確定すると `switch XID` を送り、WM がその窓を有効にします。proxy が動いていなければ、WM が順に切り替えます。
  窓の形はシェルごとに違い（`WIN4WINE_SWITCHER`）、XP（既定）、ReactOS、Windows 7 の形があります。Windows 7 の形は
  テーマ（aero.msstyles）の `AltTab` クラスの部品で描き、項目に「デスクトップ」が入ります（選ぶとすべての窓を最小化します）。
- **Windows キーと Ctrl+Esc**: Wine の窓にフォーカスがあるときは、そのまま Wine に渡します（パッチ 0029 でスタートメニューが
  開き、Win+R などのホットキーも Wine が処理します）。Linux の窓にフォーカスがあるときは、WM が Windows キーを離すまでの
  キーを受け取って `shellkeys.tsv` に書き、proxy が `SendInput` で Wine に入れます。
- **窓の寄せ（`window_snap` のときだけ）**: proxy が Win+←/→/↑/↓ を `RegisterHotKey` で受け取り、前面の窓（Linux の窓に
  フォーカスがあればその枠）を作業領域の左右の半分に寄せたり、最大化・元に戻す・最小化したりします。寄せる前の位置は proxy が
  覚えていて、Win+↓ や反対側の Win+矢印で戻します。X11 のセッションでは、Wine の移動のループが送る EVENT_SYSTEM_MOVESIZESTART/END
  （パッチ 0038）でタイトルバーのドラッグを知り、ポインタが画面の左右の端・上端にあれば XP のドラッグの枠と同じ網掛けで行き先を
  示し、離したときに寄せます。寄せた窓には `Win4WineSnapRestore` を付け、ドラッグで引き剥がすと Wine が元の大きさに戻します。
  最大化した窓もタイトルバーをドラッグすると、Wine が元の大きさに戻して動かし続けます（`WIN4WINE_WINDOW_SNAP=1` のとき）。
  Wayland のセッションでは、元の大きさの窓のドラッグは labwc が行うので、`wayland/session.sh` が labwc の snapping を有効にした
  `rc.xml` を IPC のディレクトリに作って使います。どの窓を寄せたかは labwc だけが覚えます。proxy は Win+矢印を `snap HWND 側` と
  `unsnap HWND` のコマンドで labwc に頼み、寄せた窓を `tiled.tsv` で知ります。ドラッグ中の行き先は labwc が `snapping.tsv` に書き、
  proxy が X11 と同じ網掛けの枠で示します。

## トレイアイコン（`bridge-wm/tray.py`）

- **StatusNotifierItem**: セッションバスで `org.kde.StatusNotifierWatcher` を所有し、登録されたアイコンを `tray.tsv` と
  `tray-ID.argb` に書きます。右クリックのメニューは `com.canonical.dbusmenu` から読んで `menu-ID.tsv` に書きます。
  ほかに watcher がいれば何もしません。
- **XEmbed**: ほかに X のトレイが無ければ `_NET_SYSTEM_TRAY_S0` を所有し、アイコンの窓を画面に出さない枠に入れて、
  Composite で画像を読みます。クリックは XTest で本物の押下を入れます。
- proxy は `Shell_NotifyIconW` で XP の通知領域にアイコンを出します。XP の Explorer が読める形式で届くのは、パッチ 0034 によります。

## セッション（`session/supervisor.py`）

- 起動の順: XP のログオン画面 → WM → tray.py → fcitx5 → prefix の更新（`prefix/stage.py`）→ Linux のアプリのスタートメニュー
  → proxy → XP のシェル。
- タスクバーが出たら起動できたとみなします。XP のシェルや proxy が落ちたら、Wine を止めてやり直します。
  120 秒に 3 回失敗すると、XP の DLL を使わない別の prefix で、Wine 標準の explorer と xterm を起動します（復旧用）。
  このときは `WIN4WINE_EXTERNAL_SHELL` と `WIN4WINE_SHELL` を外し、パッチの変更が効かない素の Wine にします。
- XP のログオフ・終了（パッチ 0024 が要求をファイルに書きます）では、ログオフの画面を出し、セッションを終えます。
  終了・再起動では `session.json` の `shutdown_command`・`reboot_command` を実行します。
- 環境: Wine と Linux のアプリの `PATH` の先頭にセッションの Wine を置きます。日本語入力の変数（`XMODIFIERS` など）、
  字体の設定（`linux_font` の fontconfig）、DPI の設定（下の「DPI」）、カーソルのテーマも入れます。
  Wine のプログラムの変数（prefix、`WIN4WINE_EXTERNAL_SHELL` などの opt-in、`WINEDLLOVERRIDES`、ロケール）は
  `session/wineenv.py` が組み立てます。`win4wine-wine`（`session/wine.py`、`~/.local/bin` に入れます）も同じものを使い、
  セッションの外から起動した Windows のプログラムもセッションと同じ見た目と動きにします（opt-in が無いと Wine の標準の
  見た目になります）。セッションの中ではそのまま `wine` を起動します。
- 周期的には起きません（起動中のタスクバーを待つ間だけ 1 秒ごとに確かめます）。ログは `~/.local/state/win4wine/` に書き、
  起動のたびに前回の分を `*.log.1` にします。Wine のログは既定ではエラーだけです（`WINEDEBUG=+timestamp,fixme-all`。
  セッションのすべての Windows のプログラムが受け継ぎます）。調べるときは `session.json` の `"debug"` で Wine のチャンネルを
  選びます（例: `"+timestamp,+seh,+loaddll"`）。

## Linux のアプリのスタートメニュー（`session/linux_apps.py`）

- XDG の `applications` の desktop entry を読み、表示するアプリごとに、prefix の共通の Programs にショートカットを置きます。
  名前には ` (Linux)` を付け、分類（Categories）から XP のフォルダ（アクセサリ、インターネットなど）に入れます。
- ショートカットの先は `linux-launch.exe`（`session/linux-launch.c`）で、`/usr/bin/env` 経由で Linux のプログラムを起動します。
  アイコンはアイコンテーマから .ico を作ります。
- 作ったものは `C:\xp\linux-apps\state.json` に記録し、消えたアプリの分だけを片付けます。セッション中は inotify で見張り、
  パッケージの導入・削除に追従します。

## ログイン画面（`greeter/`）

- `greeter/xp-greeter.py` は LightDM の greeter です。利用者の XP の logonui.exe・msgina.dll・winlogon.exe などから、
  画像と文字列を PE の資源として読みます（コードは動かしません）。画面は `greeter/welcome.py` が cairo と Pango で描きます。
- 必要なファイルが無ければ、LightDM に接続する前に通常の greeter（lightdm-gtk-greeter）に切り替えます。
- ログオン中とログオフ中の XP の画面は、セッションの中で `greeter/xp-splash.py` が出します。ログオン直後は XP と同じく
  利用者の絵と「Loading your personal settings...」（prefix の更新中も）、キーボードとスタートメニューの準備中は
  「Applying your personal settings...」、シェルが起動するまでは大きな「welcome」だけの画面です。greeter を入れていなければ、このチェックアウトの `.local/assets` から描きます。
- XP のカーソルは `greeter/cursors.py` が利用者の user32.dll から X のカーソルテーマ（`win4wine-xp`）として作ります。

## DPI

- DPI は XP と同じく prefix の `HKCU\Control Panel\Desktop\LogPixels` で決めます（`prefix/display.py --dpi`）。
- XP のシェルと Wine のアプリは、その DPI で自分で描きます（パッチ 0061 で system DPI aware にします）。
- supervisor は同じ値を Linux のアプリに渡します（`session/dpi.py`）。`Xft.dpi`、fontconfig の dpi、`XCURSOR_SIZE`、
  192 以上では GTK の `GDK_SCALE` も設定します。X の画面の物理的な大きさもその DPI にします（Xorg は RandR、
  `xp.sh run` の Xephyr は `-dpi`）。Tk などの、画面のミリメートルで文字の大きさを決めるアプリのためです。
- bridge-wm の XSETTINGS にも同じ値を載せます（`Xft/DPI`、`Gdk/WindowScalingFactor`、`Gdk/UnscaledDPI`、
  `Gtk/CursorThemeSize`、`Gtk/CursorThemeName`）。`Gtk/FontName` は `linux_font` を XP のメッセージの字体
  （`WindowMetrics` の `MessageFont`、英語の XP では Tahoma 8 ポイント）の大きさにしたもので、デスクトップの大きな字体の
  代わりになります。Qt は GTK のプラットフォームテーマ（`QT_QPA_PLATFORMTHEME=gtk3`、利用者が決めていなければ）で同じ字体を使います。セッションの環境変数を受け継がずに起動したアプリも同じ大きさになります。
- 96 と 120 DPI で、メモ帳の横の GTK 3/4、Qt 5/6、Electron、Firefox、Tk の文字の大きさが合うことを確かめています
  （Qt 6、Electron、Firefox は 120 DPI で全体を 1.25 倍に、GTK と Qt 5 は文字を 120 DPI で描きます）。
- ようこそ画面の DPI は `/etc/win4wine/greeter.json` の `dpi` で決めます（XP でもマシン全体の設定です）。

## フォントのスムージング（ClearType）

- XP の「画面のフォントの縁を滑らかにする」の「標準」と「ClearType」を、prefix の `HKCU\Control Panel\Desktop` の
  `FontSmoothing` と `FontSmoothingType`（1 か 2）で決めます（`prefix/display.py --font-smoothing`）。既定は XP と同じく「標準」です。
- ClearType では、XP のシェルと Wine のアプリの文字をパッチ 0062 が XP と同じ方式で描きます。
  MS UI Gothic などの埋め込みビットマップのある大きさ（22 ピクセルまで）は、XP と同じく ClearType になりません。
- supervisor は Linux のアプリにも RGB のサブピクセルで描くように伝えます（`session/dpi.py`。`Xft.rgba`、fontconfig の `rgba`）。
  Wayland のアプリ（GTK など）は自分の設定（GNOME の `font-rgba-order` など）に従います。
- ようこそ画面は変わりません。

## Wayland のセッション（`wayland/`）

- `wayland/session.sh` が IPC のディレクトリを作り（`WIN4WINE_IPC`）、パッチを当てた labwc を起動して、その中で supervisor を
  動かします（labwc の `-S`。supervisor が終わると labwc も終わります）。
- labwc が WM の役を持ち、IPC のファイルと proxy.exe は X11 のセッションと同じです。supervisor は WM を起動せず、`wlr-randr` で
  出力の倍率と位置を設定し、主モニターの名前を IPC の `primary` に書きます。
- Wine と XP のシェルは Xwayland の X11 のクライアントのままです。labwc は Xwayland に出力を実際の画素で見せるので、
  DPI で描いた XP のシェルは倍率 1.5 の出力でも 1 画素ずつ表示されます。Wayland のアプリは出力ごとの倍率で描きます。
- 詳しくは [wayland.md](wayland.md) にあります。
