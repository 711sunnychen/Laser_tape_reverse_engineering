#include "stm32f10x_it.h"
#include "config_periph.h"
#include "capture_configure.h"
#include "pll_functions.h"
#include "measure_functions.h"
#include "delay_us_timer.h"
#include "distance_calc.h"
#include "stdlib.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>

#define CALIBRATION_REPEAT_NUMBER       64  //number of averaging points for calibration - for single freqency
#define REPEAT_NUMBER                   3   //number of averaging points
#define SWITCH_DELAY                    300 //time in uS to switch frequency

AnalyseResultType result1;
AnalyseResultType result2;
AnalyseResultType result3;

extern uint16_t APD_temperature_raw ;//raw temperature value
extern float  APD_current_voltage;//value in volts
extern uint8_t  measure_enabled;//auto distance measurement enabled flag
extern volatile uint8_t capture_done;

extern volatile uint16_t adc_capture_buffer1[ADC_CAPURE_BUF_LENGTH];//signal+reference points
extern volatile uint16_t adc_capture_buffer2[ADC_CAPURE_BUF_LENGTH];//signal+reference points
extern volatile uint16_t adc_capture_buffer3[ADC_CAPURE_BUF_LENGTH];//signal+reference points

extern float  APD_temperature;//temperature value in deg

int16_t zero_phase1_calibration = 0;//phase value for zero distance
int16_t zero_phase2_calibration = 0;//phase value for zero distance
int16_t zero_phase3_calibration = 0;//phase value for zero distance

int32_t dist_resut = 0;

//local functions
ErrorStatus calibration_set_voltage(void);
AnalyseResultType process_captured_data(uint16_t* captured_data);

volatile dma_state_type dma_state = DMA_NO_DATA;
uint8_t new_data_ready = 0;//new data ready to be processed

//debug only
volatile uint32_t delta_time = 0;

//Auto switch capture process
void auto_handle_capture(void)
{
  static uint16_t* result_ptr;//pointer to data array (adc_capture_bufferX) with data to be processed
  
  if (new_data_ready == 1) return;//don't allowed to switch capture when previous data not processed
  
  if (dma_state == DMA_NO_DATA)
  {    
    pll_set_frequency_1();//162.5 + 162.505
    dwt_delay(SWITCH_DELAY);
    result_ptr = (uint16_t*)adc_capture_buffer1;
   
    start_adc_capture(result_ptr);//freq1
    dma_state = DMA_FREQ1_CAPTURING;
    new_data_ready = 1;//data3 ready
  }
  else if (dma_state == DMA_FREQ1_DONE)
  {
    //ready to switch to freq2
    pll_set_frequency_2();//191.5 + 191.505
    dwt_delay(SWITCH_DELAY);
    result_ptr = (uint16_t*)adc_capture_buffer2;
    
    start_adc_capture(result_ptr);//freq2
    dma_state = DMA_FREQ2_CAPTURING;
    new_data_ready = 1;//data1 ready
  }
  else if (dma_state == DMA_FREQ2_DONE)
  {
    //ready to switch to freq3
    pll_set_frequency_3();//193.5 + 193.505
    dwt_delay(SWITCH_DELAY);
    result_ptr = (uint16_t*)adc_capture_buffer3;
    
    start_adc_capture(result_ptr);//freq3
    dma_state = DMA_FREQ3_CAPTURING;
    new_data_ready = 1;//data2 ready
  }
  else if (dma_state == DMA_FREQ3_DONE)
  {
    //all data captured now
    dma_state = DMA_NO_DATA;
    
    do_single_adc_measurements();//measure temperature
    auto_switch_apd_voltage(result1.Amplitude);//if auto switch enabled, manual switching is not working
  }
}

void auto_handle_data_processing(void)
{
  static uint32_t old_dwt_value = 0;
  static char result_str[64];
  uint16_t result_length;

    
  if (new_data_ready == 0) return; //nothing to process
  
  if ((dma_state == DMA_FREQ1_CAPTURING) || (dma_state == DMA_FREQ1_DONE))
  {
    //debug functions
    uint32_t cur_dwt_value = get_dwt_value();
    delta_time = (cur_dwt_value - old_dwt_value) / 24;//24 MHz
    old_dwt_value = cur_dwt_value;
  
    result3 = process_captured_data((uint16_t*)adc_capture_buffer3);
    do_distance_calculation();
    
    result_length = sprintf(result_str, "DIST;%05d;AMP;%04d;TEMP;%04d;VOLT;%03d\r\n", dist_resut, result1.Amplitude, APD_temperature_raw, (uint8_t)APD_current_voltage);
    uart_dma_start_tx(result_str, result_length);//attention - new transmission interrupts previous one. 
    new_data_ready = 0;
  }
  else if ((dma_state == DMA_FREQ2_CAPTURING) || (dma_state == DMA_FREQ2_DONE)) 
  {
    result1 = process_captured_data((uint16_t*)adc_capture_buffer1);
    new_data_ready = 0;
  }
  else if ((dma_state == DMA_FREQ3_CAPTURING) || (dma_state == DMA_FREQ3_DONE)) 
  {
    result2 = process_captured_data((uint16_t*)adc_capture_buffer2);
    new_data_ready = 0;
  }
}


ErrorStatus do_phase_calibration(void)
{
  int16_t tmp_phase1 = 0;
  int16_t tmp_phase2 = 0;
  int16_t tmp_phase3 = 0;
  
#ifdef MODULE_701A
  printf("Calib Start 701A\r\n");
  enable_laser();
  measure_enabled = 1;
  
  Delay_ms(400);
  do_single_adc_measurements();//measure temperature
  auto_switch_apd_voltage(result1.Amplitude);
#else
  
  printf("Calib Start\r\n");
  enable_laser();
  measure_enabled = 1;
  
  Delay_ms(400);
  set_apd_voltage(APD_LOW_VOLTAGE);
  Delay_ms(100);
  do_single_adc_measurements();//measure temperature
#endif  

  //set freq1
  pll_set_frequency_1();//162.5 + 162.505
  Delay_ms(100);
  
  //try to find best voltage for calibration
  if (calibration_set_voltage() == ERROR) return ERROR;
  
  if (single_freq_calibration(&tmp_phase1) == ERROR) 
    return ERROR;
  printf("Zero Phase 1: %d\r\n", tmp_phase1);
  
  //set freq2
  pll_set_frequency_2();//191.5 + 191.505
  Delay_ms(100);
  if (single_freq_calibration(&tmp_phase2) == ERROR) 
    return ERROR;
  printf("Zero Phase 2: %d\r\n", tmp_phase2);
  
  //set freq3
  pll_set_frequency_3();//193.5 + 193.505
  Delay_ms(100);
  if (single_freq_calibration(&tmp_phase3) == ERROR) 
    return ERROR;
  printf("Zero Phase 3: %d\r\n", tmp_phase3);
  
  zero_phase1_calibration = tmp_phase1;
  zero_phase2_calibration = tmp_phase2;
  zero_phase3_calibration = tmp_phase3;
  
  write_data_to_flash(tmp_phase1, tmp_phase2, tmp_phase3);
  
  printf("Calib Done\r\n");
  return SUCCESS;
}

//Try to find best voltage for calibration
ErrorStatus calibration_set_voltage(void)
{
  AnalyseResultType tmp_result;
  tmp_result = do_capture();//measure signal

  if (tmp_result.Amplitude < 5) //amplitude too low
  {
    printf("Calib FAIL: Low Signal\r\n");
    return ERROR;
  } 
  
#ifndef MODULE_701A  //If module is MODULE_701A, needed voltage is set earlier
  if (tmp_result.Amplitude < 200) 
  {
    //try to increase voltage
    set_apd_voltage(APD_HIGH_VOLTAGE);
    Delay_ms(100);
    
    tmp_result = do_capture();//measure signal
    if ((tmp_result.Amplitude < 5) || (tmp_result.Amplitude > 2200))
    {
      set_apd_voltage(APD_LOW_VOLTAGE);//switch back to 80V
    }
  }
#endif
  printf("Calib voltage:%d\r\n", (uint8_t)APD_current_voltage);
  
  return SUCCESS;
}

//Calibration measurement for single frequency
//result_phase - result value will be written at this pointer
ErrorStatus single_freq_calibration(int16_t* result_phase)
{
  uint8_t i;
  AnalyseResultType tmp_result;
  int16_t phase_array[CALIBRATION_REPEAT_NUMBER];
  
  //do main measurements
  for (i = 0; i < CALIBRATION_REPEAT_NUMBER; i++)
  {
    tmp_result = do_capture();//measure signal
    
    if (tmp_result.Amplitude < CALIBRATION_MIN_AMPLITUDE)
    {
      printf("Calib FAIL: Low Signal\r\n");
      return ERROR;
    }
    phase_array[i] = tmp_result.Phase;
  }
  //calculate average phase value
  int16_t calib_result = calculate_avr_phase(phase_array, CALIBRATION_REPEAT_NUMBER);
  *result_phase = calib_result;
  
  return SUCCESS;
}

//Take 3 phases and calculate distance
void do_distance_calculation(void)
{
  //subtract zero phase offset
  int16_t tmp_phase1 = result1.Phase - zero_phase1_calibration;
  if (tmp_phase1 < 0) 
    tmp_phase1 = MAX_ANGLE * PHASE_MULT + tmp_phase1;
  
  int16_t tmp_phase2 = result2.Phase - zero_phase2_calibration;
  if (tmp_phase2 < 0) 
    tmp_phase2 = MAX_ANGLE * PHASE_MULT + tmp_phase2;
  
  int16_t tmp_phase3 = result3.Phase - zero_phase3_calibration;
  if (tmp_phase3 < 0) 
    tmp_phase3 = MAX_ANGLE * PHASE_MULT + tmp_phase3;
  
  dist_resut = triple_dist_calculaton(tmp_phase1, tmp_phase2, tmp_phase3);
}

//Phase measurement for single freqency
AnalyseResultType do_capture(void)
{
  dma_state = DMA_NO_DATA;
  new_data_ready = 0;
  
#if (REPEAT_NUMBER == 1)
  
  start_adc_capture((uint16_t*)adc_capture_buffer1);
  while(capture_done == 0) {}
  return process_captured_data((uint16_t*)adc_capture_buffer1);

#else
  
  AnalyseResultType main_result = {0,0};
  AnalyseResultType signal_result = {0,0};
  AnalyseResultType reference_result = {0,0};
  uint8_t i;
  int16_t phase_buffer[REPEAT_NUMBER];
  uint32_t amplitude_summ = 0;
  int16_t tmp_phase = 0;
  
  for (i=0; i<REPEAT_NUMBER; i++)
  {
    start_adc_capture((uint16_t*)adc_capture_buffer1);
    while(capture_done == 0) {}
    signal_result    = goertzel_analyse((uint16_t*)&adc_capture_buffer1[0]);//signal
    reference_result = goertzel_analyse((uint16_t*)&adc_capture_buffer1[1]);//reference
    
    tmp_phase = signal_result.Phase - reference_result.Phase;//difference between signal and reference phase
    if (tmp_phase < 0) 
      tmp_phase = MAX_ANGLE * PHASE_MULT + tmp_phase;
    
    amplitude_summ+= (uint32_t)signal_result.Amplitude;
    phase_buffer[i] = tmp_phase;
  }
  
  main_result.Amplitude = (uint16_t)(amplitude_summ / REPEAT_NUMBER);
  main_result.Phase = calculate_avr_phase(phase_buffer, REPEAT_NUMBER);
  
  main_result.Phase = main_result.Phase + calculate_correction(APD_temperature_raw, main_result.Amplitude, (uint8_t)APD_current_voltage);
  //phase correction can produce zero crossing
  //phase < 0 or > 360
  if (main_result.Phase < 0) 
    main_result.Phase = MAX_ANGLE * PHASE_MULT + main_result.Phase;
  else if (main_result.Phase > (MAX_ANGLE * PHASE_MULT)) 
    main_result.Phase = main_result.Phase - MAX_ANGLE * PHASE_MULT;
  
  return main_result;
#endif
}

AnalyseResultType process_captured_data(uint16_t* captured_data)
{
  AnalyseResultType main_result = {0,0};
  AnalyseResultType signal_result = {0,0};
  AnalyseResultType reference_result = {0,0};
  int16_t tmp_phase = 0;
  
  signal_result    = goertzel_analyse(&captured_data[0]);//signal
  reference_result = goertzel_analyse(&captured_data[1]);//reference
  tmp_phase = signal_result.Phase - reference_result.Phase;//difference between signal and reference phase
  if (tmp_phase < 0) 
    tmp_phase = MAX_ANGLE * PHASE_MULT + tmp_phase;
  main_result.Phase = tmp_phase;
  main_result.Amplitude = signal_result.Amplitude;
  
  //calculate correction
  main_result.Phase = main_result.Phase + calculate_correction(APD_temperature_raw, main_result.Amplitude, (uint8_t)APD_current_voltage);
  if (main_result.Phase < 0) 
    main_result.Phase = MAX_ANGLE * PHASE_MULT + main_result.Phase;
  else if (main_result.Phase > (MAX_ANGLE * PHASE_MULT)) 
    main_result.Phase = main_result.Phase - MAX_ANGLE * PHASE_MULT;
  
  return main_result;
}

//Switching APD voltage by signal level - AGC
#ifdef MODULE_701A

void auto_switch_apd_voltage(uint16_t current_amplitude)
{
  //APD voltage is depending only from a temperature
  float voltage_to_set = 0.4866667f * APD_temperature + 98.933f;
  if (voltage_to_set > 119.0f)
    voltage_to_set = 119.0f;
  set_apd_voltage(voltage_to_set);//testing only
}

#else

void auto_switch_apd_voltage(uint16_t current_amplitude)
{
  if (APD_current_voltage < (APD_LOW_VOLTAGE + 2.0f))
  {
    //LOW APD voltage now
    if (current_amplitude < 3) return;//APD overload!
    if (current_amplitude < 150) 
      set_apd_voltage(APD_HIGH_VOLTAGE);//try to increase voltage
  }
  else
  {
    //HIGH APD voltage now
    if (current_amplitude > 2200) 
      set_apd_voltage(APD_LOW_VOLTAGE);//try to decrease voltage
  }
}
#endif

//Write calibration data to flash
void write_data_to_flash(int16_t calib_phase1, int16_t calib_phase2, int16_t calib_phase3)
{
  uint32_t start_address = 0x8000000 + 1024*31;
  FLASH_Unlock();
  FLASH_ErasePage(start_address); //erase page 31
  FLASH_ProgramHalfWord(start_address, 0x1234); //data present key
  FLASH_ProgramHalfWord(start_address+2, (uint16_t)calib_phase1); //phase
  FLASH_ProgramHalfWord(start_address+4, (uint16_t)calib_phase2); //phase
  FLASH_ProgramHalfWord(start_address+6, (uint16_t)calib_phase3); //phase
  FLASH_Lock();
}

//Read calibration data
void read_calib_data_from_flash(void)
{
  uint32_t start_address = 0x8000000 + 1024*31;
  uint16_t data_key = (*(__IO uint16_t*)start_address);
  if (data_key != 0x1234)
    return; //no data in flash
  
  zero_phase1_calibration = (*(__IO int16_t*)(start_address + 2));
  zero_phase2_calibration = (*(__IO int16_t*)(start_address + 4));
  zero_phase3_calibration = (*(__IO int16_t*)(start_address + 6));
}
