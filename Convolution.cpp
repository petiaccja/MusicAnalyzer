#pragma once

#include "Convolution.hpp"

#include <immintrin.h>
#include <algorithm>


float ScalarProductSimple(float* a, float* b, size_t dim) {
	float sum = 0.0;
	for (size_t i = 0; i < dim; ++i, ++a, ++b) {
		sum += (*a) * (*b);
	}
	return sum;
}


float ScalarProductAVX(float* a, float* b, size_t dim) {
	size_t ptr_a = reinterpret_cast<size_t>(a);
	size_t ptr_b = reinterpret_cast<size_t>(b);

	// if a or b is not __m256-byte aligned, then cannot use AVX
	if (ptr_a % sizeof(__m256) != 0 || ptr_b % sizeof(__m256) != 0) {
		return ScalarProductSimple(a, b, dim);
	}
	else {
		float sum = 0;
		size_t avx_dim = dim / 8;
		__m256* avx_a = reinterpret_cast<__m256*>(a);
		__m256* avx_b = reinterpret_cast<__m256*>(b);
		for (size_t i = 0; i < avx_dim; ++i) {
			__m256 result = _mm256_dp_ps(*avx_a, *avx_b, int(0xFFFFFFF0u + 0b0001u));
			sum += result.m256_f32[0];
			sum += result.m256_f32[4];
			++avx_a;
			++avx_b;
		}
		return sum;
	}
}

float ScalarProduct(float* a, float* b, size_t dim) {
	size_t ptr_a = reinterpret_cast<size_t>(a);
	size_t ptr_b = reinterpret_cast<size_t>(b);

	int align_a = ptr_a % sizeof(__m256);
	int align_b = ptr_b % sizeof(__m256);

	// avx path
	if (align_a == align_b && (align_a % sizeof(float)) == 0) {
		float sum = 0;

		size_t avx_ptr_a = (ptr_a + sizeof(__m256) - 1) / sizeof(__m256) * sizeof(__m256);
		size_t avx_ptr_b = (ptr_b + sizeof(__m256) - 1) / sizeof(__m256) * sizeof(__m256);

		float* avx_a = reinterpret_cast<float*>(avx_ptr_a);
		float* avx_b = reinterpret_cast<float*>(avx_ptr_b);

		int dimFirst = avx_a - a;
		sum += ScalarProductSimple(a, b, dimFirst);
		int dimAvx = (dim - dimFirst) / 8 * 8;
		sum += ScalarProductAVX(avx_a, avx_b, dimAvx);
		int dimLast = dim - dimAvx - dimFirst;
		sum += ScalarProductSimple(a + dimAvx + dimFirst, b + dimAvx + dimFirst, dimLast);

		return sum;
	}
	// scalar path
	else {
		return ScalarProductSimple(a, b, dim);
	}
}


std::complex<float> ScalarProduct(float* ar, float* ai, float* b, size_t dim) {
	std::complex<float> res;
	res.real(ScalarProduct(ar, b, dim));
	res.imag(ScalarProduct(ai, b, dim));
	return res;
}