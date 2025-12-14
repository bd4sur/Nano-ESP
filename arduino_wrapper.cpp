// wrapper.cpp
#include <Arduino.h>
#include <esp32-hal-psram.h>

extern "C" {

void print_str(char* msg) {
    Serial.print(msg);
}
void print_num(int i) {
    Serial.print(i);
}
void print_float(float i) {
    Serial.print(i);
}
void *psram_calloc(size_t n, size_t sizeoftype) {
    return ps_calloc(n, sizeoftype);
}

}
