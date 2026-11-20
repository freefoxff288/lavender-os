-- Лаванда — тема в палитре рабочего стола (bg #18161d, акцент #bba7e3)
vim.cmd("hi clear")
if vim.fn.exists("syntax_on") == 1 then vim.cmd("syntax reset") end
vim.o.background = "dark"
vim.g.colors_name = "lavender"

local p = {
  bg = "#18161d", deep = "#141218", surf = "#211e28", line = "#2a2633", sel = "#3a3545",
  muted = "#6e6880", sub = "#b9b2c6", fg = "#e6e1ef", lav = "#bba7e3", lav2 = "#cdbdf0",
  rose = "#e3a7b5", amber = "#e3cfa6", sage = "#a7cdb6", blue = "#a9b8e8", cyan = "#9fd0cf",
  pink = "#d9b8dc",
}
package.loaded["lavender.palette"] = p

local function hi(g, o)
  if type(o) == "string" then return vim.api.nvim_set_hl(0, g, { link = o }) end
  local s = {}
  for k, v in pairs(o) do s[k] = (k == "fg" or k == "bg" or k == "sp") and p[v] or v end
  vim.api.nvim_set_hl(0, g, s)
end

local groups = {
  Normal = { fg = "fg", bg = "bg" }, NormalNC = { fg = "fg", bg = "bg" },
  NormalFloat = { fg = "fg", bg = "surf" }, FloatBorder = { fg = "lav", bg = "surf" },
  FloatTitle = { fg = "lav2", bg = "surf", bold = true }, FloatFooter = { fg = "muted", bg = "surf" },
  EndOfBuffer = { fg = "bg" }, LineNr = { fg = "muted" }, CursorLineNr = { fg = "lav", bold = true },
  CursorLine = { bg = "surf" }, CursorColumn = { bg = "surf" }, ColorColumn = { bg = "surf" },
  SignColumn = { fg = "muted" }, FoldColumn = { fg = "muted" }, Folded = { fg = "sub", bg = "surf" },
  WinSeparator = { fg = "sel" }, VertSplit = "WinSeparator",
  StatusLine = { fg = "sub", bg = "surf" }, StatusLineNC = { fg = "muted", bg = "surf" },
  TabLine = { fg = "muted", bg = "surf" }, TabLineFill = { bg = "deep" },
  TabLineSel = { fg = "bg", bg = "lav", bold = true }, WinBar = { fg = "sub" }, WinBarNC = { fg = "muted" },
  Visual = { bg = "sel" }, Search = { fg = "bg", bg = "amber" }, IncSearch = { fg = "bg", bg = "lav2" },
  CurSearch = "IncSearch", Substitute = { fg = "bg", bg = "rose" },
  MatchParen = { fg = "lav2", bg = "sel", bold = true },
  Pmenu = { fg = "fg", bg = "surf" }, PmenuSel = { fg = "bg", bg = "lav", bold = true },
  PmenuSbar = { bg = "surf" }, PmenuThumb = { bg = "sel" }, PmenuKind = { fg = "cyan", bg = "surf" },
  PmenuExtra = { fg = "muted", bg = "surf" }, WildMenu = "PmenuSel",
  NonText = { fg = "line" }, SpecialKey = { fg = "line" }, Whitespace = { fg = "line" },
  Directory = { fg = "lav" }, Title = { fg = "lav2", bold = true },
  ErrorMsg = { fg = "rose", bold = true }, WarningMsg = { fg = "amber" },
  ModeMsg = { fg = "sage" }, MoreMsg = { fg = "sage" }, Question = { fg = "sage" },
  QuickFixLine = { bg = "sel" }, Conceal = { fg = "muted" }, Cursor = { fg = "bg", bg = "lav" },
  DiffAdd = { bg = "#232b27" }, DiffChange = { bg = "#2b2822" }, DiffDelete = { fg = "rose", bg = "#2d2226" },
  DiffText = { bg = "#3d3729" }, Added = { fg = "sage" }, Changed = { fg = "amber" }, Removed = { fg = "rose" },
  SpellBad = { sp = "rose", undercurl = true }, SpellCap = { sp = "amber", undercurl = true },
  SpellRare = { sp = "pink", undercurl = true }, SpellLocal = { sp = "cyan", undercurl = true },

  Comment = { fg = "muted", italic = true }, Constant = { fg = "pink" }, String = { fg = "sage" },
  Character = { fg = "sage" }, Number = { fg = "amber" }, Boolean = { fg = "amber" }, Float = { fg = "amber" },
  Identifier = { fg = "fg" }, Function = { fg = "blue" }, Statement = { fg = "lav" },
  Keyword = { fg = "lav" }, Conditional = { fg = "lav" }, Repeat = { fg = "lav" },
  Operator = { fg = "sub" }, Exception = { fg = "rose" }, PreProc = { fg = "cyan" },
  Include = { fg = "lav2" }, Type = { fg = "cyan" }, StorageClass = { fg = "lav" },
  Structure = { fg = "cyan" }, Special = { fg = "pink" }, Delimiter = { fg = "sub" },
  Underlined = { fg = "blue", underline = true }, Error = { fg = "rose", bold = true },
  Todo = { fg = "bg", bg = "lav", bold = true },

  ["@variable"] = { fg = "fg" }, ["@variable.builtin"] = { fg = "pink", italic = true },
  ["@variable.parameter"] = { fg = "sub", italic = true }, ["@variable.member"] = { fg = "sub" },
  ["@property"] = { fg = "sub" }, ["@constant"] = { fg = "pink" }, ["@constant.builtin"] = { fg = "amber" },
  ["@module"] = { fg = "lav2" }, ["@label"] = { fg = "pink" },
  ["@string"] = { fg = "sage" }, ["@string.escape"] = { fg = "pink" }, ["@string.regexp"] = { fg = "pink" },
  ["@string.special"] = { fg = "cyan" }, ["@character"] = { fg = "sage" },
  ["@number"] = { fg = "amber" }, ["@boolean"] = { fg = "amber" },
  ["@type"] = { fg = "cyan" }, ["@type.builtin"] = { fg = "cyan", italic = true },
  ["@function"] = { fg = "blue" }, ["@function.builtin"] = { fg = "cyan" },
  ["@function.method"] = { fg = "blue" }, ["@constructor"] = { fg = "cyan" },
  ["@keyword"] = { fg = "lav" }, ["@keyword.return"] = { fg = "lav", italic = true },
  ["@keyword.function"] = { fg = "lav" }, ["@keyword.import"] = { fg = "lav2" },
  ["@keyword.exception"] = { fg = "rose" }, ["@operator"] = { fg = "sub" },
  ["@punctuation"] = { fg = "sub" }, ["@punctuation.bracket"] = { fg = "sub" },
  ["@punctuation.special"] = { fg = "pink" }, ["@comment"] = "Comment",
  ["@comment.todo"] = "Todo", ["@comment.error"] = { fg = "bg", bg = "rose" },
  ["@comment.warning"] = { fg = "bg", bg = "amber" }, ["@comment.note"] = { fg = "bg", bg = "blue" },
  ["@tag"] = { fg = "lav" }, ["@tag.attribute"] = { fg = "blue", italic = true },
  ["@tag.delimiter"] = { fg = "muted" }, ["@markup.heading"] = { fg = "lav2", bold = true },
  ["@markup.strong"] = { bold = true }, ["@markup.italic"] = { italic = true },
  ["@markup.link"] = { fg = "blue" }, ["@markup.link.url"] = { fg = "blue", underline = true },
  ["@markup.raw"] = { fg = "sage" }, ["@markup.list"] = { fg = "lav" }, ["@markup.quote"] = { fg = "sub", italic = true },
  ["@diff.plus"] = { fg = "sage" }, ["@diff.minus"] = { fg = "rose" }, ["@diff.delta"] = { fg = "amber" },
  ["@lsp.type.parameter"] = "@variable.parameter", ["@lsp.type.property"] = "@property",
  ["@lsp.type.namespace"] = "@module", ["@lsp.mod.deprecated"] = { strikethrough = true },
  LspReferenceText = { bg = "line" }, LspReferenceRead = { bg = "line" }, LspReferenceWrite = { bg = "line" },
  LspInlayHint = { fg = "muted", bg = "surf", italic = true }, LspSignatureActiveParameter = { fg = "lav2", bold = true },

  DiagnosticError = { fg = "rose" }, DiagnosticWarn = { fg = "amber" }, DiagnosticInfo = { fg = "blue" },
  DiagnosticHint = { fg = "cyan" }, DiagnosticOk = { fg = "sage" },
  DiagnosticVirtualTextError = { fg = "rose", bg = "#251d23" }, DiagnosticVirtualTextWarn = { fg = "amber", bg = "#24211f" },
  DiagnosticVirtualTextInfo = { fg = "blue", bg = "#1f1f29" }, DiagnosticVirtualTextHint = { fg = "cyan", bg = "#1d2226" },
  DiagnosticUnderlineError = { sp = "rose", undercurl = true }, DiagnosticUnderlineWarn = { sp = "amber", undercurl = true },
  DiagnosticUnderlineInfo = { sp = "blue", undercurl = true }, DiagnosticUnderlineHint = { sp = "cyan", undercurl = true },
  DiagnosticUnnecessary = { fg = "muted" },

  -- плагины
  GitSignsAdd = { fg = "sage" }, GitSignsChange = { fg = "amber" }, GitSignsDelete = { fg = "rose" },
  NeoTreeNormal = { fg = "sub", bg = "deep" }, NeoTreeNormalNC = { fg = "sub", bg = "deep" },
  NeoTreeEndOfBuffer = { fg = "deep", bg = "deep" }, NeoTreeWinSeparator = { fg = "deep", bg = "deep" },
  NeoTreeRootName = { fg = "lav", bold = true }, NeoTreeDirectoryIcon = { fg = "lav" },
  NeoTreeDirectoryName = { fg = "fg" }, NeoTreeFileNameOpened = { fg = "lav2" },
  NeoTreeIndentMarker = { fg = "line" }, NeoTreeExpander = { fg = "muted" },
  NeoTreeDotfile = { fg = "muted" }, NeoTreeHiddenByName = { fg = "muted" },
  NeoTreeGitModified = { fg = "amber" }, NeoTreeGitUntracked = { fg = "sage" },
  NeoTreeGitAdded = { fg = "sage" }, NeoTreeGitDeleted = { fg = "rose" }, NeoTreeGitConflict = { fg = "rose" },
  NeoTreeCursorLine = { bg = "surf" }, NeoTreeTitleBar = { fg = "bg", bg = "lav" },
  NeoTreeFloatBorder = "FloatBorder", NeoTreeTabActive = { fg = "lav", bg = "deep", bold = true },
  NeoTreeTabInactive = { fg = "muted", bg = "deep" }, NeoTreeTabSeparatorActive = { fg = "deep", bg = "deep" },
  NeoTreeTabSeparatorInactive = { fg = "deep", bg = "deep" },
  WhichKey = { fg = "lav" }, WhichKeyGroup = { fg = "blue" }, WhichKeyDesc = { fg = "fg" },
  WhichKeySeparator = { fg = "muted" }, WhichKeyNormal = { bg = "surf" }, WhichKeyTitle = "FloatTitle",
  BlinkCmpMenu = "Pmenu", BlinkCmpMenuBorder = { fg = "lav", bg = "surf" },
  BlinkCmpMenuSelection = { bg = "sel", bold = true }, BlinkCmpLabelMatch = { fg = "lav2", bold = true },
  BlinkCmpKind = { fg = "cyan" }, BlinkCmpLabelDetail = { fg = "muted" }, BlinkCmpLabelDescription = { fg = "muted" },
  BlinkCmpDocBorder = { fg = "lav", bg = "surf" }, BlinkCmpGhostText = { fg = "muted", italic = true },
  SnacksDashboardHeader = { fg = "lav" }, SnacksDashboardIcon = { fg = "lav2" },
  SnacksDashboardKey = { fg = "amber" }, SnacksDashboardDesc = { fg = "fg" },
  SnacksDashboardFooter = { fg = "muted", italic = true }, SnacksDashboardTitle = { fg = "lav2", bold = true },
  SnacksDashboardFile = { fg = "fg" }, SnacksDashboardDir = { fg = "muted" },
  SnacksIndent = { fg = "line" }, SnacksIndentScope = { fg = "muted" },
  SnacksNotifierBorderInfo = { fg = "lav" }, SnacksNotifierTitleInfo = { fg = "lav2" }, SnacksNotifierIconInfo = { fg = "lav" },
  SnacksInputBorder = { fg = "lav" }, SnacksInputTitle = { fg = "lav2" },
  FzfLuaBorder = { fg = "lav", bg = "surf" }, FzfLuaNormal = { fg = "fg", bg = "surf" },
  FzfLuaTitle = "FloatTitle", FzfLuaPreviewBorder = { fg = "sel", bg = "surf" },
  FzfLuaBackdrop = { bg = "#000000" },
  TroubleNormal = { fg = "sub", bg = "deep" }, TroubleNormalNC = { fg = "sub", bg = "deep" },
  MasonHeader = { fg = "bg", bg = "lav", bold = true }, MasonHighlight = { fg = "lav" },
  MasonHighlightBlock = { fg = "bg", bg = "lav" }, MasonHighlightBlockBold = { fg = "bg", bg = "lav", bold = true },
  MasonMuted = { fg = "muted" }, MasonMutedBlock = { fg = "sub", bg = "sel" },
  LazyH1 = { fg = "bg", bg = "lav", bold = true }, LazyButton = { fg = "sub", bg = "sel" },
  LazyButtonActive = { fg = "bg", bg = "lav2" }, LazySpecial = { fg = "lav" },
  DapBreakpoint = { fg = "rose" }, DapStopped = { fg = "sage" }, DapStoppedLine = { bg = "#232b27" },
  DapUIScope = { fg = "lav" }, DapUIType = { fg = "cyan" }, DapUIDecoration = { fg = "lav" },
  DapUIThread = { fg = "sage" }, DapUIStoppedThread = { fg = "lav2" }, DapUIFloatBorder = "FloatBorder",
  ToggleTermBorder = { fg = "lav" }, MiniTrailspace = { bg = "rose" },
  TodoBgTODO = { fg = "bg", bg = "lav", bold = true }, TodoFgTODO = { fg = "lav" },
  TodoBgFIX = { fg = "bg", bg = "rose", bold = true }, TodoFgFIX = { fg = "rose" },
  TodoBgNOTE = { fg = "bg", bg = "sage", bold = true }, TodoFgNOTE = { fg = "sage" },
  TodoBgWARN = { fg = "bg", bg = "amber", bold = true }, TodoFgWARN = { fg = "amber" },
  TodoBgHACK = { fg = "bg", bg = "amber", bold = true }, TodoFgHACK = { fg = "amber" },
  TodoBgPERF = { fg = "bg", bg = "blue", bold = true }, TodoFgPERF = { fg = "blue" },
  TodoBgTEST = { fg = "bg", bg = "cyan", bold = true }, TodoFgTEST = { fg = "cyan" },
}
for g, o in pairs(groups) do hi(g, o) end

-- цвета встроенного терминала
local term = { p.surf, p.rose, p.sage, p.amber, p.blue, p.lav, p.cyan, p.sub,
  p.muted, p.rose, p.sage, p.amber, p.blue, p.lav2, p.cyan, p.fg }
for i, c in ipairs(term) do vim.g["terminal_color_" .. (i - 1)] = c end
