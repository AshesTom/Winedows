# Winedows

A Windows XP desktop for Linux, written in C++ with Qt 6.

The Wine-based version (branches `WinXP` and `Win7`) is discontinued and will not be updated.
Development continues on the native version, in the [`nativeshell`](../../tree/nativeshell) branch.

## Installation

Requirements: a Windows XP ISO. No Microsoft files are distributed with this project.

```sh
git clone -b v1.0.0 https://github.com/AshesTom/Winedows.git
cd Winedows
./install.sh --media /path/to/windows-xp.iso
```

See the [`nativeshell` README](../../tree/nativeshell#readme) for dependencies and usage.

## License

LGPL-2.1-or-later. Originally forked from [mikuta0407/win4wine](https://github.com/mikuta0407/win4wine).
Not affiliated with Microsoft.

---

## 日本語

Linux 用の Windows XP デスクトップです（C++ / Qt 6）。

Wine 版（`WinXP`、`Win7` ブランチ）の開発は終了しました。今後は [`nativeshell`](../../tree/nativeshell) ブランチのネイティブ版のみを開発します。
インストールには Windows XP の ISO が必要です。
