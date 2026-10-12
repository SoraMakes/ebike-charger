#pragma once

#include <cstdint>
#include <string>

extern uint32_t testNowMs;
extern int testButton1Level;
extern int testButton2Level;
extern int testAdcReading;
void clearTestSerialOutput();
const std::string &getTestSerialOutput();
