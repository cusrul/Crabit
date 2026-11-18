#pragma once
// Single source of truth for every wire topic. Product code compares and sends
// these constants; raw topic literals are rejected by the architecture test.
//
// The payload format for each topic lives in docs/protocol.md. This header only
// names the topics so STM32, BW16 and the Flutter client cannot drift on text.

struct Topics {
    // System control
    static constexpr const char* SysMode = "sys/mode";
    static constexpr const char* SysNotify = "sys/notify";
    static constexpr const char* SysComm = "sys/comm";
    static constexpr const char* SysAck = "sys/ack";
    static constexpr const char* SysBle = "sys/ble";
    static constexpr const char* SysBtn = "sys/btn";
    static constexpr const char* SysNet = "sys/net";
    static constexpr const char* SysPrefs = "sys/prefs";
    static constexpr const char* SysPrefsSet = "sys/prefs/set";
    static constexpr const char* SysSettings = "sys/settings";
    static constexpr const char* SysSettingsSet = "sys/settings/set";
    static constexpr const char* SysSns = "sys/sns";
    static constexpr const char* SysTime = "sys/time";

    // BW16 boot / companion
    static constexpr const char* SysBwPair = "sys/bw/pair";
    static constexpr const char* SysBwPing = "sys/bw/ping";

    // Application and sensor data
    static constexpr const char* AppBadusb = "app/badusb";
    static constexpr const char* AppNet = "app/net";
    static constexpr const char* AppTone = "app/tone";
    static constexpr const char* AppUsb = "app/usb";
    static constexpr const char* AppWeather = "app/weather";
    static constexpr const char* SnsAll = "sns/all";

    // Network service
    static constexpr const char* NetCancel = "net/cancel";
    static constexpr const char* NetDay = "net/day";
    static constexpr const char* NetDetail = "net/detail";
    static constexpr const char* NetPass = "net/pass";
    static constexpr const char* NetPlace = "net/place";
    static constexpr const char* NetProgress = "net/progress";
    static constexpr const char* NetRun = "net/run";
    static constexpr const char* NetSsid = "net/ssid";
    static constexpr const char* NetStatus = "net/status";
    static constexpr const char* NetTime = "net/time";

    // Diagnostics
    static constexpr const char* DiagBattery = "diag/battery";
    static constexpr const char* DiagEcho = "diag/echo";
    static constexpr const char* DiagImu = "diag/imu";
    static constexpr const char* DiagRadio = "diag/radio";
    static constexpr const char* DiagSnapshot = "diag/snapshot";
    static constexpr const char* DiagWifi = "diag/wifi";
    static constexpr const char* DiagBoot = "diag/boot";

    // Audit contract
    static constexpr const char* AuditPut = "audit/put";
    static constexpr const char* AuditRead = "audit/read";
    static constexpr const char* AuditAck = "audit/ack";

    // Mode orchestration contract
    static constexpr const char* ModeRequest = "mode/request";
    static constexpr const char* ModeGrant = "mode/grant";
    static constexpr const char* ModeActive = "mode/active";
    static constexpr const char* ModeHeartbeat = "mode/heartbeat";
    static constexpr const char* ModeRelease = "mode/release";
    static constexpr const char* ModeStatus = "mode/status";

    // Prefixes used by grouped status matching.
    static constexpr const char* NetPrefix = "net/";
    static constexpr const char* DiagPrefix = "diag/";
    static constexpr const char* AuditPrefix = "audit/";
};
