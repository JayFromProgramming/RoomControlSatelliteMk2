//
// Created by Jay on 10/14/2024.
//

#ifndef ROOMINTERFACEDATASTRUCTURES_H
#define ROOMINTERFACEDATASTRUCTURES_H
#include <Arduino.h>

struct ParsedArg {
    union {
        int intVal;
        float floatVal;
        char* stringVal;
        bool boolVal;
    } value;
    enum {
        UNKNOWN,
        INT,
        FLOAT,
        STRING,
        BOOL
    } type;
};
struct ParsedKwarg {
    char* key;
    ParsedArg value;
};
typedef struct {
    ParsedArg args[10];
    ParsedKwarg* kwargs[10];
    char* objectName;
    char* eventName;
    uint8_t numArgs;
    uint8_t numKwargs;
    char stringBuffer[512]; // .5KB buffer for storing string values and kwarg keys
    uint16_t stringIndex = 0;
    bool finished;
    JsonDocument document;
} ParsedEvent_t;

#endif //ROOMINTERFACEDATASTRUCTURES_H
