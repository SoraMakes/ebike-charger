#pragma once

#include <cstdint>
#include <string>

extern uint32_t testNowMs;
extern float testOutputVoltageV;
extern int testButton1Level;
extern int testButton2Level;
void clearTestSerialOutput();
const std::string &getTestSerialOutput();
