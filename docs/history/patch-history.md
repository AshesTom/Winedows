# Wine のパッチの変遷（記録）

パッチを足したり、番号を振り直したりした経緯と、以前の番号との対応表です。今のパッチの一覧と書式は
[../patches.md](../patches.md) にあります。この文書は書いた時点の記録で、文体も当時のままです。

- 2026-10-10 の段階 2 で、Wine 10.0 用の 0001〜0076 の 76 本を機能ごとの 55 本にまとめ直した。
- 2026-10-10 の段階 5 で基にする Wine を 11.0 にした。upstream に同じ修正が入った3本を外し、11.0 で必要になった1本
  （0016）を加えて、53 本を 0001〜0053 に振り直した（下の「10.0 → 11.0」）。
- 2026-10-10 の段階 6（日本語入力）で2本（0026、0027）を加え、0053（段階 5 の 0051）を広げた。55 本を 0001〜0055 に
  振り直した（下の「段階 6」）。
- 2026-10-10 の段階 7（WoW64）で1本（0017）を加え、0025（今の 0027）と 0026（今の 0028）を直した。56 本を 0001〜0056 に
  振り直した（下の「段階 7」）。
- 2026-10-11 に 0022 を広げた（別のプロセスのクラスのアイコン。下の「64-bit のアプリのタスクバーのアイコン」）。続けて
  2本（0023、0041）を加え、58 本を 0001〜0058 に振り直した（下の「Windows キーと日本語のコンソール」）。
- 2026-10-11 の段階 8（後回しの候補）で1本（0041）を加え、0032 と 0037 を直した。59 本を 0001〜0059 に振り直した
  （下の「段階 8」）。
- 2026-10-11 の段階 9（ログイン画面）で1本（0043）を加えた。60 本を 0001〜0060 に振り直した（下の「段階 9」）。
- 2026-10-12 の段階 10（DPI）で1本（0044）を加え、0036、0046、0049 を広げた。61 本を 0001〜0061 に振り直した
  （下の「段階 10」）。
- 2026-10-12 の段階 11（Wayland のセッション）で 0027 を広げ、1 本（0062）を末尾に加えた（下の「段階 11」）。番号の振り直しは無い。
- 2026-10-12 の ReactOS のシェル（段階 12）で 0035 と 0047 を広げ、4 本（0063〜0066）を末尾に加えた（下の「段階 12」）。番号の振り直しは無い。
- 2026-10-13 に 5 本（0067、窓を動かしたときの下の窓の描き直し。0068、選んだメニュー項目のフェード。0069 と 0070、変換中の文字を入力欄の中に出す。0071、Win+R のダイアログが有効にならないことがあった）を末尾に加えた。番号の振り直しは無い。
- 2026-10-13〜16 に、ウィンドウの寄せ（0073）、描画の速さ（0074、0075）、DrawThemeParentBackground の位置（0076）、ClearType（0077）、
  Windows 7 のシェル（0078〜0119）を末尾に加えた。番号の振り直しは無い。
- 2026-10-16 に、末尾に溜まった 0067〜0119 を整理した。既存のパッチを直すだけのもの 5 本を元のパッチに含め（0071 → 0023、
  0069 → 0029、0117 → 0102、0116 と 0118 → 0105）、ほかはまとまりに合う位置に移した。Windows 7 のシェルのパッチは
  「API と Wine の修正」「版 4 のテーマ」「セッション」「見た目」に分け、名前を書式（`external`、`win7`）に揃えた。119 本を
  0001〜0114 に振り直した（下の「2026-10-16 の整理」）。最後に当てた後のソースは整理の前と同じ。
  この整理の前の Windows 7 の文書は、0078 より前のパッチを、移植用のリポジトリのコミットの番号（例: 0053 = 今の 0065
  タスクバーの背景）で書いたところがある。
- 2026-10-16 に 0115（riched20。Windows 7 の検索欄の日本語）を「Windows 7 の見た目」の最後に加えた。番号の振り直しは無い。
- 2026-10-16 の polish ブランチで 0115 を広げ（ホストの書式、DEFAULT_CHARSET）、0116（コンボ ボックスの一覧を外のクリックで閉じる）と 0117（IME の変換中の文字の下線）を末尾に加えた。master に入れる前に、まとまりの位置に並べ直す。
- 2026-10-16 の polish ブランチで、XP のセッションの細部を実機と比べて合わせる 0118〜0123 を末尾に加えた（並べ直しは master の前）。
- 同じく 0124（ReactOS のシェルの Common Controls 6）を末尾に加えた。
- 2026-10-16 の polish ブランチで、末尾の 0116〜0118 をまとまりの位置に移した（下の「polish の並べ直し」）。0119〜0124 は
  Windows 7 のシェルのために加えた仕組みを使うので、末尾の「XP と ReactOS のシェルの細部」のまとまりとして残した。あわせて、
  0069（今の 0072）が書き直していた 0059（今の 0062）の XP の拡大縮小を、書き直した後の形で 0059 に含めた。最後に当てた後のソースは並べ直しの前と同じ。
- 2026-10-17 の taskbar-settings ブランチ（Windows 7 のタスクバーの設定）で、2 本を「Windows 7 のシェルが呼ぶ API と Wine の修正」の
  最後に加え（0096 SendNotifyMessage のブロードキャストの文字列、0097 DM_REPOSITION）、最大化した窓を作業領域に合わせ直す変更を
  0024 に含めた。旧 0096〜0124 は 0098〜0126 になった（下の「タスクバーの設定」）。
- 2026-10-17 の win7-wayland ブランチ（Windows 7 のシェルの Wayland のセッション）で、0079 の gdi32 CreateBitmapFromDxSurface を
  スタブから失敗を返す関数にし（フォルダの窓の後でスタートボタンを押すとシェルが落ちた）、0107 を Wayland のセッション
  （`WIN4WINE_COMPOSITED`）では合成しないようにした。0079 の PaintMonitor をモニターごとの壁紙にし、0109 で Windows 7 の
  シェルのプロセスには DwmIsCompositionEnabled を FALSE にした。番号と本数は変えていない。labwc のパッチを広げ、wlroots に
  0003 を加えた。
- 2026-10-17 の見直し（review-fixes ブランチ）で、0075 を「Wayland のセッション」のまとまりに加えた（別のスレッドからの前面の
  知らせが遅れて届いたとき、その後に前面にした窓を活性化し直さない）。旧 0075〜0126 は 0076〜0127 になった（下の「見直し」）。
  上の win7-wayland の項の 0079・0107・0109 は、今の 0080・0108・0110。
- 2026-10-20 の性能の調整で、0009 を「Wine 一般の修正」の最後に加えた（TrackMouseEvent の追跡が、上に重なった窓で
  終わるように）。旧 0009〜0127 は 0010〜0128 になった（下の「性能」）。
- 2026-10-20 の english-win7 ブランチで、0010・0011・0110 を加えた（下の「英語版の Windows 7」）。続く quick-launch ブランチで、
  0129〜0131（XP と ReactOS の細部）の後の末尾に 0132（ほかのプロセスの窓の SetWindowPlacement）と 0133（Windows 7 の項目
  ダイアログ）を加え、0094 を広げた。
- 2026-10-21 に末尾の 0132・0133 をまとまりの位置に移した（下の「クイック起動の後の整理」）。最後に当てた後のソースは並べ直しの前と同じ。
- 2026-10-21 に AshesTom さんのフォークの XP のセッションのための 17 本を、まとまりの位置に加えた（下の「AshesTom さんのフォークの
  統合」）。150 本。
- 2026-10-22 に、フォントの滑らかさをすぐに反映する 0165 を加え、0062 を標準となしにも広げた。XP のシェルで FreeType の v35 の
  インタプリタを選ぶ案（コミット前の 0164）は採らず、システムの FreeType のヒンティングを使う。165 本。
- 同じ日に 0166 を加えた。v35 のインタプリタは、XP のシェルでフォントの滑らかさが標準のときだけ使う（ClearType ではシステムの
  FreeType のまま）。166 本。
- 2026-10-23 に mikuta0407/win4wine の master（45d7286）を取り込んだ。そちらの 0151（2 つの大きさのアイコンをそれぞれの画像から
  作る）は、このブランチの 0152（PrivateExtractIcons の 2 つの大きさ）と同じ修正なので 0151 を採り、0152 を外した。
  0151（ごみ箱の DACL）は空いた 0152 に移した。166 本。
- 同じ日に 0167（メッセージ ボックスの XP のサウンド）を加えた。167 本。
- 同じ日に、Windows XP スタイルと Windows クラシック スタイルの切り替えをすぐに全部の窓に反映する 0166 を 0165 の後に加え、0086 の GetThemeSys* がクラスのテーマのファイルの値を返すようにした。旧 0166・0167 は 0167・0168 になった。168 本。
- 2026-10-24 に、セキュリティ センターを XP と同じにするため 0169〜0171 を加え、0023（rundll32 と mshta にも WOW64 を知らせない）と 0156（CoInitializeSecurityParam のグループの COM）を広げた。171 本。
- 同じ日に、ヘルプとサポート センターのために 0172・0173 を、通知領域のバルーンの影を XP と同じく無くすために 0174 を加えた。174 本。
- 2026-10-25 に、XP のメニューとコンテキスト メニューのフェード・展開を戻し、AnimateWindow の AW_BLEND・AW_SLIDE を実装する 0175 を加えた。175 本。
- 同じ日に XP のソースと照合し、ディレクトリ監視の再開時に元のフィルターを保つ 0176 と、rename 通知を移動元・移動先の両方へ届ける 0177 を加えた。177 本。
- 0178 は XP の AnimateWindow と同じく不正なフラグの組み合わせを拒否する。178 本。

それ以前の文書（`docs/history/investigations.md` の各節と、Git 管理外の開発の記録）に出てくるパッチ番号は、書いた時点の番号（段階 2 より前の旧番号、
段階 2〜4 の 10.0 版の番号、段階 5 の 11.0 版の番号、または段階 6 の番号）。下の対応表で今の番号に読み替える。

## AshesTom さんのフォークの統合（2026-10-21）

旧番号は、この統合の前（v0.14.0、7e14d50）の番号。新しいパッチは AshesTom さんのフォーク（AshesTom/win4wine の
09c7b93 と 0ca1242。そちらの 0127〜0141、0143、0144）から入れたもの。

| 現在 | 旧番号 | 名前 |
|---|---|---|
| 0001〜0015 | 同じ | （順は同じ） |
| 0016 | （AshesTom） | `user32-sys-colors-temp` |
| 0017 | （AshesTom） | `winmm-expand-sound-paths` |
| 0018 | （AshesTom） | `kernelbase-geo-friendly-name` |
| 0019〜0041 | 0016〜0038 | （順は同じ） |
| 0042 | （AshesTom） | `winex11-external-keep-shell-window-down` |
| 0043 | （AshesTom） | `win32u-external-xp-input-languages` |
| 0044〜0051 | 0039〜0046 | （順は同じ） |
| 0052 | （AshesTom） | `user32-external-xp-caption-animation` |
| 0053〜0063 | 0047〜0057 | （順は同じ） |
| 0064 | （AshesTom） | `win32u-external-xp-simulated-bold` |
| 0065〜0084 | 0058〜0077 | （順は同じ） |
| 0085 | （AshesTom） | `uxtheme-classic-metrics-exports` |
| 0086 | （AshesTom） | `uxtheme-display-properties-styles` |
| 0087 | （AshesTom） | `uxtheme-external-theme-refresh` |
| 0088〜0143 | 0078〜0133 | （順は同じ） |
| 0144 | （AshesTom） | `apphelp-advapi32-run-checks` |
| 0145 | （AshesTom） | `uxtheme-export-ordinals` |
| 0146 | （AshesTom） | `uxtheme-is-theme-active-by-policy` |
| 0147 | （AshesTom） | `uxtheme-external-caption-small-icons` |
| 0148 | （AshesTom） | `user32-external-xp-themed-controls-beside-comctl32` |
| 0149 | （AshesTom） | `ntdll-external-xp-comctl32-beside-exe` |
| 0150 | （AshesTom） | `user32-external-xp-plain-comctl32-classes` |

フォークの 0142（wineboot が StartUp の desktop.ini を開かない）は、同じことをする 0010 があるので入れていない。入力言語（0043）、
最小化・最大化のアニメーション（0052）、擬似太字（0064）は XP のセッションだけにした（フォークでは外部シェル全体）。
apphelp と uxtheme の関数の表、タイトルバー、comctl32 の切り替えに触れる 7 本（0144〜0150）は、Windows 7 のためのパッチの後の
「XP と ReactOS のシェルの細部」に置いた。どの段階でも、最後のツリーはフォークのパッチを末尾に当てたもの（移植用のリポジトリの
タグ ash-before-reorder）と同じ。

統合の後、0086 の DrawNCPreview（画面のプロパティの見本）の文字を、呼んだ themeui.dll の文字列（日本語版では日本語）にし、
Luna の見本のタイトルの文字をスタイルのタイトルの文字色（白）にした（フォークでは英語の固定の文字と、DC の黒い文字色）。
0086 では、クラス名なしの OpenThemeDataFromFile にスタイル全体の値を返し（クラシックから Windows XP スタイルに戻せなかった）、
XP のセッションでは Wine 自身の Light を一覧に出さないようにした。0087 は、新しいスタイルを後から読んだプロセスが自分の窓に
WM_THEMECHANGED をもう一度送る。0149 と 0150 は、システムのディレクトリのプログラム（メモ帳など）を XP の comctl32 の横とみなさない
（フォークでは system32 の Wine の comctl32.dll を見て、メモ帳の編集欄とステータス バーが作られなかった）。

## クイック起動の後の整理（2026-10-21）

旧番号は、この整理の前（quick-launch ブランチのマージ、ea3f56e）の番号。0001〜0045 は同じ番号のまま。

| 現在 | 旧番号 | 名前 |
|---|---|---|
| 0046 | 0132 | `win32u-external-other-process-placement` |
| 0047〜0126 | 0046〜0125 | （順は同じ） |
| 0127 | 0133 | `comdlg32-external-win7-item-dialog` |
| 0128〜0133 | 0126〜0131 | （順は同じ） |

0046 は XP・ReactOS・Windows 7 のどのシェルの「デスクトップの表示」も使う、外部シェルの窓の扱いなので、「ウィンドウの配置、
UI 状態、メニュー、WM_PRINT、壁紙」のまとまりの最後に置いた。0127 は Windows 7 のシェルのセッションだけで効く見た目なので、
「Windows 7 の見た目」の最後に置いた。どの段階でも、最後のツリーは並べ直しの前（移植用のリポジトリのタグ pre-tidy-ql）と同じ。
説明の中の番号（ほかのパッチを指すもの）も今の番号に直した（「Old patches:」の行は書いた時の番号のまま）。

## 英語版の Windows 7（2026-10-20）

旧番号は、この変更の前（v0.12.0）の番号。0001〜0009 は同じ番号のまま。

| 現在 | 旧番号 | 名前 |
|---|---|---|
| 0010 | （新規） | `wineboot-skip-protected-startup-items` |
| 0011 | （新規） | `ntdll-basic-information-zero-attributes` |
| 0012〜0109 | 0010〜0107 | （順は同じ） |
| 0110 | （新規） | `ntdll-external-win7-readonly-folders` |
| 0111〜0131 | 0108〜0128 | （順は同じ） |

0010 と 0011 は Windows の動作に合わせる一般の修正なので、「Wine 一般の修正」のまとまりの最後に置いた。0110 は Windows 7 の
シェルのセッションだけで効くファイルの属性の扱いなので、同じく拡張属性に残す 0109（旧 0107）の次に置いた。
説明の中の番号（ほかのパッチを指すもの）も今の番号に直した。0127・0129〜0131 の説明には、以前の付け替えから
古いままだった番号（Windows 7 の対応するパッチ）があったので、それも直した。

## 性能（2026-10-20）

旧番号は、この変更の前（見直しの後）の番号。0001〜0008 は同じ番号のまま。

| 現在 | 旧番号 | 名前 |
|---|---|---|
| 0009 | （新規） | `win32u-mouse-leave-over-other-windows` |
| 0010〜0128 | 0009〜0127 | （順は同じ） |

0009 は Windows の動作に合わせる一般の修正なので、「Wine 一般の修正」のまとまりの最後に置いた。

## 見直し（2026-10-17、v0.11.0 の後）

旧番号は v0.11.0 の番号。0001〜0074 は同じ番号のまま。

| 現在 | 旧番号 | 名前 |
|---|---|---|
| 0075 | （新規） | `win32u-external-stale-activation` |
| 0076〜0127 | 0075〜0126 | （順は同じ） |

0075 は Wayland のセッションで見つかった活性化の競合（遅れて届いた前面の知らせ）の修正なので、Wayland のセッションの
まとまりに置いた。wlroots に 0004 を加え、labwc の 0001 を直した（番号は変えていない）。

## タスクバーの設定（2026-10-17）

旧番号は、この変更の前（v0.9.0）の番号。0001〜0095 は同じ番号のまま（0024 は広げた）。

| 現在 | 旧番号 | 名前 |
|---|---|---|
| 0096 | （新規） | `win32u-broadcast-notify-strings` |
| 0097 | （新規） | `user32-dm-reposition` |
| 0098〜0126 | 0096〜0124 | （順は同じ） |

## polish の並べ直し（2026-10-16）

旧番号は、並べ直しの前の polish ブランチの番号。0001〜0033 と 0119〜0124 は同じ番号のまま。

| 現在 | 旧番号 | 名前 |
|---|---|---|
| 0034 | 0116 | `winex11-external-click-outside-capture` |
| 0035 | 0117 | `riched20-external-ime-composition` |
| 0036〜0052 | 0034〜0050 | （順は同じ） |
| 0053 | 0118 | `win32u-external-xp-bitmap-metrics` |
| 0054〜0118 | 0051〜0115 | （順は同じ） |

## 2026-10-16 の整理

番号の変わったものだけを載せる（0001〜0022、0024〜0028 は同じ番号のまま）。旧番号は、整理の前の `wine/patches/` の番号。

| 現在 | 旧番号 | 名前 |
|---|---|---|
| 0023 | 0023、0071 | `win32u-external-task-list-keys` |
| 0029 | 0029、0069 | `winex11-external-xim-spot` |
| 0030 | 0070 | `imm32-external-stale-ime-ui-window` |
| 0031 | 0067 | `win32u-external-expose-flush` |
| 0032 | 0073 | `win32u-external-window-snap` |
| 0033 | 0074 | `winex11-external-gc-flush` |
| 0034 | 0030 | `win32u-external-default-cascade` |
| 0035 | 0031 | `win32u-external-ui-state` |
| 0036 | 0032 | `win32u-external-xp-popup-menus` |
| 0037 | 0033 | `user32-external-print-children` |
| 0038 | 0034 | `user32-external-rebar-alignment` |
| 0039 | 0035 | `user32-external-stretched-wallpaper` |
| 0040 | 0068 | `win32u-external-no-selection-fade` |
| 0041 | 0036 | `win32u-external-mono-text-advance` |
| 0042 | 0037 | `user32-external-drawtext` |
| 0043 | 0038 | `win32u-external-halftone-clip-mapping` |
| 0044 | 0039 | `win32u-external-transparent-blt-mapping` |
| 0045 | 0040 | `user32-external-classic-caption-glyphs` |
| 0046 | 0041 | `user32-external-icon-premultiply` |
| 0047 | 0042 | `conhost-external-double-width-cells` |
| 0048 | 0043 | `win32u-external-unpainted-window-screen` |
| 0049 | 0044 | `win32u-external-xp-dpi` |
| 0050 | 0077 | `win32u-external-cleartype` |
| 0051 | 0045 | `user32-classic-button-ideal-size` |
| 0052 | 0046 | `comctl32-external-start-button` |
| 0053 | 0047 | `comctl32-external-themed-shell-controls` |
| 0054 | 0048 | `comctl32-external-label-offsets` |
| 0055 | 0049 | `uxtheme-external-window-frames` |
| 0056 | 0050 | `uxtheme-external-caption-region` |
| 0057 | 0051 | `uxtheme-external-partial-frame-repaint` |
| 0058 | 0052 | `uxtheme-external-caption-help-button` |
| 0059 | 0053 | `uxtheme-external-image-resize` |
| 0060 | 0054 | `uxtheme-external-premultiply` |
| 0061 | 0055 | `uxtheme-external-set-window-theme-change` |
| 0062 | 0056 | `uxtheme-external-font-profile` |
| 0063 | 0057 | `uxtheme-external-localized-theme-values` |
| 0064 | 0058 | `uxtheme-external-start-user-name-shadow` |
| 0065 | 0059 | `uxtheme-external-taskbar-background` |
| 0066 | 0060 | `uxtheme-external-clock` |
| 0067 | 0061 | `uxtheme-external-rebar-grippers` |
| 0068 | 0072 | `uxtheme-external-menu-bar-height` |
| 0069 | 0075 | `uxtheme-external-paint-speed` |
| 0070 | 0076 | `uxtheme-parent-background-origin` |
| 0071 | 0062 | `winex11-external-rootless-screen` |
| 0072 | 0063 | `explorer-external-folder-shell` |
| 0073 | 0064 | `uxtheme-negative-theme-margins` |
| 0074 | 0065 | `user32-external-switch-to-this-window` |
| 0075 | 0066 | `user32-external-reactos-dialog-resource-names` |
| 0076 | 0078 | `dlls-win7-shell-imports` |
| 0077 | 0080 | `dlls-win7-start-menu` |
| 0078 | 0084 | `apphelp-allow-perm-layer` |
| 0079 | 0088 | `dlls-win7-folder-window-modules` |
| 0080 | 0099 | `advapi32-saferiisexecutablefiletype` |
| 0081 | 0087 | `xmllite-utf16-input` |
| 0082 | 0113 | `xmllite-random-access` |
| 0083 | 0089 | `combase-surrogate-bitness` |
| 0084 | 0091 | `ntdll-mui-files` |
| 0085 | 0093 | `server-hkcu-software-shared` |
| 0086 | 0094 | `comdlg32-view-font-redraw` |
| 0087 | 0095 | `comdlg32-item-dialog-windows-browser` |
| 0088 | 0097 | `comdlg32-view-activate` |
| 0089 | 0096 | `shell32-other-shell-registrations` |
| 0090 | 0098 | `comctl32-foreign-imagelists` |
| 0091 | 0114 | `kernelbase-number-format-zero` |
| 0092 | 0115 | `windowscodecs-fant-scaling` |
| 0093 | 0079 | `uxtheme-msstyles-format-4` |
| 0094 | 0090 | `uxtheme-window-theme-attribute` |
| 0095 | 0100 | `uxtheme-drawthemeparentbackgroundex` |
| 0096 | 0101 | `uxtheme-padded-border-caption-buttons` |
| 0097 | 0102、0117 | `win32u-themed-menus` |
| 0098 | 0109 | `uxtheme-drawthemetextex-callback` |
| 0099 | 0111 | `uxtheme-min-part-size` |
| 0100 | 0081 | `server-external-win7-stored-dacl` |
| 0101 | 0082 | `uxtheme-external-win7-taskbar` |
| 0102 | 0083 | `dlls-external-win7-start-button` |
| 0103 | 0085 | `shell32-external-win7-notify-icon` |
| 0104 | 0086 | `dlls-external-win7-keys-folder-windows` |
| 0105 | 0092 | `ntdll-external-win7-comctl32` |
| 0106 | 0110 | `user32-external-dt-prefixonly` |
| 0107 | 0112 | `dlls-external-win7-open-folders` |
| 0108 | 0103 | `winex11-external-win7-shadow-rounding` |
| 0109 | 0104 | `comctl32-external-win7-control-details` |
| 0110 | 0105、0116、0118 | `comctl32-external-win7-listview` |
| 0111 | 0106 | `user32-external-win7-system-icons` |
| 0112 | 0107 | `user32-external-win7-message-boxes` |
| 0113 | 0108 | `win32u-external-win7-cleartype` |
| 0114 | 0119 | `win32u-external-win7-linked-fonts` |

## 段階 12（ReactOS のシェル、2026-10-12）

| 現在 | 変更 |
|---|---|
| 0035 | WallpaperStyle 6（合わせる）と 10（塗りつぶす、ReactOS の既定）も、ReactOS の PaintDesktop と同じ計算で描く。XP の 2（拡大）は今までどおり |
| 0047 | `WIN4WINE_SHELL=reactos` のとき（ReactOS のシェル）は、スタートボタンとダイアログの部品を Wine のテーマ付きの部品に差し替えない。ReactOS の explorer はマニフェストで自分の Common Controls 6 を使う |
| 0066 | 新規。ReactOS の user32 はダイアログの部品の番号の名前を「#番号」で渡し、ReactOS の Static はその形しか読まない。Wine は Windows と同じく 0xffff と番号で渡すので、msgina のシャットダウンとログオフのダイアログのバナーとアイコンが出なかった（0047 で ReactOS の部品を使うようにしてから） |
| 0065 | 新規。Wine の SwitchToThisWindow は窓を戻すだけで前面にしなかった。ReactOS のタスクバーはこれだけで切り替えるので、Wayland のセッション（labwc は _NET_WM_USER_TIME 0 の窓を有効にしない）で戻した窓が有効にならなかった。opt-in のとき SetForegroundWindow も呼ぶ |
| 0064 | 新規。msstyles の数値の読み取りが負の符号を区切りとして飛ばし、ReactOS のテーマの負の余白（Lautus のスタートボタンの -82）が正になって、スタートボタンが 200px ほどに広がっていた。余白だけを符号つきで読む（XP の Luna の負の値は余白ではなく、変わらない） |
| 0063 | 新規。ReactOS のシェルはコントロール パネルや検索を `explorer.exe` の起動で開き、Wine の explorer の窓が開いていた。`WIN4WINE_FOLDER_SHELL` があれば、`/desktop` 以外の起動をその exe に渡す |

## 段階 11（Wayland のセッション、2026-10-12）

| 現在 | 変更 |
|---|---|
| 0027 | rootless の X サーバー（Wayland のセッションの Xwayland）ではルートの窓を読めず（BadMatch）、Explorer が落ちていた。読めなければ、影を同じ濃さの透明度を持つ黒の 32bit の窓で描き、compositor に重ねさせる。X11 では今までどおり画面を読む |
| 0062 | 新規。Xwayland では画面の読み取り（ログオフ・終了の灰色の画面の下地）が黒になっていた。XWAYLAND の拡張がある X サーバーで root の読み取りが失敗したとき、表示中の最上位の窓の中身を下から順に重ねて作る |

labwc と wlroots のパッチは `wayland/patches/`（[../wayland.md](../wayland.md)）。

## 段階 10（DPI、2026-10-12）

| 現在 | 変更 |
|---|---|
| 0044 | 新規。外部シェルのとき、プロセスを system DPI aware にする（XP には DPI 仮想化が無く、どのアプリも LogPixels で描く。Wine はマニフェストの無いアプリを 96 DPI で描いて画像のまま拡大し、ぼけていた）。AppCompatFlags の指定は優先する。GetSystemMetrics(ForDpi) の SM_CXICON を常に 32、SM_CXSMICON をメニューのボタン幅−2 の偶数への切り下げ、SM_CXCURSOR を 144 DPI まで 32・それより上で 64 にする（実XPで 96〜216 DPI の8通りを測った。`tests/dpi-metrics.c`） |
| 0036 | 外部シェルのとき、ヒンティングで変わらない送り幅を、拡大した値を1回だけ丸めた値にする（FreeType は 26.6 に丸めてから画素に丸めるので、MS UI Gothic の 15px の A・E・V・Z が XP より 1px 広かった。`tests/char-widths-dpi.c`） |
| 0046 | Start ボタンの画像リストを ILD_DPISCALE で描き、理想寸法も DPI で拡大した大きさ（切り捨て）で数える（XP の 120 DPI で 31×25、144 DPI で 37×30） |
| 0049 | caption ボタンを SM_CYSIZE−4 の正方形にし、間隔と枠からの余白を DPI によらず 2px にする。題名を SPI_GETNONCLIENTMETRICS の caption の字体で描く（テーマの 10pt を 120 DPI で換算し直すと 17px で、XP の 16px より大きかった）。DrawThemeText の単一の影の矩形を、左上だけでなく全体で動かす（中央揃えの縦のずれが半分になり、120 DPI の caption で影が 1px 上に出ていた） |

段階 10 の後、0043 を広げた: 背景を写すとき、表示中のレイヤード窓（スタートメニューのフェードする項目、shell32 の SysFader）の
部分を、その下にある同じプロセスの窓の surface から描き直す（Windows の画面の読み取りはレイヤード窓を含まない）。

0045〜0061 は前の 0044〜0060 を後ろへずらしたもの（0046 と 0049 は上の変更を含む）。0044 は win32u の文字と画素のまとまり
（0043 の後）に入れた。移植用リポジトリでは段階 10 の前の状態をタグ `stage10-base` に残した。

## 段階 9（ログイン画面、2026-10-11）

| 現在 | 変更 |
|---|---|
| 0043 | 新規。外部シェルのとき、背景ブラシの無いトップレベルの窓を表示する時、X に map する前にその下の画面を窓の面に写す（実XPはその窓が描くまで下の画面が見え、その DC から読める）。まだ表示していないトップレベルの窓の DC は画面を読む（描画は空の可視領域で切られたまま）。XP の shell32 の「Windows のログオフ」は、ブラシの無い全画面の窓（DimmedWindowClass）を表示して、自分の DC から帯ごとに読んで灰色にする。Wine は新しい面が白（表示前の DC は黒）だったので、背景が白くなっていた |

0044〜0060 は前の 0043〜0059 を後ろへずらしたもの（内容は同じ）。0043 は win32u の文字と画素のまとまり（0042 の後）に入れた。
移植用リポジトリでは段階 9 の前の状態をタグ `stage9-base` に残した。

## 段階 8（後回しの候補、2026-10-11）

| 現在 | 変更 |
|---|---|
| 0032 | 外部シェルのとき、区切りの項目の状態に MFS_GRAYED を足す。実XPは MENU・MENUEX のリソース、AppendMenu、InsertMenuItem のどれで作った区切りも MFS_GRAYED（3）を返し、Wine は MENUEX と InsertMenuItem の区切りが 0 だった（`tests/menu-separator.c`） |
| 0037 | 外部シェルのとき、文字列の末尾の `&` を描かない。DT_EXPANDTABS のタブの後の `&` の下線を、その後の字の下に引く（Wine はタブの1字を数えず、次の字の下に引くか、引かなかった）（`tests/text-underline.c`） |
| 0041 | 新規。外部シェルのとき、32-bit のアイコンを乗算済みアルファにする丸めを XP と同じ切り捨て（色×α/255）にする（Wine は四捨五入）。Alt+Tab の切り替えウィンドウのフォルダのアイコンの 146 画素の差の原因（`tests/icon-alpha.c`）。uxtheme のテーマ画像の同じ規則（0052）と合わせた |

0042〜0059 は前の 0041〜0058 を後ろへずらしたもの（内容は同じ）。0041 は user32 の文字と画素のまとまり（0040 の後）に入れた。
移植用リポジトリでは段階 8 の前の状態をタグ `stage8-base` に残した。

## Windows キーと日本語のコンソール（2026-10-11）

| 現在 | 変更 |
|---|---|
| 0023 | 新規。外部シェルのとき、Ctrl+Esc と、ほかのキーを挟まずに押して離した Windows キーで、`RegisterShellHookWindow` のウィンドウ（0020 の属性）に `HSHELL_TASKMAN` を送る。キーを受けたプロセスが先に前面を渡す（XP の Explorer の SetForegroundWindow は、入力を受けていないと拒まれる）。Win+R などのホットキーでは開かない |
| 0041（段階 8 から 0042） | 新規。外部シェルのとき、DBCS のコードページのコンソールの窓では升目を半角の幅にし、2バイトの文字に2升（`COMMON_LVB_LEADING_BYTE` / `TRAILING_BYTE`）を使う。キーの無い文字（IME の確定）も入力に入れる |

0024〜0040 は前の 0023〜0039、0042〜0058 は前の 0040〜0056 を後ろへずらしたもの（内容は同じ）。並びは 0017 のときと同じく、
使う仕組みの近く（0023 は 0020 のシェルフックの登録と 0022 の後、0041 は文字のまとまりの最後）に入れた。

## 64-bit のアプリのタスクバーのアイコン（2026-10-11）

| 現在 | 変更 |
|---|---|
| 0022 | 別のプロセスのウィンドウのクラスのアイコンも返す。外部シェルのとき owner はクラスのアイコンを server に渡し（ウィンドウを作るときと `SetClassLongPtr` のとき、既存の `set_class_info`）、`GetClassLongPtr(GCLP_HICON、GCLP_HICONSM)` が他のプロセスにそれを返す。WoW64 の XP のタスクバーは 64-bit のプロセスの PEB を読めず、64-bit のアプリのボタンが既定のアイコンになっていた |

番号と並びは変えない。移植用リポジトリでは段階 7 の状態をタグ `stage7` に残した。

## 段階 7（WoW64、2026-10-10）

| 段階 7 | 変更 |
|---|---|
| 0017 | 新規。外部シェルの XP Explorer（`xp-explorer.exe`）のプロセスにだけ、自分が WOW64 で動いていると知らせない（`ProcessWow64Information` に 0 と答える）。XP の shlwapi と shell32 は WOW64 では別の処理に入り、スタートメニューの「プリンタと FAX」のアイコンが一般のフォルダになっていた |
| 0026 | 段階 6 の 0025（XP の影）。影の濃さを double で計算する。32bit のビルド（x87）と 64bit のビルド（SSE）で丸めが違い、壁紙の上の影の画素が1段階ずれていた |
| 0027 | 段階 6 の 0026（通知アイコン）。XP の形式の `cbSize` を構造体の大きさから求める。64bit のアプリでは `NOTIFYICONDATAW_V3_SIZE` が 976 になり、XP の 952 と合わなかった |

0018〜0056 は段階 6 の 0017〜0055 を1つずつ後ろへずらしたもの（内容は上の2本以外同じ）。並びと番号は利用者の判断
（外部シェルの基盤のまとまりの 0016 の後に入れた）。WoW64 のビルドでは、すべてのパッチが i386 と x86_64 の PE、x86_64 の
Unix ライブラリとしてビルドされる。

## 段階 6（日本語入力、2026-10-10）

| 段階 6 | 変更 |
|---|---|
| 0026 | 新規。外部シェルのとき、Shell_NotifyIconW が XP のタスクバーの受け取る形式（署名、メッセージ、XP の大きさの NOTIFYICONDATAW）で送る。Wine の形式は XP の Explorer が受け付けず、Wine のアプリのアイコンも通知領域に出なかった |
| 0027 | 新規。外部シェルのとき、XIC を作る前に指定された変換位置を覚えておき、XIC を作るときに渡す。各ウィンドウの最初の変換が左上に出ていた（upstream の挙動） |
| 0053 | 段階 5 の 0051。通知領域（TrayNotifyWnd）の子には TrayNotify の背景を描き、BgType が None のパーツを部分的に透明と答える（実XPと同じ）。通知領域のアイコンの背景が黒かった |

0028〜0055 は段階 5 の 0026〜0053 を2つずつ後ろへずらしたもの（内容は 0053 以外同じ）。並びと番号は利用者の判断
（「まとまりに合う位置に入れる」に従い、0018–0027 の末尾に入れた）。

## 10.0 → 11.0（段階 5、2026-10-10）

基にする Wine は 11.0（タグ `wine-11.0`、コミット db11d0fe）。11.0 の保守版（11.0.x）はまだ出ておらず、gitlab の
`stable` ブランチの先頭も 11.0 のリリースのコミットだった。

外した3本（upstream に同じ修正が入った）:

| 10.0 版 | 内容 | 11.0 の該当箇所 |
|---|---|---|
| 0001 | binutils 2.44 での遅延 import | `tools/winebuild/import.c` に同名の `output_import_section()` があり、遅延 import の表を同じく `.data$didat` / `.rdata$didat` に置く。`LdrResolveDelayLoadedAPI` の `id` も `INT_PTR` |
| 0009 | TransparentBlt の縮小 mode | `NtGdiTransparentBlt` が作業 DC に `COLORONCOLOR` を設定する（get_dc_ptr に失敗したときは中止する点だけ違う） |
| 0021 | 題名の変更の WinEvent | `default_window_proc` の WM_SETTEXT が、すべてのアプリで `EVENT_OBJECT_NAMECHANGE` を出す。残すと opt-in 時に二重に通知される |

加えた1本:

| 11.0 版 | 内容 |
|---|---|
| 0016 | 11.0 の wineboot は KnownDLLs と、それらが import する DLL を `\KnownDlls` に登録する。comctl32 も（comdlg32 や shell32 の import として）既知 DLL になり、常に system32 から読まれる。XP Explorer は SxS の activation context なしで動き、exe と同じ `C:\xp` の XP の v6 に頼っているので、XP の v5 を読んで、アイコンの文字の背景、スタートメニューの右列、タスクバーのボタン、フォルダウィンドウのメニューバーが崩れた。opt-in 時は comctl32.dll だけ `\KnownDlls` から取らない（実XPでは SxS のリダイレクトが KnownDlls より先に効く）。判断は利用者（2026-10-10） |

11.0 の構造に合わせて書き直した主なもの（番号は 11.0 版）:

| 11.0 版 | 10.0 版 | 変更 |
|---|---|---|
| 0013 | 0015 | `include/Makefile.in` の一覧の位置（11.0 で `wine/wgl_driver.h` が無くなった）。ntdll 用の判定（`RtlQueryEnvironmentVariable_U`）を加えた |
| 0021 | 0023 | 11.0 は user handle の表を session の共有メモリに置き、型と pid も表にある。サーバーの `alloc_user_handle` 要求の型は upstream が加えたのでパッチから外し、アイコンの画素の写しは表と並ぶ配列に持つ。win32u の `struct user_object` が無くなったので、`release_user_handle_ptr` は他プロセスのアイコンの写しの一覧で判定する。protocol の生成ファイルは `tools/make_requests` で作り直した |
| 0022 | 0024 | `_NET_ACTIVE_WINDOW` の処理（11.0 で追加）と並べた |
| 0023 | 0025 | 11.0 の `window_set_config` は最大化した managed のウィンドウの大きさを関数の先頭で戻す。その条件に opt-in を加えた |
| 0024 | 0026 | 11.0 の `get_window_attributes` は override-redirect を設定しない。背景の pixmap の扱いだけを移した |
| 0025 | 0027 | 影を消す判定を 11.0 の `window_set_config` の比較の後に置いた |
| 0027 | 0029 | `wine/win4wine.h` の include（10.0 版では 0021 が加えていた）を加えた |
| 0029 | 0031 | 11.0 の win32u の DefWindowProc が PRF_CHILDREN で子を印刷するようになった（Z 順の上から、親の clip なし、所有ウィンドウと非クライアント領域はなし）。user32 側の置き換えは引き続き必要。説明を直した |
| 0033 | 0035 | `wine/heap.h` が無くなったので `malloc` を使う |
| 0035 | 0037 | upstream の COLORONCOLOR（旧 0009）の上に重ねた |
| 0038〜0040 | 0040〜0042 | 11.0 で comctl32 が v5（`comctl32.dll`）と v6（`comctl32_v6.dll`）に分かれ、Button、ComboBox、Edit、ListBox、Static は v6 だけにある。変更を `dlls/comctl32_v6/` に移し、`Win4WineRegisterStartButton` と `Win4WineRegisterControl` は `comctl32_v6.spec` から出す。共有の `commctrl.c` と `comctl32.h` の部分は v6 の `#if` の中に置いた。`wine/build.sh` は `win4wine-controls.dll` を `comctl32_v6.dll` から作る |
| 0039 | 0041 | 11.0 の user32 の `init_class_name` は呼び出し側のバッファへ名前を写すようになった。そのまま使うと NULL へ書き込んで XP Explorer のウィンドウ作成が例外で止まった（「ファイル名を指定して実行」が開かない、タスクバーが崩れるなど）ので、`RtlInitUnicodeString` にした |
| 0044 | 0046 | 11.0 の user32 は ReleaseCapture などを win32u へ直接転送するので、`NtUserReleaseCapture`、`NtUserPostQuitMessage`、`NtUserGetWindowContextHelpId` を呼ぶ |

ほかのパッチは 11.0 にそのまま当たった（行の位置だけ変わった）。upstream が近くを変えた箇所（0008 の WM_NCRBUTTONDOWN、
0012 の user32.spec、0037 の user32 の Button など）は、パッチの変更がまだ必要なことを 11.0 のコードで確かめた。

## 対応表（現在の番号 → 段階 7 → 段階 6 → 段階 5 の 11.0 版 → 10.0 版 → 段階 2 より前）

| 現在 | 内容 | DLL | 段階 7 | 段階 6 | 段階 5 | 10.0 版 | 段階 2 より前 |
|---|---|---|---|---|---|---|---|
| 0001 | GetThemeSysColor の色番号の対応 | uxtheme | 0001 | 0001 | 0001 | 0002 | 0014 の metric.c |
| 0002 | GetThemeFont の NULL DC | uxtheme | 0002 | 0002 | 0002 | 0003 | 0015 |
| 0003 | テーマ適用時の system font の高さ | uxtheme | 0003 | 0003 | 0003 | 0004 | 0018 |
| 0004 | 字体属性の区切り | uxtheme | 0004 | 0004 | 0004 | 0005 | 0024 |
| 0005 | 個別に無効にしたスクロールバー矢印 | uxtheme | 0005 | 0005 | 0005 | 0006 | 0020 |
| 0006 | SetWindowTheme の空文字列 | uxtheme | 0006 | 0006 | 0006 | 0007 | 0043 |
| 0007 | 共有テーマ画像の排他（読み込みと転送） | uxtheme | 0007 | 0007 | 0007 | 0008 | 0076（opt-in をやめ、読み込みにも拡張） |
| 0008 | caption の右クリックの離した位置の判定 | win32u | 0008 | 0008 | 0008 | 0010 | 0047 |
| 0009 | SCM 準備完了イベント | services | 0009 | 0009 | 0009 | 0011 | 0005 |
| 0010 | ドッキングステーションなし | setupapi, cfgmgr32 | 0010 | 0010 | 0010 | 0012 | 0006 |
| 0011 | シャットダウン時の更新確認 | wuaueng | 0011 | 0011 | 0011 | 0013 | 0052 |
| 0012 | DestroyReasons | user32 | 0012 | 0012 | 0012 | 0014 | 0053 |
| 0013 | opt-in 判定のヘッダー | include | 0013 | 0013 | 0013 | 0015 | 新規 |
| 0014 | desktop manager がシェルの窓と endpoint を譲る | explorer | 0014 | 0014 | 0014 | 0016 | 0003, 0009 |
| 0015 | explorer.exe のモジュール名 | kernelbase | 0015 | 0015 | 0015 | 0017 | 0007 |
| 0016 | comctl32 を KnownDlls から取らない | ntdll | 0016 | 0016 | 0016 | 新規（段階 5） | — |
| 0017 | XP Explorer に WoW64 を知らせない | wow64 | 0017 | 新規（段階 7） | — | — | — |
| 0018 | セッション終了要求の記録 | user32 | 0018 | 0017 | 0017 | 0018 | 0054 |
| 0019 | X のウィンドウの Win32 識別子 | winex11 | 0019 | 0018 | 0018 | 0019 | 0001 |
| 0020 | シェルフックの登録 | user32 | 0020 | 0019 | 0019 | 0020 | 0002 |
| 0021 | 他プロセスのシステムメニュー | win32u | 0021 | 0020 | 0020 | 0022 | 0010 |
| 0022 | 他プロセスのウィンドウアイコン | server, win32u | 0022 | 0021 | 0021 | 0023 | 0025 |
| 0023 | Ctrl+Esc と Windows キー単独で XP のスタートメニュー（HSHELL_TASKMAN） | win32u | — | — | — | — | — |
| 0024 | 作業領域の追従 | winex11, win32u | 0023 | 0022 | 0022 | 0024 | 0039 |
| 0025 | 最大化の位置と大きさ | winex11 | 0024 | 0023 | 0023 | 0025 | 0041 |
| 0026 | 描かない popup の下の画面を残す | winex11 | 0025 | 0024 | 0024 | 0026 | 0042, 0055 |
| 0027 | XP の影 | winex11 | 0026 | 0025 | 0025 | 0027 | 0072 |
| 0028 | 通知アイコンを XP のタスクバーの形式で送る | shell32 | 0027 | 0026 | 新規（段階 6） | — | — |
| 0029 | XIC を作る前の変換位置 | winex11 | 0028 | 0027 | 新規（段階 6） | — | — |
| 0030 | CW_USEDEFAULT の重ね配置 | win32u | 0029 | 0028 | 0026 | 0028 | 0049 |
| 0031 | キーボードの UI 状態 | win32u | 0030 | 0029 | 0027 | 0029 | 0050 |
| 0032 | XP の popup メニュー | win32u | 0031 | 0030 | 0028 | 0030 | 0071 |
| 0033 | WM_PRINT の子・所有ウィンドウと clip | user32, win32u | 0032 | 0031 | 0029 | 0031 | 0012, 0017 |
| 0034 | ReBar の既定の配置 | user32 | 0033 | 0032 | 0030 | 0032 | 0011 |
| 0035 | 拡大表示の壁紙 | user32 | 0034 | 0033 | 0031 | 0033 | 0056 |
| 0036 | monochrome 文字の送り幅 | win32u | 0035 | 0034 | 0032 | 0034 | 0030 |
| 0037 | DrawText の縦中央、日本語の折り返し、アクセスキーの下線 | user32 | 0036 | 0035 | 0033 | 0035 | 0031, 0066 |
| 0038 | HALFTONE の部分描画 | win32u | 0037 | 0036 | 0034 | 0036 | 0036 |
| 0039 | TransparentBlt の拡大の画素対応 | win32u | 0038 | 0037 | 0035 | 0037 | 0064 |
| 0040 | クラシックの caption 記号 | user32 | 0039 | 0038 | 0036 | 0038 | 0048 |
| 0041 | アイコンの乗算済みアルファの丸め | user32 | — | — | — | — | — |
| 0042 | 日本語のコンソールの2升の文字と IME の確定文字 | conhost | — | — | — | — | — |
| 0043 | 描く前の窓に見える画面 | win32u | — | — | — | — | — |
| 0044 | 外部シェルの DPI（system DPI aware、XP のアイコンとカーソルの寸法） | win32u | — | — | — | — | — |
| 0045 | クラシックの押しボタンの理想寸法 | user32 | 0040 | 0039 | 0037 | 0039 | 0008 |
| 0046 | Start ボタン | user32, comctl32_v6 | 0041 | 0040 | 0038 | 0040 | 0013, 0033, 0037, 0044 |
| 0047 | テーマ付きコントロール（アドレスバーを含む） | user32, comctl32_v6 | 0042 | 0041 | 0039 | 0041 | 0060, 0068 |
| 0048 | チェックボックスとグループボックスの文字位置 | comctl32_v6 | 0043 | 0042 | 0040 | 0042 | 0069 |
| 0049 | Luna の枠（caption、ボタン、アイコン、題名の影） | uxtheme, user32, win32u | 0044 | 0043 | 0041 | 0043 | 0014（metric.c 以外）, 0022, 0027, 0028, 0032, 0034, 0062, 0065 |
| 0050 | caption の角の領域 | uxtheme | 0045 | 0044 | 0042 | 0044 | 0023, 0051 |
| 0051 | 枠の部分的な描き直し | uxtheme, user32 | 0046 | 0045 | 0043 | 0045 | 0074, 0075 |
| 0052 | caption のヘルプボタン | uxtheme, user32 | 0047 | 0046 | 0044 | 0046 | 0067 |
| 0053 | テーマ画像の拡大縮小と glyph の配置 | uxtheme | 0048 | 0047 | 0045 | 0047 | 0046, 0061, 0063 |
| 0054 | 乗算済みアルファの丸め | uxtheme | 0049 | 0048 | 0046 | 0048 | 0070 |
| 0055 | SetWindowTheme の変更番号 | uxtheme | 0050 | 0049 | 0047 | 0049 | 0059 |
| 0056 | 日本語 Luna の字体 profile | uxtheme | 0051 | 0050 | 0048 | 0050 | 0026, 0035, 0040, 0057 の font-profile.c |
| 0057 | localized msstyles の文字列テーブル | uxtheme | 0052 | 0051 | 0049 | 0051 | 0058 |
| 0058 | Start のユーザー名の連続影 | uxtheme | 0053 | 0052 | 0050 | 0052 | 0057 の draw.c |
| 0059 | タスクバーと通知領域の子の背景 | uxtheme | 0054 | 0053 | 0051 | 0053 | 0016 |
| 0060 | 時計 | uxtheme | 0055 | 0054 | 0052 | 0054 | 0019 |
| 0061 | タスクバーの ReBar のグリップと末尾 | uxtheme | 0056 | 0055 | 0053 | 0055 | 0021, 0029, 0038 |

## 対応表（段階 2 より前 → 10.0 版）

| 旧 | 新 | 旧 | 新 | 旧 | 新 | 旧 | 新 |
|---|---|---|---|---|---|---|---|
| 0001 | 0019 | 0020 | 0006 | 0039 | 0024 | 0058 | 0051 |
| 0002 | 0020 | 0021 | 0055 | 0040 | 0050 | 0059 | 0049 |
| 0003 | 0016 | 0022 | 0043 | 0041 | 0025 | 0060 | 0041 |
| 0004 | 0001 | 0023 | 0044 | 0042 | 0026 | 0061 | 0047 |
| 0005 | 0011 | 0024 | 0005 | 0043 | 0007 | 0062 | 0043 |
| 0006 | 0012 | 0025 | 0023 | 0044 | 0040 | 0063 | 0047 |
| 0007 | 0017 | 0026 | 0050 | 0045 | 0009 | 0064 | 0037 |
| 0008 | 0039 | 0027 | 0043 | 0046 | 0047 | 0065 | 0043 |
| 0009 | 0016 | 0028 | 0043 | 0047 | 0010 | 0066 | 0035 |
| 0010 | 0022 | 0029 | 0055 | 0048 | 0038 | 0067 | 0046 |
| 0011 | 0032 | 0030 | 0034 | 0049 | 0028 | 0068 | 0041 |
| 0012 | 0031 | 0031 | 0035 | 0050 | 0029 | 0069 | 0042 |
| 0013 | 0040 | 0032 | 0043 | 0051 | 0044 | 0070 | 0048 |
| 0014 | 0043（metric.c は 0002） | 0033 | 0040 | 0052 | 0013 | 0071 | 0030 |
| 0015 | 0003 | 0034 | 0043 | 0053 | 0014 | 0072 | 0027 |
| 0016 | 0053 | 0035 | 0050 | 0054 | 0018 | 0073 | 0021 |
| 0017 | 0031 | 0036 | 0036 | 0055 | 0026 | 0074 | 0045 |
| 0018 | 0004 | 0037 | 0040 | 0056 | 0033 | 0075 | 0045 |
| 0019 | 0054 | 0038 | 0055 | 0057 | 0050, 0052 | 0076 | 0008 |

## 段階 3・4 での変更（2026-10-10、番号は 10.0 版）

| 番号 | 変更 |
|---|---|
| 0015 | `include/Makefile.in` の一覧に `wine/win4wine.h` を加えた。無いと未改変のソースからの configure が失敗する |
| 0035 | アクセスキーの下線を XP と同じ長さ（文字の送り幅いっぱい）にした（`tests/text-underline.c`） |

デスクトップの右クリックメニューの区切り線（段階 3）はパッチではなく registry profile 23（`prefix/registry.py`）で直した。
- 2026-10-16 の polish ブランチで、全パッチを見直して重複を除いた。番号と本数は変えていない。各パッチにあった `WIN4WINE_SHELL` の読み取り（約 20 か所、win7 の判定に外部シェルの確認の有無や大文字小文字の扱いの違いがあった）を 0013 の `win4wine_shell()` にまとめ、Windows 7 と XP のフォーカス枠（0109・0123）、メッセージ ボックスの配置の共通部分（0112・0123）、通知領域のアイコンの送り方（0028・0103）、HALFTONE の標本の計算（0043）、DefWindowProcA/W の外部シェルの後処理（0055・0057・0058）をそれぞれ一つにした。ほかに、使っていない引数や別名の関数、ずれていた関数の説明のコメントを直し、0084 のコンパイルの警告を消した。動作は変えていない（XP・ReactOS のシーンと XP の実機との比較で確認）。
