#ifndef CRABIT_BUILD_PROFILE_H
#define CRABIT_BUILD_PROFILE_H

// Weather/time are supplied by the phone over the existing authenticated BLE
// relay. Keep the old standalone HTTPS trial opt-in for recovery/comparison.
// This does not disable the independent Wi-Fi tools or change the flash layout.
#ifndef CRABIT_BW16_ENABLE_NETWORK_SYNC
#define CRABIT_BW16_ENABLE_NETWORK_SYNC 0
#endif

// User trial: IDE defaults select network tools + Hopper. Explicit wrapper
// profiles retain their own choices; product0 remains a rollback configuration.
#define CRABIT_BW16_PROFILE_PRODUCT 0
#define CRABIT_BW16_PROFILE_BLE_MOUSE_LAB 1
#define CRABIT_BW16_PROFILE_BLE_PPT_LAB 2
#define CRABIT_BW16_PROFILE_BLE_KBD_LAB 3
#define CRABIT_BW16_PROFILE_GAMES_LAB 4
#define CRABIT_BW16_PROFILE_WIFI_TOOLS_LAB 5
#define CRABIT_BW16_PROFILE_OTA_LAB 6
#define CRABIT_BW16_PROFILE_ALL_LAB 7
#define CRABIT_BW16_PROFILE_NETWORK_OTA_LAB 8

#ifndef CRABIT_BW16_PROFILE
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
#define CRABIT_BW16_PROFILE CRABIT_BW16_PROFILE_PRODUCT
#else
#define CRABIT_BW16_PROFILE CRABIT_BW16_PROFILE_WIFI_TOOLS_LAB
#define CRABIT_BW16_IDE_HOPPER_TRIAL 1
#endif
#endif

// Opt-in combination measurement; ordinary IDE/product builds remain unchanged.
#ifndef CRABIT_BW16_NETWORK_TWO_GAMES
#define CRABIT_BW16_NETWORK_TWO_GAMES 0
#endif
#ifndef CRABIT_BW16_NETWORK_GAME
#ifdef CRABIT_BW16_IDE_HOPPER_TRIAL
#define CRABIT_BW16_NETWORK_GAME 2
#else
#define CRABIT_BW16_NETWORK_GAME (CRABIT_BW16_NETWORK_TWO_GAMES ? 3 : 0)
#endif
#endif
#if CRABIT_BW16_NETWORK_GAME < 0 || CRABIT_BW16_NETWORK_GAME > 3
#error "Invalid network game selection"
#endif
#if CRABIT_BW16_NETWORK_GAME && (CRABIT_BW16_PROFILE != CRABIT_BW16_PROFILE_WIFI_TOOLS_LAB)
#error "Two-game trial requires wifi-tools-lab"
#endif

#ifndef CRABIT_BW16_PROFILE
#define CRABIT_BW16_PROFILE CRABIT_BW16_PROFILE_PRODUCT
#endif

#if (CRABIT_BW16_PROFILE < CRABIT_BW16_PROFILE_PRODUCT) || \
    (CRABIT_BW16_PROFILE > CRABIT_BW16_PROFILE_NETWORK_OTA_LAB)
#error "Unsupported CRABIT_BW16_PROFILE"
#endif

#define CRABIT_BW16_IS_PRODUCT \
    (CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_PRODUCT)
#define CRABIT_BW16_IS_ALL_LAB \
    (CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_ALL_LAB)
#define CRABIT_BW16_IS_BLE_MOUSE_LAB \
    (CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_BLE_MOUSE_LAB)
#define CRABIT_BW16_IS_BLE_PPT_LAB \
    (CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_BLE_PPT_LAB)
#define CRABIT_BW16_IS_BLE_KBD_LAB \
    (CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_BLE_KBD_LAB)

// Individual profiles measure one group; ALL_LAB measures their combination.
// These are NOT validated
// product configurations: size/range checks do not prove radio coexistence.
#define CRABIT_BW16_ENABLE_GAMES \
    ((CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_GAMES_LAB) || CRABIT_BW16_IS_ALL_LAB)
#define CRABIT_BW16_ENABLE_WIFI_LAB \
    ((CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_WIFI_TOOLS_LAB) || CRABIT_BW16_IS_ALL_LAB || (CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_NETWORK_OTA_LAB))
// The product menu App uses the installed BW16_BLE_Spam library. Keep the
// older sketch-local ble_spam.cpp implementation disabled below.
#define CRABIT_BW16_ENABLE_BLE_SPAM_APP \
    (CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_WIFI_TOOLS_LAB)
#define CRABIT_BW16_ENABLE_BLE_SPAM 0
#define CRABIT_BW16_ENABLE_OTA \
    ((CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_OTA_LAB) || CRABIT_BW16_IS_ALL_LAB || (CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_NETWORK_OTA_LAB))

#endif
