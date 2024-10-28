#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool is_digit(char c) { return c >= '0' && c <= '9'; }

struct list {
  char **ptr;
  int32_t len;
  int32_t cap;
};

int32_t list_init(struct list *l) {
  int32_t new_cap = 4;
  char **new_ptr = (char **)malloc(new_cap * sizeof(char *));
  if (new_ptr == NULL) {
    fprintf(stderr, "Failed to create new list\n");
    return 1;
  }

  l->ptr = new_ptr;
  l->cap = new_cap;
  l->len = 0;

  return 0;
}

int32_t list_push(struct list *l, char *x) {
  if (l->len == l->cap) {
    int32_t new_cap = 2 * l->cap;
    char **new_ptr = (char **)realloc(l->ptr, new_cap * sizeof(char *));
    if (new_ptr == NULL) {
      fprintf(stderr, "Failed to reallocate memory\n");
      return 1;
    }
    l->ptr = new_ptr;
    l->cap = new_cap;
  }
  l->ptr[l->len] = x;
  ++l->len;

  return 0;
}

void list_free(struct list *l) {
  for (int32_t i = 0; i < l->len; ++i) {
    free(l->ptr[i]);
  }
  free(l->ptr);
}

struct item {
  char *key;
  char *value;
};

struct dict {
  struct item *ptr;
  int32_t len;
  int32_t cap;
};

int32_t dict_init(struct dict *d) {
  int32_t new_cap = 4;
  struct item *new_ptr = (struct item *)malloc(new_cap * sizeof(struct item));
  if (new_ptr == NULL) {
    fprintf(stderr, "Failed to create new dict\n");
    return 1;
  }

  d->ptr = new_ptr;
  d->cap = new_cap;
  d->len = 0;

  return 0;
}

int32_t dict_push(struct dict *d, char *k, char *v) {
  if (d->len == d->cap) {
    int32_t new_cap = 2 * d->cap;
    struct item *new_ptr =
        (struct item *)realloc(d->ptr, new_cap * sizeof(struct item));
    if (new_ptr == NULL) {
      fprintf(stderr, "Failed to reallocate memory\n");
      return 1;
    }
    d->ptr = new_ptr;
    d->cap = new_cap;
  }
  d->ptr[d->len].key = k;
  d->ptr[d->len].value = v;
  ++d->len;

  return 0;
}

void dict_free(struct dict *d) {
  for (int32_t i = 0; i < d->len; ++i) {
    free(d->ptr[i].key);
    free(d->ptr[i].value);
  }
  free(d->ptr);
}

char *decode_bencode(const char *bencoded_value) {
  if (is_digit(bencoded_value[0])) {
    int length = atoi(bencoded_value);
    const char *colon_index = strchr(bencoded_value, ':');
    if (colon_index != NULL) {
      const char *start = colon_index + 1;
      char *decoded_str = (char *)malloc(length + 1);
      strncpy(decoded_str, start, length);
      decoded_str[length] = '\0';
      return decoded_str;
    } else {
      fprintf(stderr, "Invalid encoded value: %s\n", bencoded_value);
      exit(1);
    }
  } else {
    fprintf(stderr, "Only strings are supported at the moment\n");
    exit(1);
  }
}

int32_t decode_str(char **s, char **str);
int32_t decode_int(char **s, char **str);
int32_t decode_list(char **s, char **str);
int32_t decode_dict(char **s, char **str);

int32_t decode_str(char **s, char **str) {
  char *ptr = *s;
  if (!is_digit(*ptr)) {
    fprintf(stderr, "First character should be a digit\n");
    return 1;
  }

  char *col = strchr(ptr, ':');
  if (col == NULL) {
    fprintf(stderr, "No color seperator found\n");
    return 1;
  }

  for (; ptr != col; ++ptr) {
    if (!is_digit(*ptr)) {
      fprintf(stderr, "Invalid string length\n");
      return 1;
    }
  }

  int32_t str_len = atoi(*s);
  int32_t len = strlen(col + 1);
  if (str_len > len) {
    fprintf(stderr, "Provided string is shorter than the provided length\n");
    return 1;
  }

  char *str_m =
      (char *)malloc((3 + str_len) * sizeof(char)); // two "" and \0 + string
  if (str_m == NULL) {
    fprintf(stderr, "Failed to allocate memory\n");
  }
  str_m[0] = '"';
  memcpy(str_m + 1, col + 1, str_len);
  str_m[str_len + 1] = '"';
  str_m[str_len + 2] = '\0';

  *s = col + 1 + str_len;
  *str = str_m;

  return 0;
}

int32_t decode_int(char **s, char **str) {
  char *ptr = *s;
  if (*ptr != 'i') {
    fprintf(stderr, "Integer should begin with \"i\" delimiter");
    return 1;
  }
  char *e = strchr(ptr, 'e');
  if (e == NULL) {
    fprintf(stderr, "Integer does not contain ending delimiter\n");
    return 1;
  }
  // p points to the first digit
  char *p = ptr + 1;
  if (*p == '-' && p + 1 != e)
    ++p;
  for (char *q = p; q != e; ++q) {
    if (!is_digit(*q)) {
      fprintf(stderr, "Invalid character encountered while parsing integer\n");
      return 1;
    }
  }
  int32_t len = e - p;

  if (len == 0) {
    fprintf(stderr, "No digits present\n");
    return 1;
  }
  if (len > 1 && *p == '0') {
    fprintf(stderr, "Integer must not begin with 0\n");
    return 1;
  }
  if (len == 1 && *p == '0' && *(p - 1) == '-') {
    fprintf(stderr, "-0 not allowed\n");
    return 1;
  }

  len = e - ptr - 1;
  char *str_m = (char *)malloc((len + 1) * sizeof(char));
  memcpy(str_m, ptr + 1, len);
  str_m[len] = '\0';

  *s = e + 1;
  *str = str_m;

  return 0;
}

int32_t decode_list(char **s, char **str) {
  char *ptr = *s;
  if (*ptr++ != 'l') {
    fprintf(stderr, "List must begin with \"l\" delimiter");
    return 1;
  }

  struct list l;
  if (list_init(&l) != 0) {
    return 1;
  }

  while (*ptr != 'e' && *ptr != '\0') {
    char *str = NULL;

    if (*ptr == 'i') {
      if (decode_int(&ptr, &str) != 0) {
        fprintf(stderr, "Failed to decode bencoded integer\n");
        list_free(&l);
        return 1;
      }
    } else if (is_digit(*ptr)) {
      if (decode_str(&ptr, &str) != 0) {
        fprintf(stderr, "Failed to decode bencoded string\n");
        list_free(&l);
        return 1;
      }
    } else if (*ptr == 'l') {
      if (decode_list(&ptr, &str) != 0) {
        fprintf(stderr, "Failed to decode bencoded list\n");
        list_free(&l);
        return 1;
      }
    } else if (*ptr == 'd') {
      if (decode_dict(&ptr, &str) != 0) {
        fprintf(stderr, "Failed to decode bencoded dict\n");
        list_free(&l);
        return 1;
      }
    } else {
      fprintf(stderr, "Invalid character encountered\n");
      list_free(&l);
      return 1;
    }

    if (list_push(&l, str) != 0) {
      free(str);
      list_free(&l);
      return 1;
    }
  }

  int32_t str_len = 3;                  // two [] brackets and \0
  str_len += l.len > 0 ? l.len - 1 : 0; // l.len - 1 commas
  for (int32_t i = 0; i < l.len; ++i) {
    str_len += strlen(l.ptr[i]);
  }

  char *st = (char *)malloc(str_len * sizeof(char));
  if (st == NULL) {
    fprintf(stderr, "Failed to allocate memory\n");
    list_free(&l);
    return 1;
  }
  char *t = st;

  *t++ = '[';
  for (int32_t i = 0; i < l.len; ++i) {
    int32_t n = strlen(l.ptr[i]);
    memcpy(t, l.ptr[i], n);
    t += n;
    *t++ = ',';
  }
  if (l.len > 0)
    --t;
  *t++ = ']';
  *t = '\0';

  if (*ptr == 'e')
    ++ptr;
  *s = ptr;
  *str = st;

  // free all resources
  list_free(&l);

  return 0;
}

int32_t decode_dict(char **s, char **str) {
  char *ptr = *s;
  if (*ptr++ != 'd') {
    fprintf(stderr, "Dictionary should begin with \"d\" delimiter\n");
    return 1;
  }

  struct dict d;
  if (dict_init(&d) != 0) {
    fprintf(stderr, "Failed to initialize dictionary\n");
    return 1;
  }

  while (*ptr != 'e' && *ptr != '\0') {
    char *key_str = NULL;
    char *val_str = NULL;

    // parse key
    if (!is_digit(*ptr)) {
      fprintf(stderr, "Dictionary's key must be a valid string\n");
      dict_free(&d);
      return 1;
    }
    if (decode_str(&ptr, &key_str) != 0) {
      fprintf(stderr, "Failed to decode dictionary's key\n");
      dict_free(&d);
      return 1;
    }

    // parse value
    if (*ptr == 'i') {
      if (decode_int(&ptr, &val_str) != 0) {
        fprintf(stderr, "Failed to decode bencoded integer\n");
        dict_free(&d);
        free(key_str);
        return 1;
      }
    } else if (is_digit(*ptr)) {
      if (decode_str(&ptr, &val_str) != 0) {
        fprintf(stderr, "Failed to decode bencoded string\n");
        dict_free(&d);
        free(key_str);
        return 1;
      }
    } else if (*ptr == 'l') {
      if (decode_list(&ptr, &val_str) != 0) {
        fprintf(stderr, "Failed to decode bencoded list\n");
        dict_free(&d);
        free(key_str);
        return 1;
      }
    } else if (*ptr == 'd') {
      if (decode_dict(&ptr, &val_str) != 0) {
        fprintf(stderr, "Failed to decode bencoded dict\n");
        dict_free(&d);
        free(key_str);
        return 1;
      }
    } else {
      fprintf(stderr, "Invalid character encountered\n");
      dict_free(&d);
      free(key_str);
      return 1;
    }

    if (dict_push(&d, key_str, val_str) != 0) {
      dict_free(&d);
      free(key_str);
      free(val_str);
      return 1;
    }
  }

  for (int32_t i = 0; i < d.len - 1; ++i) {
    if (strcmp(d.ptr[i].key, d.ptr[i + 1].key) > 0) {
      fprintf(stderr, "Dictionary keys must be sorted lexicographically\n");
      dict_free(&d);
      return 1;
    }
  }

  int32_t str_len = 3;                  // two {} brackets and \0
  str_len += d.len > 0 ? d.len - 1 : 0; // d.len - 1 commas
  for (int32_t i = 0; i < d.len; ++i) {
    str_len += strlen(d.ptr[i].key);
    str_len += strlen(d.ptr[i].value);
    ++str_len; // for ":"
  }

  char *st = (char *)malloc(str_len * sizeof(char));
  if (st == NULL) {
    fprintf(stderr, "Failed to allocate memory\n");
    dict_free(&d);
    return 1;
  }
  char *t = st;

  *t++ = '{';
  for (int32_t i = 0; i < d.len; ++i) {
    int32_t key_len = strlen(d.ptr[i].key);
    int32_t val_len = strlen(d.ptr[i].value);
    memcpy(t, d.ptr[i].key, key_len);
    t += key_len;
    *t++ = ':';
    memcpy(t, d.ptr[i].value, val_len);
    t += val_len;
    *t++ = ',';
  }
  if (d.len > 0)
    --t;
  *t++ = '}';
  *t = '\0';

  if (*ptr == 'e')
    ++ptr;
  *s = ptr;
  *str = st;

  // free all resources
  dict_free(&d);

  return 0;
}

int32_t decode(char *s) {
  char *str = NULL;

  if (*s == 'i') {
    if (decode_int(&s, &str) != 0) {
      fprintf(stderr, "Invalid bencoded integer\n");
      return 1;
    }
  } else if (is_digit(*s)) {
    if (decode_str(&s, &str) != 0) {
      fprintf(stderr, "Invalid bencoded string\n");
      return 1;
    }
  } else if (*s == 'l') {
    if (decode_list(&s, &str) != 0) {
      fprintf(stderr, "Invalid bencoded list\n");
      return 1;
    }
  } else if (*s == 'd') {
    if (decode_dict(&s, &str) != 0) {
      fprintf(stderr, "Invalid bencoded dict\n");
      return 1;
    }
  } else {
    fprintf(stderr, "Invalid character encountered\n");
    return 1;
  }

  if (*s != '\0') {
    fprintf(stderr, "Encountered additional characters\n");
    free(str);
    return 1;
  }

  printf("%s\n", str);
  free(str);

  return 0;
}

int32_t main(int32_t argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: your_bittorrent.sh <command> <args>\n");
    return 1;
  }

  if (strcmp(argv[1], "decode") == 0) {
    if (decode(argv[2]) != 0) {
      return 1;
    }
  } else {
    fprintf(stderr, "Not implemented\n");
    return 1;
  }

  return 0;
}

/* int main(int argc, char *argv[]) { */
/*   // Disable output buffering */
/*   setbuf(stdout, NULL); */
/*   setbuf(stderr, NULL); */
/**/
/*   if (argc < 3) { */
/*     fprintf(stderr, "Usage: your_bittorrent.sh <command> <args>\n"); */
/*     return 1; */
/*   } */
/**/
/*   const char *command = argv[1]; */
/**/
/*   if (strcmp(command, "decode") == 0) { */
/*     // You can use print statements as follows for debugging, they'll be
 * visible */
/*     // when running tests. printf("Logs from your program will appear
 * here!\n"); */
/**/
/*     // Uncomment this block to pass the first stage */
/*     const char *encoded_str = argv[2]; */
/*     char *decoded_str = decode_bencode(encoded_str); */
/*     printf("\"%s\"\n", decoded_str); */
/*     free(decoded_str); */
/*   } else { */
/*     fprintf(stderr, "Unknown command: %s\n", command); */
/*     return 1; */
/*   } */
/**/
/*   return 0; */
/* } */
