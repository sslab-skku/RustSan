#include <stdint.h>
#include <stdlib.h>
uint64_t *__shadow_hit_counter;

uint64_t __get_shadow_hit_counter() {
  if (__shadow_hit_counter == NULL)
    return 0;
  return *__shadow_hit_counter;
}

void __init_shadow_hit_counter() {
  if (__shadow_hit_counter == NULL)
    __shadow_hit_counter = malloc(8);
  *__shadow_hit_counter = 0;
}
