#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include "perfect_hashmap.h"
#include "tomlc17.h"
#include "theme.h"
#include "log.h"

typedef struct {
    const char *name;
    size_t style_offset;
} StyleMapping;

static const StyleMapping style_map[] = {
    {"attribute", offsetof(Theme, syntax_attribute)},
    {"comment", offsetof(Theme, syntax_comment)},
    {"constant", offsetof(Theme, syntax_constant)},
    {"constant.builtin.boolean", offsetof(Theme, syntax_constant_builtin_boolean)},
    {"constant.character", offsetof(Theme, syntax_constant_character)},
    {"constant.character.escape", offsetof(Theme, syntax_constant_character_escape)},
    {"constant.numeric", offsetof(Theme, syntax_constant_numeric)},
    {"error", offsetof(Theme, syntax_error)},
    {"function", offsetof(Theme, syntax_function)},
    {"function.builtin", offsetof(Theme, syntax_function_builtin)},
    {"function.special", offsetof(Theme, syntax_function_special)},
    {"info", offsetof(Theme, syntax_info)},
    {"keyword", offsetof(Theme, syntax_keyword)},
    {"keyword.control", offsetof(Theme, syntax_keyword_control)},
    {"keyword.control.conditional", offsetof(Theme, syntax_keyword_control_conditional)},
    {"keyword.control.repeat", offsetof(Theme, syntax_keyword_control_repeat)},
    {"keyword.control.return", offsetof(Theme, syntax_keyword_control_return)},
    {"keyword.directive", offsetof(Theme, syntax_keyword_directive)},
    {"keyword.storage.modifier", offsetof(Theme, syntax_keyword_storage_modifier)},
    {"keyword.storage.type", offsetof(Theme, syntax_keyword_storage_type)},
    {"label", offsetof(Theme, syntax_label)},
    {"operator", offsetof(Theme, syntax_operator)},
    {"punctuation", offsetof(Theme, syntax_punctuation)},
    {"punctuation.bracket", offsetof(Theme, syntax_punctuation_bracket)},
    {"punctuation.delimiter", offsetof(Theme, syntax_punctuation_delimiter)},
    {"string", offsetof(Theme, syntax_string)},
    {"type", offsetof(Theme, syntax_type)},
    {"type.builtin", offsetof(Theme, syntax_type_builtin)},
    {"type.enum.variant", offsetof(Theme, syntax_type_enum_variant)},
    {"variable", offsetof(Theme, syntax_variable)},
    {"variable.other.member", offsetof(Theme, syntax_variable_other_member)},
    {"variable.parameter", offsetof(Theme, syntax_variable_parameter)},
    {"warning", offsetof(Theme, syntax_warning)},
};

PerfectHashmap styles;

void theme_init() {
    // TODO: Use or remove this
    perfect_hashmap_create(&styles, NULL, NULL, 0);
}

int compare_style_mappings(const void *key, const void *element) {
    const char *capture_name = (const char *)key;
    const StyleMapping *mapping = (const StyleMapping *)element;
    return strcmp(capture_name, mapping->name);
}

Style *theme_get_capture_style(const char* capture_name, Theme *theme) {
    Style* style = &theme->syntax_variable;
    if (capture_name) {
        StyleMapping* found = bsearch(
            capture_name,
            style_map,
            sizeof(style_map) / sizeof(style_map[0]),
            sizeof(StyleMapping),
            compare_style_mappings
        );

        if (found) {
            return (Style*)((char*)theme + found->style_offset);
        }
        log_warning("theme.theme_get_capture_style: unrecognized capture_name %s", capture_name);
    }
    return style;
}

static int hex_to_rgb(const char* hex, unsigned char* r, unsigned char* g, unsigned char* b) {
    if (!hex || hex[0] != '#' || strlen(hex) != 7) return 0;
    unsigned int rgb;
    if (sscanf(hex + 1, "%x", &rgb) != 1) return 0;
    *r = (rgb >> 16) & 0xFF;
    *g = (rgb >> 8) & 0xFF;
    *b = rgb & 0xFF;
    return 1;
}

static void set_default_style(Style* style) {
    style->fg_r = 255;
    style->fg_g = 255;
    style->fg_b = 255;
    style->bg_r = 0;
    style->bg_g = 0;
    style->bg_b = 0;
    style->bold = 0;
    style->italic = 0;
    style->underline = 0;
}

static void parse_style(toml_datum_t root, const char* key, Style* style) {
    set_default_style(style);
    char full_key[256];

    snprintf(full_key, sizeof(full_key), "%s.fg", key);
    toml_datum_t fg_data = toml_seek(root, full_key);
    if (fg_data.type == TOML_STRING) {
        if (!hex_to_rgb(fg_data.u.s, &style->fg_r, &style->fg_g, &style->fg_b)) {
            log_warning("theme.parse_style: invalid fg hex color for %s: %s", key, fg_data.u.s);
        }
    }

    snprintf(full_key, sizeof(full_key), "%s.bg", key);
    toml_datum_t bg_data = toml_seek(root, full_key);
    if (bg_data.type == TOML_STRING) {
        if (!hex_to_rgb(bg_data.u.s, &style->bg_r, &style->bg_g, &style->bg_b)) {
            log_warning("theme.parse_style: invalid bg hex color for %s: %s", key, fg_data.u.s);
        }
    }

    snprintf(full_key, sizeof(full_key), "%s.italic", key);
    toml_datum_t italic_data = toml_seek(root, full_key);
    if (italic_data.type == TOML_BOOLEAN) {
        style->italic = italic_data.u.boolean ? 1 : 0; 
    }

    snprintf(full_key, sizeof(full_key), "%s.bold", key);
    toml_datum_t bold_data = toml_seek(root, full_key);
    if (bold_data.type == TOML_BOOLEAN) {
        style->bold = bold_data.u.boolean ? 1 : 0; 
    }

    snprintf(full_key, sizeof(full_key), "%s.underline", key);
    toml_datum_t underline_data = toml_seek(root, full_key);
    if (underline_data.type == TOML_BOOLEAN) {
        style->underline = underline_data.u.boolean ? 1 : 0;
    }
}

void theme_load(const char* filename, Theme *theme) {
    toml_result_t result = toml_parse_file_ex(filename);
    if (!result.ok) {
        log_warning("theme.theme_load: cannot parse %s - %s", filename, result.errmsg);
        return;
    }

    parse_style(result.toptab, "syntax.keyword", &theme->syntax_keyword);
    parse_style(result.toptab, "syntax.keyword-storage-type", &theme->syntax_keyword_storage_type);
    parse_style(result.toptab, "syntax.keyword-storage-modifier", &theme->syntax_keyword_storage_modifier);
    parse_style(result.toptab, "syntax.keyword-control", &theme->syntax_keyword_control);
    parse_style(result.toptab, "syntax.keyword-control-repeat", &theme->syntax_keyword_control_repeat);
    parse_style(result.toptab, "syntax.keyword-control-return", &theme->syntax_keyword_control_return);
    parse_style(result.toptab, "syntax.keyword-control-conditional", &theme->syntax_keyword_control_conditional);
    parse_style(result.toptab, "syntax.keyword-directive", &theme->syntax_keyword_directive);
    parse_style(result.toptab, "syntax.function-special", &theme->syntax_function_special);
    parse_style(result.toptab, "syntax.function-builtin", &theme->syntax_function_builtin);
    parse_style(result.toptab, "syntax.string", &theme->syntax_string);
    parse_style(result.toptab, "syntax.constant-character", &theme->syntax_constant_character);
    parse_style(result.toptab, "syntax.type-enum-variant", &theme->syntax_type_enum_variant);
    parse_style(result.toptab, "syntax.constant-character-escape", &theme->syntax_constant_character_escape);
    parse_style(result.toptab, "syntax.constant-numeric", &theme->syntax_constant_numeric);
    parse_style(result.toptab, "syntax.constant-builtin-boolean", &theme->syntax_constant_builtin_boolean);
    parse_style(result.toptab, "syntax.constant", &theme->syntax_constant);
    parse_style(result.toptab, "syntax.variable", &theme->syntax_variable);
    parse_style(result.toptab, "syntax.variable-parameter", &theme->syntax_variable_parameter);
    parse_style(result.toptab, "syntax.variable-other-member", &theme->syntax_variable_other_member);
    parse_style(result.toptab, "syntax.function", &theme->syntax_function);
    parse_style(result.toptab, "syntax.type", &theme->syntax_type);
    parse_style(result.toptab, "syntax.type-builtin", &theme->syntax_type_builtin);
    parse_style(result.toptab, "syntax.attribute", &theme->syntax_attribute);
    parse_style(result.toptab, "syntax.punctuation", &theme->syntax_punctuation);
    parse_style(result.toptab, "syntax.punctuation-delimiter", &theme->syntax_punctuation_delimiter);
    parse_style(result.toptab, "syntax.punctuation-bracket", &theme->syntax_punctuation_bracket);
    parse_style(result.toptab, "syntax.comment", &theme->syntax_comment);
    parse_style(result.toptab, "syntax.operator", &theme->syntax_operator);
    parse_style(result.toptab, "syntax.label", &theme->syntax_label);
    parse_style(result.toptab, "syntax.info", &theme->syntax_info);
    parse_style(result.toptab, "syntax.warning", &theme->syntax_warning);
    parse_style(result.toptab, "syntax.error", &theme->syntax_error);

    parse_style(result.toptab, "picker.item-text", &theme->picker_item_text);
    parse_style(result.toptab, "picker.item-text-highlight", &theme->picker_item_text_highlight);

    parse_style(result.toptab, "content.background", &theme->content_background);
    parse_style(result.toptab, "content.cursor-line", &theme->content_cursor_line);
    parse_style(result.toptab, "content.line-number", &theme->content_line_number);
    parse_style(result.toptab, "content.line-number-active", &theme->content_line_number_active);
    parse_style(result.toptab, "content.line-number-sticky", &theme->content_line_number_sticky);

    parse_style(result.toptab, "statusline.mode-insert", &theme->statusline_mode_insert);
    parse_style(result.toptab, "statusline.mode-normal", &theme->statusline_mode_normal);
    parse_style(result.toptab, "statusline.text", &theme->statusline_text);

    toml_free(result);
}
