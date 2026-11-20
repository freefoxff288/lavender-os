# ~/.zshrc — void theme (ported from .bashrc)

export PATH="$HOME/.local/bin:$PATH"
export EDITOR=nvim VISUAL=nvim MANPAGER="nvim +Man!"

# --- history ---
HISTFILE=~/.zsh_history
HISTSIZE=20000
SAVEHIST=20000
setopt SHARE_HISTORY HIST_IGNORE_DUPS HIST_IGNORE_SPACE HIST_REDUCE_BLANKS
setopt AUTO_CD INTERACTIVE_COMMENTS PROMPT_SUBST

# --- completion: menu with descriptions and colors ---
fpath=(/usr/share/zsh/site-functions $fpath)
autoload -Uz compinit && compinit -d ~/.cache/zcompdump
zmodload zsh/complist
zstyle ':completion:*' menu select
zstyle ':completion:*' matcher-list 'm:{a-z}={A-Z}'           # case-insensitive
zstyle ':completion:*' list-colors "${(s.:.)LS_COLORS}"
zstyle ':completion:*' group-name ''
zstyle ':completion:*:descriptions' format '%F{green}── %d ──%f'
zstyle ':completion:*:warnings' format '%F{red}нет совпадений%f'
zstyle ':completion:*' verbose yes

# --- keys ---
bindkey -e
bindkey '^[[A' history-search-backward     # ↑ searches by typed prefix
bindkey '^[[B' history-search-forward
bindkey '^[[1;5C' forward-word             # Ctrl+→
bindkey '^[[1;5D' backward-word            # Ctrl+←
bindkey '^[[H' beginning-of-line
bindkey '^[[F' end-of-line
bindkey '^[[3~' delete-char

# --- prompt: минималистичный, без внешних программ ---
#   ~/Projects/kite  main ● ↑1                       4s · ✗ 1
#   ❯
# путь (последние 3 папки) · git: ветка, ● — есть изменения, ↑↓ — расхождение с remote
# справа: время долгой команды (≥3 с) и код ошибки · user@host — только по ssh/root
# выполненные команды сворачиваются в «❯ команда» (transient prompt)
zmodload zsh/datetime
__git_info() {
  local b; b=$(git symbolic-ref --short HEAD 2>/dev/null || git rev-parse --short HEAD 2>/dev/null) || return
  local s="%F{green}  $b"
  [[ -n $(git status --porcelain --ignore-submodules=dirty 2>/dev/null | head -1) ]] && s+=" %F{yellow}●"
  local ab; ab=$(git rev-list --left-right --count @{upstream}...HEAD 2>/dev/null)
  if [[ -n $ab ]]; then
    local behind=${ab%%[[:space:]]*} ahead=${ab##*[[:space:]]}
    (( ahead ))  && s+=" %F{cyan}↑$ahead"
    (( behind )) && s+=" %F{cyan}↓$behind"
  fi
  print -n "$s"
}
__cmd_start=0
__prompt_first=1
preexec() { __cmd_start=$EPOCHREALTIME }
precmd() {
  local st=$? r=""
  if (( __cmd_start )); then
    local d=$(( EPOCHREALTIME - __cmd_start ))
    if (( d >= 3 )); then
      if (( d >= 60 )); then r="%F{8}$(( ${d%.*} / 60 ))m$(( ${d%.*} % 60 ))s"; else r="%F{8}${d%.*}s"; fi
    fi
  fi
  (( st && __cmd_start )) && r+="${r:+ %F{8}· }%F{red}✗ $st"
  __cmd_start=0
  local who=""
  [[ -n $SSH_CONNECTION || $EUID == 0 ]] && who="%F{yellow}%n%F{8}@%m "
  local venv=""
  [[ -n $VIRTUAL_ENV ]] && venv="%F{8}(${VIRTUAL_ENV:t}) "
  (( __prompt_first )) && __prompt_first=0 || print ""
  PROMPT="${who}${venv}%F{cyan}%(4~|…/%3~|%~)$(__git_info)
%(?.%F{green}.%F{red})❯%f "
  RPROMPT="${r}%f"
}
# transient prompt: перед выполнением строка сжимается до «❯ команда»
__transient_init() {
  emulate -L zsh
  [[ $CONTEXT == start ]] || return 0
  while true; do
    zle .recursive-edit
    local -i ret=$?
    [[ $ret == 0 && $KEYS == $'\4' ]] || break
    [[ -o ignore_eof ]] || exit 0
  done
  local p=$PROMPT rp=$RPROMPT
  PROMPT='%F{8}❯%f ' RPROMPT=''
  zle .reset-prompt
  PROMPT=$p RPROMPT=$rp
  if (( ret )); then zle .send-break; else zle .accept-line; fi
  return ret
}
zle -N zle-line-init __transient_init
ZLE_RPROMPT_INDENT=0

# --- aliases ---
alias ls='eza --icons --group-directories-first'
alias ll='eza -l --icons --git --group-directories-first'
alias la='eza -la --icons --git --group-directories-first'
alias tree='eza --tree --icons'
alias cat='bat --paging=never --style=plain'
alias please='sudo'
alias update='sudo xbps-install -Su'
alias install='sudo xbps-install -S'
alias yeet='sudo xbps-remove -R'
alias search='xbps-query -Rs'
alias cls='clear && __greet'
alias ..='cd ..'
alias joke='devjoke'
alias weather='curl -s wttr.in/?0'

# --- tools ---
command -v zoxide >/dev/null && eval "$(zoxide init zsh)"          # z <part of path>
[ -f /usr/share/fzf/key-bindings.zsh ] && source /usr/share/fzf/key-bindings.zsh   # Ctrl+R, Ctrl+T, Alt+C
[ -f /usr/share/fzf/completion.zsh ] && source /usr/share/fzf/completion.zsh
export FZF_DEFAULT_OPTS='--height 40% --layout=reverse --border --color=hl:#bba7e3,hl+:#bba7e3,pointer:#bba7e3,prompt:#bba7e3,info:#a7cdb6,border:#3a3545'
export BAT_THEME=ansi

# --- greeting: once per terminal window ---
__greet() {
  # картинка-логотип (sixel) — только в foot и если окно не уже 78 колонок, иначе маленький текстовый логотип
  if [[ $TERM == foot* && $COLUMNS -ge 78 ]]; then fastfetch; else fastfetch --logo void_small --logo-type builtin; fi
}
[ -z "$FASTFETCH_SHOWN" ] && [ -n "$WAYLAND_DISPLAY" ] && export FASTFETCH_SHOWN=1 && __greet

# --- заставка: через 5 минут простоя в приглашении растёт бонсай; любая клавиша — назад ---
if [[ -z $SSH_TTY ]] && (( $+commands[cbonsai] )); then
  TMOUT=300
  TRAPALRM() { cbonsai -S -L 40 -w 6; zle && zle reset-prompt; }
fi

# --- suggestions (grey, accept with →) and live syntax colors; must be last ---
ZSH_AUTOSUGGEST_HIGHLIGHT_STYLE='fg=8'
ZSH_AUTOSUGGEST_STRATEGY=(history completion)
source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh 2>/dev/null
source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh 2>/dev/null
typeset -gA ZSH_HIGHLIGHT_STYLES
ZSH_HIGHLIGHT_STYLES[command]='fg=magenta,bold'
ZSH_HIGHLIGHT_STYLES[builtin]='fg=magenta,bold'
ZSH_HIGHLIGHT_STYLES[alias]='fg=magenta,bold'
ZSH_HIGHLIGHT_STYLES[function]='fg=magenta,bold'
ZSH_HIGHLIGHT_STYLES[unknown-token]='fg=red,bold'
ZSH_HIGHLIGHT_STYLES[path]='fg=cyan,underline'
ZSH_HIGHLIGHT_STYLES[single-quoted-argument]='fg=yellow'
ZSH_HIGHLIGHT_STYLES[double-quoted-argument]='fg=yellow'
ZSH_HIGHLIGHT_STYLES[precommand]='fg=green,bold'
ZSH_HIGHLIGHT_STYLES[comment]='fg=8'
