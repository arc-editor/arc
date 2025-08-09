#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <errno.h>
#include "tomlc17.h"
#include "tree_sitter/api.h"
#include "config.h"
#include "log.h"
#include "theme.h"

static const char* default_theme_toml =
    "[content]\n"
    "line-number = { fg = \"#646482\", bg = \"#26283C\" }\n"
    "line-number-active = { fg = \"#8C96B4\", bg = \"#32374B\" }\n"
    "line-number-sticky = { fg = \"#646482\", bg = \"#1B2130\" }\n"
    "cursor-line = { bg = \"#32374B\" }\n"
    "selection = { bg = \"#474A63\" }\n"
    "background = { bg = \"#26283C\" }\n"
    "whitespace = { fg = \"#474A63\" }\n"
    "\n"
    "[search]\n"
    "match = { bg = \"#474A63\" }\n"
    "\n"
    "[statusline]\n"
    "mode-insert = { fg = \"#141428\", bg = \"#D2F0BE\", bold = true }\n"
    "mode-normal = { fg = \"#141428\", bg = \"#C8C8FF\", bold = true }\n"
    "mode-visual = { fg = \"#141428\", bg = \"#f5e0dc\", bold = true }\n"
    "mode-command = { fg = \"#141428\", bg = \"#8aadf4\", bold = true }\n"
    "text = { fg = \"#D7DCFA\", bg = \"#1F2130\" }\n"
    "\n"
    "\n"
    "[syntax]\n"
    "keyword = { fg = \"#c6a0f6\" }\n"
    "keyword-storage-type = { fg = \"#c6a0f6\" }\n"
    "keyword-storage-modifier = { fg = \"#c6a0f6\" }\n"
    "keyword-control = { fg = \"#c6a0f6\" }\n"
    "keyword-control-repeat = { fg = \"#c6a0f6\" }\n"
    "keyword-control-return = { fg = \"#c6a0f6\" }\n"
    "keyword-control-conditional = { fg = \"#c6a0f6\" }\n"
    "keyword-directive = { fg = \"#c6a0f6\" }\n"
    "function-special = { fg = \"#c6a0f6\" }\n"
    "string = { fg = \"#a6da95\" }\n"
    "constant-character = { fg = \"#8bd5ca\" }\n"
    "type-enum-variant = { fg = \"#8bd5ca\" }\n"
    "constant-character-escape = { fg = \"#f5bde6\" }\n"
    "constant-numeric = { fg = \"#f5a97f\" }\n"
    "constant-builtin-boolean = { fg = \"#f5a97f\" }\n"
    "constant = { fg = \"#f5a97f\" }\n"
    "variable = { fg = \"#cad3f5\" }\n"
    "variable-parameter = { fg = \"#ee99a0\", italic = true }\n"
    "variable-other-member = { fg = \"#8aadf4\" }\n"
    "function = { fg = \"#8aadf4\" }\n"
    "type = { fg = \"#eed49f\" }\n"
    "type-builtin = { fg = \"#eed49f\" }\n"
    "attribute = { fg = \"#eed49f\" }\n"
    "punctuation = { fg = \"#939ab7\" }\n"
    "punctuation-delimiter = { fg = \"#939ab7\" }\n"
    "punctuation-bracket = { fg = \"#939ab7\" }\n"
    "comment = { fg = \"#939ab7\", italic = true }\n"
    "operator = { fg = \"#91d7e3\" }\n"
    "label = { fg = \"#7dc4e4\" }\n"
    "info = { fg = \"#8aadf4\" }\n"
    "warning = { fg = \"#eed49f\" }\n"
    "error = { fg = \"#ee99a0\" }\n"
    "\n"
    "[diagnostics]\n"
    "hint = { fg = \"#8aadf4\" }\n"
    "info = { fg = \"#8aadf4\" }\n"
    "warning = { fg = \"#eed49f\" }\n"
    "error = { fg = \"#ee99a0\" }\n"
    "\n"
    "[picker]\n"
    "item-text = { fg = \"#cad3f5\" }\n"
    "item-text-highlight = { fg = \"#8aadf4\", bold = true }\n"
    "\n"
    "[popup.border]\n"
    "fg = \"#8aadf4\"\n"
    "\n"
    "[picker.border]\n"
    "fg = \"#8aadf4\"\n"
;

static const char* onedark_theme_toml =
    "[content]\n"
    "line-number = { fg = \"#495162\", bg = \"#282c34\" }\n"
    "line-number-active = { fg = \"#abb2bf\", bg = \"#2c313c\" }\n"
    "line-number-sticky = { fg = \"#495162\", bg = \"#21252b\" }\n"
    "cursor-line = { bg = \"#2c313c\" }\n"
    "selection = { bg = \"#3e4452\" }\n"
    "background = { bg = \"#282c34\" }\n"
    "whitespace = { fg = \"#4b5263\" }\n"
    "\n"
    "[search]\n"
    "match = { bg = \"#314365\" }\n"
    "\n"
    "[statusline]\n"
    "mode-insert = { fg = \"#282c34\", bg = \"#98c379\", bold = true }\n"
    "mode-normal = { fg = \"#282c34\", bg = \"#61afef\", bold = true }\n"
    "mode-visual = { fg = \"#282c34\", bg = \"#c678dd\", bold = true }\n"
    "mode-command = { fg = \"#282c34\", bg = \"#e5c07b\", bold = true }\n"
    "text = { fg = \"#abb2bf\", bg = \"#21252b\" }\n"
    "\n"
    "[syntax]\n"
    "keyword = { fg = \"#c678dd\" }\n"
    "keyword-storage-type = { fg = \"#c678dd\" }\n"
    "keyword-storage-modifier = { fg = \"#c678dd\" }\n"
    "keyword-control = { fg = \"#c678dd\" }\n"
    "keyword-control-repeat = { fg = \"#c678dd\" }\n"
    "keyword-control-return = { fg = \"#c678dd\" }\n"
    "keyword-control-conditional = { fg = \"#c678dd\" }\n"
    "keyword-directive = { fg = \"#c678dd\" }\n"
    "function-special = { fg = \"#61afef\" }\n"
    "string = { fg = \"#98c379\" }\n"
    "constant-character = { fg = \"#56b6c2\" }\n"
    "type-enum-variant = { fg = \"#56b6c2\" }\n"
    "constant-character-escape = { fg = \"#56b6c2\" }\n"
    "constant-numeric = { fg = \"#d19a66\" }\n"
    "constant-builtin-boolean = { fg = \"#d19a66\" }\n"
    "constant = { fg = \"#d19a66\" }\n"
    "variable = { fg = \"#e06c75\" }\n"
    "variable-parameter = { fg = \"#abb2bf\", italic = true }\n"
    "variable-other-member = { fg = \"#e06c75\" }\n"
    "function = { fg = \"#61afef\" }\n"
    "type = { fg = \"#e5c07b\" }\n"
    "type-builtin = { fg = \"#e5c07b\" }\n"
    "attribute = { fg = \"#d19a66\" }\n"
    "punctuation = { fg = \"#abb2bf\" }\n"
    "punctuation-delimiter = { fg = \"#abb2bf\" }\n"
    "punctuation-bracket = { fg = \"#abb2bf\" }\n"
    "comment = { fg = \"#7f848e\", italic = true }\n"
    "operator = { fg = \"#56b6c2\" }\n"
    "label = { fg = \"#e06c75\" }\n"
    "info = { fg = \"#61afef\" }\n"
    "warning = { fg = \"#e5c07b\" }\n"
    "error = { fg = \"#e06c75\" }\n"
    "\n"
    "[diagnostics]\n"
    "hint = { fg = \"#61afef\" }\n"
    "info = { fg = \"#61afef\" }\n"
    "warning = { fg = \"#e5c07b\" }\n"
    "error = { fg = \"#e06c75\" }\n"
    "\n"
    "[picker]\n"
    "item-text = { fg = \"#abb2bf\" }\n"
    "item-text-highlight = { fg = \"#61afef\", bold = true }\n"
    "\n"
    "[popup.border]\n"
    "fg = \"#3e4452\"\n"
    "\n"
    "[picker.border]\n"
    "fg = \"#3e4452\"\n"
;

static const char* catppuccin_latte_toml =
    "[content]\n"
    "line-number = { fg = \"#bcc0cc\", bg = \"#eff1f5\" }\n"
    "line-number-active = { fg = \"#7287fd\", bg = \"#e6e9ef\" }\n"
    "line-number-sticky = { fg = \"#bcc0cc\", bg = \"#dce0e8\" }\n"
    "cursor-line = { bg = \"#e6e9ef\" }\n"
    "selection = { bg = \"#acb0be\" }\n"
    "background = { bg = \"#eff1f5\" }\n"
    "whitespace = { fg = \"#bcc0cc\" }\n"
    "\n"
    "[search]\n"
    "match = { bg = \"#acb0be\" }\n"
    "\n"
    "[statusline]\n"
    "mode-insert = { fg = \"#eff1f5\", bg = \"#40a02b\", bold = true }\n"
    "mode-normal = { fg = \"#eff1f5\", bg = \"#dc8a78\", bold = true }\n"
    "mode-visual = { fg = \"#eff1f5\", bg = \"#7287fd\", bold = true }\n"
    "mode-command = { fg = \"#eff1f5\", bg = \"#fe640b\", bold = true }\n"
    "text = { fg = \"#4c4f69\", bg = \"#e6e9ef\" }\n"
    "\n"
    "[syntax]\n"
    "keyword = { fg = \"#8839ef\" }\n"
    "keyword-storage-type = { fg = \"#8839ef\" }\n"
    "keyword-storage-modifier = { fg = \"#8839ef\" }\n"
    "keyword-control = { fg = \"#8839ef\" }\n"
    "keyword-control-repeat = { fg = \"#8839ef\" }\n"
    "keyword-control-return = { fg = \"#8839ef\" }\n"
    "keyword-control-conditional = { fg = \"#8839ef\", italic = true }\n"
    "keyword-directive = { fg = \"#8839ef\" }\n"
    "function-special = { fg = \"#1e66f5\" }\n"
    "string = { fg = \"#40a02b\" }\n"
    "constant-character = { fg = \"#179299\" }\n"
    "type-enum-variant = { fg = \"#179299\" }\n"
    "constant-character-escape = { fg = \"#ea76cb\" }\n"
    "constant-numeric = { fg = \"#fe640b\" }\n"
    "constant-builtin-boolean = { fg = \"#fe640b\" }\n"
    "constant = { fg = \"#fe640b\" }\n"
    "variable = { fg = \"#4c4f69\" }\n"
    "variable-parameter = { fg = \"#e64553\", italic = true }\n"
    "variable-other-member = { fg = \"#1e66f5\" }\n"
    "function = { fg = \"#1e66f5\" }\n"
    "type = { fg = \"#df8e1d\" }\n"
    "type-builtin = { fg = \"#df8e1d\" }\n"
    "attribute = { fg = \"#df8e1d\" }\n"
    "punctuation = { fg = \"#7c7f93\" }\n"
    "punctuation-delimiter = { fg = \"#7c7f93\" }\n"
    "punctuation-bracket = { fg = \"#7c7f93\" }\n"
    "comment = { fg = \"#9ca0b0\", italic = true }\n"
    "operator = { fg = \"#04a5e5\" }\n"
    "label = { fg = \"#209fb5\" }\n"
    "info = { fg = \"#1e66f5\" }\n"
    "warning = { fg = \"#df8e1d\" }\n"
    "error = { fg = \"#d20f39\" }\n"
    "\n"
    "[diagnostics]\n"
    "hint = { fg = \"#179299\" }\n"
    "info = { fg = \"#1e66f5\" }\n"
    "warning = { fg = \"#df8e1d\" }\n"
    "error = { fg = \"#d20f39\" }\n"
    "\n"
    "[picker]\n"
    "item-text = { fg = \"#4c4f69\" }\n"
    "item-text-highlight = { fg = \"#1e66f5\", bold = true }\n"
    "\n"
    "[popup.border]\n"
    "fg = \"#dce0e8\"\n"
    "\n"
    "[picker.border]\n"
    "fg = \"#dce0e8\"\n"
;

static const char* catppuccin_frappe_toml =
    "[content]\n"
    "line-number = { fg = \"#626880\", bg = \"#303446\" }\n"
    "line-number-active = { fg = \"#babbf1\", bg = \"#3b3f52\" }\n"
    "line-number-sticky = { fg = \"#626880\", bg = \"#232634\" }\n"
    "cursor-line = { bg = \"#3b3f52\" }\n"
    "selection = { bg = \"#51576d\" }\n"
    "background = { bg = \"#303446\" }\n"
    "whitespace = { fg = \"#626880\" }\n"
    "\n"
    "[search]\n"
    "match = { bg = \"#51576d\" }\n"
    "\n"
    "[statusline]\n"
    "mode-insert = { fg = \"#303446\", bg = \"#a6d189\", bold = true }\n"
    "mode-normal = { fg = \"#303446\", bg = \"#f2d5cf\", bold = true }\n"
    "mode-visual = { fg = \"#303446\", bg = \"#babbf1\", bold = true }\n"
    "mode-command = { fg = \"#303446\", bg = \"#ef9f76\", bold = true }\n"
    "text = { fg = \"#c6d0f5\", bg = \"#292c3c\" }\n"
    "\n"
    "[syntax]\n"
    "keyword = { fg = \"#ca9ee6\" }\n"
    "keyword-storage-type = { fg = \"#ca9ee6\" }\n"
    "keyword-storage-modifier = { fg = \"#ca9ee6\" }\n"
    "keyword-control = { fg = \"#ca9ee6\" }\n"
    "keyword-control-repeat = { fg = \"#ca9ee6\" }\n"
    "keyword-control-return = { fg = \"#ca9ee6\" }\n"
    "keyword-control-conditional = { fg = \"#ca9ee6\", italic = true }\n"
    "keyword-directive = { fg = \"#ca9ee6\" }\n"
    "function-special = { fg = \"#8caaee\" }\n"
    "string = { fg = \"#a6d189\" }\n"
    "constant-character = { fg = \"#81c8be\" }\n"
    "type-enum-variant = { fg = \"#81c8be\" }\n"
    "constant-character-escape = { fg = \"#f4b8e4\" }\n"
    "constant-numeric = { fg = \"#ef9f76\" }\n"
    "constant-builtin-boolean = { fg = \"#ef9f76\" }\n"
    "constant = { fg = \"#ef9f76\" }\n"
    "variable = { fg = \"#c6d0f5\" }\n"
    "variable-parameter = { fg = \"#ea999c\", italic = true }\n"
    "variable-other-member = { fg = \"#8caaee\" }\n"
    "function = { fg = \"#8caaee\" }\n"
    "type = { fg = \"#e5c890\" }\n"
    "type-builtin = { fg = \"#e5c890\" }\n"
    "attribute = { fg = \"#e5c890\" }\n"
    "punctuation = { fg = \"#949cbb\" }\n"
    "punctuation-delimiter = { fg = \"#949cbb\" }\n"
    "punctuation-bracket = { fg = \"#949cbb\" }\n"
    "comment = { fg = \"#737994\", italic = true }\n"
    "operator = { fg = \"#99d1db\" }\n"
    "label = { fg = \"#85c1dc\" }\n"
    "info = { fg = \"#8caaee\" }\n"
    "warning = { fg = \"#e5c890\" }\n"
    "error = { fg = \"#e78284\" }\n"
    "\n"
    "[diagnostics]\n"
    "hint = { fg = \"#81c8be\" }\n"
    "info = { fg = \"#8caaee\" }\n"
    "warning = { fg = \"#e5c890\" }\n"
    "error = { fg = \"#e78284\" }\n"
    "\n"
    "[picker]\n"
    "item-text = { fg = \"#c6d0f5\" }\n"
    "item-text-highlight = { fg = \"#8caaee\", bold = true }\n"
    "\n"
    "[popup.border]\n"
    "fg = \"#232634\"\n"
    "\n"
    "[picker.border]\n"
    "fg = \"#232634\"\n"
;

static const char* catppuccin_macchiato_toml =
    "[content]\n"
    "line-number = { fg = \"#5b6078\", bg = \"#24273a\" }\n"
    "line-number-active = { fg = \"#b7bdf8\", bg = \"#303347\" }\n"
    "line-number-sticky = { fg = \"#5b6078\", bg = \"#181926\" }\n"
    "cursor-line = { bg = \"#303347\" }\n"
    "selection = { bg = \"#494d64\" }\n"
    "background = { bg = \"#24273a\" }\n"
    "whitespace = { fg = \"#5b6078\" }\n"
    "\n"
    "[search]\n"
    "match = { bg = \"#494d64\" }\n"
    "\n"
    "[statusline]\n"
    "mode-insert = { fg = \"#24273a\", bg = \"#a6da95\", bold = true }\n"
    "mode-normal = { fg = \"#24273a\", bg = \"#f4dbd6\", bold = true }\n"
    "mode-visual = { fg = \"#24273a\", bg = \"#b7bdf8\", bold = true }\n"
    "mode-command = { fg = \"#24273a\", bg = \"#f5a97f\", bold = true }\n"
    "text = { fg = \"#cad3f5\", bg = \"#1e2030\" }\n"
    "\n"
    "[syntax]\n"
    "keyword = { fg = \"#c6a0f6\" }\n"
    "keyword-storage-type = { fg = \"#c6a0f6\" }\n"
    "keyword-storage-modifier = { fg = \"#c6a0f6\" }\n"
    "keyword-control = { fg = \"#c6a0f6\" }\n"
    "keyword-control-repeat = { fg = \"#c6a0f6\" }\n"
    "keyword-control-return = { fg = \"#c6a0f6\" }\n"
    "keyword-control-conditional = { fg = \"#c6a0f6\", italic = true }\n"
    "keyword-directive = { fg = \"#c6a0f6\" }\n"
    "function-special = { fg = \"#8aadf4\" }\n"
    "string = { fg = \"#a6da95\" }\n"
    "constant-character = { fg = \"#8bd5ca\" }\n"
    "type-enum-variant = { fg = \"#8bd5ca\" }\n"
    "constant-character-escape = { fg = \"#f5bde6\" }\n"
    "constant-numeric = { fg = \"#f5a97f\" }\n"
    "constant-builtin-boolean = { fg = \"#f5a97f\" }\n"
    "constant = { fg = \"#f5a97f\" }\n"
    "variable = { fg = \"#cad3f5\" }\n"
    "variable-parameter = { fg = \"#ee99a0\", italic = true }\n"
    "variable-other-member = { fg = \"#8aadf4\" }\n"
    "function = { fg = \"#8aadf4\" }\n"
    "type = { fg = \"#eed49f\" }\n"
    "type-builtin = { fg = \"#eed49f\" }\n"
    "attribute = { fg = \"#eed49f\" }\n"
    "punctuation = { fg = \"#939ab7\" }\n"
    "punctuation-delimiter = { fg = \"#939ab7\" }\n"
    "punctuation-bracket = { fg = \"#939ab7\" }\n"
    "comment = { fg = \"#6e738d\", italic = true }\n"
    "operator = { fg = \"#91d7e3\" }\n"
    "label = { fg = \"#7dc4e4\" }\n"
    "info = { fg = \"#8aadf4\" }\n"
    "warning = { fg = \"#eed49f\" }\n"
    "error = { fg = \"#ed8796\" }\n"
    "\n"
    "[diagnostics]\n"
    "hint = { fg = \"#8bd5ca\" }\n"
    "info = { fg = \"#8aadf4\" }\n"
    "warning = { fg = \"#eed49f\" }\n"
    "error = { fg = \"#ed8796\" }\n"
    "\n"
    "[picker]\n"
    "item-text = { fg = \"#cad3f5\" }\n"
    "item-text-highlight = { fg = \"#8aadf4\", bold = true }\n"
    "\n"
    "[popup.border]\n"
    "fg = \"#181926\"\n"
    "\n"
    "[picker.border]\n"
    "fg = \"#181926\"\n"
;

static const char* catppuccin_mocha_toml =
    "[content]\n"
    "line-number = { fg = \"#585b70\", bg = \"#1e1e2e\" }\n"
    "line-number-active = { fg = \"#b4befe\", bg = \"#2a2b3c\" }\n"
    "line-number-sticky = { fg = \"#585b70\", bg = \"#11111b\" }\n"
    "cursor-line = { bg = \"#2a2b3c\" }\n"
    "selection = { bg = \"#45475a\" }\n"
    "background = { bg = \"#1e1e2e\" }\n"
    "whitespace = { fg = \"#585b70\" }\n"
    "\n"
    "[search]\n"
    "match = { bg = \"#45475a\" }\n"
    "\n"
    "[statusline]\n"
    "mode-insert = { fg = \"#1e1e2e\", bg = \"#a6e3a1\", bold = true }\n"
    "mode-normal = { fg = \"#1e1e2e\", bg = \"#f5e0dc\", bold = true }\n"
    "mode-visual = { fg = \"#1e1e2e\", bg = \"#b4befe\", bold = true }\n"
    "mode-command = { fg = \"#1e1e2e\", bg = \"#fab387\", bold = true }\n"
    "text = { fg = \"#cdd6f4\", bg = \"#181825\" }\n"
    "\n"
    "[syntax]\n"
    "keyword = { fg = \"#cba6f7\" }\n"
    "keyword-storage-type = { fg = \"#cba6f7\" }\n"
    "keyword-storage-modifier = { fg = \"#cba6f7\" }\n"
    "keyword-control = { fg = \"#cba6f7\" }\n"
    "keyword-control-repeat = { fg = \"#cba6f7\" }\n"
    "keyword-control-return = { fg = \"#cba6f7\" }\n"
    "keyword-control-conditional = { fg = \"#cba6f7\", italic = true }\n"
    "keyword-directive = { fg = \"#cba6f7\" }\n"
    "function-special = { fg = \"#89b4fa\" }\n"
    "string = { fg = \"#a6e3a1\" }\n"
    "constant-character = { fg = \"#94e2d5\" }\n"
    "type-enum-variant = { fg = \"#94e2d5\" }\n"
    "constant-character-escape = { fg = \"#f5c2e7\" }\n"
    "constant-numeric = { fg = \"#fab387\" }\n"
    "constant-builtin-boolean = { fg = \"#fab387\" }\n"
    "constant = { fg = \"#fab387\" }\n"
    "variable = { fg = \"#cdd6f4\" }\n"
    "variable-parameter = { fg = \"#eba0ac\", italic = true }\n"
    "variable-other-member = { fg = \"#89b4fa\" }\n"
    "function = { fg = \"#89b4fa\" }\n"
    "type = { fg = \"#f9e2af\" }\n"
    "type-builtin = { fg = \"#f9e2af\" }\n"
    "attribute = { fg = \"#f9e2af\" }\n"
    "punctuation = { fg = \"#9399b2\" }\n"
    "punctuation-delimiter = { fg = \"#9399b2\" }\n"
    "punctuation-bracket = { fg = \"#9399b2\" }\n"
    "comment = { fg = \"#6c7086\", italic = true }\n"
    "operator = { fg = \"#89dceb\" }\n"
    "label = { fg = \"#74c7ec\" }\n"
    "info = { fg = \"#89b4fa\" }\n"
    "warning = { fg = \"#f9e2af\" }\n"
    "error = { fg = \"#f38ba8\" }\n"
    "\n"
    "[diagnostics]\n"
    "hint = { fg = \"#94e2d5\" }\n"
    "info = { fg = \"#89b4fa\" }\n"
    "warning = { fg = \"#f9e2af\" }\n"
    "error = { fg = \"#f38ba8\" }\n"
    "\n"
    "[picker]\n"
    "item-text = { fg = \"#cdd6f4\" }\n"
    "item-text-highlight = { fg = \"#89b4fa\", bold = true }\n"
    "\n"
    "[popup.border]\n"
    "fg = \"#11111b\"\n"
    "\n"
    "[picker.border]\n"
    "fg = \"#11111b\"\n"
;

// Create directories recursively (like mkdir -p)
int mkdir_recursive(const char *path, __mode_t mode) {
    char *path_copy = strdup(path);
    if (!path_copy) {
        return -1;
    }
    
    char *p = path_copy;
    
    // Skip leading slash if present
    if (*p == '/') {
        p++;
    }
    
    while ((p = strchr(p, '/'))) {
        *p = '\0';
        
        if (mkdir(path_copy, mode) != 0 && errno != EEXIST) {
            free(path_copy);
            return -1;
        }
        
        *p = '/';
        p++;
    }
    
    // Create the final directory
    if (mkdir(path_copy, mode) != 0 && errno != EEXIST) {
        free(path_copy);
        return -1;
    }
    
    free(path_copy);
    return 0;
}

// Extract directory path from full file path and create directories
int ensure_directory_exists(const char *file_path) {
    char *path_copy = strdup(file_path);
    if (!path_copy) {
        log_warning("config.ensure_directory_exists: strdup failed");
        return -1;
    }
    
    // Find the last slash to separate directory from filename
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        // No directory component, just a filename
        free(path_copy);
        return 0;
    }
    
    // Terminate string at last slash to get directory path
    *last_slash = '\0';
    
    // Create the directory structure
    if (mkdir_recursive(path_copy, 0755) != 0) {
        log_warning("config.ensure_directory_exists: mkdir_recursive failed for %s", path_copy);
        free(path_copy);
        return -1;
    }
    
    free(path_copy);
    return 0;
}

// caller must free the return value
char *get_path(const char *partial_format, ...) {
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        log_warning("config.make_config_path: unable to get env HOME");
        return NULL;
    }

    // Format the partial path with variadic arguments
    va_list args;
    va_start(args, partial_format);
    int partial_len = vsnprintf(NULL, 0, partial_format, args);
    va_end(args);

    if (partial_len < 0) {
        log_warning("config.make_config_path: vsnprintf failed (size calc)");
        return NULL;
    }

    char *partial_path = malloc(partial_len + 1);
    if (!partial_path) {
        log_warning("config.make_config_path: malloc failed for partial_path");
        return NULL;
    }

    va_start(args, partial_format);
    vsnprintf(partial_path, partial_len + 1, partial_format, args);
    va_end(args);

    const char *prefix = "/.config/arc";
    size_t full_len = strlen(home_dir) + strlen(prefix) + partial_len + 1;
    char *full_path = malloc(full_len);
    if (!full_path) {
        log_warning("config.make_config_path: malloc failed for full_path");
        free(partial_path);
        return NULL;
    }

    snprintf(full_path, full_len, "%s%s%s", home_dir, prefix, partial_path);
    free(partial_path);
    if (ensure_directory_exists(full_path) != 0) {
        log_warning("config.make_config_path: failed to create directories for %s", full_path);
        free(full_path);
        return NULL;
    }
    return full_path;
}

TSLanguage *config_load_language(char *name) {
    if (name == NULL) return NULL;
    char *parser_path = get_path("/grammars/%s.so", name);
    if (!parser_path) {
        log_error("config.load_language: make_config_path failed");
        return NULL;
    }
    
    size_t symbol_len = strlen("tree_sitter_") + strlen(name) + 1;
    char *symbol = malloc(symbol_len);
    if (!symbol) {
        log_error("config.load_language: memory allocation failed for symbol");
        free(parser_path);
        return NULL;
    }
    snprintf(symbol, symbol_len, "tree_sitter_%s", name);
    
    void *handle = dlopen(parser_path, RTLD_NOW);
    if (!handle) {
        log_warning("config.load_language: dlopen failed: %s", dlerror());
        free(parser_path);
        free(symbol);
        return NULL;
    }
    
    dlerror(); // Clear any existing error
    
    TSLanguageFn fn = (TSLanguageFn)dlsym(handle, symbol);
    const char *err = dlerror();
    if (err != NULL) {
        log_error("config.load_language: dlsym failed: %s", err);
        dlclose(handle);
        free(parser_path);
        free(symbol);
        return NULL;
    }
    
    log_info("config.load_language: success: %s from %s", symbol, parser_path);
    
    // Note: We don't dlclose(handle) here because the TSLanguage object
    // may contain function pointers that need the library to remain loaded
    
    free(parser_path);
    free(symbol);
    
    return fn();
}

TSQuery *config_load_highlights(TSLanguage *language, char *name) {
    if (name == NULL || language == NULL) return NULL;
    char *highlights_path = get_path("/highlights/%s.scm", name);
    if (!highlights_path) {
        log_error("config.config_load_highlights: make_config_path failed");
        return NULL;
    }
    
    FILE *file = fopen(highlights_path, "r");
    if (!file) {
        log_warning("config.config_load_highlights: failed to open highlights file");
        free(highlights_path);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        log_error("config.config_load_highlights: invalid or empty highliths file");
        fclose(file);
        free(highlights_path);
        return NULL;
    }
    
    // Allocate buffer and read file
    char *highlights_scm = malloc(file_size + 1);
    if (!highlights_scm) {
        log_error("config.config_load_highlights: failed to allocate memory for highliths");
        fclose(file);
        free(highlights_path);
        return NULL;
    }
    
    size_t bytes_read = fread(highlights_scm, 1, file_size, file);
    if ((long)bytes_read != file_size) {
        log_error("config.config_load_highlights: failed to read complete highlights file");
        free(highlights_scm);
        free(highlights_path);
        fclose(file);
        return NULL;
    }
    
    highlights_scm[file_size] = '\0';
    fclose(file);
    
    // Parse the query
    uint32_t error_offset;
    TSQueryError error_type;
    
    TSQuery *query = ts_query_new(
        language,
        highlights_scm,
        strlen(highlights_scm),
        &error_offset,
        &error_type
    );
    free(highlights_scm);
    if (!query) {
        log_warning("config.config_load_highlights: failed to parse highlight query at offset %u", error_offset);
        free(highlights_path);
        return NULL;
    }
    log_info("config.config_load_highlights: loaded highlights from %s", highlights_path);
    free(highlights_path);
    return query;
}

static WhitespaceRender parse_whitespace_render(toml_datum_t datum) {
    if (datum.type != TOML_STRING) return WHITESPACE_RENDER_NONE;
    if (strcmp(datum.u.s, "all") == 0) return WHITESPACE_RENDER_ALL;
    if (strcmp(datum.u.s, "trailing") == 0) return WHITESPACE_RENDER_TRAILING;
    return WHITESPACE_RENDER_NONE;
}

void config_load(Config *config) {
    config->theme = strdup("default");
    config->whitespace.space = WHITESPACE_RENDER_NONE;
    config->whitespace.tab = WHITESPACE_RENDER_NONE;
    config->whitespace.space_char = strdup("·");
    config->whitespace.tab_char = strdup("→");

    char *path = get_path("/config.toml");
    if (!path) {
        log_error("config.config_load: make_config_path failed");
        return;
    }
    
    toml_result_t result = toml_parse_file_ex(path);
    if (!result.ok) {
        log_warning("config.config_load: cannot parse %s - %s", path, result.errmsg);
        return;
    }

    toml_datum_t theme_data = toml_seek(result.toptab, "theme");
    if (theme_data.type == TOML_STRING) {
        config->theme = strdup(theme_data.u.s);
    }

    toml_datum_t space_data = toml_seek(result.toptab, "editor.whitespace.render.space");
    config->whitespace.space = parse_whitespace_render(space_data);

    toml_datum_t tab_data = toml_seek(result.toptab, "editor.whitespace.render.tab");
    config->whitespace.tab = parse_whitespace_render(tab_data);

    toml_datum_t space_char_data = toml_seek(result.toptab, "editor.whitespace.characters.space");
    if (space_char_data.type == TOML_STRING) {
        free(config->whitespace.space_char);
        config->whitespace.space_char = strdup(space_char_data.u.s);
    }

    toml_datum_t tab_char_data = toml_seek(result.toptab, "editor.whitespace.characters.tab");
    if (tab_char_data.type == TOML_STRING) {
        free(config->whitespace.tab_char);
        config->whitespace.tab_char = strdup(tab_char_data.u.s);
    }

    free(path);
    toml_free(result);
}

void config_destroy(Config *config) {
    free(config->theme);
    free(config->whitespace.space_char);
    free(config->whitespace.tab_char);
}

void config_load_theme(char *name, Theme *theme) {
    if (name == NULL) return;
    char *path = get_path("/themes/%s.toml", name);
    if (!path) {
        log_error("config.config_load_theme: make_config_path failed");
        return;
    }

    if (access(path, F_OK) == -1) {
        const char* toml_content = NULL;
        if (strcmp(name, "default") == 0) {
            toml_content = default_theme_toml;
        } else if (strcmp(name, "onedark") == 0) {
            toml_content = onedark_theme_toml;
        } else if (strcmp(name, "catppuccin_latte") == 0) {
            toml_content = catppuccin_latte_toml;
        } else if (strcmp(name, "catppuccin_frappe") == 0) {
            toml_content = catppuccin_frappe_toml;
        } else if (strcmp(name, "catppuccin_macchiato") == 0) {
            toml_content = catppuccin_macchiato_toml;
        } else if (strcmp(name, "catppuccin_mocha") == 0) {
            toml_content = catppuccin_mocha_toml;
        }

        if (toml_content) {
            FILE* file = fopen(path, "w");
            if (file) {
                fputs(toml_content, file);
                fclose(file);
                log_info("config.config_load_theme: created %s theme at %s", name, path);
            } else {
                log_warning("config.config_load_theme: failed to create %s theme at %s", name, path);
            }
        }
    }

    log_info("config.config_load_theme: loading theme from %s", path);
    theme_load(path, theme);
    free(path);
}
