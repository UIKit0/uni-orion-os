/*
 * fixed_point.c
 *
 *  Created on: Nov 5, 2013
 *      Author: osd
 */

#include "fixed_point.h"

//for some reason if q is defined as a constant, the compiler complains
#define q 14 // fractional part
const int f = 1 << q; // conversion constant

/*
 * converts an integer number to
 * fixed_point representation
 */
int64_t fp_from_int(int n){
	return (int64_t)n * f;
}

/*
 * converts a number from fixed_point
 * representation to integer by rounding
 * the result to zero
 */
int fp_to_int_rz(int64_t fp){
	return (int)(fp / f);
}

/*
 * converts a number from fixed_point
 * representation to integer by rounding
 * the result to the nearest integer
 */
int fp_to_int_rn(int64_t fp){
	return fp >= 0 ? (fp + f / 2) / f : (fp - f / 2) / f;
}

/*
 * returns the sum between two numbers
 * in fixed_point representation
 */
int64_t fp_add(int64_t fp_x, int64_t fp_y)
{
	return fp_x + fp_y;
}

/*
 * returns the difference between two numbers
 * in fixed_point representation
 */
int64_t fp_subtract(int64_t fp_x, int64_t fp_y)
{
	return fp_x - fp_y;
}

/*
 * returns the product between two numbers
 * in fixed_point representation
 */
int64_t fp_mult(int64_t fp_x, int64_t fp_y)
{
	return fp_x * fp_y / f;
}

/*
 * returns the division between two numbers
 * in fixed_point representation
 */
int64_t fp_div(int64_t fp_x, int64_t fp_y)
{
	return fp_x * f / fp_y;
}

/*
 * returns the sum in fixed_point representation
 * between a number in fixed_point representation
 * and an integer
 */
int64_t fp_int_add(int64_t fp, int n)
{
	return fp + fp_from_int(n);
}

/*
 * returns the difference in fixed_point representation
 * between a number in fixed_point representation
 * and an integer
 */
int64_t fp_int_subtract(int64_t fp, int n)
{
	return fp - fp_from_int(n);
}

/*
 * returns the product in fixed_point representation
 * between a number in fixed_point representation
 * and an integer
 */
int64_t fp_int_mult(int64_t fp, int n)
{
	return fp * n;
}

/*
 * returns the division in fixed_point representation
 * between a number in fixed_point representation
 * and an integer
 */
int64_t fp_int_div(int64_t fp, int n)
{
	return fp / n;
}
