#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool is_digit(char c) { return c >= '0' && c <= '9'; }

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

int32_t decode(char *s, char **ret) {
  char *t = strchr(s, ':');
  if (t == NULL) {
    fprintf(stderr, "Invalid bencode string\n");
    return 1;
  }
  // ensure there is something before ":"
  if (s == t) {
    fprintf(stderr, "Invalid length\n");
    return 1;
  }
  // ensure all characters before ":" are digits
  for (char *p = s; p != t; ++p) {
    if (!is_digit(*p)) {
      fprintf(stderr, "Invalid length\n");
      return 1;
    }
  }
  int32_t len = atoi(s);
  int32_t str_len = strlen(t + 1);
  if (len > str_len) {
    fprintf(stderr, "Provided length is bigger than the string length\n");
    return 1;
  }
  *(t + 1 + len) = '\0';
  *ret = t + 1;
  return 0;
}

int32_t main(int32_t argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: your_bittorrent.sh <command> <args>\n");
    return 1;
  }

  if (strcmp(argv[1], "decode") == 0) {
    char *d_str = NULL;
    if (decode(argv[2], &d_str) != 0) {
      fprintf(stderr, "Failed to decode bencoded string\n");
      return 1;
    }
    printf("\"%s\"\n", d_str);
    return 0;
  } else {
    fprintf(stderr, "Not implemented\n");
    return 1;
  }
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
