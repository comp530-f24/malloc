/* -*- mode:c; c-file-style:"k&r"; c-basic-offset: 4; tab-width:4;
 * indent-tabs-mode:nil; mode:auto-fill; fill-column:78; -*- */
/* vim: set ts=4 sw=4 et tw=78 fo=cqt wm=0: */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
  void *x = malloc(5);
  printf("Hello %p\n", x);
  return (errno);
}
