#ifndef THEME_H
#define THEME_H

typedef struct {
    unsigned char fg_r;
    unsigned char fg_g;
    unsigned char fg_b;
    unsigned char bg_r;
    unsigned char bg_g;
    unsigned char bg_b;
    unsigned char bold;
    unsigned char italic;
    unsigned char priority;
} Style;


// Theme struct holding RGB colors for each capture type
typedef struct {
    Style syntax_keyword;
    Style syntax_keyword_storage_type;
    Style syntax_keyword_storage_modifier;
    Style syntax_keyword_control;
    Style syntax_keyword_control_repeat;
    Style syntax_keyword_control_return;
    Style syntax_keyword_control_conditional;
    Style syntax_keyword_directive;
    Style syntax_function_special;
    Style syntax_string;
    Style syntax_constant_character;
    Style syntax_type_enum_variant;
    Style syntax_constant_character_escape;
    Style syntax_constant_numeric;
    Style syntax_constant_builtin_boolean;
    Style syntax_constant;
    Style syntax_variable;
    Style syntax_variable_parameter;
    Style syntax_variable_other_member;
    Style syntax_function;
    Style syntax_function_builtin;
    Style syntax_type;
    Style syntax_type_builtin;
    Style syntax_attribute;
    Style syntax_punctuation;
    Style syntax_punctuation_delimiter;
    Style syntax_punctuation_bracket;
    Style syntax_comment;
    Style syntax_operator;
    Style syntax_label;
    Style syntax_info;
    Style syntax_warning;
    Style syntax_error;

    Style picker_item_text;
    Style picker_item_text_highlight;

    Style content_line_number;
    Style content_line_number_active;
    Style content_line_number_sticky;
    Style content_cursor_line;
    Style content_background;
 
    Style statusline_mode_insert;
    Style statusline_mode_normal;
    Style statusline_text;
} Theme;

// Load and parse theme from a TOML file
// Returns 1 on success, 0 on failure
void theme_load(const char* filename, Theme *theme);

Style *theme_get_capture_style(const char* capture_name, Theme *theme);

#endif
