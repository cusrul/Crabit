"""Host tests compile the actual STM32 phone-weather receiver with faked HAL."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests import test_network_weather as legacy

ROOT = Path(__file__).resolve().parents[2]

PHONE_DRIVER = (
    legacy.HOST_DRIVER.split("int main(int argc, char** argv)", 1)[0]
    + r"""
using Crabit::NetworkWeather::lastUpdated;
using Crabit::NetworkWeather::phoneClockValid;
using Crabit::NetworkWeather::phoneCount;
using Crabit::NetworkWeather::phoneDay;
using Crabit::NetworkWeather::phoneLabel;

void phone(const char* payload, uint16_t id, const char* expected) {
    const auto before=published.size();
    require(rx("app/weather",payload,id),"phone topic was not consumed");
    require(published.size()==before+1,"phone command must return one correlated result");
    expectEqual(published.back().topic,"app/weather","phone reply topic");
    require(published.back().request_id==id,"phone reply request id mismatch");
    expectEqual(published.back().value,expected,"phone result");
}
void startPhone(uint32_t token=7, uint16_t id=1) {
    char command[100]; std::snprintf(command,sizeof(command),"begin=%lu,40712345,-73987654,480,2",(unsigned long)token);
    phone(command,id,"OK");
}
void stageComplete(uint32_t token=7, uint16_t id=2) {
    char command[64]; std::snprintf(command,sizeof(command),"time=%lu,1709210096",(unsigned long)token);
    phone(command,id,"OK");
    std::snprintf(command,sizeof(command),"day=%lu,0,20240229,1,-5,15,20",(unsigned long)token);
    phone(command,id+1,"OK");
    std::snprintf(command,sizeof(command),"day=%lu,1,20240301,2,0,20,10",(unsigned long)token);
    phone(command,id+2,"OK");
}
void commitPhone(uint32_t token, uint16_t id) {
    char command[40]; std::snprintf(command,sizeof(command),"commit=%lu",(unsigned long)token);
    phone(command,id,"DONE");
}
int main(int argc,char** argv) {
    try {
        init(); require(argc==2,"scenario required");
        const std::string which=argv[1];
        if (which=="success") {
            phone("read",90,"count=0;updated=0;clock=0"); startPhone(); stageComplete();
            require(phoneCount()==0,"staged forecast leaked before commit"); commitPhone(7,5);
            require(phoneCount()==2 && lastUpdated()==1709210096u && phoneClockValid(),"phone snapshot metadata mismatch");
            require(Crabit::NetworkWeather::lastUpdatedOffset()==480,"snapshot timezone not retained");
            require(phoneDay(0).date==20240229 && phoneDay(1).date==20240301,"committed days mismatch");
            require(!busy() && GlobalStore.net_time[0]=='\0',"phone path touched legacy network state");
            require(std::string(phoneLabel()).find("已更新")!=std::string::npos,"success label mismatch");
            phone("read",91,"count=2;updated=1709210096;clock=1");
        } else if (which=="malformed") {
            const auto no_reply=published.size(); require(rx("app/weather","read",0),"zero-id read was not consumed");
            require(published.size()==no_reply,"zero request id must not receive an uncorrelated result");
            phone("begin=0,0,0,0,1",1,"INVALID");
            phone("begin=2147483648,0,0,0,1",2,"INVALID");
            phone("begin=7,999999999999999999999,0,0,1",3,"INVALID");
            startPhone(); phone("time=8,1709210096",2,"STALE");
            phone("begin=8,0,0,0,1",7,"BUSY");
            phone("time=7,999999999999999999999",3,"INVALID");
            phone("day=7,0,20240230,1,0,1,0",4,"INVALID");
            phone("day=7,0,4294967296,1,0,1,0",8,"INVALID");
            phone("day=7,0,20240229,1,0,1,0",5,"OK");
            phone("day=7,0,20240301,1,0,1,0",6,"INVALID");
            require(phoneCount()==0 && !phoneClockValid(),"bad input changed committed cache/clock");
        } else if (which=="incomplete") {
            startPhone(); stageComplete(); phone("commit=7",5,"DONE");
            startPhone(8,20); phone("commit=8",21,"INCOMPLETE");
            require(std::string(phoneLabel()).find("同步")!=std::string::npos,"pending state reported stale success");
            require(phoneCount()==2 && lastUpdated()==1709210096u,"incomplete commit erased old cache");
        } else if (which=="cancel-stale") {
            startPhone(); phone("cancel=7",2,"OK");
            phone("time=7,1709210096",3,"STALE");
            startPhone(8,4); fake_tick+=29999; phone("time=8,1709210096",5,"OK");
            fake_tick+=1; phone("read",6,"count=0;updated=0;clock=0");
            phone("time=8,1709210096",8,"STALE");
            startPhone(9,7);
            require(phoneCount()==0,"cancel/TTL changed cache");
            fake_tick+=30000;
            phone("clock=1709210096,480",9,"DONE");
            require(phoneClockValid(),"expired upload blocked standalone clock");
        } else if (which=="rtc") {
            startPhone(); stageComplete(); commitPhone(7,5);
            startPhone(8,20); stageComplete(8,21);
            rtc_time_result=HAL_ERROR; phone("commit=8",24,"RTC_FAILED");
            require(phoneCount()==2 && lastUpdated()==1709210096u,"RTC failure erased prior snapshot");
            require(!phoneClockValid(),"failed RTC write retained a verified clock claim");
            rtc_time_result=HAL_OK; corrupt_rtc_readback=true; phone("commit=8",25,"RTC_FAILED");
            corrupt_rtc_readback=false; require(phoneCount()==2,"bad readback committed forecast");
            commitPhone(8,26);
            phone("clock=1709242200,180",30,"DONE");
            require(phoneClockValid(),"standalone clock success not recorded");
            phone("clock=1709242200,841",31,"INVALID");
            rtc_date_result=HAL_ERROR; phone("clock=1709242200,0",32,"RTC_FAILED"); rtc_date_result=HAL_OK;
            require(!phoneClockValid(),"partial standalone RTC write retained verified clock claim");
        } else if (which=="legacy-busy") {
            configureDefault(); require(begin(),"legacy Wi-Fi session failed to start");
            phone("begin=7,0,0,0,1",1,"BUSY"); cancel();
            require(phoneCount()==0,"busy request altered phone cache");
        } else throw std::runtime_error("unknown scenario");
        std::cout<<which<<" passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
"""
)


class PhoneWeatherHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = next(
            (
                p
                for p in (legacy.PREFERRED_COMPILER, shutil.which("g++"))
                if p and Path(p).is_file()
            ),
            None,
        )
        if compiler is None:
            raise RuntimeError("existing local g++ required")
        cls.temporary = tempfile.TemporaryDirectory(prefix="crabit-phone-weather-host-")
        temp = Path(cls.temporary.name)
        inc, src, shared = (
            temp / "firmware/stm32/Core/Inc",
            temp / "firmware/stm32/Core/Src",
            temp / "firmware/shared",
        )
        for folder in (inc, src, shared, inc / "NanoUI"):
            folder.mkdir(parents=True, exist_ok=True)
        for original, target in (
            (ROOT / "firmware/shared/weather_contract.h", shared / "weather_contract.h"),
            (ROOT / "firmware/shared/crabit_wire.h", shared / "crabit_wire.h"),
            (ROOT / "firmware/stm32/Core/Inc/network_weather.h", inc / "network_weather.h"),
            (ROOT / "firmware/stm32/Core/Src/network_weather.cpp", src / "network_weather.cpp"),
        ):
            shutil.copyfile(original, target)
            if original.read_bytes() != target.read_bytes():
                raise AssertionError("production source staging mismatch")
        (inc / "global_store.h").write_text(legacy.MOCK_GLOBAL_STORE, encoding="utf-8")
        (inc / "NanoUI/NanoUI.h").write_text(legacy.MOCK_NANOUI, encoding="utf-8")
        event_mock = legacy.MOCK_EVENT_BUS.replace(
            "bool amebaTransitionActive();",
            "bool amebaTransitionActive(); bool amebaSessionActive();",
        )
        (inc / "event_bus.h").write_text(event_mock, encoding="utf-8")
        (inc / "rtc.h").write_text(legacy.MOCK_RTC, encoding="utf-8")
        (inc / "companion_trace.h").write_text(legacy.MOCK_COMPANION_TRACE, encoding="utf-8")
        driver = PHONE_DRIVER.replace(
            "int rtc_get_time_result = HAL_OK, rtc_get_date_result = HAL_OK;",
            "int rtc_get_time_result = HAL_OK, rtc_get_date_result = HAL_OK; bool corrupt_rtc_readback=false;",
        )
        driver = driver.replace(
            "*value = last_rtc_time; return rtc_get_time_result;",
            "*value = last_rtc_time; if (corrupt_rtc_readback) value->Seconds += 2; return rtc_get_time_result;",
        )
        driver = driver.replace(
            "bool amebaTransitionActive() { return transition_active; }",
            "bool amebaTransitionActive() { return transition_active; } bool amebaSessionActive() { return false; }",
        )
        driver_path = temp / "phone_weather_host.cpp"
        driver_path.write_text(driver, encoding="utf-8")
        cls.binary = temp / "phone_weather_host.exe"
        built = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-I",
                str(inc),
                str(src / "network_weather.cpp"),
                str(driver_path),
                "-o",
                str(cls.binary),
            ],
            text=True,
            capture_output=True,
        )
        if built.returncode:
            raise RuntimeError("host compile failed:\n" + built.stdout + built.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), name + " passed")

    def test_atomic_phone_transaction_and_readback(self):
        self.run_case("success")

    def test_malformed_overflow_duplicate_day_and_token_mismatch(self):
        self.run_case("malformed")

    def test_missing_days_and_failed_commit_retain_old_snapshot(self):
        self.run_case("incomplete")

    def test_cancel_and_tick_checked_ttl(self):
        self.run_case("cancel-stale")

    def test_rtc_failure_retry_and_standalone_clock(self):
        self.run_case("rtc")

    def test_legacy_wifi_session_refuses_phone_begin(self):
        self.run_case("legacy-busy")


if __name__ == "__main__":
    unittest.main()
