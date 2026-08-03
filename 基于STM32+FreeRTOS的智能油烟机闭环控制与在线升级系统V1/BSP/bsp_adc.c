#include "bsp_adc.h"
#include "bsp_delay.h"
#include "bsp_gpio.h"

typedef struct{
    ADC_TypeDef *adc;
    HAL_Pin_t pin_id;
    uint8_t channel;
    uint8_t sample_time;
} HAL_adc_map_t;

static const HAL_adc_map_t hal_adc_map[HAL_ADC_IN_MAX] = {
    [HAL_ADC_IN_GAS_SENSOR] = {ADC1, HAL_PIN_GAS_SENSOR, GAS_ADC_CHANNEL, ADC_SampleTime_239Cycles5},
};
void HAL_ADC_GlobalInit(void)
{
    ADC_InitTypeDef adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    ADC_DeInit(ADC1);

    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;

    ADC_Init(ADC1, &adc);
    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));

    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

void HAL_ADC_InputInit(HAL_AdcInput_t input)
{
    HAL_GPIO_InitPin(hal_adc_map[input].pin_id, HAL_GPIO_MODE_ANALOG);
}

uint16_t HAL_ADC_Read(HAL_AdcInput_t input)
{
    ADC_RegularChannelConfig(
        hal_adc_map[input].adc,
        hal_adc_map[input].channel,
        1,
        hal_adc_map[input].sample_time
    );

    ADC_SoftwareStartConvCmd(hal_adc_map[input].adc, ENABLE);

    while (!ADC_GetFlagStatus(hal_adc_map[input].adc, ADC_FLAG_EOC));

    return ADC_GetConversionValue(hal_adc_map[input].adc);
}

uint16_t HAL_ADC_ReadAverage(HAL_AdcInput_t input, uint8_t times)
{
    uint32_t sum = 0;
    uint8_t i;

    for (i = 0; i < times; i++)
    {
        sum += HAL_ADC_Read(input);
        delay_ms(5);
    }

    return (uint16_t)(sum / times);
}
