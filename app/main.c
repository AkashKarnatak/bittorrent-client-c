#include <openssl/sha.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool is_digit(char c) { return c >= '0' && c <= '9'; }

typedef struct bevalue_t bevalue_t;
typedef struct bedictitem_t bedictitem_t;

typedef enum { BE_INT, BE_STR, BE_VEC } betype_t;

typedef struct {
  char *str;
  int32_t n;
} bestring_t;

typedef struct {
  union {
    bevalue_t *list;
    bedictitem_t *dict;
  } data;
  bool is_dict;
  int32_t len;
  int32_t cap;
} bevec_t;

struct bevalue_t {
  betype_t type;
  union {
    int64_t i;
    bestring_t str;
    bevec_t vec;
  } val;
};

struct bedictitem_t {
  bestring_t key;
  bevalue_t val;
};

int32_t bevec_init(bevec_t *l, bool is_dict) {
  bevec_t li;
  int32_t new_cap = 4;
  if (is_dict) {
    bedictitem_t *data = (bedictitem_t *)malloc(new_cap * sizeof(bedictitem_t));
    if (data == NULL) {
      fprintf(stderr, "Failed allocate memory\n");
      return 1;
    }
    li.data.dict = data;
  } else {
    bevalue_t *data = (bevalue_t *)malloc(new_cap * sizeof(bevalue_t));
    if (data == NULL) {
      fprintf(stderr, "Failed allocate memory\n");
      return 1;
    }
    li.data.list = data;
  }
  li.cap = new_cap;
  li.len = 0;
  li.is_dict = is_dict;
  *l = li;
  return 0;
}

int32_t bevec_push(bevec_t *l, void *x) {
  if (l->len == l->cap) {
    int32_t new_cap = 2 * l->cap;
    if (l->is_dict) {
      bedictitem_t *new_data =
          (bedictitem_t *)realloc(l->data.dict, new_cap * sizeof(bedictitem_t));
      if (new_data == NULL) {
        fprintf(stderr, "Failed re-allocate memory\n");
        return 1;
      }
      l->data.dict = new_data;
    } else {
      bevalue_t *new_data =
          (bevalue_t *)realloc(l->data.list, new_cap * sizeof(bevalue_t));
      if (new_data == NULL) {
        fprintf(stderr, "Failed re-allocate memory\n");
        return 1;
      }
      l->data.list = new_data;
    }
    l->cap = new_cap;
  }

  if (l->is_dict) {
    l->data.dict[l->len++] = *(bedictitem_t *)x;
  } else {
    l->data.list[l->len++] = *(bevalue_t *)x;
  }

  return 0;
}

void bevec_free(bevec_t *v);

void bevalue_free(bevalue_t *v) {
  switch (v->type) {
  case BE_INT:
    break;
  case BE_STR:
    break;
  case BE_VEC:
    bevec_free(&v->val.vec);
    break;
  }
}

void bevec_free(bevec_t *v) {
  if (v->is_dict) {
    for (int32_t i = 0; i < v->len; ++i) {
      bevalue_free(&v->data.dict[i].val);
    }
    free(v->data.dict);
  } else {
    for (int32_t i = 0; i < v->len; ++i) {
      bevalue_free(&v->data.list[i]);
    }
    free(v->data.list);
  }
}

// TODO: disambiguate error and key not found
bevalue_t *bevec_dict_get(bevec_t *v, char *str) {
  if (!v->is_dict) {
    fprintf(stderr, "Not a dictionary\n");
    return NULL;
  }
  bevalue_t *val = NULL;
  for (int32_t i = 0; i < v->len; ++i) {
    bestring_t key = v->data.dict[i].key;
    if (strncmp(key.str, str, key.n) == 0) {
      val = &v->data.dict[i].val;
      break;
    }
  }
  return val;
}

int32_t next_value(char **ptr, bevalue_t *beval);

int32_t next_str(char **ptr, bestring_t *bestr) {
  char *begin = *ptr;
  for (; **ptr != ':' && **ptr != '\0'; ++*ptr) {
    if (!is_digit(**ptr)) {
      fprintf(stderr, "Invalid string encoding\n");
      return 1;
    }
  }
  if (**ptr != ':') {
    fprintf(stderr, "No color seperator found\n");
    return 1;
  }
  int32_t n = atoi(begin);
  ++*ptr;
  if (bestr != NULL) {
    bestr->str = *ptr;
    bestr->n = n;
  }
  *ptr += n;
  return 0;
}

int32_t next_int(char **ptr, int64_t *val) {
  if (*(*ptr)++ != 'i') {
    fprintf(stderr, "Invalid integer encoding\n");
    return 1;
  }
  char *begin = *ptr;
  if (*begin == '-')
    ++*ptr;
  for (; **ptr != 'e' && **ptr != '\0'; ++*ptr) {
    if (!is_digit(**ptr)) {
      fprintf(stderr, "Not an integer - invalid character\n");
      return 1;
    }
  }
  if (**ptr != 'e') {
    fprintf(stderr, "No end delimiter found\n");
    return 1;
  }
  int32_t len = *ptr - begin;
  if ((len == 0) || (len == 1 && *begin == '-') || (len > 1 && *begin == '0') ||
      (len >= 2 && *begin == '-' && *(begin + 1) == '0')) {
    fprintf(stderr, "Invalid integer\n");
    return 1;
  }
  char *end;
  int64_t i = strtoll(begin, &end, 10);
  if (begin == end) {
    fprintf(stderr, "No digits found\n");
    return 1;
  }
  if (val != NULL) {
    *val = i;
  }
  ++*ptr;
  return 0;
}

// TODO: disambiguate error and key not found
char *dict_get_raw(char **ptr, char *str) {
  if (*(*ptr)++ != 'd') {
    fprintf(stderr, "Not a dictionary\n");
    return NULL;
  }

  while (**ptr != 'e' && **ptr != '\0') {
    bestring_t key;

    if (next_str(ptr, &key) != 0) {
      fprintf(stderr, "Failed to parse dict key\n");
      return NULL;
    }

    if (strncmp(key.str, str, key.n) == 0) {
      return *ptr;
    }

    bevalue_t v;
    if (next_value(ptr, &v) != 0) {
      fprintf(stderr, "Failed to parse dict value\n");
      return NULL;
    }
    bevalue_free(&v);
  }

  return NULL;
}

int32_t next_value(char **ptr, bevalue_t *beval) {
  if (is_digit(**ptr)) {
    bestring_t str;
    bevalue_t v;

    if (next_str(ptr, &str) != 0) {
      fprintf(stderr, "Failed to parse string\n");
      return 1;
    }
    v.type = BE_STR;
    v.val.str = str;
    *beval = v;

  } else if (**ptr == 'i') {
    int64_t i;
    bevalue_t v;

    if (next_int(ptr, &i) != 0) {
      fprintf(stderr, "Failed to parse integer\n");
      return 1;
    }
    v.type = BE_INT;
    v.val.i = i;
    *beval = v;

  } else if (**ptr == 'l') {
    bevec_t vec;
    bevalue_t v;
    if (bevec_init(&vec, false) != 0) {
      fprintf(stderr, "Failed to initialize vector\n");
      return 1;
    }

    ++*ptr;
    while (**ptr != 'e' && **ptr != '\0') {
      bevalue_t val;
      if (next_value(ptr, &val) != 0) {
        fprintf(stderr, "Failed to parse next value\n");
        return 1;
      }
      if (bevec_push(&vec, &val) != 0) {
        fprintf(stderr, "Failed to insert element into vector\n");
        return 1;
      }
    }
    if (*(*ptr)++ == '\0') {
      fprintf(stderr, "Invalid list - cannot find end delimiter\n");
      return 1;
    }

    v.type = BE_VEC;
    v.val.vec = vec;
    *beval = v;

  } else if (**ptr == 'd') {
    bevec_t vec;
    bevalue_t v;
    if (bevec_init(&vec, true) != 0) {
      fprintf(stderr, "Failed to initialize vector\n");
      return 1;
    }

    ++*ptr;
    while (**ptr != 'e' && **ptr != '\0') {
      bestring_t key;
      if (next_str(ptr, &key) != 0) {
        fprintf(stderr, "Failed to parse dict key\n");
        return 1;
      }

      bevalue_t val;
      if (next_value(ptr, &val) != 0) {
        fprintf(stderr, "Failed to parse dict value\n");
        return 1;
      }

      bedictitem_t pair;
      pair.key = key;
      pair.val = val;
      if (bevec_push(&vec, &pair) != 0) {
        fprintf(stderr, "Failed to insert element into vector\n");
        return 1;
      }
    }
    if (*(*ptr)++ == '\0') {
      fprintf(stderr, "Invalid list - cannot find end delimiter\n");
      return 1;
    }

    v.type = BE_VEC;
    v.val.vec = vec;
    *beval = v;

  } else {
    fprintf(stderr, "Invalid type\n");
    return 1;
  }
  return 0;
}

void be_print(bevalue_t *v, char **str) {
  switch (v->type) {
  case BE_INT:
    *str += sprintf(*str, "%ld", v->val.i);
    break;
  case BE_STR:
    *str += sprintf(*str, "\"%.*s\"", v->val.str.n, v->val.str.str);
    break;
  case BE_VEC:
    if (v->val.vec.is_dict) {
      *(*str)++ = '{';
      for (int32_t i = 0; i < v->val.vec.len; ++i) {
        // print key
        bevalue_t val;
        val.type = BE_STR;
        val.val.str = v->val.vec.data.dict[i].key;
        be_print(&val, str);
        // print color
        *(*str)++ = ':';
        // print value
        be_print(&v->val.vec.data.dict[i].val, str);
        *(*str)++ = ',';
      }
      if (v->val.vec.len != 0) {
        --*str;
      }
      *(*str)++ = '}';
    } else {
      *(*str)++ = '[';
      for (int32_t i = 0; i < v->val.vec.len; ++i) {
        be_print(&v->val.vec.data.list[i], str);
        *(*str)++ = ',';
      }
      if (v->val.vec.len != 0) {
        --*str;
      }
      *(*str)++ = ']';
    }
    break;
  }
  **str = '\0';
}

void print_hex(unsigned char *s) {
  for (int32_t i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    printf("%02x", s[i]);
  }
  printf("\n");
}

int32_t decode(char *s) {
  bevalue_t v;
  if (next_value(&s, &v) != 0) {
    return 1;
  }
  char buf[1024];
  char *str = buf;
  be_print(&v, &str);
  printf("%s\n", buf);
  bevalue_free(&v);
  return 0;
}

int32_t parse(char *filename) {
  // open file
  FILE *f = fopen(filename, "r");
  if (f == NULL) {
    perror("Failed to open torrent file");
    return 1;
  }

  // read contents
  fseek(f, 0, SEEK_END);
  int64_t fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  char buf[fsize + 1];
  if (fread(buf, 1, fsize, f) != fsize) {
    fprintf(stderr, "Error while reading file contents\n");
    return 1;
  }
  buf[fsize] = '\0';

  // close file
  fclose(f);

  char *s = buf;
  bevalue_t v;
  if (next_value(&s, &v) != 0) {
    return 1;
  }
  if (v.type != BE_VEC && !v.val.vec.is_dict) {
    fprintf(stderr, "Not a dictionary\n");
    return 1;
  }

  bevalue_t *announce_v = bevec_dict_get(&v.val.vec, "announce");
  if (announce_v == NULL || announce_v->type != BE_STR) {
    fprintf(stderr, "Invalid announce key\n");
    return 1;
  }

  bevalue_t *info_v = bevec_dict_get(&v.val.vec, "info");
  if (info_v == NULL || info_v->type != BE_VEC || !info_v->val.vec.is_dict) {
    fprintf(stderr, "Invalid info key\n");
    return 1;
  }

  bevalue_t *length_v = bevec_dict_get(&info_v->val.vec, "length");
  if (length_v == NULL || length_v->type != BE_INT) {
    fprintf(stderr, "Invalid length key\n");
    return 1;
  }

  bevalue_t *piece_length_v = bevec_dict_get(&info_v->val.vec, "piece length");
  if (piece_length_v == NULL || piece_length_v->type != BE_INT) {
    fprintf(stderr, "Invalid piece length key\n");
    return 1;
  }

  bevalue_t *pieces_v = bevec_dict_get(&info_v->val.vec, "pieces");
  if (pieces_v == NULL || pieces_v->type != BE_STR) {
    fprintf(stderr, "Invalid pieces key\n");
    return 1;
  }

  s = buf;
  char *raw_info_v = dict_get_raw(&s, "info");
  if (raw_info_v == NULL) {
    fprintf(stderr, "Unable to find info key\n");
    return 1;
  }
  bevalue_t v2;
  if (next_value(&s, &v2) != 0) {
    fprintf(stderr, "Failed to parse dict value\n");
    return 1;
  }
  bevalue_free(&v2);
  int32_t n = s - raw_info_v;
  unsigned char sha[SHA_DIGEST_LENGTH];
  SHA1((unsigned char *)raw_info_v, n, (unsigned char *)sha);

  printf("Tracker URL: %.*s\n", announce_v->val.str.n, announce_v->val.str.str);
  printf("Length: %ld\n", length_v->val.i);
  printf("Info Hash: ");
  print_hex(sha);
  printf("Piece Length: %ld\n", piece_length_v->val.i);
  printf("Piece Hashes:\n");
  unsigned char *ptr = (unsigned char *)pieces_v->val.str.str;
  for (int32_t i = 0; i < pieces_v->val.str.n; i += SHA_DIGEST_LENGTH) {
    print_hex(ptr);
    ptr += SHA_DIGEST_LENGTH;
  }

  bevalue_free(&v);
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
  } else if (strcmp(argv[1], "info") == 0) {
    if (parse(argv[2]) != 0) {
      return 1;
    }
  } else {
    fprintf(stderr, "Not implemented\n");
    return 1;
  }

  return 0;
}
