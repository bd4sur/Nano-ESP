#ifndef WRAPPER_H
#define WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

void print_str(char* msg);
void print_num(int i);
void print_float(float i);

void *psram_calloc(size_t n, size_t sizeoftype);

#ifdef __cplusplus
}
#endif

#endif