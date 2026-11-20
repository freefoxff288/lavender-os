# qutebrowser config
config.load_autoconfig(False)

# Noctalia colors (regenerated on palette change)
config.source("noctalia/colors.py")

# --- look ---
c.fonts.default_family = ["Noto Sans", "Symbols Nerd Font"]
c.fonts.default_size = "11pt"
c.tabs.position = "top"
c.tabs.padding = {"top": 5, "bottom": 5, "left": 8, "right": 8}
c.tabs.indicator.width = 0
c.tabs.favicons.scale = 1.0
c.tabs.last_close = "close"
c.statusbar.padding = {"top": 4, "bottom": 4, "left": 6, "right": 6}
c.colors.webpage.preferred_color_scheme = "dark"   # sites with native dark mode
c.colors.webpage.darkmode.enabled = True           # force dark on the rest
c.colors.webpage.darkmode.policy.images = "never"
c.colors.webpage.bg = "#18161d"

# --- behaviour ---
c.content.blocking.enabled = True
c.content.blocking.method = "both"                 # Brave adblock + hosts
c.content.blocking.adblock.lists = [
    "https://easylist.to/easylist/easylist.txt",
    "https://easylist.to/easylist/easyprivacy.txt",
    "https://secure.fanboy.co.nz/fanboy-annoyance.txt",
]
c.content.autoplay = False
c.content.notifications.enabled = "ask"
c.auto_save.session = True                         # restore tabs on start
c.url.default_page = "about:blank"
c.url.start_pages = ["about:blank"]
c.url.searchengines = {
    "DEFAULT": "https://www.google.com/search?q={}",
    "ddg": "https://duckduckgo.com/?q={}",
    "yt": "https://www.youtube.com/results?search_query={}",
    "gh": "https://github.com/search?q={}",
    "w": "https://ru.wikipedia.org/w/index.php?search={}",
}
c.downloads.location.directory = "~/Downloads"
c.downloads.position = "bottom"
c.input.insert_mode.auto_load = True
c.qt.args = ["enable-gpu-rasterization", "ignore-gpu-blocklist"]

# --- keys ---
config.bind("<Ctrl-t>", "cmd-set-text -s :open -t")
config.bind("<Ctrl-w>", "tab-close")
config.bind("<Ctrl-Shift-t>", "undo")
config.bind("<Ctrl-l>", "cmd-set-text :open {url:pretty}")
config.bind("<Ctrl-r>", "reload")
config.bind(",d", "config-cycle colors.webpage.darkmode.enabled true false ;; reload")
config.bind(",m", "spawn mpv {url}")
