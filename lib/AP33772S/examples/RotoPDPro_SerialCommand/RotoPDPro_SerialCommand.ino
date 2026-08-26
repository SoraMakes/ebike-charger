#include <Arduino.h>
#include <AP33772S.h>
#include <INA238.h>

// put function declarations here:
AP33772S usbpd;
INA238 ina238(0x40);

String cmd;

void AP33772S_Pro_init(){
  usbpd.clearConfig(AP33772_CONFIG::UVP_EN); // Required for RotoPD Pro. Prevent UVP from issuing hard reset.
  usbpd.begin(); // Load in profiles
  usbpd.setOutput(0); // Turn off output

  if(!ina238.begin())
  {
    Serial.println("Cannot find INA");
  }

  ina238.setADCRange(1);
  ina238.setMaxCurrentShunt(7, 0.005); // Based on RotoPD Pro schematic
  ina238.setShuntVoltageConversionTime(INA238_150_us);
  ina238.setAverage(INA238_16_SAMPLES); 
  ina238.setOverCurrentLimit(5100); // Allow max out 5A threshold
  ina238.setDiagnoseAlertBit(INA238_DIAG_ALERT_LATCH); //Set to Alert latch
}

void setup() {
  // put your setup code here, to run once:
  Wire.begin();

  Serial.begin(115200);
  delay(1000); //Ensure everything got enough time to bootup
  AP33772S_Pro_init();
  usbpd.setOutput(0); //Enable output
}

void loop() {
  if (Serial.available() > 0) {

    cmd = Serial.readStringUntil('\n');
    cmd.trim();   // remove whitespace

    // ---- on ----
    if (cmd.equalsIgnoreCase("on")) {
      usbpd.setOutput(1); //Enable output
      Serial.println("Output ON");
    }

    // ---- off ----
    else if (cmd.equalsIgnoreCase("off")) {
      usbpd.setOutput(0); //Disable output
      Serial.println("Output OFF");
    }

    // ---- temperature ----
    else if (cmd.equalsIgnoreCase("temp")) {
      Serial.print("Board temperature: ");
      Serial.print(ina238.getTemperature(), 3);
      Serial.println(" C");
    }

    // ---- profile ----
    else if (cmd.equalsIgnoreCase("profile")) {
      usbpd.displayProfiles();
    }

    // ---- clear ----
    else if (cmd.equalsIgnoreCase("clear")) {
      Serial.println("Cleared INA238 Alert");
      ina238.getDiagnoseAlert(); //Reading the DIAG reg will clear ALERT latch
    }

    // ---- stat ----
    else if (cmd.equalsIgnoreCase("stat")) {
      Serial.print("Bus Voltage: ");
      Serial.print(ina238.getBusVoltage(), 3);
      Serial.print(" V\t");

      Serial.print("Current: ");
      Serial.print(ina238.getAmpere(), 3);
      Serial.print(" A\t");

      Serial.print("Temperature: ");
      Serial.print(ina238.getTemperature(), 3);
      Serial.println(" *C");
    }
    
    // ---- setocp X ----
    else if (cmd.startsWith("setocp ")) {
      String valStr = cmd.substring(7);  // everything after "setocp "
      valStr.trim();

      int current = valStr.toInt();
      if (current <= 0 || current > 5000) {
        Serial.println("->Invalid current");
        return;
      }

      Serial.print("Setting over current limit to ");
      Serial.println(current);

      // Pass current to your function
      ina238.setOverCurrentLimit(current); // Set circuit breaker threshold;
    }

    // ---- setavs X ----
    else if (cmd.startsWith("setavs ")) {
      String valStr = cmd.substring(7);  // everything after "setavs "
      valStr.trim();

      int voltage = valStr.toInt();
      if (voltage <= 15000 || voltage >= 48000) {
        Serial.println("->Invalid voltage");
        return;
      }

      Serial.print("Setting voltage to ");
      Serial.println(voltage);

      // Pass voltage to your function
      usbpd.setAVSPDO(usbpd.getAVSIndex(), voltage, 4900);
    }

    // ---- setpdo X ----
    else if (cmd.startsWith("setpdo ")) {
      String valStr = cmd.substring(7);  // everything after "setpdo "
      valStr.trim();

      int pdo = valStr.toInt();

      Serial.print("Setting PDO to ");
      Serial.println(pdo);

      // Pass voltage to your function
      usbpd.setFixPDO(pdo, 3000);
    }

    // ---- unknown ----
    else {
      Serial.println("Unknown command");
    }
  }
}

