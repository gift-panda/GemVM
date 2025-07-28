#include <math.h>

#include "value.h"
#include <stdbool.h>

#define CHECK_NUM_ARGS(fn, n)

#define CHECK_ARG_TYPE(fn, index, predicate)


static Value math_abs(int argCount, Value* args) {
  CHECK_NUM_ARGS("abs", 1);
  CHECK_ARG_TYPE("abs", 0, IS_NUMBER);
  return NUMBER_VAL(fabs(AS_NUMBER(args[0])));
}
#include <stdio.h>
static Value math_min(int argCount, Value* args) {
  CHECK_NUM_ARGS("min", 2);
  CHECK_ARG_TYPE("min", 0, IS_NUMBER);
  CHECK_ARG_TYPE("min", 1, IS_NUMBER);
  Value a = args[0];
  Value b = args[1];
  if (AS_NUMBER(a) < AS_NUMBER(b))
    return a;
  return b;
}

static Value math_max(int argCount, Value* args) {
  CHECK_NUM_ARGS("max", 2);
  CHECK_ARG_TYPE("max", 0, IS_NUMBER);
  CHECK_ARG_TYPE("max", 1, IS_NUMBER);
  return NUMBER_VAL(fmax(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value math_clamp(int argCount, Value* args) {
  CHECK_NUM_ARGS("clamp", 3);
  double x = AS_NUMBER(args[0]);
  double a = AS_NUMBER(args[1]);
  double b = AS_NUMBER(args[2]);
  return NUMBER_VAL(fmax(a, fmin(b, x)));
}

static Value math_sign(int argCount, Value* args) {
  CHECK_NUM_ARGS("sign", 1);
  double x = AS_NUMBER(args[0]);
  return NUMBER_VAL((x > 0) - (x < 0));
}

static Value math_pow(int argCount, Value* args) {
  CHECK_NUM_ARGS("pow", 2);
  return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value math_sqrt(int argCount, Value* args) {
  CHECK_NUM_ARGS("sqrt", 1);
  return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

static Value math_cbrt(int argCount, Value* args) {
  CHECK_NUM_ARGS("cbrt", 1);
  return NUMBER_VAL(cbrt(AS_NUMBER(args[0])));
}

static Value math_exp(int argCount, Value* args) {
  CHECK_NUM_ARGS("exp", 1);
  return NUMBER_VAL(exp(AS_NUMBER(args[0])));
}

static Value math_log(int argCount, Value* args) {
  CHECK_NUM_ARGS("log", 1);
  return NUMBER_VAL(log(AS_NUMBER(args[0])));
}

static Value math_log10(int argCount, Value* args) {
  CHECK_NUM_ARGS("log10", 1);
  return NUMBER_VAL(log10(AS_NUMBER(args[0])));
}

static Value math_sin(int argCount, Value* args) {
  CHECK_NUM_ARGS("sin", 1);
  return NUMBER_VAL(sin(AS_NUMBER(args[0])));
}

static Value math_cos(int argCount, Value* args) {
  CHECK_NUM_ARGS("cos", 1);
  return NUMBER_VAL(cos(AS_NUMBER(args[0])));
}

static Value math_tan(int argCount, Value* args) {
  CHECK_NUM_ARGS("tan", 1);
  return NUMBER_VAL(tan(AS_NUMBER(args[0])));
}

static Value math_asin(int argCount, Value* args) {
  CHECK_NUM_ARGS("asin", 1);
  return NUMBER_VAL(asin(AS_NUMBER(args[0])));
}

static Value math_acos(int argCount, Value* args) {
  CHECK_NUM_ARGS("acos", 1);
  return NUMBER_VAL(acos(AS_NUMBER(args[0])));
}

static Value math_atan(int argCount, Value* args) {
  CHECK_NUM_ARGS("atan", 1);
  return NUMBER_VAL(atan(AS_NUMBER(args[0])));
}

static Value math_atan2(int argCount, Value* args) {
  CHECK_NUM_ARGS("atan2", 2);
  return NUMBER_VAL(atan2(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value math_floor(int argCount, Value* args) {
  CHECK_NUM_ARGS("floor", 1);
  return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

static Value math_ceil(int argCount, Value* args) {
  CHECK_NUM_ARGS("ceil", 1);
  return NUMBER_VAL(ceil(AS_NUMBER(args[0])));
}

static Value math_round(int argCount, Value* args) {
  CHECK_NUM_ARGS("round", 1);
  return NUMBER_VAL(round(AS_NUMBER(args[0])));
}

static Value math_trunc(int argCount, Value* args) {
  CHECK_NUM_ARGS("trunc", 1);
  return NUMBER_VAL(trunc(AS_NUMBER(args[0])));
}

static Value math_mod(int argCount, Value* args) {
  CHECK_NUM_ARGS("mod", 2);
  return NUMBER_VAL(fmod(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value math_lerp(int argCount, Value* args) {
  CHECK_NUM_ARGS("lerp", 3);
  double a = AS_NUMBER(args[0]);
  double b = AS_NUMBER(args[1]);
  double t = AS_NUMBER(args[2]);
  return NUMBER_VAL(a + t * (b - a));
}
