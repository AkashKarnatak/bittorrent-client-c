#include <arpa/inet.h>
#include <assert.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <netinet/in.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

const int32_t PEER_INFO_SIZE = 6;

bool is_digit(char c) { return c >= '0' && c <= '9'; }
uint32_t min(uint32_t x, uint32_t y) { return x < y ? x : y; }

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

void print_hex(uint8_t *s) {
  for (int32_t i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    printf("%02x", s[i]);
  }
  printf("\n");
}

void print_ip(uint8_t *s) {
  printf("%d.%d.%d.%d:%d\n", s[0], s[1], s[2], s[3], (s[4] << 8) | s[5]);
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

char *read_file(char *filename) {
  // open file
  FILE *f = fopen(filename, "r");
  if (f == NULL) {
    perror("Failed to open torrent file");
    return NULL;
  }

  // read contents
  fseek(f, 0, SEEK_END);
  int64_t fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc((fsize + 1) * sizeof(char));
  if (buf == NULL) {
    fprintf(stderr, "Failed to allocate memory\n");
    return NULL;
  }
  if (fread(buf, 1, fsize, f) != fsize) {
    fprintf(stderr, "Error while reading file contents\n");
    return NULL;
  }
  buf[fsize] = '\0';

  // close file
  fclose(f);
  return buf;
}

void urlencode(uint8_t *str, int32_t n, char *buf) {
  for (int32_t i = 0; i < n; ++i) {
    buf += sprintf(buf, "%%%02x", str[i]);
  }
}

size_t write_data(void *buffer, size_t size, size_t nmemb, bestring_t *res) {
  size_t realsize = size * nmemb;

  char *new_str = (char *)realloc(res->str, res->n + realsize + 1);
  if (new_str == NULL) {
    fprintf(stderr, "Failed to reallocate memory\n");
    return 0;
  }

  res->str = new_str;
  memcpy(res->str + res->n, buffer, realsize);
  res->n += realsize;
  res->str[res->n] = '\0';

  return realsize;
}

int32_t parse(char *filename) {
  char *buf = read_file(filename);
  if (buf == NULL) {
    fprintf(stderr, "Failed to read file\n");
    return 1;
  }
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
  uint8_t sha[SHA_DIGEST_LENGTH];
  SHA1((uint8_t *)raw_info_v, n, (uint8_t *)sha);

  printf("Tracker URL: %.*s\n", announce_v->val.str.n, announce_v->val.str.str);
  printf("Length: %ld\n", length_v->val.i);
  printf("Info Hash: ");
  print_hex(sha);
  printf("Piece Length: %ld\n", piece_length_v->val.i);
  printf("Piece Hashes:\n");
  uint8_t *ptr = (uint8_t *)pieces_v->val.str.str;
  for (int32_t i = 0; i < pieces_v->val.str.n; i += SHA_DIGEST_LENGTH) {
    print_hex(ptr);
    ptr += SHA_DIGEST_LENGTH;
  }

  free(buf);
  bevalue_free(&v);
  return 0;
}

int32_t perform_get_request(char *bencode_buf, bestring_t *res) {
  char *s = bencode_buf;
  bevalue_t v;
  assert(next_value(&s, &v) == 0);

  bevalue_t *announce_v = bevec_dict_get(&v.val.vec, "announce");
  assert(announce_v != NULL && announce_v->type == BE_STR);
  bevalue_t *info_v = bevec_dict_get(&v.val.vec, "info");
  assert(info_v != NULL && info_v->type == BE_VEC && info_v->val.vec.is_dict);
  bevalue_t *length_v = bevec_dict_get(&info_v->val.vec, "length");
  assert(length_v != NULL || length_v->type == BE_INT);

  s = bencode_buf;
  char *raw_info_v = dict_get_raw(&s, "info");
  assert(raw_info_v != NULL);
  bevalue_t v2;
  assert(next_value(&s, &v2) == 0);
  bevalue_free(&v2);

  int32_t n = s - raw_info_v;
  uint8_t hash[SHA_DIGEST_LENGTH];
  SHA1((uint8_t *)raw_info_v, n, (uint8_t *)hash);

  CURL *handle = curl_easy_init();
  assert(handle != NULL);

  uint8_t id[20];
  char url_buf[1024], enc_id[100], enc_hash[100];
  assert(RAND_bytes(id, 20) == 1);
  urlencode(hash, SHA_DIGEST_LENGTH, enc_hash);
  urlencode(id, 20, enc_id);
  uint32_t url_size = announce_v->val.str.n;
  char *url = announce_v->val.str.str;
  int64_t length = length_v->val.i;
  sprintf(url_buf,
          "%.*s?info_hash=%s&peer_id=%s&port=6881&uploaded=0&downloaded=0&"
          "left=%ld&compact=1",
          url_size, url, enc_hash, enc_id, length);
  curl_easy_setopt(handle, CURLOPT_URL, url_buf);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, res);

  assert(curl_easy_perform(handle) == CURLE_OK);
  curl_easy_cleanup(handle);
  bevalue_free(&v);
  return 0;
}

int32_t perform_handshake(int32_t sockfd, char *bencode_buf,
                          uint8_t *data_buf) {
  char *s = bencode_buf;

  char *raw_info_v = dict_get_raw(&s, "info");
  assert(raw_info_v != NULL);
  bevalue_t v2;
  assert(next_value(&s, &v2) == 0);
  bevalue_free(&v2);
  int32_t len = s - raw_info_v;
  uint8_t hash[SHA_DIGEST_LENGTH];
  SHA1((uint8_t *)raw_info_v, len, (uint8_t *)hash);

  uint8_t id[20];
  RAND_bytes(id, 20);

  uint32_t n = 0;

  // perform handshake
  data_buf[0] = 19;
  memcpy(data_buf + 1, "BitTorrent protocol", 19);
  memset(data_buf + 20, 0, 8);
  memcpy(data_buf + 28, hash, 20);
  memcpy(data_buf + 48, id, 20);
  assert(send(sockfd, data_buf, 68, 0) == 68);

  // receive handshake
  assert(recv(sockfd, data_buf, 1, 0) == 1);
  assert((n = recv(sockfd, data_buf + 1, data_buf[0] + 48, 0)) ==
         data_buf[0] + 48); // remaining 48 and additional 4 if bitfield is sent

  return 0;
}

int32_t discover(char *filename) {
  char *buf = read_file(filename);
  if (buf == NULL) {
    fprintf(stderr, "Failed to read file\n");
    return 1;
  }

  bestring_t res = {.str = (char *)malloc(0), .n = 0};
  assert(perform_get_request(buf, &res) == 0);
  bevalue_t res_v;
  char *s = res.str;
  assert(next_value(&s, &res_v) == 0);

  bevalue_t *peers_v = bevec_dict_get(&res_v.val.vec, "peers");
  assert(peers_v != NULL && peers_v->type == BE_STR);

  for (int32_t i = 0; i < peers_v->val.str.n; i += PEER_INFO_SIZE) {
    print_ip((uint8_t *)(peers_v->val.str.str + i));
  }

  free(res.str);
  free(buf);
  bevalue_free(&res_v);
  return 0;
}

int32_t handshake(char *filename, char *peer_info) {
  char *buf = read_file(filename);
  if (buf == NULL) {
    fprintf(stderr, "Failed to read file\n");
    return 1;
  }

  char *ip = peer_info;
  char *port = strchr(peer_info, ':');
  assert(port != NULL);
  *port++ = '\0';

  int32_t sockfd;
  struct sockaddr_in addr;
  assert((sockfd = socket(AF_INET, SOCK_STREAM, 0)) > 0);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(port));
  addr.sin_addr.s_addr = inet_addr(ip);
  assert(connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == 0);

  uint8_t recv_buf[100] = {0};
  uint8_t id[20] = {0};
  perform_handshake(sockfd, buf, recv_buf);
  memcpy(id, recv_buf + recv_buf[0] + 29, 20);
  printf("Peer ID: ");
  print_hex(id);

  free(buf);
  return 0;
}

int32_t download_piece(int32_t sockfd, uint8_t *buf, uint32_t index,
                       uint32_t piece_size, uint8_t **piece) {
  uint8_t *new_piece = (uint8_t *)malloc(piece_size * sizeof(uint8_t));
  if (new_piece == NULL) {
    fprintf(stderr, "Failed to allocate memory\n");
    return 1;
  }
  *piece = new_piece;
  int32_t n;

  for (int32_t i = 0, piece_dld = 0; piece_dld != piece_size; ++i) {
    // request block
    uint32_t block_size = min(piece_size - piece_dld, 1 << 14);
    *(uint32_t *)buf = htonl(13);
    buf[4] = 6;
    *(uint32_t *)(buf + 5) = htonl(index);
    *(uint32_t *)(buf + 9) = htonl(i << 14);
    *(uint32_t *)(buf + 13) = htonl(block_size);
    assert(send(sockfd, buf, 17, 0) == 17);

    // receive block
    assert(recv(sockfd, buf, 13, 0) == 13);
    assert(buf[4] == 7);
    n = ntohl(*(uint32_t *)buf) - 9;
    for (int32_t block_dld = 0; block_dld != n;) {
      block_dld += recv(sockfd, buf + 13 + block_dld, n - block_dld, 0);
      assert(block_dld > 0);
      printf("##### downloading: %d\n", block_dld);
    }
    memcpy(*piece + piece_dld, buf + 13, block_size);
    piece_dld += block_size;

    printf("%d\n", buf[4]);
    uint32_t i = ntohl(*(uint32_t *)(buf + 5));
    uint32_t b = ntohl(*(uint32_t *)(buf + 9));
    printf("Index: %d\n", i);
    printf("Begin: %d\n", b);
    printf("Downloaded: %d\n", block_size);
  }
  return 0;
}

int32_t verify_piece(uint8_t *piece, uint32_t piece_size, uint8_t *hash) {
  uint8_t md[20];
  SHA1(piece, piece_size, md);
  assert(memcmp(md, hash, 20) == 0);
  return 0;
}

int32_t save_piece(uint8_t *piece, uint32_t piece_size, char *filename) {
  FILE *f = fopen(filename, "a");
  if (f == NULL) {
    perror("Failed to open file");
    return 1;
  }
  assert(fwrite(piece, sizeof(uint8_t), piece_size, f) == piece_size);
  fclose(f);
  return 0;
}

int32_t download(char *outfile, char *filename, char *piece_index) {
  char *buf = read_file(filename);
  assert(buf != NULL);

  bestring_t res = {.str = malloc(0), .n = 0};
  assert(perform_get_request(buf, &res) == 0);
  bevalue_t res_v;
  char *s = res.str;
  assert(next_value(&s, &res_v) == 0);

  bevalue_t *peers_v = bevec_dict_get(&res_v.val.vec, "peers");
  assert(peers_v != NULL && peers_v->type == BE_STR);

  uint8_t *ptr = (uint8_t *)peers_v->val.str.str;

  int32_t sockfd;
  struct sockaddr_in addr;
  assert((sockfd = socket(AF_INET, SOCK_STREAM, 0)) > 0);
  addr.sin_family = AF_INET;
  addr.sin_port = *(uint16_t *)(ptr + 4);
  addr.sin_addr.s_addr = *(uint32_t *)ptr;
  assert(connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == 0);

  uint8_t data_buf[20000] = {0};
  uint32_t n = 0;

  assert(perform_handshake(sockfd, buf, data_buf) == 0);

  // receive bitfield
  n = recv(sockfd, data_buf, 4, 0);
  if (n == 0) {
    printf("Peer does not have any piece\n");
    exit(0);
  }
  assert(n == 4);
  n = ntohl(*(uint32_t *)data_buf);
  assert(n <= 20000);
  assert(recv(sockfd, data_buf + 4, n, 0) == n);
  assert(data_buf[4] == 5);

  uint8_t bitfield[n - 1];
  memcpy(bitfield, data_buf + 5, n - 1);

  // express interest
  *(uint32_t *)data_buf = htonl(1);
  data_buf[4] = 2;
  assert(send(sockfd, data_buf, 5, 0) == 5);

  // expect unchoked message
  assert(recv(sockfd, data_buf, 4, 0) == 4);
  n = ntohl(*(uint32_t *)data_buf);
  assert(n <= 20000);
  assert(recv(sockfd, data_buf + 4, n, 0) == n);
  assert(data_buf[4] == 1);

  bevalue_t v;
  s = buf;
  assert(next_value(&s, &v) == 0);
  bevalue_t *info_v = bevec_dict_get(&v.val.vec, "info");
  assert(info_v != NULL && info_v->type == BE_VEC && info_v->val.vec.is_dict);
  bevalue_t *length_v = bevec_dict_get(&info_v->val.vec, "length");
  assert(length_v != NULL && length_v->type == BE_INT);
  bevalue_t *pieces_v = bevec_dict_get(&info_v->val.vec, "pieces");
  assert(pieces_v != NULL && pieces_v->type == BE_STR);
  bevalue_t *piece_length_v = bevec_dict_get(&info_v->val.vec, "piece length");
  assert(piece_length_v != NULL && piece_length_v->type == BE_INT);

  uint32_t total_length = length_v->val.i;
  uint32_t piece_length = piece_length_v->val.i;
  int32_t index = atoi(piece_index);
  uint32_t piece_size = min(total_length - index * piece_length, piece_length);
  uint8_t *piece = NULL;
  assert(download_piece(sockfd, data_buf, index, piece_size, &piece) == 0);
  assert(verify_piece(piece, piece_size,
                      (uint8_t *)pieces_v->val.str.str +
                          index * SHA_DIGEST_LENGTH) == 0);
  assert(save_piece(piece, piece_size, outfile) == 0);

  bevalue_free(&v);
  bevalue_free(&res_v);
  free(res.str);
  free(piece);
  free(buf);
  return 0;
}

int32_t download_everything(char *outfile, char *filename) {
  char *buf = read_file(filename);
  assert(buf != NULL);

  bestring_t res = {.str = malloc(0), .n = 0};
  assert(perform_get_request(buf, &res) == 0);
  bevalue_t res_v;
  char *s = res.str;
  assert(next_value(&s, &res_v) == 0);

  bevalue_t *peers_v = bevec_dict_get(&res_v.val.vec, "peers");
  assert(peers_v != NULL && peers_v->type == BE_STR);

  uint8_t *ptr = (uint8_t *)peers_v->val.str.str;

  int32_t sockfd;
  struct sockaddr_in addr;
  assert((sockfd = socket(AF_INET, SOCK_STREAM, 0)) > 0);
  addr.sin_family = AF_INET;
  addr.sin_port = *(uint16_t *)(ptr + 4);
  addr.sin_addr.s_addr = *(uint32_t *)ptr;
  assert(connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == 0);

  uint8_t data_buf[20000] = {0};
  uint32_t n = 0;

  assert(perform_handshake(sockfd, buf, data_buf) == 0);

  // receive bitfield
  n = recv(sockfd, data_buf, 4, 0);
  if (n == 0) {
    printf("Peer does not have any piece\n");
    exit(0);
  }
  assert(n == 4);
  n = ntohl(*(uint32_t *)data_buf);
  assert(n <= 20000);
  assert(recv(sockfd, data_buf + 4, n, 0) == n);
  assert(data_buf[4] == 5);

  uint8_t bitfield[n - 1];
  memcpy(bitfield, data_buf + 5, n - 1);

  // express interest
  *(uint32_t *)data_buf = htonl(1);
  data_buf[4] = 2;
  assert(send(sockfd, data_buf, 5, 0) == 5);

  // expect unchoked message
  assert(recv(sockfd, data_buf, 4, 0) == 4);
  n = ntohl(*(uint32_t *)data_buf);
  assert(n <= 20000);
  assert(recv(sockfd, data_buf + 4, n, 0) == n);
  assert(data_buf[4] == 1);

  bevalue_t v;
  s = buf;
  assert(next_value(&s, &v) == 0);
  bevalue_t *info_v = bevec_dict_get(&v.val.vec, "info");
  assert(info_v != NULL && info_v->type == BE_VEC && info_v->val.vec.is_dict);
  bevalue_t *length_v = bevec_dict_get(&info_v->val.vec, "length");
  assert(length_v != NULL && length_v->type == BE_INT);
  bevalue_t *pieces_v = bevec_dict_get(&info_v->val.vec, "pieces");
  assert(pieces_v != NULL && pieces_v->type == BE_STR);
  bevalue_t *piece_length_v = bevec_dict_get(&info_v->val.vec, "piece length");
  assert(piece_length_v != NULL && piece_length_v->type == BE_INT);

  int32_t index = 0;
  uint32_t total_length = length_v->val.i;
  uint32_t piece_length = piece_length_v->val.i;
  uint32_t remaining = total_length;
  n = sizeof(bitfield) / sizeof(bitfield[0]);
  for (int32_t i = 0; i < n; ++i) {
    for (int32_t j = 7; j >= 0; --j, ++index) {
      if (1 << j & bitfield[i]) {
        uint8_t *piece = NULL;
        uint32_t piece_size = min(remaining, piece_length);
        assert(download_piece(sockfd, data_buf, index, piece_size, &piece) ==
               0);
        assert(verify_piece(piece, piece_size,
                            (uint8_t *)pieces_v->val.str.str +
                                index * SHA_DIGEST_LENGTH) == 0);
        assert(save_piece(piece, piece_size, outfile) == 0);
        free(piece);
        remaining -= piece_size;
      }
    }
  }

  bevalue_free(&v);
  bevalue_free(&res_v);
  free(res.str);
  free(buf);
  return 0;
}

int32_t main(int32_t argc, char **argv) {
  if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
    fprintf(stderr, "Failed to initalize curl\n");
    return 1;
  }
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
  } else if (strcmp(argv[1], "peers") == 0) {
    if (discover(argv[2]) != 0) {
      return 1;
    }
  } else if (strcmp(argv[1], "handshake") == 0) {
    if (handshake(argv[2], argv[3]) != 0) {
      return 1;
    }
  } else if (strcmp(argv[1], "download_piece") == 0) {
    assert(strcmp(argv[2], "-o") == 0);
    if (download(argv[3], argv[4], argv[5]) != 0) {
      return 1;
    }
  } else if (strcmp(argv[1], "download") == 0) {
    assert(strcmp(argv[2], "-o") == 0);
    if (download_everything(argv[3], argv[4]) != 0) {
      return 1;
    }
  } else {
    fprintf(stderr, "Not implemented\n");
    return 1;
  }

  return 0;
}
