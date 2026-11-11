-- Neovim settings for the *clean* variant of the recorded demo
-- (NV3D_DEMO_CLEAN=1 examples/demo/record.sh). Neovim then runs with --clean,
-- so the recording does not depend on anyone's personal config, and this
-- file is loaded with --cmd before the file is opened. By default the demo
-- uses your own Neovim config instead.
--
-- Environment:
--   NV3D_DEMO_COLORSCHEME  colour scheme (default: habamax)
--   NV3D_DEMO_CLANGD       clangd executable (default: clangd on PATH, or Mason's)

vim.cmd.colorscheme(vim.env.NV3D_DEMO_COLORSCHEME or 'habamax')
vim.o.number = true
vim.o.cursorline = true
vim.o.ruler = false
vim.o.swapfile = false
vim.o.signcolumn = 'no'
vim.o.winborder = 'rounded' -- floating windows (the LSP hover) get a border
vim.opt.shortmess:append('I')

-- The program is typed verbatim: no auto-indent or comment continuation.
-- Deferred, because the C/C++ ftplugin and indent scripts run after this
-- autocmd and would switch them back on.
vim.api.nvim_create_autocmd('FileType', {
  pattern = { 'c', 'cpp' },
  callback = function(ev)
    vim.schedule(function()
      vim.bo[ev.buf].autoindent = false
      vim.bo[ev.buf].cindent = false
      vim.bo[ev.buf].smartindent = false
      vim.bo[ev.buf].indentexpr = ''
      vim.bo[ev.buf].formatoptions = ''
    end)
  end,
})

-- LSP: clangd provides the hover window shown in the demo. Diagnostics are
-- hidden, as half-typed code is full of (expected) errors.
local function find_clangd()
  -- (No nil entries in this list: ipairs would stop at the first one.)
  local candidates = { 'clangd', vim.fn.expand('~/.local/share/nvim/mason/bin/clangd') }
  if vim.env.NV3D_DEMO_CLANGD and vim.env.NV3D_DEMO_CLANGD ~= '' then
    table.insert(candidates, 1, vim.env.NV3D_DEMO_CLANGD)
  end
  for _, c in ipairs(candidates) do
    if vim.fn.executable(c) == 1 then return vim.fn.exepath(c) end
  end
end

vim.diagnostic.config({ signs = false, underline = false, virtual_text = false })

local clangd = find_clangd()
if clangd then
  vim.lsp.config('clangd', {
    cmd = { clangd },
    filetypes = { 'c', 'cpp' },
    root_markers = { 'compile_commands.json', '.git' },
  })
  vim.lsp.enable('clangd')
end
-- <leader>i (Space, i) shows the LSP hover window; the demo script presses it.
vim.g.mapleader = ' '
vim.keymap.set('n', '<leader>i', function()
  if #vim.lsp.get_clients({ bufnr = 0 }) > 0 then vim.lsp.buf.hover() end
end)
if not clangd then
  vim.schedule(function()
    vim.notify('demo: clangd not found, recording without the LSP hover', vim.log.levels.WARN)
  end)
end
