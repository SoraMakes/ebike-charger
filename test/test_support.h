#pragma once

#include <cstdint>
#include <string>

extern uint32_t testNowMs;
extern float testOutputVoltageV;
void clearTestSerialOutput();
const std::string &getTestSerialOutput();
