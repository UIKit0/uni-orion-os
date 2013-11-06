/*
 * fixed_point.h
 *
 *  Created on: Nov 5, 2013
 *      Author: Orion Team
 */

#ifndef FIXED_POINT_H_
#define FIXED_POINT_H_

int64_t fp_from_int(int n);

int fp_to_int_rz(int64_t fp);

int fp_to_int_rn(int64_t fp);

int64_t fp_add(int64_t x, int64_t y);

int64_t fp_subtract(int64_t x, int64_t y);

int64_t fp_mult(int64_t x, int64_t y);

int64_t fp_div(int64_t x, int64_t y);

int64_t fp_int_add(int64_t x, int y);

int64_t fp_int_subtract(int64_t x, int y);

int64_t fp_int_mult(int64_t x, int y);

int64_t fp_int_div(int64_t x, int y);


#endif /* FIXED_POINT_H_ */
