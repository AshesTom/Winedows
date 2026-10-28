# Winedows

**Your Linux desktop, with the real Windows shell**: Windows' own `explorer.exe` on a patched Wine as the desktop
shell of Linux, with Linux applications in its taskbar and frames. No Microsoft files are included; the few that are
needed come from your own ISO.

Each Windows has its own branch:

| Branch | What it is |
|---|---|
| [**`WinXP`**](../../tree/WinXP) | **Windows XP is XP**: XP's shell (Luna or Classic), about 0.5 GB of RAM for the whole session, the ReactOS shell when you have no XP ISO, and XP Explorer (XP in a window of your current desktop) |
| [**`Win7`**](../../tree/Win7) | **Windows 7 is 7**: Windows 7's shell, its logon screen, cursors and file dialogs (Aero Glass in progress) |

Pick a branch and follow its README:

```sh
git clone -b WinXP https://github.com/AshesTom/Winedows.git ~/winedows   # or -b Win7
```

`master` holds nothing but this page.

Winedows started as a fork of [mikuta0407/win4wine](https://github.com/mikuta0407/win4wine).
Experimental; tested on a freshly installed Debian 13. Unofficial, not affiliated with Microsoft or the Wine project.
Licensed under [LGPL-2.1-or-later](LICENSE), like Wine.

---

## 日本語

Windows 本物の `explorer.exe` を、パッチを当てた Wine の上で Linux のデスクトップシェルとして動かすプロジェクトです。
Microsoft のファイルは同梱せず、お手元の ISO から取り出して使います。Windows ごとにブランチが分かれています。

- [**`WinXP`**](../../tree/WinXP): **XP は XP**。XP のシェル（セッション全体で約 0.5 GB のメモリ）、XP の ISO が無いときの ReactOS のシェル、XP Explorer。
- [**`Win7`**](../../tree/Win7): **7 は 7**。Windows 7 のシェル、ログオン画面、カーソル、ファイルのダイアログ（Aero のガラスは作業中）。

`master` にはこのページしかありません。使い方は各ブランチの README を見てください。
