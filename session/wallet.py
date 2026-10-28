#!/usr/bin/env python3
"""The user's password wallet (KWallet or GNOME Keyring) in the session.

Chromium and the Electron applications (browsers, chat clients, editors)
keep the key that encrypts their cookies and saved passwords in the wallet
that they pick from the desktop's name (XDG_CURRENT_DESKTOP): KWallet under
KDE, the Secret Service (GNOME Keyring) under GNOME and the other GTK
desktops, and a fixed key of their own ("basic") under a desktop they do not
know, such as XP-Shell. Without the key the cookies of the user's own
desktop cannot be read, so every site was signed out, and those written in
the XP session could not be read back in the user's own desktop.

The session therefore names, after XP-Shell, the desktop whose wallet the
user has, and unlocks that wallet as the user's desktop would: PAM
(pam_kwallet5, pam_gnome_keyring) keeps the login password for it.

session.json "wallet": "auto" (the default), "kwallet", "gnome-keyring" or
"none".

  wallet.py           prints the wallet found and the variables it sets
"""
import os
from pathlib import Path
import shutil
import subprocess

# pam_kwallet_init passes the password kept by pam_kwallet5 to kwalletd, as
# Plasma's plasma-kwallet-pam.service does.
PAM_KWALLET_INIT = ('/usr/lib/pam_kwallet_init', '/usr/libexec/pam_kwallet_init',
                    '/usr/lib/x86_64-linux-gnu/libexec/pam_kwallet_init',
                    '/usr/lib/aarch64-linux-gnu/libexec/pam_kwallet_init')


def data_home():
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')


def kwallet_version():
    """6 or 5 for the installed kwalletd, or None."""
    for version in (6, 5):
        if shutil.which(f'kwalletd{version}'):
            return version
    return None


def detect(choice='auto'):
    """'kwallet', 'gnome-keyring' or None: the wallet the user has.

    KWallet when the user has a wallet file of it (a KDE desktop made one),
    otherwise GNOME Keyring when it is installed."""
    if choice == 'none':
        return None
    if choice in ('kwallet', 'gnome-keyring'):
        return choice
    if kwallet_version() and any((data_home() / 'kwalletd').glob('*.kwl')):
        return 'kwallet'
    if shutil.which('gnome-keyring-daemon'):
        return 'gnome-keyring'
    return None


def environment(wallet, env):
    """The variables that make Chromium pick WALLET, given the session's ENV."""
    desktops = [d for d in env.get('XDG_CURRENT_DESKTOP', '').split(':') if d]
    if wallet == 'kwallet':
        name, extra = 'KDE', {'KDE_SESSION_VERSION': str(kwallet_version() or 6)}
    elif wallet == 'gnome-keyring':
        name, extra = 'GNOME', {}
    else:
        return {}
    if name in desktops:
        return {}
    session = ':'.join(desktops)
    desktops.append(name)
    # What the session shows and starts (the Start menu's OnlyShowIn,
    # session/linux_apps.py) follows its own name only.
    extra.update(XDG_CURRENT_DESKTOP=':'.join(desktops), WIN4WINE_DESKTOP=session)
    # Qt would take KDE's or GTK's platform theme from the added name (Breeze,
    # the desktop's fonts); it keeps the one it has under XP-Shell.
    if not env.get('QT_QPA_PLATFORMTHEME'):
        extra['QT_QPA_PLATFORMTHEME'] = 'generic'
    return extra


def unlock(wallet, env, log=subprocess.DEVNULL):
    """Unlock WALLET with the password PAM kept at the login (best effort)."""
    if wallet == 'kwallet':
        command = next((path for path in PAM_KWALLET_INIT if os.access(path, os.X_OK)), None)
        if command:
            # It ends at once when PAM kept nothing; kwalletd then asks for the password.
            subprocess.Popen([command], env=env, stdout=log, stderr=log, start_new_session=True)
    elif wallet == 'gnome-keyring':
        # Joins the daemon that pam_gnome_keyring started (or starts one) with the Secret Service.
        subprocess.run(['gnome-keyring-daemon', '--start', '--components=secrets'], env=env,
                       stdout=subprocess.DEVNULL, stderr=log, timeout=15, check=False)


if __name__ == '__main__':
    found = detect()
    print('wallet:', found or 'none')
    for key, value in environment(found, dict(os.environ, XDG_CURRENT_DESKTOP='XP-Shell')).items():
        print(f'{key}={value}')
