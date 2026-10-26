#ifndef CRABIT_NETWORK_WEATHER_H
#define CRABIT_NETWORK_WEATHER_H
#include "../../../shared/weather_contract.h"
#include "../../../shared/crabit_wire.h"
namespace Crabit { namespace NetworkWeather {
void init();
bool receive(const CrabitWire::Message& message);
bool configured();
bool begin();
void tick();
void cancel();
void abort(const char* reason);
bool busy();
uint16_t session();
const char* status();
const char* label();
bool timeValid();
unsigned count();
const CrabitWeather::Day& day(unsigned index);
const char* condition(uint16_t code);
unsigned phoneCount();
const CrabitWeather::Day& phoneDay(unsigned index);
uint32_t lastUpdated();
int16_t lastUpdatedOffset();
bool phoneClockValid();
const char* phoneLabel();
} }
#endif
