#include <NanoUI.h>

WindowManager* WindowManager::instance = nullptr;
NanoUI::EmbeddedVector<Application*> AppManager::appStack;
NanoUI::EmbeddedVector<Application*> AppManager::backgroundApps;
Canvas* AppManager::sysCanvas = nullptr;
InputDriver* AppManager::sysInput = nullptr;
Widget* Popup::pendingContent = nullptr;
bool Popup::isVisible = false;
int16_t Popup::targetW = 0;
int16_t Popup::targetH = 0;
const char* Popup::toastMsg = nullptr;
uint32_t Popup::toastEndTime = 0;
uint32_t Popup::revision_ = 0;
char* Popup::toastStorage_ = nullptr;
