/*
 * fixed_point.c
 *
 *  Created on: Nov 5, 2013
 *      Author: osd
 */

#include "fixed_point.h"

const int q = 14; // fractional part
const int f = 1 << q; // conversion constant

int64_t fp_from_int(int n){
	return (int64_t)n * f;
}

int fp_to_int_rz(int64_t fp){
	return (int)(fp / f);
}

int fp_to_int_rn(int64_t fp){
	return fp >= 0 ? (fp + f / 2) / f : (fp - f / 2) / f;
}

