"""Reproduce the captured empty circular-IDLE event using production code."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_companion_uart_probe import function

ROOT = Path(__file__).resolve().parents[2]


class IdlePhantomTest(unittest.TestCase):
    def test_actual_callback_filters_empty_idle_but_preserves_data_and_tc(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        port = (ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp").read_text(encoding="utf-8")
        callback = function(port, 'extern "C" void HAL_UARTEx_RxEventCallback(')
        parser = function(port, "for (uint16_t i = 0; i < total_len; i++)")
        fixture = r"""
#include "crabit_wire.h"
#include "companion_trace.h"
#include <cassert>
#include <vector>
#define HAL_UART_RXEVENT_TC 0
#define HAL_UART_RXEVENT_IDLE 2
#define DMA_CCR_CIRC 1
#define RESET 0
#define HAL_IS_BIT_SET(reg, bit) (((reg) & (bit)) != 0)
#define __HAL_DMA_GET_COUNTER(dma) read_counter(dma)
#define __HAL_DMA_GET_TC_FLAG_INDEX(dma) 1
#define __HAL_DMA_GET_FLAG(dma, flag) read_flags(dma)
#define portYIELD_FROM_ISR(value) (void)(value)
using BaseType_t = int;
constexpr int pdFALSE = 0, pdPASS = 1, RX_BUF_LEN = 512;
struct DmaInstance { unsigned CCR; } instance{DMA_CCR_CIRC};
struct Dma { DmaInstance* Instance; unsigned remaining, tc_pending; } dma{&instance,511,0};
bool complete_between_reads = false;
std::vector<unsigned> register_reads;
unsigned read_counter(Dma* d) {
    register_reads.push_back(1);
    const unsigned remaining = d->remaining;
    if (complete_between_reads) d->tc_pending = 1;
    return remaining;
}
unsigned read_flags(Dma* d) { register_reads.push_back(2); return d->tc_pending; }
int usart;
void* USART1 = &usart;
struct UART_HandleTypeDef { void* Instance; Dma* hdmarx; uint16_t RxXferSize; unsigned event; } uart{USART1,&dma,511,2};
unsigned HAL_UARTEx_GetRxEventType(UART_HandleTypeDef* h) { return h->event; }
struct RxQueueMsg { uint8_t idx; uint16_t len; };
uint8_t rx_write_idx = 0;
void* rx_msg_queue = &usart;
unsigned dbg_rx_evts = 0, dbg_rx_errs = 0, restarts = 0, filtered = 0;
std::vector<RxQueueMsg> queued;
int xQueueSendFromISR(void*, const RxQueueMsg* message, BaseType_t*) { queued.push_back(*message); return pdPASS; }
void STM32_RestartUartRx() { ++restarts; }
namespace Crabit { namespace CompanionBle { bool hardwareActive() { return true; } }
namespace CompanionTrace {
void record(Event event, uint16_t, uint32_t, uint32_t) { if (event == RxEmptyIdle) ++filtered; }
void message(const char*, const char*, uint16_t) {}
} }
"""
        fixture += (
            callback
            + r"""
unsigned trace_frames = 0, trace_bad_frames = 0, trace_ui_drops = 0;
uint8_t rx_stitch_buf[CrabitWire::FRAME_SIZE] = {}, decoded[1024] = {};
uint16_t stitch_idx = 0;
struct EventBus { static bool enqueueMessage(const CrabitWire::Message&) { return true; } };
void feed(const uint8_t* raw_data, uint16_t total_len) {
"""
        )
        fixture += (
            parser
            + r"""
}
void reset_case() {
    queued.clear(); restarts = 0; rx_write_idx = 0;
    dma.remaining = 511; dma.tc_pending = 0; instance.CCR = DMA_CCR_CIRC; uart.event = 2;
    complete_between_reads = false; register_reads.clear();
}
int main() {
    reset_case();
    HAL_UARTEx_RxEventCallback(&uart,511);
    assert(queued.empty() && restarts == 0 && rx_write_idx == 0 && filtered == 1);
    uint8_t payload[CrabitWire::PAYLOAD_SIZE], pong[CrabitWire::FRAME_SIZE];
    size_t payload_len = 0, frame_len = 0;
    assert(CrabitWire::encode_message("sys/notify", "BW16 PONG OK!", 0, payload,sizeof(payload),&payload_len));
    assert(CrabitWire::encode_frame(payload,payload_len,pong,sizeof(pong),&frame_len));
    dma.remaining = 511 - unsigned(frame_len);
    HAL_UARTEx_RxEventCallback(&uart,uint16_t(frame_len));
    assert(queued.size() == 1 && queued[0].idx == 0 && queued[0].len == 32);
    assert(restarts == 1 && rx_write_idx == 1);
    feed(pong,queued[0].len); assert(trace_frames == 1 && trace_bad_frames == 0);
    reset_case(); dma.tc_pending = 1; // IDLE beats pending full-transfer IRQ.
    HAL_UARTEx_RxEventCallback(&uart,511);
    assert(queued.size() == 1 && queued[0].len == 511 && restarts == 1 && rx_write_idx == 1);
    reset_case(); complete_between_reads = true;
    HAL_UARTEx_RxEventCallback(&uart,511);
    assert(register_reads.size() == 2 && register_reads[0] == 1 && register_reads[1] == 2);
    assert(queued.size() == 1 && queued[0].len == 511 && restarts == 1);
    reset_case(); uart.event = HAL_UART_RXEVENT_TC; // TC flag already cleared by HAL.
    HAL_UARTEx_RxEventCallback(&uart,511);
    assert(queued.size() == 1 && queued[0].len == 511 && restarts == 1);
    reset_case(); dma.remaining = 0; // Full buffer before circular counter reload.
    HAL_UARTEx_RxEventCallback(&uart,511); assert(queued.size() == 1);
    reset_case(); dma.remaining = 510; // Fresh data arrived after HAL's size snapshot.
    HAL_UARTEx_RxEventCallback(&uart,511); assert(queued.size() == 1);
    reset_case(); instance.CCR = 0; // Do not alter normal-DMA semantics.
    HAL_UARTEx_RxEventCallback(&uart,511); assert(queued.size() == 1);
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-idle-callback-") as folder:
            source = Path(folder) / "callback.cpp"
            binary = Path(folder) / "callback.exe"
            source.write_text(fixture, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "firmware/shared"),
                    "-I",
                    str(ROOT / "firmware/stm32/Core/Inc"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_hal_empty_idle_bytes_cannot_poison_following_pong(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        hal = (
            ROOT / "firmware/stm32/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart.c"
        ).read_text(encoding="utf-8")
        port = (ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp").read_text(encoding="utf-8")
        branch = function(hal, "if (nb_remaining_rx_data == huart->RxXferSize)")
        parser = function(port, "for (uint16_t i = 0; i < total_len; i++)")
        fixture = r"""
#include "crabit_wire.h"
#include <cassert>
#include <cstdio>
#include "companion_trace.h"
#define USE_HAL_UART_REGISTER_CALLBACKS 0
#define HAL_UART_RXEVENT_IDLE 2
#define DMA_CCR_CIRC 1
#define HAL_IS_BIT_SET(reg, bit) (((reg) & (bit)) != 0)
struct DmaInstance { unsigned CCR; };
struct Dma { DmaInstance* Instance; };
struct Uart { Dma* hdmarx; uint16_t RxXferSize; unsigned RxEventType; };
uint16_t callback_size = 0;
void HAL_UARTEx_RxEventCallback(Uart*, uint16_t size) { callback_size = size; }
void idle(Uart* huart, uint16_t nb_remaining_rx_data) {
"""
        fixture += (
            branch
            + r"""
}
unsigned trace_frames = 0, trace_bad_frames = 0, trace_ui_drops = 0, dbg_rx_errs = 0;
uint8_t rx_stitch_buf[CrabitWire::FRAME_SIZE] = {};
uint16_t stitch_idx = 0;
uint8_t decoded[1024] = {};
namespace Crabit { namespace CompanionTrace {
void record(Event, uint16_t, uint32_t, uint32_t) {}
void message(const char*, const char*, uint16_t) {}
} }
struct EventBus { static bool enqueueMessage(const CrabitWire::Message&) { return true; } };
void feed(const uint8_t* raw_data, uint16_t total_len) {
"""
        )
        fixture += (
            parser
            + r"""
}
int main() {
    DmaInstance instance{DMA_CCR_CIRC}; Dma dma{&instance};
    Uart uart{&dma, 511, 0};
    idle(&uart, 511); // DMA has received no bytes in the new transfer.
    assert(callback_size == 511 && uart.RxEventType == HAL_UART_RXEVENT_IDLE);
    uint8_t payload[CrabitWire::PAYLOAD_SIZE], pong[CrabitWire::FRAME_SIZE];
    size_t payload_len = 0, frame_len = 0;
    assert(CrabitWire::encode_message("sys/notify", "BW16 PONG OK!", 0,
        payload, sizeof(payload), &payload_len));
    assert(CrabitWire::encode_frame(payload, payload_len, pong, sizeof(pong), &frame_len));
    assert(frame_len == 32); // Matches the captured next actual RX event.
    feed(pong, uint16_t(frame_len)); assert(trace_frames == 1);
    stitch_idx = 0; trace_frames = trace_bad_frames = 0;
    uint8_t untouched[511] = {};
    feed(untouched, callback_size);
    assert(trace_frames == 0 && trace_bad_frames > 0 && stitch_idx == 1);
    feed(pong, uint16_t(frame_len));
    assert(trace_frames == 1 && stitch_idx == 0);
    puts("production HAL + parser: following PONG survives 511 phantom zero bytes");
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-idle-phantom-") as folder:
            source = Path(folder) / "idle.cpp"
            binary = Path(folder) / "idle.exe"
            source.write_text(fixture, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "firmware/shared"),
                    "-I",
                    str(ROOT / "firmware/stm32/Core/Inc"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
