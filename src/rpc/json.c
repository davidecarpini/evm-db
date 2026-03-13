#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/*
 * Minimal JSON parser for JSON-RPC requests.
 *
 * We only need to extract:
 * - "method": string
 * - "id": number or string
 * - "params": array of strings/hex values
 *
 * This is intentionally simple. For production, use a proper JSON parser
 * (e.g., cJSON, yyjson). For the MVP, pattern matching is sufficient
 * and avoids adding a dependency.
 */

/* Find a JSON string value for a given key.
   Returns pointer to the opening quote of the value, or NULL. */
static const char *json_find_string(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *pos = strstr(json, search);
    if (!pos) return NULL;

    pos += strlen(search);

    /* Skip whitespace and colon */
    while (*pos && (*pos == ' ' || *pos == ':' || *pos == '\t')) pos++;

    if (*pos != '"') return NULL;
    return pos;
}

/* Extract a JSON string value. Writes to out without quotes. */
static int json_extract_string(const char *json, const char *key,
                               char *out, size_t out_len) {
    const char *pos = json_find_string(json, key);
    if (!pos) return -1;

    pos++; /* skip opening quote */

    size_t i = 0;
    while (*pos && *pos != '"' && i < out_len - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';

    return 0;
}

/* Extract the "id" field — can be a number or string. */
static int json_extract_id(const char *json, char *out, size_t out_len) {
    char search[] = "\"id\"";
    const char *pos = strstr(json, search);
    if (!pos) {
        strncpy(out, "1", out_len);
        return 0;
    }

    pos += strlen(search);
    while (*pos && (*pos == ' ' || *pos == ':' || *pos == '\t')) pos++;

    if (*pos == '"') {
        /* String id */
        pos++;
        size_t i = 0;
        while (*pos && *pos != '"' && i < out_len - 1) {
            out[i++] = *pos++;
        }
        out[i] = '\0';
    } else {
        /* Numeric id */
        size_t i = 0;
        while (*pos && (isdigit((unsigned char)*pos) || *pos == '-')
               && i < out_len - 1) {
            out[i++] = *pos++;
        }
        out[i] = '\0';
    }

    return 0;
}

int evmdb_json_parse_method(const char *json, char *method, size_t method_len,
                            char *id_buf, size_t id_len) {
    json_extract_string(json, "method", method, method_len);
    json_extract_id(json, id_buf, id_len);
    return 0;
}

/*
 * Extract params array. Handles simple cases:
 * - "params": ["0xabc...", "latest"]
 * - "params": [{"from": "0x...", "to": "0x...", ...}]
 *
 * For string params, extracts them directly.
 * For object params, extracts the first object as a raw JSON string.
 */
int evmdb_json_parse_params(const char *json, char params[][256],
                            int max_params) {
    const char *pos = strstr(json, "\"params\"");
    if (!pos) return 0;

    pos += 8; /* skip "params" */
    while (*pos && (*pos == ' ' || *pos == ':' || *pos == '\t')) pos++;

    if (*pos != '[') return 0;
    pos++; /* skip '[' */

    int count = 0;

    while (*pos && *pos != ']' && count < max_params) {
        /* Skip whitespace */
        while (*pos && (*pos == ' ' || *pos == ',' || *pos == '\t'
               || *pos == '\n' || *pos == '\r')) {
            pos++;
        }

        if (*pos == ']') break;

        if (*pos == '"') {
            /* String parameter */
            pos++; /* skip opening quote */
            size_t i = 0;
            while (*pos && *pos != '"' && i < 255) {
                params[count][i++] = *pos++;
            }
            params[count][i] = '\0';
            if (*pos == '"') pos++;
            count++;
        } else if (*pos == '{') {
            /* Object parameter — skip for now, just store "object" */
            strncpy(params[count], "{object}", 256);
            /* Skip to matching } */
            int depth = 1;
            pos++;
            while (*pos && depth > 0) {
                if (*pos == '{') depth++;
                if (*pos == '}') depth--;
                pos++;
            }
            count++;
        } else if (isdigit((unsigned char)*pos) || *pos == '-') {
            /* Numeric parameter */
            size_t i = 0;
            while (*pos && *pos != ',' && *pos != ']' && i < 255) {
                params[count][i++] = *pos++;
            }
            params[count][i] = '\0';
            count++;
        } else {
            /* null, true, false, or unknown — skip */
            while (*pos && *pos != ',' && *pos != ']') pos++;
            count++;
        }
    }

    return count;
}
