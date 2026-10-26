/*
    Name: BW16 BLE Spam Library
    Description: Spams multiple BLE access points on Ameba RTL8720DN.
    Platform: BW16 (RTL8720dn) - Ameba Arduino
    License: MIT
*/

#ifndef _BW16_BLE_SPAM_H_
#define _BW16_BLE_SPAM_H_

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEAdvert.h>
#include <BLEAdvertData.h>

extern "C" {
#include "gap_adv.h"
}

class BW16BLESpammer {
public:
    enum SpamType { 
        MICROSOFT, 
        SOUR_APPLE, 
        APPLE_JUICE, 
        SAMSUNG, 
        GOOGLE, 
        ALL_BURST, 
        ANDROID_BURST, 
        IOS_ACTION_MODAL, 
        IOS_NEW_DEVICE 
    };

    BW16BLESpammer();

    /**
     * @brief Initialize the BLE peripheral (must be called in setup or before start).
     */
    void begin();

    /**
     * @brief Start advertising the chosen payload type.
     * @param type The type of spam payload to broadcast.
     */
    void start(SpamType type);

    /**
     * @brief Stop advertising and disable the BLE stack.
     */
    void stop();

    /**
     * @brief Call this function inside your main loop() to handle payload/MAC rotation.
     */
    void tick();

    /**
     * @brief Check if the spammer is currently active.
     */
    bool isActive() const;

    /**
     * @brief Set the dwell time (rotation interval) in milliseconds. Default is 1000ms.
     */
    void setInterval(unsigned long ms);

    /**
     * @brief Enable or disable detailed Hex Dump logging to Serial.
     */
    void setDebugLogging(bool enable);

private:
    BLEAdvert* _advert;
    bool _bleActive;
    SpamType _currentType;
    bool _burstMode;
    bool _androidOnlyBurst;
    uint8_t _burstPhase;
    unsigned long _lastBurstMs;
    unsigned long _intervalMs;
    bool _debugLog;

    BLEAdvertData _advData;
    uint8_t _msPayloadBuf[64];
    uint8_t _googleBuf[32];

    void _generateRandomMac(uint8_t *mac);
    void _dumpPayload(uint8_t* data, uint8_t len);
    BLEAdvertData _generateUniversalAdvertisementData(SpamType type);
};

#endif // _BW16_BLE_SPAM_H_
