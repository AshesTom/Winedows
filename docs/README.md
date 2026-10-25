# ドキュメント

| 文書 | 内容 |
|---|---|
| [architecture.md](architecture.md) | 仕組み。部品の役割、WM と proxy と IPC、Linux の窓の XP の枠、トレイ、セッション、ログイン画面、DPI |
| [design.md](design.md) | 設計の要点。どの部品を XP のものにしたか、守っていることとその理由 |
| [patches.md](patches.md) | Wine の 181 本のパッチの一覧、opt-in の判定、並び順、書式、足し方 |
| [development.md](development.md) | 開発の手順。chroot でのビルド、試験、手作業での導入、既存の環境の更新 |
| [reactos.md](reactos.md) | ReactOS のシェル。XP の ISO の代わりに ReactOS の explorer を使うセッション、取得とライセンス、prefix、制限 |
| [wayland.md](wayland.md) | Wayland のセッション（試験中）。パッチを当てた labwc、モニターごとの倍率、X11 のセッションとの違い |
| [original-features.md](original-features.md) | XP には無いオリジナル機能（既定はオフ）。窓の寄せ（`window_snap`） |

使い方と導入は、リポジトリの [README](../README.md) と [INSTALL.md](../INSTALL.md) にあります。

## 記録（history/）

開発の途中で書いた記録です。書いた時点の内容と文体のままにしてあります。今の説明は上の文書にあります。

| 文書 | 内容 |
|---|---|
| [history/investigations.md](history/investigations.md) | 調査と判断の記録（行った順）。不具合の原因、実際の XP で測った値、採らなかった方法 |
| [history/patch-history.md](history/patch-history.md) | パッチの追加と番号の振り直しの経緯、以前の番号との対応表 |
| [history/wayland-study.md](history/wayland-study.md) | Wayland の compositor の比較と、最初の試作の結果 |

記録に出てくる「段階 N」は、開発の作業の区切り（2026-10 の段階 1〜11）です。
