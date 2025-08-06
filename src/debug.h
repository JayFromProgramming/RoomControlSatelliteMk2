//
// Created by Aidan on 7/21/2025.
//
#pragma once
#ifndef DEBUG_H
#define DEBUG_H

#define PRINT_TIME() Serial.printf("%010lu - ", micros())
#define PRINT_FUNCTION() Serial.print(__PRETTY_FUNCTION__); Serial.print(" - ")
#define PRINT_HEADER() PRINT_TIME(); PRINT_FUNCTION()
#define DEBUG_PRINT(...) PRINT_HEADER(); Serial.printf(__VA_ARGS__); Serial.println()

#endif //DEBUG_H
