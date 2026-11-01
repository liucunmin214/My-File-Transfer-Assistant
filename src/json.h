/*
 * json.h - 轻量 JSON 解析头文件（仅支持对象/字符串/数字/布尔/数组，足够配置文件用）
 */
#ifndef FT_JSON_H
#define FT_JSON_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct json_value json_value;

enum json_type {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
};

struct json_value {
    enum json_type type;
    union {
        int           bool_val;
        double        num_val;
        wchar_t      *str_val;
        struct { json_value **items; int count; } arr;
        struct { wchar_t **keys; json_value **values; int count; } obj;
    } v;
};

/* 从宽字符串解析 JSON */
json_value *json_parse(const wchar_t *s);

/* 从文件解析 JSON（UTF-8） */
json_value *json_parse_file(const wchar_t *path);

/* 写 JSON 到文件（UTF-8） */
int json_write_file(const wchar_t *path, const json_value *root);

/* 对象查找 */
json_value *json_object_get(const json_value *obj, const wchar_t *key);
wchar_t    *json_object_get_str(const json_value *obj, const wchar_t *key, const wchar_t *def);
int         json_object_get_int(const json_value *obj, const wchar_t *key, int def);
int         json_object_get_bool(const json_value *obj, const wchar_t *key, int def);

/* 新构建函数 */
json_value *json_new_object(void);
json_value *json_new_string(const wchar_t *s);
json_value *json_new_number(double n);
json_value *json_new_bool(int b);
void        json_object_set(json_value *obj, const wchar_t *key, json_value *val);

/* 释放 */
void json_free(json_value *v);

#ifdef __cplusplus
}
#endif

#endif /* FT_JSON_H */
