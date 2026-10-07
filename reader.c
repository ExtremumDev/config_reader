#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "configer.h"

#define MAX_KEY_LENGTH 50
#define BASE_VALUE_BUFFER_SIZE 64
#define MAX_VALUE_BUFFER_SIZE 1024

#define READ_BUFFER_SIZE 10

#define BASE_ARRAY_SIZE 1

enum reader_states{
    init,
    lvalue,
    equal,
    rvalue_start,
    next_arr_el,
    string_value,
    error
};

struct array_skeleton{
    char **list;
    size_t next_idx;
    size_t size;
};

struct reader_fsm{
    enum reader_states state;
    char *buffer;
    size_t buffer_pointer;
    size_t current_buffer_size;
    struct array_skeleton current_array;
    char arr_el_quote;
};

/* Temporary configuration structure(exists only in parsing time)*/
struct config {
    struct setting *first_config_el;
    struct setting *last_config_el;
};

void create_arr(struct array_skeleton *a){
    a->arr = malloc(sizeof(char *) * BASE_ARRAY_SIZE);
    a->size = BASE_ARRAY_SIZE;
    a->next_idx = 0;
}

void add_to_arr(struct array_skeleton *a, char *new_val)
{
    a->arr[a->next_idx] = new_val;
    a->next_idx++;
    if(a->next_idx >= a->size){
        char **new_arr = malloc(sizeof(char*) * (a->size + 10));
        memcpy(new_arr, a->arr, a->next_idx * sizeof(char*));
        free(a->arr);
        a->arr = new_arr;
    }

}

void prepare_arr(struct array_skeleton *a)
{
    if(a->next_idx + 1 != a->size){
        char **res_arr = malloc(sizeof(char*) * (a->next_idx + 1));
        memcpy(res_arr, a->arr, a->next_idx * sizeof(char*));
        free(a->arr);
        a->arr = res_arr;
    }
    a->arr[a->next_idx] = NULL;
}

struct setting *create_config_el(struct config *conf){
    struct setting *new_el = malloc(sizeof(struct setting));
    new_el->next = NULL;
    new_el->value.string_value = NULL;
    new_el->type = string_value;

    new_el->key = malloc(MAX_KEY_LENGTH + 1);

    if(conf->last_config_el != NULL){
        conf->last_config_el->next = new_el;
    }

    if(conf->first_config_el == NULL){
        conf->first_config_el = new_el;
    }

    conf->last_config_el = new_el;
    return new_el;
}

void create_buffer(struct reader_fsm *fsm, size_t base_size)
{
    fsm->buffer = malloc(base_size);
    fsm->buffer_pointer = 0;
    fsm->current_buffer_size = base_size;
}

void prepare_string_value(char **value_buf, size_t value_size, size_t buf_size) // Value size: character size + zero byte
/*Slice buffer according to the exact size of value and set zero byte to the end of buffer*/
{
    if(buf_size - value_size > 10){
        *value_buf = realloc(*value_buf, value_size);
    }
    (*value_buf)[value_size] = 0; /* Byte at <value_size> index is next to value buffer - place for zero byte */
}


int process_config(char const *buffer, size_t buf_size, struct config *conf, struct reader_fsm* fsm)
{
    int i;
    for(i = 0; i < buf_size; i++){
        switch(fsm->state){
            case error:
                if(buffer[i] == '\n'){
                    fsm->state = init;
                }
                continue;
            case init:
                if(buffer[i] == ' ' || buffer[i] == '\n' || buffer[i] == '\t'){
                    continue;
                }

                // New setting
                fsm->state = lvalue;
                struct setting *new_setting = create_config_el(conf);
                fsm->buffer_pointer = 0;
                new_setting->key[fsm->buffer_pointer] = buffer[i];
                fsm->buffer_pointer++;

                break;
            case lvalue:
                if(conf->last_config_el == NULL){ // FUTURE: COMPLETE ERROR HANLDING
                    continue;
                }

                if(buffer[i] == ' ' || buffer[i] == '\t'){
                    prepare_string_value(
                        &(conf->last_config_el->key),
                        fsm->buffer_pointer,
                        MAX_KEY_LENGTH + 1
                    );
                    fsm->state = equal;
                    continue;
                }

                if(buffer[i] == '='){
                    prepare_string_value(
                        &(conf->last_config_el->key),
                        fsm->buffer_pointer,
                        MAX_KEY_LENGTH + 1
                    );
                    fsm->state = rvalue_start;

                    continue;
                }

                if(fsm->buffer_pointer > MAX_KEY_LENGTH){ // Kepp space for zeror byte(which max in MAX_KEY_LENGTH + 1) and dont leave buffer's capacity
                    // FUTURE: ERROR: MAX KEY LENGTH REACHED
                    continue;
                }

                if(buffer[i] == '\n'){
                    // FUTURE: ERROR: BREAK LINE BEFORE VALUE REACHED
                }

                conf->last_config_el->key[fsm->buffer_pointer] = buffer[i];
                fsm->buffer_pointer++;
                break;
            case equal:
                if(buffer[i] == ' ' || buffer[i] == '\t'){
                    continue;
                }

                if(buffer[i] == '\n'){
                    // FUTURE: ERROR: BREAK LINE BEFORE VALUE REACHED
                }

                if(buffer[i] == '='){
                    fsm->state = rvalue_start;
                    continue;
                }

                // Not space character and not equal char -> error

            case rvalue_start:
                if(buffer[i] == ' ' || buffer[i] == '\t'){
                    continue;
                }
                if(buffer[i] == '\n'){
                    fsm->state = error;
                    continue;
                }
                if(buffer[i] == '('){
                   conf->last_config_el->type = list; 
                   create_array(&(fsm->current_array));
                }

                if(buffer[i] == '"' || buffer[i] == '\''){
                    fsm->arr_el_quote = buffer[i];
                    fsm->state = string_value;
                    continue;
                }
                
                break;
            case next_arr_el:
                if(buffer[i] == '"' || buffer[i] == '\''){
                    fsm->arr_el_quote = buffer[i];
                    fsm->state = string_value;
                }
                if(buffer[i] == ')'){
                    prepare_array(fsm->current_array);

                    fsm->state = init;
                }
                continue;
            case string_value:
                if(buffer[i] == '\n'){
                    prepare_string_value(
                        &(conf->last_config_el->value),
                        fsm->buffer_pointer,
                        fsm->current_buffer_size
                    );
                    fsm->buffer_pointer = 0;
                    fsm->state = init;
                    continue;
                }

                if(fsm->buffer_pointer >= fsm->current_buffer_size - 1){ /* We should keep space for zero byte, so extend when last byte will be occupied*/
                    fsm->current_buffer_size = fsm->current_buffer_size * 2;

                    if(fsm->current_buffer_size > MAX_VALUE_BUFFER_SIZE){
                        if(fsm->current_buffer_size == MAX_VALUE_BUFFER_SIZE){
                            // FUTURE: ERROR: MAX VALUE REACHED
                            fsm->state = error;
                            continue;
                        }
                        fsm->current_buffer_size = MAX_VALUE_BUFFER_SIZE;
                    }
                    conf->last_config_el->value = realloc(conf->last_config_el->value, fsm->current_buffer_size);
                }

                conf->last_config_el->value[fsm->buffer_pointer] = buffer[i];
                fsm->buffer_pointer++;
        }
    }

    return 0;
}


struct config_section *parse_config(const char *config_file_name)
{
    struct config_section *conf_section;
    struct config conf;
    char *buffer; size_t data_size;
    struct reader_fsm fsm;
    int fd = open(config_file_name, O_RDONLY);

    if(fd == -1){
        return NULL;
    }

    buffer = malloc(READ_BUFFER_SIZE);

    conf.first_config_el = conf.last_config_el = NULL;
    fsm.state = init;

    conf_section = malloc(sizeof(struct config_section));
    conf_section->first_setting = NULL;
    
    while((data_size = read(fd, buffer, READ_BUFFER_SIZE)) != 0){
        int res = process_config(buffer, data_size, &conf, &fsm);


        res = lseek(fd, data_size, SEEK_CUR);

        printf("%d %d\n", data_size, res);

        if(res == -1){
            conf_section->first_setting = conf.first_config_el;
            return conf_section;
        }
    }

    if(fsm.state == string_value){
        prepare_string_value(
            &(conf.last_config_el->value),
            fsm.buffer_pointer,
            fsm.current_buffer_size
        );
    }

    conf_section->first_setting = conf.first_config_el;
    return conf_section;
}

void delete_config_section(struct config_section* c)
{
    struct setting *s = c->first_setting;
    while(s != NULL){
        if(s->key != NULL){
            free(s->key);
        }

        switch (s->type)
        {
            case idle:
                if(s->value != NULL){
                    free(s->value);
                }
                break;
        }
        s = s->next;
    }
    free(c);
}

#ifdef MAIN_FL

int main(int argc, char **argv)
{
    struct config_section *c = parse_config(argv[1]);
    struct setting *el = c->first_setting;
    while(el != NULL){
        printf("%s = %s\n", el->key, el->value);
        el = el->next;
    }
    delete_config_section(c);

    return 0;
}
#endif
