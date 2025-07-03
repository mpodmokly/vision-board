#ifndef SIGN_DETECTOR_H
#define SIGN_DETECTOR_H

#include <stdint.h>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

extern tflite::MicroInterpreter* interpreter;
extern TfLiteTensor* input;
extern TfLiteTensor* output;

int detect_in_image(uint8_t* image_rgb888, int width, int height, float* out_confidence);
void init_buffers();

#endif // SIGN_DETECTOR_H
