#ifndef APP_DCDC_TEST_H
#define APP_DCDC_TEST_H

#include "app_dcdc_ctrl.h"

class DCDC3TestApp : public DCDC3CtrlApp {
public:
    const char* getAppName() override { return "DCDC3"; }
};

#endif
