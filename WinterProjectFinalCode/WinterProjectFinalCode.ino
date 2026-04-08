#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include "BluetoothSerial.h"

// --- Bluetooth Setup ---
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please update your board manager.
#endif

BluetoothSerial SerialBT; 

// --- RFID Setup ---
#define SS_PIN 5
#define RST_PIN 4
MFRC522 rfid(SS_PIN, RST_PIN);

// --- Accelerometer Setup ---
const int MPU_ADDR = 0x68; 

// --- Floor Markers (Your Tags) ---
byte floor1Tag[4] = {0x90, 0x77, 0x63, 0x20}; 
byte floor2Tag[4] = {0xE3, 0x94, 0x5B, 0x9F}; 

// --- State Machine Memory ---
String currentLocation = "now in transit (like between floors)";
String lastPrintedLocation = "";
String currentMotion = "Parked";

// --- Stopwatches ---
unsigned long lastScanTime = 0;
const int scanCooldown = 1500; 
unsigned long lastMotionCheck = 0;
const int motionInterval = 1000; 
unsigned long parkedStartTime = 0;
bool isSleeping = false;

// --- Helper Function to send data to BOTH USB and Bluetooth ---
void broadcastLog(String message) {
  Serial.println(message);   // Sends to physical USB cable
  SerialBT.println(message); // Sends wirelessly via Bluetooth
}

void setup() {
  Serial.begin(115200);
  
  // Start Bluetooth and give device a name
  SerialBT.begin("Asset_Tracker_01"); 
  
  // WAKE UP MPU6050
  Wire.begin(21, 22); 
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  
  Wire.write(0);     
  Wire.endTransmission(true);

  // START RFID 
  SPI.begin();
  rfid.PCD_Init();

  broadcastLog("Tracker Online. Bluetooth Serial Active.");
  broadcastLog("-------------------------------------------");
}

void loop() {
  // loop calling dedicated functions
  checkRFIDGate();
  checkMotion();
  delay(10); 
}

// --- MODULAR FUNCTION: Handles all RFID and Location Logic ---
void checkRFIDGate() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    if (millis() - lastScanTime > scanCooldown) {
      if (checkTag(rfid.uid.uidByte, floor1Tag)) {
        currentLocation = (currentLocation == "now on floor 1") ? "now in transit" : "now on floor 1";
      }   
      else if (checkTag(rfid.uid.uidByte, floor2Tag)) {
        currentLocation = (currentLocation == "now on floor 2") ? "now in transit" : "now on floor 2";
      }
      lastScanTime = millis(); 
    }
    rfid.PICC_HaltA(); 
  }

  // Print location only if it changed
  if (currentLocation != lastPrintedLocation) {
    broadcastLog("\n>>> LOCATION ALERT: " + currentLocation + " <<<");
    lastPrintedLocation = currentLocation;
  }
}

// --- MODULAR FUNCTION: Handles Accelerometer, Heartbeat, and Sleep ---
void checkMotion() {
  if (millis() - lastMotionCheck > motionInterval) {
    // 1. Grab raw data from hardware registers
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);  
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 6, true);  
    
    int16_t ax = Wire.read() << 8 | Wire.read();  
    int16_t ay = Wire.read() << 8 | Wire.read();  
    int16_t az = Wire.read() << 8 | Wire.read();  

    // 2. Check independent axis thresholds
    if (abs(ax) > 1800 || abs(ay) > 1800 || az > 18000 || az < 14500) {
      currentMotion = "Moving";
      parkedStartTime = millis(); 
      if (isSleeping) {
        broadcastLog("\n>>> MOTION DETECTED: Waking up heartbeat! <<<");
        isSleeping = false;
      }
    } else {
      currentMotion = "Parked";
    }

    // 3. Manage power saving sleep mode
    if (currentMotion == "Parked" && !isSleeping) {
      if (millis() - parkedStartTime > 20000) {
        broadcastLog("\n>>> SYSTEM SLEEP: Parked for 20s. Muting heartbeat. <<<");
        isSleeping = true;
      }
    }

    // 4. Output heartbeat if awake
    if (!isSleeping) {
      String dashboardMsg = "[1-SEC CHECK] State: " + currentLocation + " | Motion: " + currentMotion;
      broadcastLog(dashboardMsg);
    }
    lastMotionCheck = millis(); 
  }
}

// --- HELPER FUNCTION: Compares tag IDs ---
bool checkTag(byte* scannedTag, byte* knownTag) {
  for (int i = 0; i < 4; i++) {
    if (scannedTag[i] != knownTag[i]) { return false; }
  }
  return true; 
}
