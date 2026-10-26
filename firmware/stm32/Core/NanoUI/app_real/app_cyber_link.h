#ifndef APP_CYBER_LINK_H
#define APP_CYBER_LINK_H

#include "NanoUI/NanoUI.h"
#include "event_bus.h"
#include "app_coproc.h"
#include "hw_input.h"
#include "hw_sensors.h"
#include "ameba_session.h"
#include "companion_ble.h"
#include "companion_trace.h"
#include "network_weather.h"

class CyberLinkApp : public Application {
public:
    enum class Mode : uint8_t { Ui, Ble, BleBackground, Wifi };
    explicit CyberLinkApp(Mode selected = Mode::Ui) : mode(selected) {}

private:
    const Mode mode;
    char last_ble_status[16] = {};
    char last_network_status[24] = {};
    const char* background_result = "OFF";
    uint32_t mode_sent_at = 0;
    bool resume_ble_on_exit = false;
    bool ble_stop_requested = false;
    enum class State : uint8_t {
        PauseBleWait, Boot, AwaitReady, UartFailed, HandoverStart, HandoverWait, ModeWait, Running,
        PowerOff, PowerOffWait, RestoreStart, SpiWait, DisplayWakeWait,
        RecoveredWait, Done
    };

    Canvas* canvas = nullptr;         // Borrowed from the platform.
    InputDriver* input = nullptr;
    State state = State::Boot;
    State last_trace_state = State::Done;
    uint32_t last_trace_tick = 0;
    uint32_t state_start = 0;
    uint32_t anim_timer = 0;
    int dot_cnt = 0;
    uint32_t hard_reset_timer = 0;
    bool reset_held = false;
    int16_t accum_dx = 0;
    int16_t accum_dy = 0;
    uint8_t accum_btn_mask = 0;
    uint32_t last_sensor_sync = 0;
    uint32_t last_optical_sample = 0;
    bool optical_sample_seen = false;

    void traceUiStage(bool unmounting = false) {
        if (mode != Mode::Ui) return;
        // Numeric observations only; no hardware access, protocol commands or
        // inferred peer readiness. Bit3 distinguishes an unmount observation.
        const uint32_t flags = (g_coproc_active ? 1u : 0u) |
            (Crabit::CompanionBle::active() ? 2u : 0u) |
            (Crabit::CompanionBle::transitioning() ? 4u : 0u) |
            (unmounting ? 8u : 0u);
        Crabit::CompanionTrace::record(Crabit::CompanionTrace::UiStage, 0,
            static_cast<uint32_t>(state), flags);
    }

    void showStatus(const char* text) {
        if (mode == Mode::BleBackground) {
            // The normal STM32 page still owns OLED/SPI in background BLE.
            // Paint its existing PopupRoot instead of a diagnostic full page.
            const char* message = "蓝牙正在启动";
            const char* status = !strcmp(text, "STM32 RECOVERED!") ? background_result : text;
            if (!strcmp(status, "UART RX FAILED")) message = "串口接收失败";
            else if (!strcmp(status, "UART SEND FAILED")) message = "串口发送失败";
            else if (!strcmp(status, "BLE START FAILED")) message = "蓝牙启动失败";
            else if (!strcmp(status, "BLE TIMEOUT")) message = "蓝牙启动超时";
            else if (!strcmp(status, "OFF")) message = "蓝牙已关闭";
            Popup::showToast(message);
            if (WindowManager::instance) WindowManager::instance->drawFrame();
            return;
        }
        canvas->clear();
        if (mode == Mode::Wifi) {
            const char* first = text;
            const char* second = "";
            const char* stage = Crabit::NetworkWeather::status();
            if (!strcmp(stage, "BOOT")) { first = "正在唤醒"; second = "无线模块"; }
            else if (!strcmp(stage, "CONFIGURING")) { first = "正在下发"; second = "网络配置"; }
            else if (!strcmp(stage, "UNCONFIGURED")) { first = "请先配置"; second = "网络与地区"; }
            else if (!strcmp(stage, "CONFIGURED")) { first = "网络配置"; second = "已就绪"; }
            else if (!strcmp(stage, "HTTP_SEND")) { first = "天气请求"; second = "正在发送"; }
            else if (!strcmp(stage, "HTTP_READ")) { first = "等待服务器"; second = "响应状态"; }
            else if (!strcmp(stage, "HTTP_HEADERS")) { first = "正在读取"; second = "响应头"; }
            else if (!strcmp(stage, "HTTP_BODY")) { first = "正在接收"; second = "天气数据"; }
            else if (!strcmp(stage, "HTTPS_CONNECT")) { first = "天气服务"; second = "建立安全连接"; }
            else if (!strcmp(stage, "WEATHER_CLOSE")) { first = "天气服务"; second = "关闭连接"; }
            else if (!strcmp(stage, "WEATHER_PARSE")) { first = "正在解析"; second = "天气数据"; }
            else if (!strcmp(stage, "CONNECTING")) { first = "正在连接"; second = "无线网络"; }
            else if (!strcmp(stage, "WIFI_ON")) { first = "网络已连接"; second = "准备校时"; }
            else if (!strcmp(stage, "TIME_OK")) { first = "时间已校准"; second = "正在获取天气"; }
            else if (!strcmp(stage, "NTP_REQUEST")) { first = "网络校时"; second = "正在请求"; }
            else if (!strcmp(stage, "TIME_UNAVAILABLE")) { first = "校时未成功"; second = "继续取天气"; }
            else if (!strcmp(stage, "WEATHER_ONLY")) { first = "天气已更新"; second = "时间未校准"; }
            else if (!strcmp(stage, "TIME_PENDING")) { first = "等待网络"; second = "时间回包"; }
            else if (!strcmp(stage, "DONE")) { first = "天气与时间"; second = "更新完成"; }
            else if (!strcmp(stage, "TIMEOUT")) { first = "天气更新"; second = "等待超时"; }
            else if (!strcmp(stage, "WIFI_FAILED")) { first = "无线网络"; second = "连接失败"; }
            else if (!strcmp(stage, "TIME_FAILED")) { first = "网络时间"; second = "校准失败"; }
            else if (!strcmp(stage, "HTTP_FAILED")) { first = "天气请求"; second = "失败"; }
            else if (!strcmp(stage, "PARSE_FAILED")) { first = "天气数据"; second = "解析失败"; }
            else if (!strcmp(stage, "INCOMPLETE")) { first = "天气数据"; second = "不完整"; }
            else if (!strcmp(stage, "CANCELLED")) { first = "更新已取消"; second = "保留旧天气"; }
            else if (!strcmp(stage, "UART_RX_FAILED")) { first = "串口接收"; second = "初始化失败"; }
            else if (!strcmp(stage, "TX_FAILED")) { first = "串口发送"; second = "失败"; }
            else if (!strcmp(text, "正在唤醒无线")) { first = "正在唤醒"; second = "无线模块"; }
            else if (!strcmp(text, "等待无线就绪")) { first = "等待无线"; second = "模块就绪"; }
            else if (!strcmp(text, "启动网络模式")) { first = "正在启动"; second = "网络模式"; }
            else if (!strcmp(text, "UART RX FAILED")) { first = "串口接收"; second = "初始化失败"; }
            else if (!strcmp(text, "串口发送失败")) { first = "串口发送"; second = "失败"; }
            else if (!strcmp(text, "请先配置网络与地区")) { first = "请先配置"; second = "网络与地区"; }
            else if (!strcmp(text, "配置就绪")) { first = "网络配置"; second = "已就绪"; }
            else if (!strcmp(text, "更新失败 请查看诊断")) { first = "天气更新"; second = "失败，请查诊断"; }
            drawStatusLines("网络服务", first, second);
        } else {
            const char* first = "模块状态", *second = text;
            if (!strcmp(text, "1. WAKING AMEBA...")) { first = "正在唤醒模块"; second = "准备启动 BW16"; }
            else if (!strcmp(text, "PAUSE BLE")) { first = "正在暂停蓝牙"; second = "随后进入 BW16"; }
            else if (!strcmp(text, "BLE STOP FAILED")) { first = "蓝牙关闭失败"; second = "未启动 BW16"; }
            else if (!strcmp(text, "BLE STOP TIMEOUT")) { first = "蓝牙关闭超时"; second = "未启动 BW16"; }
            else if (!strncmp(text, "2. AWAIT READY", 14)) { first = "等待模块就绪"; second = "正在建立通信"; }
            else if (!strcmp(text, "3. HANDING OVER!")) { first = "正在进入 BW16"; second = "即将切换屏幕"; }
            else if (!strcmp(text, "3. BLE MODE")) { first = "正在启动蓝牙"; second = "等待广播就绪"; }
            else if (!strcmp(text, "UART RX FAILED")) { first = "串口接收失败"; second = "请退出后重试"; }
            else if (!strcmp(text, "UART SEND FAILED")) { first = "串口发送失败"; second = "请退出后重试"; }
            else if (!strcmp(text, "WAIT BLE")) { first = "等待蓝牙就绪"; second = "请稍候"; }
            else if (!strcmp(text, "ADVERTISING")) { first = "蓝牙正在广播"; second = "可从客户端连接"; }
            else if (!strcmp(text, "CONNECTED") || !strcmp(text, "READY")) { first = "蓝牙已连接"; second = "等待客户端操作"; }
            else if (!strcmp(text, "PAIR_CONFIRMED")) { first = "连接已确认"; second = "等待客户端操作"; }
            else if (!strcmp(text, "DISCONNECTED")) { first = "蓝牙已断开"; second = "等待重新连接"; }
            else if (!strcmp(text, "STM32 RECOVERED!")) { first = "已返回手表"; second = "屏幕已恢复"; }
            else if (!strcmp(text, "FAILED") || !strcmp(text, "BLE START FAILED")) { first = "蓝牙启动失败"; second = "正在返回手表"; }
            else if (!strcmp(text, "BLE TIMEOUT")) { first = "蓝牙启动超时"; second = "正在返回手表"; }
            drawStatusLines(mode == Mode::Ui ? "BW16 应用" : "蓝牙服务", first, second);
        }
        canvas->display();
    }

    void drawStatusLines(const char* title, const char* first, const char* second) {
        auto centered = [this](int y, const char* value) {
            const int width = get_utf8_string_width(value, 8);
            canvas->drawText((128 - width) / 2, y, value, 8);
        };
        centered(5, title);
        centered(27, first);
        centered(43, second);
    }

    void waitIn(State next) {
        state = next;
        // Sample after hardware calls, which may include their proven delays.
        state_start = HAL_GetTick();
    }

    void checkEscape(uint32_t now) {
        // Preserve the physical UP+DOWN >2s escape, including tick wrap.
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET &&
            HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET) {
            if (!reset_held) {
                hard_reset_timer = now;
                reset_held = true;
            } else if (uint32_t(now - hard_reset_timer) > 2000) {
                state = State::PowerOff;
                Crabit::setAmebaPhase(Crabit::AmebaPhase::Transition);
                reset_held = false;
            }
        } else {
            reset_held = false;
        }
    }

    void runLink(uint32_t now) {
        UIEvent e;
        while ((e = input->poll()).type != EVT_NONE) {
            if (e.type == EVT_UP)    accum_btn_mask |= (1 << 0);
            if (e.type == EVT_DOWN)  accum_btn_mask |= (1 << 1);
            if (e.type == EVT_LEFT)  accum_btn_mask |= (1 << 2);
            if (e.type == EVT_RIGHT) accum_btn_mask |= (1 << 3);
            if (e.type == EVT_OK)    accum_btn_mask |= (1 << 4);
            if (e.type == EVT_BACK)  accum_btn_mask |= (1 << 5);
        }
        checkEscape(now);

        NanoUI::Sensor* pat = NanoUI::SensorManager::getInstance().getSensor("PAT9125");
        int lift = 1;
        if (pat) {
            auto* pdata = static_cast<const NanoUI::SensorDataOptical*>(pat->getData());
            if (pdata->is_valid) {
                lift = pdata->lifted ? 1 : 0;
                // SensorHub updates more slowly than this FRAME App. Consume
                // each delta once, including a valid first sample at tick zero.
                if (!optical_sample_seen || pdata->timestamp != last_optical_sample) {
                    accum_dx += pdata->dx;
                    accum_dy += pdata->dy;
                    last_optical_sample = pdata->timestamp;
                    optical_sample_seen = true;
                }
            }
        }

        // Retain the existing >=20ms send gate and input accumulation.
        if (uint32_t(now - last_sensor_sync) >= 20) {
            last_sensor_sync = now;
            uint8_t tx_mask = accum_btn_mask;
            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) tx_mask |= (1 << 0);
            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET) tx_mask |= (1 << 1);
            // PH3 remains exclusively decoded by input->poll().
            float ax = 0, ay = 0, az = 0;
            NanoUI::Sensor* imu = NanoUI::SensorManager::getInstance().getSensor("QMI8658");
            if (imu) {
                auto* idata = static_cast<const NanoUI::SensorDataIMU*>(imu->getData());
                ax = idata->ax; ay = idata->ay; az = idata->az;
            }
            int bat = 0;
            NanoUI::Sensor* batt = NanoUI::SensorManager::getInstance().getSensor("BATTERY");
            if (batt) {
                auto* bdata = static_cast<const NanoUI::SensorDataBattery*>(batt->getData());
                bat = (int)bdata->percent;
                if (bat > 100) bat = 100; else if (bat < 0) bat = 0;
            }
            char val_buf[64];
            snprintf(val_buf, sizeof(val_buf), "%d,%d,%d|%d,%d,%d|%d|%d",
                     (int)(ax*100), (int)(ay*100), (int)(az*100),
                     accum_dx, accum_dy, lift, bat, tx_mask);
            EventBus::publish("sns/all", val_buf);
            accum_dx = 0;
            accum_dy = 0;
            accum_btn_mask = 0;
        }
    }

public:
    const char* getAppName() override { return mode == Mode::Ui ? "AMEBA_LINK" : "AMEBA_BLE_MODE"; }
    AppMode getMode() override { return APP_MODE_FRAME; }

    void onMount(Canvas* target, InputDriver* source) override {
        canvas = target;
        input = source;
        state = State::Boot;
        state_start = anim_timer = hard_reset_timer = last_sensor_sync = 0;
        dot_cnt = 0;
        reset_held = false;
        accum_dx = accum_dy = 0;
        accum_btn_mask = 0;
        last_optical_sample = 0;
        optical_sample_seen = false;
        background_result = "OFF";
        mode_sent_at = 0;
        resume_ble_on_exit = mode == Mode::Ui && Crabit::CompanionBle::active();
        last_trace_state = State::Done;
        last_trace_tick = HAL_GetTick();
        if (mode == Mode::Ui)
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::UiEntry, 0,
                resume_ble_on_exit ? 1u : 0u, Crabit::CompanionBle::transitioning() ? 1u : 0u);
        ble_stop_requested = false;
        if (resume_ble_on_exit) {
            state = State::PauseBleWait;
            state_start = HAL_GetTick();
            showStatus("PAUSE BLE");
        }
        if (mode != Mode::Ui && mode != Mode::Wifi) {
            // Display observation only; do not carry a prior run's READY.
            GlobalStore.ble_status[0] = '\0';
            last_ble_status[0] = '\0';
        }
        Crabit::setAmebaPhase(Crabit::AmebaPhase::Transition);
    }

    void onUpdate(uint32_t) override {
        if (!canvas || !input) return;
        const uint32_t now = HAL_GetTick();
        if (mode == Mode::Ui) {
            if (last_trace_state != state) {
                traceUiStage();
                last_trace_state = state;
            }
            if (uint32_t(now - last_trace_tick) >= 1000) {
                last_trace_tick = now;
                Crabit::CompanionTrace::record(Crabit::CompanionTrace::UiHeartbeat, 0,
                    static_cast<uint32_t>(state), g_coproc_active ? 1u : 0u);
            }
        }
        switch (state) {
        case State::PauseBleWait:
            // Use the existing asynchronous stop, including its power-off wait.
            // Never boot/release SPI while that owner is active or transitioning.
            if (!Crabit::CompanionBle::active() && !Crabit::CompanionBle::transitioning()) {
                state = State::Boot;
            } else if (uint32_t(now - state_start) >= 6000) {
                resume_ble_on_exit = false; // Ownership is uncertain; do not retry or reboot it.
                Popup::showToast("蓝牙关闭超时");
                showStatus("BLE STOP TIMEOUT");
                state = State::Done;
                requestExit();
            } else if (!ble_stop_requested && !Crabit::CompanionBle::transitioning()) {
                if (Crabit::CompanionBle::stop()) ble_stop_requested = true;
                else {
                    resume_ble_on_exit = false;
                    Popup::showToast("蓝牙关闭失败");
                    showStatus("BLE STOP FAILED");
                    state = State::Done;
                    requestExit();
                }
            }
            break;
        case State::Boot:
            showStatus(mode == Mode::Wifi ? "正在唤醒无线" : "1. WAKING AMEBA...");
            // Keep this bounded, platform-owned boot sequence byte-for-byte.
            // Its internal delays are not a steady-state frame-time promise.
            if (mode == Mode::Ui) Crabit::CompanionTrace::record(Crabit::CompanionTrace::BootBegin);
            BW16_NormalBoot();
            if (mode == Mode::Ui) Crabit::CompanionTrace::record(Crabit::CompanionTrace::BootEnd);
            waitIn(State::AwaitReady);
            anim_timer = state_start;
            break;
        case State::AwaitReady:
            if (uint32_t(now - anim_timer) > 300) {
                anim_timer = now;
                dot_cnt = (dot_cnt + 1) % 4;
                const char* labels[] = {"2. AWAIT READY", "2. AWAIT READY.",
                                        "2. AWAIT READY..", "2. AWAIT READY..."};
                showStatus(mode == Mode::Wifi ? "等待无线就绪" : labels[dot_cnt]);
            }
            if (uint32_t(now - state_start) > 1200) {
                const bool uart_ready = STM32_PrepareAmebaUartRx();
                if (mode == Mode::Ui)
                    Crabit::CompanionTrace::record(Crabit::CompanionTrace::RxPrepared, 0, uart_ready ? 1u : 0u);
                if (uart_ready) {
                    state = State::HandoverStart;
                } else {
                    showStatus("UART RX FAILED");
                    if (mode == Mode::Wifi) Crabit::NetworkWeather::abort("UART_RX_FAILED");
                    if (mode == Mode::BleBackground || mode == Mode::Wifi) {
                        background_result = "UART RX FAILED";
                        state = State::PowerOff;
                    } else state = State::UartFailed;
                }
            }
            break;
        case State::UartFailed:
            // No retry, OLED handover or mode command after an RX setup error.
            // Retain the normal physical escape and its existing restore path.
            checkEscape(now);
            break;
        case State::HandoverStart:
            showStatus(mode == Mode::Wifi ? "启动网络模式" : mode == Mode::Ui ? "3. HANDING OVER!" : "3. BLE MODE");
            waitIn(State::HandoverWait);
            break;
        case State::HandoverWait:
            if (uint32_t(now - state_start) < 150) break;
            if (mode == Mode::Ui) {
                canvas->clear(); canvas->display();
                Safe_SPI_Release();
                g_coproc_active = true;
                traceUiStage();
            }
            // Same proven ordinary mode-command path; no sys/comm session.
            if (mode == Mode::BleBackground) {
                if (!EventBus::tryPublish("sys/mode", "BLE", Crabit::CompanionBle::sessionId())) {
                    background_result = "UART SEND FAILED";
                    showStatus(background_result);
                    state = State::PowerOff;
                    break;
                }
                mode_sent_at = HAL_GetTick();
            } else if (mode == Mode::Wifi) {
                if (!EventBus::tryPublish("sys/mode", "WIFI", Crabit::NetworkWeather::session())) {
                    Crabit::NetworkWeather::abort("TX_FAILED"); state = State::PowerOff; break;
                }
            } else {
                // Same single enqueue as publish(), preserving failure behavior
                // for this diagnostic build; record admission without retrying.
                const bool queued = EventBus::tryPublish("sys/mode", mode == Mode::Ui ? "UI" : "BLE", 0);
                Crabit::CompanionTrace::record(Crabit::CompanionTrace::ModeQueued, 0,
                    mode == Mode::Ui ? 0u : 1u, queued ? 1u : 0u);
            }
            waitIn(State::ModeWait);
            break;
        case State::ModeWait:
            if (uint32_t(now - state_start) < 150) break;
            state = State::Running;
            Crabit::setAmebaPhase(Crabit::AmebaPhase::Running);
            if (mode == Mode::Wifi) showStatus(Crabit::NetworkWeather::label());
            else if (mode != Mode::Ui) showStatus("WAIT BLE");
            break;
        case State::Running:
            if (mode == Mode::Wifi) {
                UIEvent event;
                while ((event = input->poll()).type != EVT_NONE) {
                    if (event.type == EVT_BACK) Crabit::NetworkWeather::cancel();
                }
                checkEscape(now);
                Crabit::NetworkWeather::tick();
                if (strcmp(last_network_status, Crabit::NetworkWeather::status())) {
                    snprintf(last_network_status, sizeof(last_network_status), "%s", Crabit::NetworkWeather::status());
                    showStatus(Crabit::NetworkWeather::label());
                }
                if (!Crabit::NetworkWeather::busy() || state == State::PowerOff) {
                    Crabit::NetworkWeather::cancel(); state = State::PowerOff;
                    Crabit::setAmebaPhase(Crabit::AmebaPhase::Transition);
                }
                break;
            }
            runLink(now);
            if (mode != Mode::Ui && strcmp(last_ble_status, GlobalStore.ble_status) != 0) {
                strncpy(last_ble_status, GlobalStore.ble_status, sizeof(last_ble_status) - 1);
                last_ble_status[sizeof(last_ble_status) - 1] = '\0';
                showStatus(last_ble_status[0] ? last_ble_status : "WAIT BLE");
            }
            if (mode == Mode::BleBackground && state == State::Running) {
                if (Crabit::CompanionBle::adoptModeBle()) {
                    // No bus handover or reset: the platform now owns BLE.
                    state = State::Done;
                    Crabit::setAmebaPhase(Crabit::AmebaPhase::Inactive);
                    if (WindowManager::instance) WindowManager::instance->markDirty();
                    requestExit();
                } else if (strcmp(GlobalStore.ble_status, "FAILED") == 0 ||
                           uint32_t(now - mode_sent_at) >= 5000) {
                    background_result = strcmp(GlobalStore.ble_status, "FAILED") == 0
                        ? "BLE START FAILED" : "BLE TIMEOUT";
                    showStatus(background_result);
                    state = State::PowerOff;
                    Crabit::setAmebaPhase(Crabit::AmebaPhase::Transition);
                }
            }
            break;
        case State::PowerOff:
            BW16_ForceOff();
            waitIn(State::PowerOffWait);
            break;
        case State::PowerOffWait:
            if (uint32_t(now - state_start) >= 250) {
                if (mode == Mode::Ui) state = State::RestoreStart;
                else {
                    // BLE never released the STM32 OLED/SPI bus.
                    if (mode == Mode::BleBackground)
                        Crabit::CompanionBle::finishModeBleStart(background_result);
                    showStatus(mode == Mode::Wifi ? Crabit::NetworkWeather::label() : "STM32 RECOVERED!");
                    waitIn(State::RecoveredWait);
                }
            }
            break;
        case State::RestoreStart:
            Safe_SPI_Claim();
            HAL_SPI_DeInit(&hspi3);
            waitIn(State::SpiWait);
            break;
        case State::SpiWait:
            if (uint32_t(now - state_start) < 5) break;
            HAL_SPI_Init(&hspi3);
            OLED_Init();
            OLED_SetSleep(false);
            waitIn(State::DisplayWakeWait);
            break;
        case State::DisplayWakeWait:
            if (uint32_t(now - state_start) < 50) break;
            g_coproc_active = false;
            showStatus("STM32 RECOVERED!");
            waitIn(State::RecoveredWait);
            break;
        case State::RecoveredWait:
            if (uint32_t(now - state_start) < 800) break;
            if (WindowManager::instance) WindowManager::instance->markDirty();
            state = State::Done;
            requestExit();
            break;
        case State::Done:
            break;
        }
    }

    // OLED writes deliberately remain ordered with the hardware steps above.
    // In RUN, no render callback or ordinary WindowManager draw owns this OLED.
    void onUnmount() override {
        if (mode == Mode::Wifi) Crabit::NetworkWeather::cancel();
        canvas = nullptr;
        input = nullptr;
        // An unexpected forced removal must not silently authorize PMU recovery
        // while BW16 may still own the buses. Normal exit reached Done first.
        Crabit::setAmebaPhase(state == State::Done ? Crabit::AmebaPhase::Inactive
                                                : Crabit::AmebaPhase::Transition);
        traceUiStage(true);
        if (state == State::Done && resume_ble_on_exit)
            Crabit::CompanionBle::resumeAfterUi();
    }
};

#endif
