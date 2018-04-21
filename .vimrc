if has ("syntax")
	syntax on
endif

set autoindent
set cindent

set nu

set ts=4
set shiftwidth=4


au BufReadPost *
\ if line("'\"") > 0 && line("'\"") <= line("$") |
\   exe "norm g`\"" |
\ endif
