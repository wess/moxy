#ifndef JSON_H
#define JSON_H

/* json node types */
#define JN_NULL 0
#define JN_BOOL 1
#define JN_NUM  2
#define JN_STR  3
#define JN_ARR  4
#define JN_OBJ  5

typedef struct JsonNode {
    int type;
    int bval;
    double nval;
    char *sval;
    struct JsonNode **items;
    int ilen, icap;
    char **keys;
    struct JsonNode **vals;
    int olen, ocap;
} JsonNode;

/* constructors */
JsonNode *json_null(void);
JsonNode *json_bool(int val);
JsonNode *json_number(double val);
JsonNode *json_int(int val);
JsonNode *json_string(const char *val);
JsonNode *json_array(void);
JsonNode *json_object(void);

/* mutators */
void json_array_push(JsonNode *arr, JsonNode *val);
void json_object_set(JsonNode *obj, const char *key, JsonNode *val);

/* accessors */
JsonNode *json_object_get(JsonNode *obj, const char *key);
JsonNode *json_array_get(JsonNode *arr, int idx);
const char *json_string_val(JsonNode *n);
int json_int_val(JsonNode *n);
int json_array_len(JsonNode *arr);

/* parse / serialize / free */
JsonNode *json_parse(const char *src);
char *json_serialize(JsonNode *n);
void json_free(JsonNode *n);

/* document store */
void doc_open(const char *uri, const char *content, int version);
void doc_close(const char *uri);
const char *doc_content(const char *uri);

/* utilities */
JsonNode *run_diagnostics(const char *content, const char *moxy_path);
JsonNode *scan_symbols(const char *content);
void word_at_pos(const char *content, int line, int col, char *buf, int bufsz);

/* json-rpc framing */
void jrpc_send(JsonNode *msg);

#endif
