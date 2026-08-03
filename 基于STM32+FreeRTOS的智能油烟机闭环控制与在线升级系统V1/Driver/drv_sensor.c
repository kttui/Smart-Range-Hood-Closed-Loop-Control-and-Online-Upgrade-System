#include "drv_sensor.h"
#include "bsp_adc.h"
#include "bsp_gpio.h"
#include "bsp_delay.h"

/* =========================================================
 *                通用接口实现
 * ========================================================= */

int Sensor_Init(SensorDevice_t *dev)
{
    //1.安全检查，指针不能为空，操作表和初始化函数必须存在
    if (dev == 0 || dev->ops == 0 || dev->ops->init == 0)
    {
        return SENSOR_ERR_PARAM;
    }
    //2.多态调用，调用具体设备的初始化函数。设备的状态由具体实现来维护，业务层不关心细节。
    return dev->ops->init(dev);
}

int Sensor_Read(SensorDevice_t *dev, SensorData_t *data)
{
    //1.安全检查，指针不能为空，操作表和读取函数必须存在
    if (dev == 0 || data == 0 || dev->ops == 0 || dev->ops->read == 0)
    {
        return SENSOR_ERR_PARAM;
    }
    //2.状态检查，设备必须处于就绪状态才能读取数据。这个检查放在通用接口层，可以避免具体实现忘记检查状态导致的错误。
    if (dev->state != SENSOR_STATE_READY)
    {
        return SENSOR_ERR_STATE;
    }
    //3.多态调用，调用具体设备的读取函数。设备的状态由具体实现来维护，业务层不关心细节。
    return dev->ops->read(dev, data);
}

int Sensor_Deinit(SensorDevice_t *dev)
{
    //1.安全检查，指针不能为空，操作表和反初始化函数必须存在
    if (dev == 0 || dev->ops == 0 || dev->ops->deinit == 0)
    {
        return SENSOR_ERR_PARAM;
    }
    //2.多态调用，调用具体设备的反初始化函数。设备的状态由具体实现来维护，业务层不关心细节。
    return dev->ops->deinit(dev);
}

/* =========================================================
 *                GAS Sensor 实现
 * ========================================================= */
//定义私有数据结构，存放设备相关的上下文信息。对于光照传感器，我们需要知道ADC通道和采样次数等信息。
typedef struct
{
    HAL_AdcInput_t adc_channel;        // ADC输入通道，具体值由hal_adc.h定义
    uint8_t samples;        // 采样次数，读取函数会根据这个值来决定调用多少次ADC读取并取平均值，以提高测量稳定性。具体值可以根据实际情况调整，过多会增加读取时间，过少可能不够稳定。
} GasSensorPriv_t;

#define GAS_ADC_SAMPLES 10        //默认采样次数，可以根据实际情况调整
//全局私有数据实例，供光照传感器设备对象使用。对于简单的设备，如果没有复杂的上下文信息，可以直接把这个结构体放在设备对象的private_data里；如果有多个实例或者需要动态分配，可以改为指针并在初始化函数里分配。
static GasSensorPriv_t g_gas_priv = {
    .adc_channel = HAL_ADC_IN_GAS_SENSOR,
    .samples = GAS_ADC_SAMPLES
};
//气体传感器的具体实现。对于气体传感器，我们通过ADC读取原始值，然后根据ADC的分辨率（假设是12位，即0-4095）来计算气体浓度。这里假设ADC值越大表示气体浓度越高，所以直接使用原始值来计算浓度。
static int Gas_Init(SensorDevice_t *dev)
{
    GasSensorPriv_t *priv;        
    //1.安全检查，指针不能为空，设备的私有数据必须存在
    if (dev == 0 || dev->private_data == 0)
    {
        return SENSOR_ERR_PARAM;
    }
    
    priv = (GasSensorPriv_t *)dev->private_data;//获取私有数据指针，准备进行硬件初始化

    HAL_ADC_GlobalInit();//全局初始化ADC外设，配置时钟、分辨率等全局参数。对于简单的项目，可以在这里直接调用；对于复杂的项目，可能需要在更高层进行全局初始化，以避免重复初始化。
    HAL_ADC_InputInit(priv->adc_channel);//初始化ADC输入通道，配置对应的GPIO引脚为模拟输入等。具体实现由hal_adc.h提供，驱动层只需要调用即可。

    dev->state = SENSOR_STATE_READY;
    return SENSOR_OK;
}
//读取函数，负责采集数据并填充到 SensorData_t 结构中。对于光照传感器，我们只关心light_percent字段，temperature和humidity字段可以设置为0。
static int Gas_Read(SensorDevice_t *dev, SensorData_t *data)
{
    uint16_t raw;//原始ADC值，范围0-4095
    GasSensorPriv_t *priv;//私有数据指针，获取ADC通道和采样次数等信息

    if (dev == 0 || data == 0 || dev->private_data == 0)
    {
        return SENSOR_ERR_PARAM;
    }

    priv = (GasSensorPriv_t *)dev->private_data;//获取私有数据指针，准备进行数据采集
    raw = HAL_ADC_ReadAverage(priv->adc_channel, priv->samples);//调用HAL层的函数读取ADC值，并进行平均处理以提高稳定性。具体实现由hal_adc.h提供，驱动层只需要调用即可。

    if (raw >= 4095)//安全检查，防止除以0或者计算出错误的百分比。根据实际情况调整这个阈值，如果ADC的分辨率不是12位，应该改为对应的最大值。
    {
        data->gas_percent = 0;
    }
    else//根据ADC值计算气体浓度的百分比。这里假设ADC值越大表示气体浓度越高，所以直接使用原始值来计算百分比。具体的计算公式可能需要根据实际的传感器特性进行调整。
    {
        data->gas_percent = (uint8_t)(raw * 100 / 4095);
    }

    return SENSOR_OK;
}
//反初始化函数，负责释放资源。对于光照传感器，如果没有动态分配资源或者特殊的反初始化需求，可以直接把状态设置为未初始化即可。
static int Gas_Deinit(SensorDevice_t *dev)
{
    if (dev == 0)
    {
        return SENSOR_ERR_PARAM;
    }

    dev->state = SENSOR_STATE_UNINIT;
    return SENSOR_OK;
}
//气体传感器的操作表，指向该设备的功能函数实现。通过这个指针，业务层可以调用设备的功能函数，而不需要关心具体的实现细节。
static const SensorOps_t g_gas_ops = {
    .init = Gas_Init,
    .read = Gas_Read,
    .deinit = Gas_Deinit
};
//气体传感器设备对象，包含了设备的通用属性和一个指向操作表的指针。每个具体传感器设备实例都会是这个结构的一个对象，并且有自己的操作表实现。
SensorDevice_t g_gas_sensor = {
    .name = "gas_sensor",
    .type = SENSOR_TYPE_GAS_SENSOR,
    .state = SENSOR_STATE_UNINIT,
    .private_data = &g_gas_priv,
    .ops = &g_gas_ops
};

/* =========================================================
 *                DHT11 实现
 * ========================================================= */
//定义私有数据结构，存放设备相关的上下文信息。对于DHT11，我们可以存放上次读取的温湿度值，以便在读取失败时返回上次的值，增加鲁棒性。
typedef struct
{
    uint8_t last_temp;
    uint8_t last_humi;
} DHT11Priv_t;
//全局私有数据实例，供DHT11设备对象使用。对于简单的设备，如果没有复杂的上下文信息，可以直接把这个结构体放在设备对象的private_data里；如果有多个实例或者需要动态分配，可以改为指针并在初始化函数里分配。
static DHT11Priv_t g_dht11_priv = {
    .last_temp = 0,
    .last_humi = 0
};
// DHT11的具体实现。DHT11的通信协议比较特殊，需要通过GPIO模拟时序来进行通信。这里我们实现了DHT11的起始时序、读取字节和读取原始数据的函数，并在读取函数中调用这些底层函数来获取温湿度值。
static void DHT11_SetPinOutput(void)
{
    HAL_GPIO_InitPin(HAL_PIN_DHT11, HAL_GPIO_MODE_OUTPUT_PP);
}
static void DHT11_SetPinInput(void)
{
    HAL_GPIO_InitPin(HAL_PIN_DHT11, HAL_GPIO_MODE_INPUT_FLOATING);
    /* 若硬件没有外部上拉，可改为 INPUT_PU */
}
static void DHT11_WritePin(uint8_t level)
{
    HAL_GPIO_WritePin(HAL_PIN_DHT11, level);
}
static uint8_t DHT11_ReadPin(void)
{
    return HAL_GPIO_ReadPin(HAL_PIN_DHT11);
}

/*
 * DHT11 起始时序：
 * 1. 主机拉低至少18ms
 * 2. 主机拉高20~40us
 * 3. 主机释放总线，切换为输入
 */
static void DHT11_Start(void)
{
    DHT11_SetPinOutput();//配置GPIO为输出模式，准备发送起始信号

    DHT11_WritePin(0);//主机拉低至少18ms，确保DHT11能够检测到起始信号。这里我们设置为20ms，稍微长一些以增加可靠性。
    delay_ms(20);

    DHT11_WritePin(1);//主机拉高20~40us，准备切换为输入模式等待DHT11的响应。这里我们设置为30us，处于推荐范围内。
    delay_us(30);

    DHT11_SetPinInput();   //主机释放总线，切换为输入模式，等待DHT11的响应。DHT11会在20~40us内拉低总线作为响应信号。
}

static uint8_t DHT11_ReadByte(void)
{
    uint8_t i;//循环变量，控制读取8位数据
    uint8_t bit_value;//当前位的值，0或1
    uint8_t read_data = 0;//读取到的字节数据，初始值为0
    uint8_t t;//计时变量，用于测量信号持续的时间，以区分0和1。根据DHT11的协议，0的高电平持续约26~28us，1的高电平持续约70us，所以我们可以通过测量高电平的持续时间来判断当前位是0还是1。

    for (i = 0; i < 8; i++)//循环读取8位数据，构成一个字节
    {
        t = 0;
        while (DHT11_ReadPin() == 0 && t < 100)
        {
            delay_us(1);
            t++;
        }

        delay_us(30);

        bit_value = 0;
        if (DHT11_ReadPin() == 1)
        {
            bit_value = 1;
        }

        t = 0;
        while (DHT11_ReadPin() == 1 && t < 100)
        {
            delay_us(1);
            t++;
        }

        read_data <<= 1;
        read_data |= bit_value;
    }

    return read_data;
}

static int DHT11_ReadRaw(uint8_t *temp, uint8_t *humi)
{
    uint8_t i;
    uint8_t t;
    uint8_t buf[5] = {0};
    uint8_t sum = 0;

    if (temp == 0 || humi == 0)
    {
        return SENSOR_ERR_PARAM;
    }

    DHT11_Start();
    delay_us(20);

    t = 0;
    while (DHT11_ReadPin() == 1 && t < 100)
    {
        delay_us(1);
        t++;
    }
    if (t >= 100) return SENSOR_ERR_TIMEOUT;

    t = 0;
    while (DHT11_ReadPin() == 0 && t < 100)
    {
        delay_us(1);
        t++;
    }
    if (t >= 100) return SENSOR_ERR_TIMEOUT;

    t = 0;
    while (DHT11_ReadPin() == 1 && t < 100)
    {
        delay_us(1);
        t++;
    }
    if (t >= 100) return SENSOR_ERR_TIMEOUT;

    for (i = 0; i < 5; i++)
    {
        buf[i] = DHT11_ReadByte();
    }

    sum = buf[0] + buf[1] + buf[2] + buf[3];

    if (buf[4] != sum)
    {
        return SENSOR_ERR_CHECKSUM;
    }

    *humi = buf[0];
    *temp = buf[2];

    return SENSOR_OK;
}
// DHT11的初始化函数，负责配置硬件并准备就绪。对于DHT11，我们需要配置对应的GPIO引脚，并把设备状态设置为就绪。
static int DHT11_Init_Impl(SensorDevice_t *dev)
{
    if (dev == 0)
    {
        return SENSOR_ERR_PARAM;
    }

    DHT11_SetPinOutput();
    DHT11_WritePin(1);

    dev->state = SENSOR_STATE_READY;
    return SENSOR_OK;
}

static int DHT11_Read_Impl(SensorDevice_t *dev, SensorData_t *data)
{
    int ret;
    uint8_t temp;
    uint8_t humi;
    DHT11Priv_t *priv;

    if (dev == 0 || data == 0 || dev->private_data == 0)
    {
        return SENSOR_ERR_PARAM;
    }

    priv = (DHT11Priv_t *)dev->private_data;

    ret = DHT11_ReadRaw(&temp, &humi);
    if (ret != SENSOR_OK)
    {
        dev->state = SENSOR_STATE_ERROR;
        return ret;
    }

    data->temperature = temp;
    data->humidity = humi;

    priv->last_temp = temp;
    priv->last_humi = humi;

    dev->state = SENSOR_STATE_READY;
    return SENSOR_OK;
}

static int DHT11_Deinit_Impl(SensorDevice_t *dev)
{
    if (dev == 0)
    {
        return SENSOR_ERR_PARAM;
    }

    dev->state = SENSOR_STATE_UNINIT;
    return SENSOR_OK;
}

static const SensorOps_t g_dht11_ops = {
    .init = DHT11_Init_Impl,
    .read = DHT11_Read_Impl,
    .deinit = DHT11_Deinit_Impl
};

SensorDevice_t g_dht11_sensor = {
    .name = "dht11_sensor",
    .type = SENSOR_TYPE_DHT11,
    .state = SENSOR_STATE_UNINIT,
    .private_data = &g_dht11_priv,
    .ops = &g_dht11_ops
};
