# Windows 7 のシェル（実験的）

Windows 7 SP1（x64）の `explorer.exe` とその周りの DLL を、XP のシェルの代わりに動かすセッションです。見た目は
「Windows 7 ベーシック」（Aero のテーマを、デスクトップの合成なしで描いたもの）です。半透明のガラスなどの Aero の効果はまだありません。
XP のシェルと同じ Wine、同じ bridge（ウィンドウマネージャー、代理ウィンドウ、トレイ）、同じ supervisor で動きます。

```sh
scripts/setup.sh --media /path/to/Windows7SP1x64.iso
```

ログイン画面でセッション「**Windows 7 Shell (Wine, X11)**」を選びます。`--greeter` を付けると、LightDM のログイン画面が Windows 7 のログオン画面になり、そのまま Windows 7 のシェルに入ります。

> [!WARNING]
> XP のシェルより新しく、確かめた範囲も狭い試作です。下の「制限」を読んでから試してください。
> Windows 7 の ISO はご自身で用意してください。Microsoft のファイルの扱いは、Microsoft の使用許諾に従ってください。

## 使う Windows 7 のファイル

`extract/win7.py` が、ISO の `sources/install.wim` から次のものだけを `.local/assets-win7` に取り出します。
エディションごとのイメージのうち Professional を使います（無ければ最後のもの）。シェルのファイルはどのエディションでも同じです。
言語はイメージの既定の言語で決まり、その言語の MUI ファイル（`ja-JP\shell32.dll.mui` など）も取り出します。

| ファイル | 使い方 |
|---|---|
| `explorer.exe` | `C:\w7\w7-explorer.exe` として置くシェル（x64） |
| `explorerframe`、`shell32`、`shlwapi`、`propsys`、`shdocvw`、`dui70`、`duser`、`profapi`、`thumbcache`、`linkinfo` | Windows 7 のシェルの DLL。シェルと、セッションのプログラムのファイルのダイアログが使います（下の「どのプログラムが Windows 7 の DLL を使うか」） |
| `sndvolsso`、`winbrand`、`shacct`、`authui` | シェルだけが使う DLL（通知領域の音量、スタートメニューのエディション名・利用者の画像・終了） |
| `actxprxy` | Windows 7 のインターフェースの proxy。`w7actxprxy.dll` の名前で置きます（Wine の actxprxy.dll は Wine の DLL の proxy に残します） |
| `imageres`、`sud`、`DeviceCenter` | 文字列とアイコンだけを読む DLL（override なし） |
| `Cursors\aero_*.cur`、`aero_*.ani`、`user32.dll` | Windows 7 の Aero のカーソル（X のカーソルのテーマ `win4wine-win7` にします。I ビームと十字は user32.dll のもの） |
| `Branding\Basebrd\basebrd.dll` | ログオン画面のエディションの名前 |
| 上の DLL と `imageres` の 32bit 版（`SysWOW64`） | 32bit のプログラムのファイルのダイアログ、アイコンと名前 |
| Common Controls 6（amd64 と x86） | Windows 7 の DLL だけが使う comctl32。`C:\w7\comctl32.dll`（32bit は `C:\w7\wow64\comctl32.dll`） |
| user32.dll（データ） | システム アイコンだけを読む。`C:\w7\res\user32-w7.dll`（Wine の 0132） |
| `ja-JP\notepad.exe.mui`（データ） | テキスト ファイルの種類名（「テキスト ドキュメント」、新規作成の名前）だけを読む。`C:\w7\res\notepad.exe.mui`。メモ帳そのものは Wine のもの |
| Segoe UI、Tahoma、Microsoft Sans Serif、メイリオと MS UI Gothic（日本語のとき） | Windows 7 の UI とダイアログのフォント |
| `Resources\Themes\Aero` | Aero のテーマ（形式 4 の msstyles。Wine の 0113 で読みます） |
| `img0.jpg`、User Account Pictures、スタートメニューの `desktop.ini` | 壁紙、利用者の画像、スタートメニューのフォルダの日本語の名前 |

取り出したファイルには手を加えません。prefix には、Windows 7 と同じく DLL の隣の言語のフォルダに MUI ファイルを置き
（`system32\ja-JP\shell32.dll.mui`）、Wine がそれを読みます（Wine のパッチ 0102。言語に依存しない DLL で見つからない
リソースを、その DLL の MUI ファイルから探します）。名前を変えて置くもの（`w7-explorer.exe`、`w7actxprxy.dll`）は、MUI ファイルも
その名前にします。

### レジストリ

Windows 7 は、部品（component）ごとのマニフェスト（`WinSxS\Manifests`）にファイルとレジストリの値を書いています。
`extract/manifests.py` が、上のファイルの部品のマニフェストから、レジストリのキーと値、設定の既定値（configuration の schema）を
`.reg` にします。インストール済みの Windows のレジストリを丸ごと移すことはしません。使うファイルの部品が登録するものだけを入れます。
アイコンを番号（`shell32.dll,3`）で指す値は、リソースの ID（`shell32.dll,-4`）に直します（ID は Windows 7 と Wine の shell32 で同じ
ですが、並びは違います）。32bit の部品は自分のマニフェスト（`wow64_`）を持つので、32bit の登録（`registry-wow64.reg`）も同じように作り、
32bit の regedit で入れます。テキスト ファイルの種類とアイコンは notepad の部品から取ります（開くのは Wine の Notepad です）。

Windows 7 の comdlg32 の部品が登録する `FileOpenDialog`（シェルの「参照」が開くダイアログ）は、prefix/win7.py が Wine の comdlg32 の
ダイアログに向けます（部品のファイルは Wine のものなので、取り出しません）。

## prefix（`prefix/win7.py`）

`.local/prefix-win7` は WoW64 の prefix で、XP の prefix と同じ考え方で作ります。

- Windows 7 のシェルの DLL は `C:\w7\native`（64bit）と `C:\w7\wow64`（32bit）に置き、`system32` と `syswow64` に写します。
  どのプログラムがどれを使うかは、下の「どのプログラムが Windows 7 の DLL を使うか」のとおりです。
- Windows 7 の Common Controls 6 は `C:\w7\comctl32.dll`（32bit は `C:\w7\wow64\comctl32.dll`）に置きます。Windows 7 のシェルの
  セッションでは、シェル（`w7-explorer.exe`）と、ほかのプログラムの中の Windows 7 の DLL がこれを使い、プログラム自身は Wine の
  Common Controls 6 のままです（Wine のパッチ 0126。Windows 7 の comctl32 をプログラムに使わせると Wine のアプリが壊れるため）。
  サムネイルを作る `dllhost.exe` は、Wine の Common Controls 6 のままにします。
- `HKLM\...\Winlogon` の `Shell` を `w7-explorer.exe` にします。
- 利用者のフォルダー（ドキュメント・ピクチャ・ミュージック・ビデオなど）とライブラリは Windows 7 と同じ日本語の名前で出ます
  （desktop.ini と、Windows 7 の読み取り専用の代わりのシステム属性。Linux のフォルダーへのリンクになっているものには書きません）。
- フォントのつなぎ（FontLink）と置き換え（FontSubstitutes）は日本語の Windows 7 と同じです（MS Shell Dlg は MS UI Gothic、
  MS Shell Dlg 2 は Tahoma、Segoe UI には Tahoma とメイリオ。参照用の実機で読んだ値）。古い形のダイアログの文字も Windows 7 と
  同じフォントです。以前に取り出したファイル（MS UI Gothic と Tahoma が無い）では、これまでどおりメイリオ UI で代わりにします。
- 見た目: ClearType（Windows 7 の既定。色の付き方も Windows 7 と同じ: Wine の 0134）、Aero のテーマ（`prefix/theme-settings.c`）。テーマのフォント（メイリオ 9pt）を利用者の
  フォントとして保存します（`prefix/theme-fonts.c`。Windows でテーマを当てたときと同じ。日本語のフォント名はテーマの MUI にあるので、
  prefix の言語で動かします）。壁紙は Windows 7 と同じく `TranscodedWallpaper.bmp` に変換して使います。
- 「ドキュメント」は Linux のホーム（`H:`）です（XP のシェルと同じ）。
- `win7.py stage` は毎回のセッションの前に動き、Wine の更新で上書きされた DLL と登録（「コンピューター」「コントロール パネル」
  「ネットワーク」の名前など）を入れ直します。

### どのプログラムが Windows 7 の DLL を使うか

Windows のアプリのファイルを開く・保存するダイアログを Windows 7 の形にするため、ダイアログが読み込む DLL は
アプリにも使わせます。それ以外の DLL はシェルだけです。

| プログラム | Windows 7 の DLL | 設定 |
|---|---|---|
| シェル（`w7-explorer.exe`） | 上の表のシェルの DLL すべて（14 個）と、Windows 7 の Common Controls 6 | AppDefaults で `native` |
| Windows のアプリ（64bit と 32bit） | ファイルのダイアログの 10 個（`explorerframe`、`shell32`、`shlwapi`、`propsys`、`shdocvw`、`dui70`、`duser`、`profapi`、`thumbcache`、`linkinfo`） | DllOverrides で `native,builtin` |
| Wine 自身のプログラム（デスクトップの `explorer.exe`、サービス、`start`、`winemenubuilder`、`winebrowser`、`control`、`rundll32`、`winecfg` など） | なし | AppDefaults で `builtin` |
| サムネイルを作る `dllhost.exe` | ファイルのダイアログの 10 個（Common Controls 6 は Wine のもの） | AppDefaults で comctl32 だけ `builtin` |

アプリの中では、Windows 7 の DLL は Windows 7 の Common Controls 6 を、アプリ自身は Wine の Common Controls 6 を使います
（Wine のパッチ 0126）。

Windows 7 の DLL でうまく動かないアプリには、Wine の DLL を使わせることができます（ファイルのダイアログは Wine のものに
なります）。実行ファイルの名前で指定し、prefix に記録されるので、次からもそうなります。

```sh
prefix/win7.py builtin .local/prefix-win7 app.exe          # Wine の DLL にする
prefix/win7.py builtin .local/prefix-win7 app.exe --undo   # Windows 7 の DLL に戻す
```

一度だけ試すときは、セッションの端末から
`WINEDLLOVERRIDES="$WINEDLLOVERRIDES;explorerframe,shell32,shlwapi,propsys,shdocvw,dui70,duser,profapi,thumbcache,linkinfo,comctl32=b" wine app.exe`
のように起動します（セッションが入れている値に足します）。

## カーソルとログオン画面

- カーソル: `greeter/cursors.py --win7` が、Windows 7 の Aero の配色（shell-cursors の部品のマニフェストにある既定の割り当て）の
  `.cur`・`.ani` を X のカーソルのテーマ `win4wine-win7` にします。画像の影は Windows 7 のファイルに入っているものです。待ち（`aero_busy.ani`）と
  バックグラウンドで作業中（`aero_working.ani`）は動くカーソルです。Wine のアプリも Linux のアプリも、このテーマのカーソルを使います。
- ログオン画面: `greeter/win7-greeter.py`（LightDM の greeter）が、Windows 7 の LogonUI の画面を描きます（`greeter/w7logon.py`）。
  背景（imageres.dll。画面の縦横比に近いもの）、利用者の写真とその枠、パスワードの欄と矢印のボタン、下のボタンと文字（authui.dll と
  その MUI、メイリオ）、エディションの名前（basebrd.dll）は、メディアのファイルのものです。大きさや位置は authui のレイアウトの
  ファイル（UIFILE）と、Windows 7 SP1 の実機（1024x768 と 1280x800）で測ったものです。パスワードが違うときのメッセージ、
  シャットダウンのボタンと、その横の ▲ のメニュー（再起動、スリープ・休止状態（できる場合）、シャットダウン）があります。
  利用者の写真は、LightDM（AccountsService）に設定されたものか、Windows 7 の既定の `user.bmp` です。
- 利用者が複数いるときは、Windows 7 と同じく、まず利用者の一覧（小さな枠の写真と名前、名前の順、1 行で中央）を出し、
  選んだ利用者の写真とパスワードの欄の下に「ユーザーの切り替え」のボタンを出します。クリックか矢印キーと Enter で選び、
  「ユーザーの切り替え」・Esc・Alt+W で一覧に戻ります。枠の絵（authui.dll）、間隔、位置、ボタンの大きさは実機
  （1280x800、2 人と 4 人、長い名前）で測りました。画面に 1 行で入らない人数のときの折り返しは測っていません。
- ログオン後と終わるとき: シェルが動き出すまで「ようこそ」、ログオフ中は「ログオフしています...」、終了と再起動では
  「シャットダウンしています...」を、ログオン画面の背景に回る印と一緒に出します（`greeter/w7-splash.py`）。文字は
  winlogon.exe と wininit.exe の MUI、回る印は imageres.dll の BITMAP 5000〜5004（18 コマ）、並び方は authui のレイアウト（状態の表示）と
  実機で測ったものです。ログオン画面と同じファイルを使い、`--greeter` で入れたときはそのファイルを、`windows7/7.sh run` のようにログオン画面なしで始めたときは取り出したファイル（prefix の `win7-shell.json` の `assets`）を読みます（session.json の `welcome_screen` で止められます）。prefix の更新（Wine の更新のあとなど）の間は、Windows 7 が初めてのログオンで出す「デスクトップを準備しています...」（winlogon.exe.mui の 1009）を「ようこそ」の代わりに出します。

## セッション

session.json（`~/.config/win4wine/win7-session.json`）の `"shell": "win7"` で、supervisor は次のように動きます。

- `C:\w7\w7-explorer.exe` を起動し、`prefix/win7.py stage` を使います。ようこそ画面は Windows 7 のもの（下の「カーソルとログオン画面」）です。
- `WIN4WINE_SHELL=win7` を設定します。Windows 7 のためだけの Wine のパッチ（0120、0121、0123〜0125 の一部）は、これがあるときだけ効きます。
- Linux の窓は、アプリ（X11 の `WM_CLASS`、Wayland の app_id）ごとにタスクバーのボタンにまとまります（proxy が窓に
  AppUserModelID を付けます）。スタートメニューにあるアプリなら、ジャンプリストの題とアイコンはそのアプリのもので、
  「タスク バーにこのプログラムを表示する」で固定でき、固定したボタンから起動できます（`session/linux_apps.py` が
  `C:\xp\linux-apps\relaunch.tsv` に名前・コマンド・アイコンを書き、proxy が Windows 7 の shell32 と同じ窓のプロパティ
  （AppUserModel の RelaunchCommand、RelaunchIconResource、RelaunchDisplayNameResource）に付けます。以前は題が "proxy" でした）。
- Linux のトレイアイコン（StatusNotifierItem と XEmbed）は、Windows 7 の通知領域に出ます（Wine の 0124）。
- 日本語入力は XP のシェルと同じ fcitx5 + Mozc です。入力モード（「あ」など）は通知領域に出て、Wine のアプリでは窓の中で
  変換します。
- Windows のアプリがスタートメニューに作るショートカットは、「すべてのプログラム」に並びます。
- ファイルを開く・保存するダイアログ: Windows のアプリ（64bit・32bit）は Windows 7 のダイアログです（一覧は Windows 7 の shell32 と
  explorerframe が描きます。ダイアログの枠は Wine の comdlg32 で、Windows 7 と同じ大きさ・配置・フォントにし、上の段の戻る・進む、
  パンくずのアドレス（各フォルダーの ▸ でその中のフォルダーの一覧、空いた所のクリックで入力）、更新、検索欄も実機と同じ部品で
  描きます。Wine の 0137）。検索は今のフォルダーの中を名前で絞り込みます（実機は Windows Search の索引を検索しますが、prefix には
  ありません）。最初に開くのは「ドキュメント」フォルダーです（実機はドキュメント ライブラリ。ライブラリの一覧には Windows Search の
  部品が要ります）。Notepad のように古い形のダイアログを使うアプリも、一覧は Windows 7 の
  ものです（Wine の 0106、0108）。Linux のアプリ（GTK 3・GTK 4・Qt）はそれぞれのダイアログです。どちらも「ドキュメント」（`H:`）は
  Linux のホームで、日本語の名前のファイルも開け、保存できます。
- Windows キー、Ctrl+Esc（Linux の窓からも）、Win+R、Win+E、Alt+Tab が使えます。
- タスクバーを右クリックして「プロパティ」を開くと、実機と同じように「小さいアイコンを使う」（高さ 30 px のタスクバー）と
  「タスク バーのボタン」（常に結合、ラベルを非表示／タスク バーがいっぱいの場合に結合／結合しない）を変えられます。
  「適用」か「OK」ですぐにタスクバーが変わり、最大化した窓（Wine の窓も Linux の窓も）も新しい作業領域に合わせ直します
  （Wine の 0030、0111、0112）。設定は prefix のレジストリ（`HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced` の
  `TaskbarSmallIcons` と `TaskbarGlomLevel`）に残り、次のセッションでもそのままです。Linux の窓のボタンも、ラベル付きのボタンになります。
- クイック起動: 実機と同じく、タスクバーの右クリック →「ツール バー」→「新規ツール バー」で
  `%APPDATA%\Microsoft\Internet Explorer\Quick Launch` を選ぶと、タスクバーに出ます（「ボタン名の表示」と「タイトルの表示」を外し、
  固定を外して左へ動かすと Vista までの形）。左へ動かした直後は、実機と同じくクイック起動の帯が残りの幅を取り、起動中のアプリは
  右端に寄ります。起動中のアプリの帯のつまみを左へ引きずると、普通の幅に戻ります（ログオフで保存されます）。フォルダには Windows 7 の既定のユーザーと同じく「デスクトップの表示」と
  「ウィンドウを切り替える」が入ります（ISO から取り出したものを prefix が一度だけ置きます。EXTRACT_VERSION 6）。ツールバーは
  ログオフで保存され、次のセッションでもそのままです。「デスクトップの表示」は 2 回目に窓を元に戻し、最後に使っていた窓が前面に
  戻ります（Wine の 0051）。フォルダの選択のダイアログは実機と同じ「フォルダー:」と「フォルダーの選択」で、入力した `%APPDATA%`
  などを展開し、入力したフォルダを受け付けます（Wine の 0105、0137）。
- ピン留め: タスクバーのボタンを右クリックして「タスク バーにこのプログラムを表示する」で、Windows のアプリも Linux のアプリも
  タスクバーに残り、そこから起動できます（ログオフ後も残ります）。実機の既定のピン留め（Internet Explorer など）は置きません。
- タスク一覧の空いた所の明るい光（大きいアイコンのとき）は実機と同じです（Windows 7 の TaskBand2 の部品。小さいアイコンでは
  出ません）。
- フォルダーは、Windows のアプリからも、Wine 自身のプログラム（`wine explorer C:\パス`、`wine start C:\パス`）からも Windows 7 の
  フォルダーウィンドウで開きます（Wine の 0128）。フォルダーウィンドウのアドレス バーと検索欄の高さは実機と同じです（Wine の 0119）。
- フォルダーウィンドウのナビゲーション ウィンドウは実機と同じです: 「お気に入り」のダウンロード・デスクトップ・最近表示した場所
  （prefix の `user-profile.exe` が Links フォルダーに作るリンクと日本語の名前）、ライブラリごとのアイコン（Wine の 0100）。
  既定のライブラリ（ドキュメント・ピクチャ・ビデオ・ミュージック）も `user-profile.exe` が Windows 7 の shell32 に作らせます。
  ダウンロードが Linux のフォルダーへのリンクのときは、そこに desktop.ini を置かないため、アイコンは普通のフォルダーです。
- 詳細ペインなど、アイコンを元の大きさ以外で出すところは Windows 7 と同じく面積平均で縮めます（Wine の 0110）。
- リスト ビュー（Explorer テーマ）とメニュー バーは、実機と同じくマウスの下の項目を光らせます。無効な窓のメニュー バーの文字は
  灰色です（Wine の 0131、0117）。
- Tahoma（MS Shell Dlg 2）などの中の日本語は、実機と同じ大きさと字形です（8 pt のダイアログの文字は MS UI Gothic の
  12 px のビットマップ。Wine の 0135）。
- 種類の列のテキスト ファイルは「テキスト ドキュメント」です（Windows 7 のメモ帳の MUI の文字列。メモ帳そのものは Wine のもの）。
- Alt+Tab は Windows 7 ベーシックの形です。Windows 7 のテーマの `AltTab` クラスで描き（背景、選んだ項目の枠、題の字体と色）、
  大きさや並びは Windows 7 SP1 の実機に合わせています（1 行に 7 個、行は作業領域に入るだけ）。最初の行の最後に「デスクトップ」が
  入り、選ぶとすべての窓を最小化します（Windows 7 では、デスクトップを見せたあと元に戻せますが、ここでは窓ごとに戻します）。
- 窓の枠とキャプションのボタンは Windows 7 ベーシックと同じ大きさです（枠 8 px、最小化・最大化・閉じるのボタン 31x17。Wine の
  0116。Windows 7 SP1 の実機で同じ位置・大きさの Notepad と画素ごとに比べています）。
- メニュー（メニュー バーとポップアップ）は Windows 7 のテーマで描きます（Wine の 0117、0129）。項目の大きさや文字の位置、影は
  実機と画素ごとに同じです（ClearType の色を除く）。Windows 7 の既定どおりキーボードの手掛かりは隠れ、アクセス キーの下線は
  Alt や F10 でメニューを使う間だけ出ます。
- システム アイコン（アプリの既定のアイコン、メッセージ ボックスのエラー・警告・情報・質問、盾）は Windows 7 のものです。Windows 7 の
  user32.dll をデータとしてだけ `C:\w7\res\user32-w7.dll` に置き、Wine の user32 がそこから読みます（Wine の 0132。名前を変えて
  いるのは、Wine の user32 と取り違えないため）。
- メッセージ ボックスは Windows 7 の形（白地に下のボタン帯、右寄せのボタン）で、既定のボタンの脈動とプログレス バーの光も
  Windows 7 と同じです（Wine の 0133）。
- スタートメニューの右の列の文字は実機と同じく 1 回だけ描きます（Wine の 0127）。
- Windows のアプリのコントロール（Wine の Common Controls 6）は、ドロップダウン リスト、ステータス バー、タブ、プログレス バー、
  エディットの枠、トラック バー、リスト ビュー（詳細表示、Explorer テーマの選択と列の線）、フォーカスの点線、文字の位置を Windows 7 に合わせています（Wine の
  0130、0131）。
- スタートメニューの「シャットダウン」と、その横のメニューの「ログオフ」「再起動」で、セッションを終えます（XP のシェルと同じく、
  終了と再起動では session.json の `shutdown_command`・`reboot_command` を実行します）。

- 壁紙は、モニターが複数あるときも、実機と同じくモニターごとに描きます（Wine の 0094 の PaintMonitor）。
- 日本語の prefix では、メモ帳（Wine のもの）の既定のフォントを日本語版の Windows 7 のメモ帳と同じ「ＭＳ ゴシック」12 ポイントに
  します（利用者が選んだフォントは変えません）。Wine の既定（System）には日本語の字が無く、日本語のフォントの無い Linux では
  四角になっていました。
- タスクバーのボタンを指したときの窓の一覧は、実機の Windows 7 ベーシックと同じ見た目です（シェルにはデスクトップの合成が無いと
  答えます。Wine の 0125）。
- Wayland のセッション「Windows 7 Shell (Wine, Wayland)」もあります（`setup.sh --wayland`）。モニターごとの倍率が
  使え、スタート ボタンの縁は compositor が本物の透明度で描きます（[wayland.md](wayland.md)）。

Windows 7 のシェルのために足した Wine のパッチは 0094〜0136 です（[patches.md](patches.md)）。

## 制限

- Aero の効果（ガラス、ライブ サムネイル、Aero Peek）はありません。デスクトップの合成（DWM）が無いためです。
- ログオン画面（`--greeter`）の「コンピューターの簡単操作」のボタンは何もしません。
  左上の入力言語のボタンはありません。
- 確かめたのは日本語版の Windows 7 Professional SP1（x64）です。英語版は、取り出したファイルの ja-JP の MUI を、Microsoft の英語の言語パック（KB2483139）の en-US のものに入れ替えて確かめました（ログオン画面、「ようこそ」「ログオフしています」、スタートメニュー、フォルダーウィンドウ、ファイルのダイアログ。X11 と Wayland）。字体は Segoe UI、ロケールは `en_US.UTF-8` です。英語版のメディアそのものはまだ試していません。
- ネットワーク、ヘルプとサポート、コントロール パネルの中身は、Wine にその部品が無いので使えません。
- スタートメニューの「ロック」「スリープ」「休止状態」は何もしません。
- Wine は利用者を管理者として動かすので、管理者かどうかを題名に出すアプリ（Notepad++ の「[Administrator]」など）はそう表示します
  （Windows Vista 以降として動く prefix の Wine で同じです。Windows 7 の DLL によるものではありません）。
