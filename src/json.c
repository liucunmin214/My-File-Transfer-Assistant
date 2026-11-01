/*
 * json.c - 极简 JSON 解析器与序列化器（wchar_t 宽字符串，UTF-8 文件读写）
 * 仅支持：对象 / 字符串 / 数字 / 布尔 / 数组 / null。足以处理 config.json。
 */
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#include <windows.h>

/* ================== UTF-8 <-> wchar_t 辅助 ================== */
static int utf8_to_wchar(const char *u8, wchar_t *w, int wmax) {
    int i = 0, wi = 0;
    while (u8[i]) {
        unsigned c = (unsigned char)u8[i];
        unsigned cp = 0, bytes = 0;
        if (c < 0x80)       { cp = c; bytes = 1; }
        else if (c < 0xC0)  return -1;
        else if (c < 0xE0)  { cp = c & 0x1F; bytes = 2; }
        else if (c < 0xF0)  { cp = c & 0x0F; bytes = 3; }
        else if (c < 0xF8)  { cp = c & 0x07; bytes = 4; }
        else return -1;
        for (int j = 1; j < bytes; j++) {
            unsigned cc = (unsigned char)u8[i+j];
            if ((cc & 0xC0) != 0x80) return -1;
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += bytes;
        if (wi + 2 >= wmax) return -1;
        if (cp < 0x10000) { w[wi++] = (wchar_t)cp; }
        else { w[wi++] = (wchar_t)(0xD800 + ((cp - 0x10000) >> 10));
               w[wi++] = (wchar_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)); }
    }
    w[wi] = 0;
    return wi;
}

static int wchar_to_utf8(const wchar_t *w, char *u8, int umax) {
    int ui = 0, i = 0;
    while (w[i]) {
        unsigned cp;
        wchar_t ch = w[i++];
        if (ch >= 0xD800 && ch <= 0xDBFF && w[i] >= 0xDC00 && w[i] <= 0xDFFF) {
            cp = 0x10000 + ((ch & 0x3FF) << 10) + (w[i] & 0x3FF);
            i++;
        } else cp = ch;
        char tmp[4]; int n = 0;
        if      (cp < 0x80)     { tmp[0] = (char)cp; n = 1; }
        else if (cp < 0x800)    { tmp[0] = (char)(0xC0 | (cp>>6));
                                  tmp[1] = (char)(0x80 | (cp&0x3F)); n = 2; }
        else if (cp < 0x10000)  { tmp[0] = (char)(0xE0 | (cp>>12));
                                  tmp[1] = (char)(0x80 | ((cp>>6)&0x3F));
                                  tmp[2] = (char)(0x80 | (cp&0x3F)); n = 3; }
        else                    { tmp[0] = (char)(0xF0 | (cp>>18));
                                  tmp[1] = (char)(0x80 | ((cp>>12)&0x3F));
                                  tmp[2] = (char)(0x80 | ((cp>>6)&0x3F));
                                  tmp[3] = (char)(0x80 | (cp&0x3F)); n = 4; }
        if (ui + n >= umax) return -1;
        memcpy(u8 + ui, tmp, n); ui += n;
    }
    u8[ui] = 0;
    return ui;
}

/* ================== 解析器 ================== */
typedef struct {
    const wchar_t *s;
    int p;
} jp;

static void jskip(jp *j) {
    while (j->s[j->p]) {
        wchar_t c = j->s[j->p];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') j->p++;
        else if (c == L'/') {
            if (j->s[j->p+1] == L'/') { while (j->s[j->p] && j->s[j->p] != L'\n') j->p++; }
            else if (j->s[j->p+1] == L'*') { j->p += 2;
                while (j->s[j->p] && !(j->s[j->p] == L'*' && j->s[j->p+1] == L'/')) j->p++;
                if (j->s[j->p]) j->p += 2;
            } else break;
        } else break;
    }
}

static json_value *parse_value(jp *j);

static wchar_t *parse_hex4(jp *j) {
    static wchar_t dummy;
    int v = 0;
    for (int k = 0; k < 4; k++) {
        wchar_t c = j->s[j->p++];
        v <<= 4;
        if      (c >= L'0' && c <= L'9') v |= (c - L'0');
        else if (c >= L'a' && c <= L'f') v |= (c - L'a' + 10);
        else if (c >= L'A' && c <= L'F') v |= (c - L'A' + 10);
        else return &dummy;
    }
    static wchar_t buf[2] = {0};
    buf[0] = (wchar_t)v; buf[1] = 0;
    return buf;
}

static wchar_t *parse_string_raw(jp *j) {
    if (j->s[j->p] != L'"') return NULL;
    j->p++;
    int cap = 32, len = 0;
    wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t));
    while (j->s[j->p]) {
        wchar_t c = j->s[j->p++];
        if (c == L'"') {
            buf[len] = 0;
            return buf;
        }
        if (c == L'\\') {
            wchar_t ec = j->s[j->p++];
            wchar_t ins[8] = {0};
            switch (ec) {
                case L'"':  ins[0] = L'"'; break;
                case L'\\': ins[0] = L'\\'; break;
                case L'/':  ins[0] = L'/'; break;
                case L'n':  ins[0] = L'\n'; break;
                case L'r':  ins[0] = L'\r'; break;
                case L't':  ins[0] = L'\t'; break;
                case L'b':  ins[0] = L'\b'; break;
                case L'f':  ins[0] = L'\f'; break;
                case L'u':  wcscpy_s(ins, 8, parse_hex4(j)); break;
                default:    ins[0] = ec;
            }
            int sl = (int)wcslen(ins);
            if (len + sl + 1 > cap) { cap *= 2; buf = (wchar_t *)realloc(buf, cap * sizeof(wchar_t)); }
            wcscpy_s(buf + len, cap - len, ins);
            len += sl;
        } else {
            if (len + 2 > cap) { cap *= 2; buf = (wchar_t *)realloc(buf, cap * sizeof(wchar_t)); }
            buf[len++] = c;
            buf[len] = 0;
        }
    }
    free(buf); return NULL;
}

static json_value *parse_string(jp *j) {
    wchar_t *s = parse_string_raw(j);
    if (!s) return NULL;
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    v->type = JSON_STRING;
    v->v.str_val = s;
    return v;
}

static json_value *parse_number(jp *j) {
    int start = j->p;
    if (j->s[j->p] == L'-') j->p++;
    while (iswdigit(j->s[j->p])) j->p++;
    int is_float = 0;
    if (j->s[j->p] == L'.') {
        is_float = 1; j->p++;
        while (iswdigit(j->s[j->p])) j->p++;
    }
    if (j->s[j->p] == L'e' || j->s[j->p] == L'E') {
        is_float = 1; j->p++;
        if (j->s[j->p] == L'+' || j->s[j->p] == L'-') j->p++;
        while (iswdigit(j->s[j->p])) j->p++;
    }
    wchar_t tmp[128];
    int len = j->p - start;
    if (len >= 128) len = 127;
    wcsncpy_s(tmp, 128, j->s + start, len);
    tmp[len] = 0;
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    v->type = JSON_NUMBER;
    if (is_float) v->v.num_val = wcstod(tmp, NULL);
    else          v->v.num_val = (double)_wtoi64(tmp);
    return v;
}

static json_value *parse_array(jp *j) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    v->type = JSON_ARRAY;
    v->v.arr.items = NULL; v->v.arr.count = 0;
    j->p++;
    jskip(j);
    if (j->s[j->p] == L']') { j->p++; return v; }
    int cap = 4;
    v->v.arr.items = (json_value **)malloc(cap * sizeof(json_value *));
    for (;;) {
        jskip(j);
        json_value *item = parse_value(j);
        if (!item) { json_free(v); return NULL; }
        if (v->v.arr.count == cap) { cap *= 2; v->v.arr.items = (json_value **)realloc(v->v.arr.items, cap * sizeof(json_value*)); }
        v->v.arr.items[v->v.arr.count++] = item;
        jskip(j);
        if (j->s[j->p] == L',') { j->p++; continue; }
        if (j->s[j->p] == L']') { j->p++; break; }
        json_free(v); return NULL;
    }
    return v;
}

static json_value *parse_object(jp *j) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    v->type = JSON_OBJECT;
    v->v.obj.keys = NULL; v->v.obj.values = NULL; v->v.obj.count = 0;
    j->p++;
    jskip(j);
    if (j->s[j->p] == L'}') { j->p++; return v; }
    int cap = 4;
    v->v.obj.keys = (wchar_t **)malloc(cap * sizeof(wchar_t *));
    v->v.obj.values = (json_value **)malloc(cap * sizeof(json_value *));
    for (;;) {
        jskip(j);
        wchar_t *key = parse_string_raw(j);
        if (!key) { json_free(v); return NULL; }
        jskip(j);
        if (j->s[j->p++] != L':') { free(key); json_free(v); return NULL; }
        jskip(j);
        json_value *val = parse_value(j);
        if (!val) { free(key); json_free(v); return NULL; }
        if (v->v.obj.count == cap) {
            cap *= 2;
            v->v.obj.keys   = (wchar_t **)realloc(v->v.obj.keys, cap * sizeof(wchar_t*));
            v->v.obj.values = (json_value **)realloc(v->v.obj.values, cap * sizeof(json_value*));
        }
        v->v.obj.keys[v->v.obj.count] = key;
        v->v.obj.values[v->v.obj.count] = val;
        v->v.obj.count++;
        jskip(j);
        if (j->s[j->p] == L',') { j->p++; continue; }
        if (j->s[j->p] == L'}') { j->p++; break; }
        json_free(v); return NULL;
    }
    return v;
}

static json_value *parse_value(jp *j) {
    jskip(j);
    wchar_t c = j->s[j->p];
    if (c == L'{') return parse_object(j);
    if (c == L'[') return parse_array(j);
    if (c == L'"') return parse_string(j);
    if (c == L'-' || iswdigit(c)) return parse_number(j);
    if (!wcsncmp(j->s + j->p, L"true", 4)) { j->p += 4;
        json_value *v = (json_value*)calloc(1, sizeof(*v));
        v->type = JSON_BOOL; v->v.bool_val = 1; return v; }
    if (!wcsncmp(j->s + j->p, L"false", 5)) { j->p += 5;
        json_value *v = (json_value*)calloc(1, sizeof(*v));
        v->type = JSON_BOOL; v->v.bool_val = 0; return v; }
    if (!wcsncmp(j->s + j->p, L"null", 4)) { j->p += 4;
        json_value *v = (json_value*)calloc(1, sizeof(*v));
        v->type = JSON_NULL; return v; }
    return NULL;
}

json_value *json_parse(const wchar_t *s) {
    jp j = { s, 0 };
    return parse_value(&j);
}

json_value *json_parse_file(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    char *u8 = (char *)malloc((size_t)sz + 4);
    fread(u8, 1, (size_t)sz, f);
    u8[sz] = 0; fclose(f);
    int skip = 0;
    if ((unsigned char)u8[0] == 0xEF && (unsigned char)u8[1] == 0xBB && (unsigned char)u8[2] == 0xBF) skip = 3;
    int wcap = (int)((size_t)sz + 4) * 2;
    wchar_t *w = (wchar_t *)malloc((size_t)wcap * sizeof(wchar_t));
    if (utf8_to_wchar(u8 + skip, w, wcap) < 0) { free(u8); free(w); return NULL; }
    free(u8);
    json_value *r = json_parse(w);
    free(w);
    return r;
}

json_value *json_object_get(const json_value *obj, const wchar_t *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < obj->v.obj.count; i++)
        if (!wcscmp(obj->v.obj.keys[i], key)) return obj->v.obj.values[i];
    return NULL;
}
wchar_t *json_object_get_str(const json_value *obj, const wchar_t *key, const wchar_t *def) {
    json_value *v = json_object_get(obj, key);
    if (v && v->type == JSON_STRING) return v->v.str_val;
    return (wchar_t *)def;
}
int json_object_get_int(const json_value *obj, const wchar_t *key, int def) {
    json_value *v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) return (int)v->v.num_val;
    if (v && v->type == JSON_BOOL)   return v->v.bool_val ? 1 : 0;
    return def;
}
int json_object_get_bool(const json_value *obj, const wchar_t *key, int def) {
    json_value *v = json_object_get(obj, key);
    if (v && v->type == JSON_BOOL)   return v->v.bool_val ? 1 : 0;
    if (v && v->type == JSON_NUMBER) return v->v.num_val != 0 ? 1 : 0;
    return def;
}

json_value *json_new_object(void) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    v->type = JSON_OBJECT; return v;
}
json_value *json_new_string(const wchar_t *s) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    v->type = JSON_STRING;
    v->v.str_val = (wchar_t *)malloc((wcslen(s) + 1) * sizeof(wchar_t));
    wcscpy_s(v->v.str_val, wcslen(s) + 1, s);
    return v;
}
json_value *json_new_number(double n) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    v->type = JSON_NUMBER; v->v.num_val = n; return v;
}
json_value *json_new_bool(int b) {
    json_value *v = (json_value *)calloc(1, sizeof(*v));
    v->type = JSON_BOOL; v->v.bool_val = b ? 1 : 0; return v;
}
void json_object_set(json_value *obj, const wchar_t *key, json_value *val) {
    if (!obj || obj->type != JSON_OBJECT) return;
    for (int i = 0; i < obj->v.obj.count; i++) {
        if (!wcscmp(obj->v.obj.keys[i], key)) {
            free(obj->v.obj.keys[i]);
            json_free(obj->v.obj.values[i]);
            obj->v.obj.keys[i] = (wchar_t *)malloc((wcslen(key) + 1) * sizeof(wchar_t));
            wcscpy_s(obj->v.obj.keys[i], wcslen(key) + 1, key);
            obj->v.obj.values[i] = val;
            return;
        }
    }
    obj->v.obj.count++;
    obj->v.obj.keys   = (wchar_t **)realloc(obj->v.obj.keys, obj->v.obj.count * sizeof(wchar_t *));
    obj->v.obj.values = (json_value **)realloc(obj->v.obj.values, obj->v.obj.count * sizeof(json_value *));
    int idx = obj->v.obj.count - 1;
    obj->v.obj.keys[idx] = (wchar_t *)malloc((wcslen(key) + 1) * sizeof(wchar_t));
    wcscpy_s(obj->v.obj.keys[idx], wcslen(key) + 1, key);
    obj->v.obj.values[idx] = val;
}

void json_free(json_value *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING: free(v->v.str_val); break;
        case JSON_ARRAY:
            for (int i = 0; i < v->v.arr.count; i++) json_free(v->v.arr.items[i]);
            free(v->v.arr.items); break;
        case JSON_OBJECT:
            for (int i = 0; i < v->v.obj.count; i++) {
                free(v->v.obj.keys[i]);
                json_free(v->v.obj.values[i]);
            }
            free(v->v.obj.keys); free(v->v.obj.values); break;
        default: break;
    }
    free(v);
}

static void append_c(FILE *f, const char *s) { fputs(s, f); }

static void write_str(FILE *f, const wchar_t *s) {
    append_c(f, "\"");
    for (int i = 0; s[i]; i++) {
        wchar_t c = s[i];
        char tmp[8];
        switch (c) {
            case L'"':  append_c(f, "\\\""); break;
            case L'\\': append_c(f, "\\\\"); break;
            case L'\n': append_c(f, "\\n"); break;
            case L'\r': append_c(f, "\\r"); break;
            case L'\t': append_c(f, "\\t"); break;
            case L'\b': append_c(f, "\\b"); break;
            case L'\f': append_c(f, "\\f"); break;
            default:
                if (c < 0x20) {
                    _snprintf_s(tmp, 8, _TRUNCATE, "\\u%04X", c);
                    append_c(f, tmp);
                } else {
                    char u8[8];
                    wchar_t one[2] = {c, 0};
                    wchar_to_utf8(one, u8, 8);
                    append_c(f, u8);
                }
        }
    }
    append_c(f, "\"");
}

static void write_val(FILE *f, const json_value *v, int indent) {
    if (!v) { append_c(f, "null"); return; }
    switch (v->type) {
        case JSON_NULL:   append_c(f, "null"); break;
        case JSON_BOOL:   append_c(f, v->v.bool_val ? "true" : "false"); break;
        case JSON_NUMBER: {
            char buf[64];
            if (v->v.num_val == floor(v->v.num_val) && fabs(v->v.num_val) < 1e18)
                _snprintf_s(buf, 64, _TRUNCATE, "%.0f", v->v.num_val);
            else
                _snprintf_s(buf, 64, _TRUNCATE, "%.10g", v->v.num_val);
            append_c(f, buf);
            break;
        }
        case JSON_STRING: write_str(f, v->v.str_val); break;
        case JSON_ARRAY:
            append_c(f, "[");
            for (int i = 0; i < v->v.arr.count; i++) {
                if (i) append_c(f, ", ");
                write_val(f, v->v.arr.items[i], indent);
            }
            append_c(f, "]"); break;
        case JSON_OBJECT:
            append_c(f, "{\n");
            for (int i = 0; i < v->v.obj.count; i++) {
                for (int j = 0; j < indent + 2; j++) append_c(f, " ");
                write_str(f, v->v.obj.keys[i]);
                append_c(f, ": ");
                write_val(f, v->v.obj.values[i], indent + 2);
                if (i < v->v.obj.count - 1) append_c(f, ",");
                append_c(f, "\n");
            }
            for (int j = 0; j < indent; j++) append_c(f, " ");
            append_c(f, "}"); break;
    }
}

int json_write_file(const wchar_t *path, const json_value *root) {
    wchar_t *dup = (wchar_t *)malloc((wcslen(path) + 1) * sizeof(wchar_t));
    wcscpy_s(dup, wcslen(path) + 1, path);
    wchar_t *slash = wcsrchr(dup, L'\\');
    if (slash) { *slash = 0; CreateDirectoryW(dup, NULL); }
    free(dup);

    FILE *f = _wfopen(path, L"wb, ccs=UTF-8");
    if (!f) return -1;
    fputc(0xEF, f); fputc(0xBB, f); fputc(0xBF, f);
    write_val(f, root, 0);
    fclose(f);
    return 0;
}
