#ifndef WRAPPER_H
#define WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "utils.h"

void print_str(char* msg);
void print_num(int i);
void print_float(float i);

void *psram_calloc(size_t n, size_t sizeoftype);

int32_t worker(uint8_t worker_i2c_addr, float *x, uint32_t layer_from, uint32_t layer_to, uint32_t pos, uint32_t max_seq_len, uint32_t n_embd);

#ifdef __cplusplus
}
#endif

#endif