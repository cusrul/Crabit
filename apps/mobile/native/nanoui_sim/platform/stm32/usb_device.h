#ifndef CRABIT_NANOUI_SIM_USB_DEVICE_H
#define CRABIT_NANOUI_SIM_USB_DEVICE_H
struct USBD_HandleTypeDef {};
constexpr int USBD_STATE_DEFAULT = 1;
constexpr int USBD_STATE_ADDRESSED = 2;
constexpr int USBD_STATE_CONFIGURED = 3;
constexpr int USBD_STATE_SUSPENDED = 4;
inline USBD_HandleTypeDef hUsbDeviceFS{};
#endif
