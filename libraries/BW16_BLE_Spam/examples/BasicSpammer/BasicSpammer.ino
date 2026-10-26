/*
  BasicSpammer.ino
  
  Demonstrates how to use the BW16_BLE_Spam library.
  This example initializes the spammer and continuously cycles 
  through various Apple and Windows device spoofing payloads.
*/

#include <Arduino.h>
#include <BW16BLESpam.h>

BW16BLESpammer bleSpammer;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting BW16 BLE Spammer...");

  // Initialize the BLE hardware
  bleSpammer.begin();

  // Enable detailed hex dumping to the Serial monitor (optional)
  bleSpammer.setDebugLogging(true);

  // Set the rotation interval (Dwell time) in milliseconds.
  // 1000ms is highly recommended for iOS and Windows to recognize the device.
  bleSpammer.setInterval(1000);

  // Start the spammer in ALL_BURST mode (cycles through Apple, Google, and Windows)
  bleSpammer.start(BW16BLESpammer::ALL_BURST);
}

void loop() {
  // You must call tick() repeatedly in your loop to handle payload 
  // rotation and MAC address randomization.
  bleSpammer.tick();
  
  // Do not put blocking delays (like delay(5000)) in your loop, 
  // otherwise the spammer will not rotate properly.
  delay(10);
}
