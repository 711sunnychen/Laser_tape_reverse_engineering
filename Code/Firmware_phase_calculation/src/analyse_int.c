#include "stm32f10x_it.h"
#include "config_periph.h"
#include "capture_configure.h"
#include "math.h"
#include "analyse.h"
#include "stdlib.h"

#ifndef M_PI
#define M_PI            3.1415926535 
#endif

#define INT_COEF    32768

#define OMEGA ((2.0 * M_PI * K_COEF) / POINTS_TO_SAMPLE)

int64_t sin_coef = 0;
int64_t cos_coef = 0;
int64_t g_coef = 0;//goertzel coefficient


void init_goertzel(void)
{
  float tmp_val = 0.0;
  
  tmp_val = sin(OMEGA) * (float)INT_COEF;
  sin_coef = (int32_t)tmp_val;
  
  tmp_val = cos(OMEGA) * (float)INT_COEF;  
  cos_coef = (int32_t)tmp_val;
  
  g_coef = 2 * cos_coef;
}

//data goes like data[used] + data[not used] + data[used] ...
AnalyseResultType goertzel_analyse(uint16_t* data)
{
  uint16_t i;
  int64_t q0, q1, q2;
  int64_t real, imag;
  int16_t   scalingFactor = POINTS_TO_SAMPLE / 2.0;
  AnalyseResultType result;
  
  q0=0;
  q1=0;//n-1
  q2=0;//n-2
  
  for(i=0; i < POINTS_TO_SAMPLE; i++)
  {
    q0 = (g_coef * q1 / INT_COEF) - q2 + (int64_t)data[i*2] * (int64_t)INT_COEF;
    q2 = q1;
    q1 = q0;
  }
  
  real = (q1 - q2 * cos_coef/INT_COEF);
  imag = q2 * sin_coef/ INT_COEF;
  result = do_result_conversion((float)real, (float)imag);
  result.Amplitude = result.Amplitude / scalingFactor;
  return result;
}

//convert from Re/Im to Amplitude/Phase
AnalyseResultType do_result_conversion(float real, float imag)
{
  AnalyseResultType result;
  float amplitude = imag * imag + real*real;
  result.Amplitude = (uint32_t)sqrt(amplitude);
  
  float phase = atan2(imag, real);
  phase = phase * (float)(PHASE_MULT * MAX_ANGLE / 2) / M_PI;
  result.Phase = (int16_t)(phase);
  
  return result;
}

//phase in deg (0-3599)
//calculate average phase value
//data - array of values
int16_t calculate_avr_phase(int16_t* data, uint16_t length)
{
  uint16_t i;
  uint8_t zero_cross_flag = 0;
  int32_t tmp_val = 0;
  
  //test for zero cross
  for (i=0; i < length; i++)
  {
    if ((data[i] < (ZERO_ANGLE1 * PHASE_MULT)) || (data[i] > (ZERO_ANGLE2 * PHASE_MULT))) zero_cross_flag = 1;
  }
  
  if (zero_cross_flag == 0) //no zero cross 
  {
    //simple avarege calculation
    for (i = 0; i < length; i++) {tmp_val+= (int32_t)data[i];}
    tmp_val = tmp_val / length;
    return (uint16_t)tmp_val;
  }
  else
  {
    //remove zero crossing
    for (i = 0; i < length; i++) 
    {
      tmp_val+= (data[i] > HMAX_ANGLE * PHASE_MULT) ?  ((HMAX_ANGLE + MAX_ANGLE) * PHASE_MULT - data[i]) : (HMAX_ANGLE * PHASE_MULT - data[i]); //0->180, 359->181
    }
    tmp_val = tmp_val / length;//positive value
    //remove additional shift
    tmp_val = HMAX_ANGLE * PHASE_MULT - tmp_val;//can be negative
    if (tmp_val < 0) tmp_val = MAX_ANGLE * PHASE_MULT + tmp_val;
    return tmp_val;
  }
}

//calctulase phase_correction value
int16_t calculate_correction(uint16_t raw_temperature, uint16_t amplitude, uint8_t apd_voltage)
{
  int32_t tmp_value = 0;
  double tmp_value_d = 0.0;
  
  if (apd_voltage == 80)
  {
    //temperature compensation
    tmp_value = (int32_t)raw_temperature * (int32_t)(-7* PHASE_MULT) / 1024;
  }
  else if (apd_voltage == 95)
  {
    //temperature compensation
    tmp_value_d = -1.0 * (double)PHASE_MULT * exp( ((double)raw_temperature - 720.0) / 280.0);
    tmp_value = (int32_t)tmp_value_d;
    //amplitude compensation
    tmp_value = tmp_value-((int32_t)amplitude/(-1000/PHASE_MULT) + 2*PHASE_MULT);
    //Voltage correction
    tmp_value-= 55;//PHASE_MULT used
  }
  
  tmp_value_d = (double)tmp_value * CORR_360_DEG;
  return (int16_t)tmp_value_d;
  
  //return (int16_t)tmp_value;
}




