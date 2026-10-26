# bash for XP's Command Prompt (session/xp_cmd.py): the user's own settings,
# then XP's banner, its prompt and cmd's names for the common commands.
[[ -f ~/.bashrc ]] && source ~/.bashrc

# The folder as XP names it: H:\ is the home, C:\ the prefix's drive, Z:\ the
# root of the Linux system (Wine's drives of the session).
__xp_folder() {
    local path=$PWD drive=Z: c=${XP_CMD_PREFIX:+$XP_CMD_PREFIX/drive_c}
    if [[ -n $c && ( $path == "$c" || $path == "$c"/* ) ]]; then
        drive=C: path=${path#"$c"}
    elif [[ $path == "$HOME" || $path == "$HOME"/* ]]; then
        drive=H: path=${path#"$HOME"}
    fi
    path=${path//\//\\}
    printf '%s%s' "$drive" "${path:-\\}"
}

# A folder named the XP way (H:\Downloads, Z:\etc, C:\windows) to its Linux path.
__xp_path() {
    local name=$1 rest
    case $name in
        [A-Za-z]:*)
            rest=${name:2}
            rest=${rest//\\//}
            case ${name:0:1} in
                [Hh]) name=$HOME/${rest#/} ;;
                [Zz]) name=/${rest#/} ;;
                [Cc]) name=${XP_CMD_PREFIX:-$HOME/.wine}/drive_c/${rest#/} ;;
            esac ;;
    esac
    printf '%s' "$name"
}

cd() {
    local here
    if [[ $1 == '\' ]]; then
        here=$(__xp_folder)
        builtin cd "$(__xp_path "${here:0:2}\\")"
    elif (($#)) && [[ $1 != -* ]]; then
        builtin cd "$(__xp_path "$1")" "${@:2}"
    else
        builtin cd "$@"
    fi
}
alias cd..='cd ..'
alias cls=clear dir='ls -l' type=cat copy='cp -i' move='mv -i' del='rm -i' erase='rm -i' ren='mv -i' \
    md=mkdir rd=rmdir
ver() { printf '\nMicrosoft Windows XP [Version 5.1.2600]\n'; }

# cmd.exe prints an empty line after a command's output, then its prompt.
PROMPT_COMMAND=
# The window keeps its title (the shortcut's name, as XP's console does).
PS1='\[\e]0;${XP_CMD_TITLE:-Command Prompt}\a\]\n$(__xp_folder)>'
PS2='More? '
printf 'Microsoft Windows XP [Version 5.1.2600]\n(C) Copyright 1985-2001 Microsoft Corp.\n'
