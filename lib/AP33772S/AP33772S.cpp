/*
AP33772S.h - Class file for the AP33772S USB-C PD 3.1 Sink Controller Arduino Library.

Version: 1.1.0
(c) 2024 CentyLab
www.centylab.com

This program is free software: you can redistribute it and/or modify
it under the terms of the version 3 GNU General Public License as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#if ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include "AP33772S.h"

byte AP33772S::readBuf[READ_BUFF_LENGTH] = {0};
byte AP33772S::writeBuf[WRITE_BUFF_LENGTH] = {0};
int AP33772S::_voltageAVSbyte = 0;
int AP33772S::_currentAVSbyte = 0;
int AP33772S::_indexAVS = 0;

/**
 * @brief Class constuctor
 * @param &wire reference of Wire class. Pass in Wire or Wire1
 */
AP33772S::AP33772S(TwoWire &wire)
{
    _i2cPort = &wire;
}

bool AP33772S::begin(TwoWire &wire)
{
    _i2cPort = &wire;
    return begin();
}

bool AP33772S::begin()
{
    _i2cPort->beginTransmission(AP33772S_ADDRESS);
    if (_i2cPort->endTransmission() != 0) {
        return false;
    }
    // Insufficient delay after I2C init before the capability request has
    // caused "no PDOs read" symptoms on other AP33772S boards. Give the chip
    // time to settle before reading the PDO table.
    // This blocks startup configuration; attached sources may negotiate before
    // CONFIG/MASK/SYSTEM are applied within the ~100 ms STARTED window.
    delay(1000);
    return refreshPdoTable();
}

bool AP33772S::refreshPdoTable()
{
    if (!i2c_read(AP33772S_ADDRESS, CMD_SRCPDO, MAX_PDO_ENTRIES * 2)) {
        return false;
    }
    memset(SRC_SPRandEPRpdoArray, 0, sizeof(SRC_SPRandEPRpdoArray));
    for (int i = 0; i < MAX_PDO_ENTRIES * 2; i += 2) {
        int pdoIndex = (i / 2);
        SRC_SPRandEPRpdoArray[pdoIndex].byte0 = readBuf[i];
        SRC_SPRandEPRpdoArray[pdoIndex].byte1 = readBuf[i + 1];
    }
    mapPPSAVSInfo();
    return true;
}

bool AP33772S::setInterruptRegisterMask(uint8_t mask)
{
    writeBuf[0] = mask;
    return i2c_write(AP33772S_ADDRESS, CMD_MASK, 1);
}

uint8_t AP33772S::readStatus()
{
    if (!i2c_read(AP33772S_ADDRESS, CMD_STATUS, 1)) {
        return 0;
    }
    return readBuf[0];
}

uint8_t AP33772S::readOpMode()
{
    if (!i2c_read(AP33772S_ADDRESS, CMD_OPMODE, 1)) {
        return 0;
    }
    return readBuf[0];
}

uint8_t AP33772S::readMsgResult()
{
    if (!i2c_read(AP33772S_ADDRESS, CMD_PD_MSGRLT, 1)) {
        return 0;
    }
    return readBuf[0];
}

bool AP33772S::requestFixedPDO(uint8_t pdoIndex)
{
    if (!isFixedPdoAt(pdoIndex)) return false;
    // Request at the PDO's full advertised current (CURRENT_SEL = current_max).
    RDO_DATA_T rdo{};
    rdo.REQMSG_Fields.PDO_INDEX   = pdoIndex;
    rdo.REQMSG_Fields.CURRENT_SEL = SRC_SPRandEPRpdoArray[pdoIndex - 1].fixed.current_max;
    writeBuf[0] = rdo.byte0;
    writeBuf[1] = rdo.byte1;
    return i2c_write(AP33772S_ADDRESS, CMD_PD_REQMSG, 2);
}

bool AP33772S::isFixedPdoAt(int pdoIndex) const
{
    if (pdoIndex < 1 || pdoIndex > MAX_PDO_ENTRIES) return false;
    const SRC_SPRandEPR_PDO_Fields &pdo = SRC_SPRandEPRpdoArray[pdoIndex - 1];
    // DETECT (bit 15) is the datasheet's own validity flag, and PD_REQMSG's
    // PDO_INDEX is only valid for a detected PDO. It subsumes the old
    // "both bytes zero" heuristic, which would accept a slot holding stale or
    // partially-decoded data with DETECT clear.
    if (pdo.fixed.detect == 0) return false;
    return pdo.fixed.type == 0;
}

float AP33772S::getPdoVoltageV(int pdoIndex) const
{
    if (!isFixedPdoAt(pdoIndex)) return 0.0f;
    // SPR slots 1..7 use 100 mV units, EPR slots 8..13 use 200 mV.
    const bool isEPR = (pdoIndex >= 8);
    return SRC_SPRandEPRpdoArray[pdoIndex - 1].fixed.voltage_max * (isEPR ? 200.0f : 100.0f) / 1000.0f;
}

float AP33772S::getPdoMaxCurrentA(int pdoIndex) const
{
    if (!isFixedPdoAt(pdoIndex)) return 0.0f;
    // Conservative lower bound of the CURRENT_MAX bucket, inverse of currentMap().
    const uint8_t code = SRC_SPRandEPRpdoArray[pdoIndex - 1].fixed.current_max;
    // Code 0 is the bucket "0.00A ~ 1.24A (Less than)", so its conservative
    // lower bound is 0 A, not 1 A. Returning 1 A was the one branch that was an
    // optimistic guess rather than a lower bound, and it over-budgeted the
    // power limit by up to 2x for any source advertising such a profile.
    // Callers must therefore treat 0 A as "unusable" - see findBestFixedPdo().
    if (code == 0) return 0.0f;
    if (code >= 15) return 5.0f;
    return (1250 + (code - 1) * 250) / 1000.0f;
}

/**
 * @brief Print all available profile out to Serial
 */
void AP33772S::displayProfiles()
{
  Serial.println("Profile list detected: ");
  for (int i = 0; i < 26; i += 2) {
    int pdoIndex = (i / 2);  // Calculate the PDO index
    displayPDOInfo(pdoIndex);
  }
}

	
/**
 * @brief Hard reset
 */
 void AP33772S::reset()
 {
  i2c_read(AP33772S_ADDRESS, CMD_PD_CMDMSG, 1);
  readBuf[0] |= 1;   // Set the bit
  writeBuf[0] = readBuf[0];
  i2c_write(AP33772S_ADDRESS, CMD_PD_CMDMSG, 1);
 }

/**
 * @brief Decode PDO information from SRC_SPRandEPR_PDO_Fields
 * @param pdoIndex feed from loop
 */
void AP33772S::displayPDOInfo(int pdoIndex) {
  // Determine if it's SPR or EPR based on pdoIndex
  bool isEPR = (pdoIndex >= 7 && pdoIndex <= 12);  // 1-6 for SPR, 7-12 for EPR
  // Check if both bytes are zero
  if (SRC_SPRandEPRpdoArray[pdoIndex].byte0 == 0 && SRC_SPRandEPRpdoArray[pdoIndex].byte1 == 0) {
    return;  // If both bytes are zero, exit the function
  }
  
  // Print the PDO type and index
  Serial.print(pdoIndex <= 6 ? " SRC_SPR_PDO" : " SRC_EPR_PDO");
  Serial.print(pdoIndex+1);
  Serial.print(": ");
  
  // Now, the individual fields can be accessed through the union in the struct
  if (SRC_SPRandEPRpdoArray[pdoIndex].fixed.type == 0) {  // Fixed PDO
    // Print parsed values
    Serial.print("Fixed PDO: ");
    Serial.print(SRC_SPRandEPRpdoArray[pdoIndex].fixed.voltage_max * (isEPR ? 200 : 100)); // Voltage in 200mV units for EPR, 100mV for SPR
    Serial.print("mV ");
    displayCurrentRange(SRC_SPRandEPRpdoArray[pdoIndex].fixed.current_max);  // Assuming displayCurrentRange function is available
  } else {  // PPS or AVS PDO
    // Print parsed values
    Serial.print(isEPR ? "AVS PDO: " : "PPS PDO: ");
    if (isEPR) {
      displayEPRVoltageMin(SRC_SPRandEPRpdoArray[pdoIndex].avs.voltage_min);  // Assuming displayVoltageMin function is available
    } else {
      displaySPRVoltageMin(SRC_SPRandEPRpdoArray[pdoIndex].pps.voltage_min);  // Assuming displayVoltageMin function is available
    }
    Serial.print(SRC_SPRandEPRpdoArray[pdoIndex].fixed.voltage_max * (isEPR ? 200 : 100)); // Maximum Voltage in 200mV units for EPR, 100mV for SPR
    Serial.print("mV ");
    displayCurrentRange(SRC_SPRandEPRpdoArray[pdoIndex].fixed.current_max);  // Assuming displayCurrentRange function is available
  }
  Serial.println();
}

/**
 * @brief Search through the list of profile and look for PPS, AVS
 * @bug If system has 2 PPS profiles. The index only show the last one. Use displayPDOInfo() to check.
 */
void AP33772S::mapPPSAVSInfo()
{
  _indexPPSUser = -1;
  _indexAVSUser = -1;
  for(int i = 1; i<=13; i++)
  {
    if(i < 8 && SRC_SPRandEPRpdoArray[i-1].pps.type == 1)
    {
      _indexPPSUser = i;
    }
    else if(i >= 8 && SRC_SPRandEPRpdoArray[i-1].avs.type == 1)
    {
      _indexAVSUser = i;
    }
  }
}


/**
 * @brief Request fixed PDO voltage, work for both standard and EPR mode
 * @param pdoIndex index 1
 * @param max_current unit in mA
 */
void AP33772S::setFixPDO(int pdoIndex, int max_current) 
{
  // cancelAVSTimer();

  RDO_DATA_T rdoData;

  // Max current sanity check
  if(max_current <= 0) return;

  // For Fix voltage, only need to set PDO_INDEX and CURRENT_SEL
  // No need to change the selected voltage
  // handle the same in standard as well as EPR

  // PDO index need to be fixed type
  if(SRC_SPRandEPRpdoArray[pdoIndex-1].fixed.type == 0){
    // Now that we are in fix PDO mode
    Serial.println("Type is fixed."); // DEBUG

    rdoData.REQMSG_Fields.PDO_INDEX = pdoIndex;  // Index 1

    // Serial.printf("You entered current: %dmA", max_current); //DEBUG
    // Serial.println(SRC_SPRandEPRpdoArray[pdoIndex].fixed.current_max); //DEBUG
    // Serial.println(currentMap(max_current)); //DEBUG
    if(currentMap(max_current) > SRC_SPRandEPRpdoArray[pdoIndex-1].fixed.current_max) 
    {
      Serial.println("Current not in range."); // DEBUG
      return; // Check if current setting is in range
    }

    rdoData.REQMSG_Fields.CURRENT_SEL = currentMap(max_current);
    // Note: For profile less than or equal to 3A power, CURRENT_SEL = 9 will not work.
    // rdoData.REQMSG_Fields.CURRENT_SEL = 9; 
    writeBuf[0] = rdoData.byte0;  // Store the upper 8 bits
    writeBuf[1] = rdoData.byte1;  // Store the lower 8 bits
    i2c_write(AP33772S_ADDRESS, CMD_PD_REQMSG, 2);
  }
  return;
}

/**
 * @brief Request PPS voltage
 * @param pdoIndex index 1
 * @param target_voltage unit in mV
 * @param max_current unit in mA
 * @bug only work if min PPS voltage is 3.3V
 */
void AP33772S::setPPSPDO(int pdoIndex, int target_voltage, int max_current) 
{
  // cancelAVSTimer();
  RDO_DATA_T rdoData;
  
  int voltage_min_decoded;
  // Sanity check include, check if the value is in EPR range (index < 8) and also AVS mode
  if(pdoIndex < 8 && SRC_SPRandEPRpdoArray[pdoIndex-1].pps.type == 1)
  {
    Serial.println("Type is PPS."); // DEBUG
    // Now that we are in PPS mode

    rdoData.REQMSG_Fields.PDO_INDEX = pdoIndex;  // Index 1

    if(currentMap(max_current) > SRC_SPRandEPRpdoArray[pdoIndex-1].pps.current_max) 
    {
      Serial.println("PPS Current not in range."); // DEBUG
      return; // Check if current setting is in range
    }

    //Decode voltage_min
    if(SRC_SPRandEPRpdoArray[pdoIndex-1].pps.voltage_min > 0) voltage_min_decoded = 3300;

    if(target_voltage < voltage_min_decoded || 
          target_voltage > SRC_SPRandEPRpdoArray[pdoIndex-1].pps.voltage_max*100 ) 
    {
      Serial.println("PPS Voltage not in range."); // DEBUG
      return; // Check if current setting is in range
    }

    rdoData.REQMSG_Fields.VOLTAGE_SEL = target_voltage/100;  // Output Voltage in 200mV units
    rdoData.REQMSG_Fields.CURRENT_SEL = currentMap(max_current);

    writeBuf[0] = rdoData.byte0;  // Store the upper 8 bits
    writeBuf[1] = rdoData.byte1;  // Store the lower 8 bits
    i2c_write(AP33772S_ADDRESS, CMD_PD_REQMSG, 2);
  }
  return;
}

/**
 * @brief Request AVS voltage
 * @param pdoIndex index 1
 * @param target_voltage unit in mV
 * @param max_current unit in mA
 * @bug only work if min AVS voltage is 15V, AVS max voltage is not capped at 30V
 */
void AP33772S::setAVSPDO(int pdoIndex, int target_voltage, int max_current) 
{
  // cancelAVSTimer();

  RDO_DATA_T rdoData;

  int voltage_min_decoded;
  // Sanity check include, check if the value is in EPR range (index >= 8) and also AVS mode
  if(pdoIndex >= 8 && SRC_SPRandEPRpdoArray[pdoIndex-1].avs.type == 1)
  {
    Serial.println("Type is AVS."); // DEBUG
    // Now that we are in AVS mode

    //Decode voltage_min
    if(SRC_SPRandEPRpdoArray[pdoIndex-1].avs.voltage_min > 0) voltage_min_decoded = 15000;

    rdoData.REQMSG_Fields.PDO_INDEX = pdoIndex;  // Index 1

    if(currentMap(max_current) > SRC_SPRandEPRpdoArray[pdoIndex-1].avs.current_max) 
    {
      Serial.println("AVS Current not in range."); // DEBUG
      return; // Check if current setting is in range
    }

    //Decode voltage_min
    if(SRC_SPRandEPRpdoArray[pdoIndex-1].avs.voltage_min > 0) voltage_min_decoded = 15000;

    if(target_voltage < voltage_min_decoded || 
          target_voltage > SRC_SPRandEPRpdoArray[pdoIndex-1].avs.voltage_max*200 ) 
    {
      Serial.println("AVS Voltage not in range."); // DEBUG
      return; // Check if current setting is in range
    }

    rdoData.REQMSG_Fields.VOLTAGE_SEL = target_voltage/200;  // Output Voltage in 200mV units
    rdoData.REQMSG_Fields.CURRENT_SEL = currentMap(max_current);

    writeBuf[0] = rdoData.byte0;  // Store the upper 8 bits
    writeBuf[1] = rdoData.byte1;  // Store the lower 8 bits
    i2c_write(AP33772S_ADDRESS, CMD_PD_REQMSG, 2);

    _indexAVS = rdoData.REQMSG_Fields.PDO_INDEX;
    _voltageAVSbyte = rdoData.REQMSG_Fields.VOLTAGE_SEL; 
    _currentAVSbyte = rdoData.REQMSG_Fields.CURRENT_SEL;

    // Required to maintain AVS voltage negotiation.
    // setupAVSTimer();
  }
  return;
}

// void AP33772S::timerISR1()
// {
//   // Clear the alarm irq
//   hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM1);

//   // Reset the alarm register
//   timer_hw->alarm[ALARM_NUM1] = timer_hw->timerawl + DELAY1;

//   RDO_DATA_T rdoData;
//   rdoData.REQMSG_Fields.PDO_INDEX = _indexAVS;
//   rdoData.REQMSG_Fields.VOLTAGE_SEL = _voltageAVSbyte;
//   rdoData.REQMSG_Fields.CURRENT_SEL = _currentAVSbyte;

//   // Trying to call non static function/variable
//   // TODO Check scope if keep spawning too many object
//   writeBuf[0] = rdoData.byte0;  // Store the upper 8 bits
//   writeBuf[1] = rdoData.byte1;  // Store the lower 8 bits
//   i2c_write(AP33772S_ADDRESS, CMD_PD_REQMSG, 2);
// }

// void AP33772S::setupAVSTimer()
// {
//   // Set up 0.5s timer using timer1
//   hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM1);
//   // Associate an interrupt handler with the ALARM_IRQ
//   irq_set_exclusive_handler(ALARM_IRQ1, timerISR1);
//   // Enable the alarm interrupt
//   irq_set_enabled(ALARM_IRQ1, true);
//   // Write the lower 32 bits of the target time to the alarm register, arming it.
//   timer_hw->alarm[ALARM_NUM1] = timer_hw->timerawl + DELAY1;
// }

// void AP33772S::cancelAVSTimer()
// {
//   hw_clear_bits(&timer_hw->inte, 1u << ALARM_NUM1); // Disable alarm interrupt
//   hw_set_bits(&timer_hw->intr, 1u << ALARM_NUM1);   // Clear pending interrupt
// }


/**
 * @brief Set resistance value of 10K NTC at 25C, 50C, 75C and 100C.
 *          Default is 10000, 4161, 1928, 974Ohm
 * @param TR25, TR50, TR75, TR100 unit in Ohm
 * @attention Blocking function due to long I2C write, min blocking time 15ms
 */
void AP33772S::setNTC(int TR25, int TR50, int TR75, int TR100)
{
  writeBuf[0] = TR25 & 0xff;
  writeBuf[1] = (TR25 >> 8) & 0xff;
  i2c_write(AP33772S_ADDRESS, CMD_TR25, 2);
  delay(5);
  writeBuf[0] = TR50 & 0xff;
  writeBuf[1] = (TR50 >> 8) & 0xff;
  i2c_write(AP33772S_ADDRESS, CMD_TR50, 2);
  delay(5);
  writeBuf[0] = TR75 & 0xff;
  writeBuf[1] = (TR75 >> 8) & 0xff;
  i2c_write(AP33772S_ADDRESS, CMD_TR75, 2);
  delay(5);
  writeBuf[0] = TR100 & 0xff;
  writeBuf[1] = (TR100 >> 8) & 0xff;
  i2c_write(AP33772S_ADDRESS, CMD_TR100, 2);
}

/**
 * @brief Read NTC temperature
 * @return tempearture in C
 */
int AP33772S::readTemp()
{
    i2c_read(AP33772S_ADDRESS, CMD_TEMP, 1);
    return readBuf[0]; // I2C read return 1C/LSB
}

/**
 * @brief Read VBUS voltage
 * @return voltage in mV
 */
int AP33772S::readVoltage()
{
    i2c_read(AP33772S_ADDRESS, CMD_VOLTAGE, 2);
    return ((readBuf[1] << 8) | readBuf[0]) * 80; // I2C read return 80mV/LSB
}

/**
 * @brief Read VBUS current
 * @return current in mA
 */
int AP33772S::readCurrent()
{
    i2c_read(AP33772S_ADDRESS, CMD_CURRENT, 1);
    return readBuf[0] * 24; // I2C read return 24mA/LSB
}

/**
 * @brief Read VREQ The latest requested voltage negotiated with the source
 * @return voltage in mV
 */
int AP33772S::readVREQ()
{
    i2c_read(AP33772S_ADDRESS, CMD_VREQ, 2);
    return ((readBuf[1] << 8) | readBuf[0]) * 50; // I2C read return 50mV/LSB
}

/**
 * @brief Read IREQ The latest requested voltage negotiated with the source
 * @return current in mA
 */
int AP33772S::readIREQ()
{
    i2c_read(AP33772S_ADDRESS, CMD_IREQ, 2);
    return ((readBuf[1] << 8) | readBuf[0]) * 10; // I2C read return 10mA/LSB
}

/**
 * @brief Read VSELMIN register. The Minimum Selection Voltage
 * @return voltage in mV
 */
int AP33772S::readVSELMIN()
{
  i2c_read(AP33772S_ADDRESS, CMD_VSELMIN, 1);
  return readBuf[0] * 200; // I2C read return 200mV/LSB
}

/**
 * @brief Set VSELMIN register. The Minimum Selection Voltage
 * @param voltage in mV
 */
void AP33772S::setVSELMIN(int voltage)
{
  writeBuf[0] = voltage/200; // 200mV/LSB
  i2c_write(AP33772S_ADDRESS, CMD_VSELMIN, 1);
}

/**
 * @brief Read UVP Threshold, percentage(%) of VREQ
 * @return percentage, should only return 80%, 75%, or 70%. -1 for error
 */
int AP33772S::readUVPTHR()
{
  i2c_read(AP33772S_ADDRESS, CMD_UVPTHR, 1);
  switch(readBuf[0])
  {
    case 1:
      return 80;
    case 2:
      return 75;
    case 3:
      return 70;
  }
  return -1;
}

/**
 * @brief Set UVP Threshold, percentage(%) of VREQ
 * @param value percentage. If 80% then value = 80
 */
void AP33772S::setUVPTHR(int value)
{
  if(value >= 70 && value <= 80)
  {
    switch(value)
    {
      case 80:
        writeBuf[0] = 1;
        break;
      case 75:
        writeBuf[0] = 2;
        break;
      case 70:
        writeBuf[0] = 3;
        break;
      default:
        return; // Error
    }
  }
  else return;
}

/**
 * @brief Read OVP Threshold Voltage is the VREQ voltage plus OVPTHR offset voltage (mV)
 * @return voltage in mV
 */
int AP33772S::readOVPTHR()
{
  i2c_read(AP33772S_ADDRESS, CMD_OVPTHR, 1);
  return readBuf[0] * 80; // I2C read return 80mV/LSB
}

/**
 * @brief Set OVP Threshold Voltage is the VREQ voltage plus OVPTHR offset voltage (mV)
 * @param voltage in mV
 */
void AP33772S::setOVPTHR(int value)
{
  writeBuf[0] = value/80; //80mV/LSB
  i2c_write(AP33772S_ADDRESS, CMD_OVPTHR, 1);
}

int AP33772S::readOCPTHR()
{
  i2c_read(AP33772S_ADDRESS, CMD_OCPTHR, 1);
  return readBuf[0] * 50; // I2C read return 50mA/LSB
}
void AP33772S::setOCPTHR(int value)
{
  writeBuf[0] = value/50; // 50mA/LSB
  i2c_write(AP33772S_ADDRESS, CMD_OCPTHR, 1);
}
int AP33772S::readOTPTHR()
{
  i2c_read(AP33772S_ADDRESS, CMD_OTPTHR, 1);
  return readBuf[0]; // I2C read return 1C/LSB
}
void AP33772S::setOTPTHR(int value)
{
  writeBuf[0] = value; // 1C/LSB
  i2c_write(AP33772S_ADDRESS, CMD_OTPTHR, 1);
}
int AP33772S::readDRTHR()
{
  i2c_read(AP33772S_ADDRESS, CMD_DRTHR, 1);
  return readBuf[0]; // I2C read return 1C/LSB
}
void AP33772S::setDRTHR(int value)
{
  writeBuf[0] = value; // 1C/LSB
  i2c_write(AP33772S_ADDRESS, CMD_DRTHR, 1);
}


/**
 * @brief Get internal PPS profile index (index start at 1)
 * @return indexPPS
 */
int AP33772S::getPPSIndex()
{
  return _indexPPSUser;
}

/**
 * @brief Get internal AVS profile index (index start at 1)
 * @return indexAVS
 */
int AP33772S::getAVSIndex()
{
  return _indexAVSUser;
}


void AP33772S::displaySPRVoltageMin(unsigned int current_max) {
  switch (current_max) {
    case 0:
      Serial.print("Reserved");
      break;
    case 1:
      Serial.print("3300mV~");
      break;
    case 2:
      Serial.print("3300mV < VOLTAGE_MIN ≤ 5000mV ");
      break;
    case 3:
      Serial.print("others");
      break;
    default:
      Serial.print("Invalid value");
      break;
  }
}


void AP33772S::displayEPRVoltageMin(unsigned int current_max) {
  switch (current_max) {
    case 0:
      Serial.print("Reserved");
      break;
    case 1:
      Serial.print("15000mV~");
      break;
    case 2:
      Serial.print("15000mV < VOLTAGE_MIN ≤ 20000mV ");
      break;
    case 3:
      Serial.print("others");
      break;
    default:
      Serial.print("Invalid value");
      break;
  }
}

/**
 * @brief take in current in mA unit
 * @return value from 0 to 15
 * @return -1 if there is an error
 */
int AP33772S::currentMap(int current)
{
  // Check if the value is out of bounds
  if (current < 0 || current > 5000) {
      return -1; // Return -1 for invalid inputs
  }

  // If value is below 1250, return 0
  if (current < 1250) {
      return 0;
  }

  // Calculate the result for ranges above 1250
  return ((current - 1250) / 250) + 1;
}

void AP33772S::displayCurrentRange(unsigned int current_max) {
  switch (current_max) {
    case 0:
      Serial.print("0.00A ~ 1.24A (Less than)");
      break;
    case 1:
      Serial.print("1.25A ~ 1.49A");
      break;
    case 2:
      Serial.print("1.50A ~ 1.74A");
      break;
    case 3:
      Serial.print("1.75A ~ 1.99A");
      break;
    case 4:
      Serial.print("2.00A ~ 2.24A");
      break;
    case 5:
      Serial.print("2.25A ~ 2.49A");
      break;
    case 6:
      Serial.print("2.50A ~ 2.74A");
      break;
    case 7:
      Serial.print("2.75A ~ 2.99A");
      break;
    case 8:
      Serial.print("3.00A ~ 3.24A");
      break;
    case 9:
      Serial.print("3.25A ~ 3.49A");
      break;
    case 10:
      Serial.print("3.50A ~ 3.74A");
      break;
    case 11:
      Serial.print("3.75A ~ 3.99A");
      break;
    case 12:
      Serial.print("4.00A ~ 4.24A");
      break;
    case 13:
      Serial.print("4.25A ~ 4.49A");
      break;
    case 14:
      Serial.print("4.50A ~ 4.99A");
      break;
    case 15:
      Serial.print("5.00A ~ (More than)");
      break;
    default:
      Serial.print("Invalid value");
      break;
  }
}

void BinaryStrZeroPad(int Number,char ZeroPadding){
//ZeroPadding = nth bit, e.g for a 16 bit number nth bit = 15
signed char i=ZeroPadding;

	while(i>=0){
	    if((Number & (1<<i)) > 0) Serial.write('1');
	    else Serial.write('0');
	    --i;
	}

        Serial.println();
}

/**
 * @brief Turn on/off the NMOS switch
 * @param flag 0 or 1 for OFF/ON
 * @return 1 if flag make sense
 * @bug can add code to check Vout voltage to ensure on or off, worry about settle time required for VOUT
 */
bool AP33772S::setOutput(uint8_t flag){
    switch(flag){
        case 0:
            writeBuf[0] = 0b00010001; //turn off
            i2c_write(AP33772S_ADDRESS, CMD_SYSTEM, 1);
            return 1;
            break; //Sanity
        case 1:
            writeBuf[0] = 0b00010010; //turn on
            i2c_write(AP33772S_ADDRESS, CMD_SYSTEM, 1);
            return 1;
            break; //Sanity
        default:
            return 0; //Error, dont know the input
    }
	
}

/**
 * @brief Set a single mask flag
 * @param AP33772_MASK target
 * 
 **/
void AP33772S::setMask(AP33772_MASK flag)
{
  // First read in what is currently in the location. Bail on a failed read:
  // a zeroed buffer would silently clear every other mask bit.
  if (!i2c_read(AP33772S_ADDRESS, CMD_MASK, 1)) return;
  readBuf[0] |= flag;   // Set the bit
  writeBuf[0] = readBuf[0];
  i2c_write(AP33772S_ADDRESS, CMD_MASK, 1);
}

/**
 * @brief Clear a single mask flag
 * @param AP33772_MASK target
 * 
 **/
void AP33772S::clearMask(AP33772_MASK flag)
{
  // First read in what is currently in the location. Bail on a failed read:
  // a zeroed buffer would silently clear every other mask bit.
  if (!i2c_read(AP33772S_ADDRESS, CMD_MASK, 1)) return;
  readBuf[0] &= ~flag;   // Set the bit
  writeBuf[0] = readBuf[0];
  i2c_write(AP33772S_ADDRESS, CMD_MASK, 1);
}

/**
 * @brief Set a single config flag
 * @param AP33772_CONFIG target
 * 
 **/
void AP33772S::setConfig(AP33772_CONFIG flag)
{
  // First read in what is currently in the location. Bail on a failed read:
  // writing back a zeroed buffer would clear every other protection bit.
  if (!i2c_read(AP33772S_ADDRESS, CMD_CONFIG, 1)) return;
  readBuf[0] |= flag;   // Set the bit
  writeBuf[0] = readBuf[0];
  i2c_write(AP33772S_ADDRESS, CMD_CONFIG, 1);
}

/**
 * @brief Clear a single config flag
 * @param AP33772_CONFIG target
 * 
 **/
void AP33772S::clearConfig(AP33772_CONFIG flag)
{
  // First read in what is currently in the location. Bail on a failed read:
  // writing back a zeroed buffer would clear every other protection bit.
  if (!i2c_read(AP33772S_ADDRESS, CMD_CONFIG, 1)) return;
  readBuf[0] &= ~flag;   // Set the bit
  writeBuf[0] = readBuf[0];
  i2c_write(AP33772S_ADDRESS, CMD_CONFIG, 1);
}

/**
 * @brief Read entire MASK register
 * @return value of MASK register in byte
 * 
 **/
byte AP33772S::readMask()
{
    i2c_read(AP33772S_ADDRESS, CMD_MASK, 1);
    return readBuf[0];
}

/*************LOW LEVEL HELPER FUNCTIONS***************/

/** @brief Read register bytes; return false on an I2C error or short read. */
bool AP33772S::i2c_read(byte slvAddr, byte cmdAddr, byte len)
{
    // clear readBuffer
    for (byte i = 0; i < READ_BUFF_LENGTH; i++)
    {
        readBuf[i] = 0;
    }
    byte i = 0;
    _i2cPort->beginTransmission(slvAddr); // transmit to device SLAVE_ADDRESS
    _i2cPort->write(cmdAddr);             // sets the command register
    if (_i2cPort->endTransmission() != 0) {
        return false;
    }

    _i2cPort->requestFrom(slvAddr, len); // request len bytes from peripheral device
    if (len > _i2cPort->available())
    {
        // Short read. Reporting success here left readBuf all zeros, which
        // callers cannot distinguish from a genuine all-zero register: a failed
        // CONFIG read made clearConfig() write 0x00 and disable EVERY
        // protection (DR/OTP/OCP/OVP/UVP) instead of just clearing one bit.
        return false;
    }
    while (_i2cPort->available())
    {
        readBuf[i] = (byte)_i2cPort->read();
        i++;
    }
    return true;
}

/** @brief Write register bytes; return false on an I2C error. */
bool AP33772S::i2c_write(byte slvAddr, byte cmdAddr, byte len)
{
    _i2cPort->beginTransmission(slvAddr); // transmit to device SLAVE_ADDRESS
    _i2cPort->write(cmdAddr);             // sets the command register
    _i2cPort->write(writeBuf, len);       // write data with len
    byte err = _i2cPort->endTransmission(); // stop transmitting

    // clear readBuffer
    for (byte i = 0; i < WRITE_BUFF_LENGTH; i++)
    {
        writeBuf[i] = 0;
    }
    return (err == 0);
}
