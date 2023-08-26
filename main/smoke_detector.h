#ifndef SMOKE_DETECTOR_H
#define SMOKE_DETECTOR_H

// #define DEBUG_SLEEP

#define SMOKE_PIN GPIO_NUM_12
#define TIME_BATTERY_UPDATE 21600 // 6h

// time to check if smoke is still detected
#define TIME_SMOKE_CHECK 30 // 30s

// #define ADC_RAW_OFFSET 2240
// #define ADC_EQUATION_COEF 0.0104
// #define ADC_EQUATION_OFFSET 0.8259

#define ADC_RAW_OFFSET 2145
#define ADC_EQUATION_COEF 0.0111
#define ADC_EQUATION_OFFSET -0.0925

extern uint8_t lastBatteryPercentageRemaining;

void smoke_detector_main(void *arg);
void debugLoop(void *arg);
void updateBattery(void);
void sendSmoke(uint8_t smoke);
void setup_sleep(time_t time_s, uint8_t gpio_en);
void sleep_watchdog(void *arg);
void app_main(void);

#endif