#include "stm32f10x.h"
#include "analyse.h"

#ifndef _CAPTURE_CONFIGURE_H
#define _CAPTURE_CONFIGURE_H

#ifdef FAST_CAPTURE
  #define ADC_TRIGGER_FREQ        250000//sampling frequency, Hz
  #define POINTS_TO_SAMPLE        (250)//number of sampled points in single capture
#else
  #define ADC_TRIGGER_FREQ        50000//sampling frequency, Hz
  #define POINTS_TO_SAMPLE        (250 * 3)//number of sampled points in single capture
#endif

#define REPEAT_NUMBER           1 //number of averaging points



#define ADC_CAPURE_BUF_LENGTH   (POINTS_TO_SAMPLE*2)//signal points + reference points

#define TIMER1_FREQ             24000000 //Hz
#define TIMER1_DIV              6

#define TIMER1_PERIOD           (uint16_t)(TIMER1_FREQ / TIMER1_DIV / ADC_TRIGGER_FREQ)

#define SIGNAL_FREQ             5000 //APD signal and reference signal frequency, Hz

//#define K_COEF  15
#define K_COEF                  ((float)SIGNAL_FREQ * (float)POINTS_TO_SAMPLE / (float)ADC_TRIGGER_FREQ) //Goertzel coefficent - MUST be integer, or quality fall

#define SWITCH_DELAY            800//time in uS to switch frequency             

void switch_apd_voltage(uint8_t new_voltage);
void prepare_capture(void);
void start_adc_capture(void);


AnalyseResultType do_capture(void);


#endif
