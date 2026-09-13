#ifndef CONFIGER_H_SENTRY
#define CONFIGER_H_SENTRY


typedef enum {
    idle,
    list
} config_el_type;


typedef union  {
    char *string_value;
    char **value_list;
} setting_value;

struct setting {
    struct setting *next;
    char *key;
    config_el_type type;
    setting_value value;
};

struct config_section{
    struct setting *first_setting;
};


struct config_section *parse_config(const char *config_file_name);

void delete_config_section(struct config_section*);
#endif