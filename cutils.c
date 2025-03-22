#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cutils.h"

char *to_lowercase(const char *str) {
  if (!str)
    return NULL;

  char *result = strdup(str);
  for (int i = 0; result[i]; i++) {
    result[i] = tolower(result[i]);
  }
  return result;
}