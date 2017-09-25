#pragma once


#include <complex>


float ScalarProductSimple(float* a, float* b, size_t dim);
float ScalarProductAVX(float* a, float* b, size_t dim);
float ScalarProduct(float* a, float* b, size_t dim);

std::complex<float> ScalarProduct(float* ar, float* ai, float* b, size_t dim);