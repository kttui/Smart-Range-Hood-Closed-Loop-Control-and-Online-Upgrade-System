#include <stdint.h>
#include "drv_actuator.h"
#include "bsp_gpio.h"

// GPIO 执行器的私有数据结构，包含 GPIO 引脚信息和当前状态等。业务层在初始化 LED/BEEP 设备时会填充这个结构体，并把它的指针赋值给 ActuatorDevice_t 的 private_data 字段。
typedef struct
{
    HAL_Pin_t pin_id;       /* 对应 bsp_gpio.h 中的 HAL_PIN_xxx */
    uint8_t active_level;   /* 高有效 / 低有效 */
    uint8_t current_on;     /* 当前逻辑状态：0关 1开 */
} GPIOActuatorPriv_t;

/* RGB 状态灯由三路 GPIO 组成，对外只暴露一个颜色型执行器。 */
typedef struct
{
    GPIOActuatorPriv_t red;
    GPIOActuatorPriv_t green;
    GPIOActuatorPriv_t blue;
    RGBColor_t current_color;
} RGBActuatorPriv_t;

/* =========================================================
 *                通用接口实现
 * ========================================================= */

int Actuator_Init(ActuatorDevice_t *dev)
{
    if (dev == 0 || dev->ops == 0 || dev->ops->init == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    return dev->ops->init(dev);
}

int Actuator_Set(ActuatorDevice_t *dev, const ActuatorValue_t *value)
{
    if (dev == 0 || value == 0 || dev->ops == 0 || dev->ops->set == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    if (dev->state != ACTUATOR_STATE_READY)
    {
        return ACTUATOR_ERR_STATE;
    }

    return dev->ops->set(dev, value);
}

int Actuator_Get(ActuatorDevice_t *dev, ActuatorValue_t *value)
{
    if (dev == 0 || value == 0 || dev->ops == 0 || dev->ops->get == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    if (dev->state != ACTUATOR_STATE_READY)
    {
        return ACTUATOR_ERR_STATE;
    }

    return dev->ops->get(dev, value);
}

int Actuator_Deinit(ActuatorDevice_t *dev)
{
    if (dev == 0 || dev->ops == 0 || dev->ops->deinit == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    return dev->ops->deinit(dev);
}

/* =========================================================
 *                GPIO 执行器公共实现
 * ========================================================= */
//
static void GPIOActuator_ConfigOutput(HAL_Pin_t pin_id)//配置 GPIO 引脚为推挽输出模式。业务层在执行器设备的初始化函数中会调用这个函数来完成 GPIO 的配置。
{
    HAL_GPIO_InitPin(pin_id, HAL_GPIO_MODE_OUTPUT_PP);
}
//根据执行器的 on/off 状态和电平有效性来计算实际输出电平，并通过 HAL_GPIO_WritePin 来控制 GPIO 引脚的电平。业务层在执行器设备的设置函数中会调用这个函数来改变 GPIO 的输出状态。
static int GPIOActuator_Write(GPIOActuatorPriv_t *priv, uint8_t on)
{
    uint8_t level;

    if (priv == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }
    
    level = on ? priv->active_level : (uint8_t)!priv->active_level;
    HAL_GPIO_WritePin(priv->pin_id, level);

    priv->current_on = on ? 1 : 0;
    return ACTUATOR_OK;
}

static int GPIOActuator_InitCommon(ActuatorDevice_t *dev)
{
    GPIOActuatorPriv_t *priv;

    if (dev == 0 || dev->private_data == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    priv = (GPIOActuatorPriv_t *)dev->private_data;

    GPIOActuator_ConfigOutput(priv->pin_id);

    /* 默认上电关闭 */
    GPIOActuator_Write(priv, 0);

    dev->state = ACTUATOR_STATE_READY;
    return ACTUATOR_OK;
}

static int GPIOActuator_SetCommon(ActuatorDevice_t *dev, const ActuatorValue_t *value)
{
    GPIOActuatorPriv_t *priv;
    int ret;

    if (dev == 0 || value == 0 || dev->private_data == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    priv = (GPIOActuatorPriv_t *)dev->private_data;

    ret = GPIOActuator_Write(priv, value->on);
    if (ret != ACTUATOR_OK)
    {
        dev->state = ACTUATOR_STATE_ERROR;
        return ret;
    }

    dev->state = ACTUATOR_STATE_READY;
    return ACTUATOR_OK;
}

static int GPIOActuator_GetCommon(ActuatorDevice_t *dev, ActuatorValue_t *value)
{
    GPIOActuatorPriv_t *priv;

    if (dev == 0 || value == 0 || dev->private_data == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    priv = (GPIOActuatorPriv_t *)dev->private_data;
    value->on = priv->current_on;

    return ACTUATOR_OK;
}

static int GPIOActuator_DeinitCommon(ActuatorDevice_t *dev)
{
    GPIOActuatorPriv_t *priv;

    if (dev == 0 || dev->private_data == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    priv = (GPIOActuatorPriv_t *)dev->private_data;

    /* 反初始化前先关闭，避免设备残留导通 */
    GPIOActuator_Write(priv, 0);

    dev->state = ACTUATOR_STATE_UNINIT;
    return ACTUATOR_OK;
}

/* =========================================================
 *                RGB 状态灯实现
 * ========================================================= */

static int RGBActuator_WriteColor(RGBActuatorPriv_t *priv, RGBColor_t color)
{
    uint8_t red_on = 0;
    uint8_t green_on = 0;
    uint8_t blue_on = 0;


    if (priv == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    /*
     * 颜色映射只在这里维护。
     * 业务层设置 RGB_COLOR_PURPLE 即可，不需要知道紫色由红灯和蓝灯组合。
     */
    switch (color)
    {
    case RGB_COLOR_OFF:
        /*
         * 关闭 RGB 时三路都必须写“逻辑关”。
         * 当前硬件 LED_R/G/B 是低电平有效，如果这里误写成 1，
         * GPIOActuator_Write() 会把引脚拉低，等于上电默认点亮三路灯。
         * 若外部没有合适限流电阻，PC7/PC8/PC9 会持续灌电流，可能导致主控发热。
         */
        red_on = 1;
        green_on = 1;
        blue_on = 1;
        break;

    case RGB_COLOR_RED:
        red_on = 1;
        break;

    case RGB_COLOR_GREEN:
        green_on = 1;
        break;

    case RGB_COLOR_BLUE:
        blue_on = 1;
        break;

    case RGB_COLOR_PURPLE:
        red_on = 1;
        blue_on = 1;
        break;

    default:
        return ACTUATOR_ERR_PARAM;
    }

    GPIOActuator_Write(&priv->red, red_on);
    GPIOActuator_Write(&priv->green, green_on);
    GPIOActuator_Write(&priv->blue, blue_on);
    priv->current_color = color;

    return ACTUATOR_OK;
}

static int RGB_LED_Init(ActuatorDevice_t *dev)
{
    RGBActuatorPriv_t *priv;

    if (dev == 0 || dev->private_data == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    priv = (RGBActuatorPriv_t *)dev->private_data;

    GPIOActuator_ConfigOutput(priv->red.pin_id);
    GPIOActuator_ConfigOutput(priv->green.pin_id);
    GPIOActuator_ConfigOutput(priv->blue.pin_id);

    RGBActuator_WriteColor(priv, RGB_COLOR_OFF);

    dev->state = ACTUATOR_STATE_READY;
    return ACTUATOR_OK;
}

static int RGB_LED_Set(ActuatorDevice_t *dev, const ActuatorValue_t *value)
{
    RGBActuatorPriv_t *priv;
    RGBColor_t target_color;
    int ret;

    if (dev == 0 || value == 0 || dev->private_data == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    priv = (RGBActuatorPriv_t *)dev->private_data;
    target_color = value->on ? value->rgb_color : RGB_COLOR_OFF;

    ret = RGBActuator_WriteColor(priv, target_color);
    if (ret != ACTUATOR_OK)
    {
        dev->state = ACTUATOR_STATE_ERROR;
        return ret;
    }

    dev->state = ACTUATOR_STATE_READY;
    return ACTUATOR_OK;
}

static int RGB_LED_Get(ActuatorDevice_t *dev, ActuatorValue_t *value)
{
    RGBActuatorPriv_t *priv;

    if (dev == 0 || value == 0 || dev->private_data == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    priv = (RGBActuatorPriv_t *)dev->private_data;
    value->rgb_color = priv->current_color;
    value->on = (priv->current_color == RGB_COLOR_OFF) ? 0 : 1;

    return ACTUATOR_OK;
}

static int RGB_LED_Deinit(ActuatorDevice_t *dev)
{
    RGBActuatorPriv_t *priv;

    if (dev == 0 || dev->private_data == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    priv = (RGBActuatorPriv_t *)dev->private_data;
    RGBActuator_WriteColor(priv, RGB_COLOR_OFF);

    dev->state = ACTUATOR_STATE_UNINIT;
    return ACTUATOR_OK;
}

static RGBActuatorPriv_t g_rgb_led_priv = {
    .red = {
        .pin_id = HAL_PIN_LED_R,
        .active_level = LED_R_ACTIVE_LEVEL,
        .current_on = 0
    },
    .green = {
        .pin_id = HAL_PIN_LED_G,
        .active_level = LED_G_ACTIVE_LEVEL,
        .current_on = 0
    },
    .blue = {
        .pin_id = HAL_PIN_LED_B,
        .active_level = LED_B_ACTIVE_LEVEL,
        .current_on = 0
    },
    .current_color = RGB_COLOR_OFF
};

static const ActuatorOps_t g_rgb_led_ops = {
    .init = RGB_LED_Init,
    .set = RGB_LED_Set,
    .get = RGB_LED_Get,
    .deinit = RGB_LED_Deinit
};

ActuatorDevice_t g_rgb_led_actuator = {
    .name = "rgb_led",
    .type = ACTUATOR_TYPE_RGB,
    .state = ACTUATOR_STATE_UNINIT,
    .private_data = &g_rgb_led_priv,
    .ops = &g_rgb_led_ops
};

int RGB_Led_SetColor(RGBColor_t color)
{
    ActuatorValue_t value;

    value.on = (color == RGB_COLOR_OFF) ? 0 : 1;
    value.rgb_color = color;

    return Actuator_Set(&g_rgb_led_actuator, &value);
}

int RGB_Led_GetColor(RGBColor_t *color)
{
    ActuatorValue_t value;
    int ret;

    if (color == 0)
    {
        return ACTUATOR_ERR_PARAM;
    }

    ret = Actuator_Get(&g_rgb_led_actuator, &value);
    if (ret != ACTUATOR_OK)
    {
        return ret;
    }

    *color = value.rgb_color;
    return ACTUATOR_OK;
}

/* =========================================================
 *                独立照明灯实现
 * ========================================================= */
//LED_Light设备定义
static int LED_Light_Init(ActuatorDevice_t *dev)
{
    return GPIOActuator_InitCommon(dev);
}
static int LED_Light_Set(ActuatorDevice_t *dev, const ActuatorValue_t *value)
{
    return GPIOActuator_SetCommon(dev, value);
}

static int LED_Light_Get(ActuatorDevice_t *dev, ActuatorValue_t *value)
{
    return GPIOActuator_GetCommon(dev, value);
}

static int LED_Light_Deinit(ActuatorDevice_t *dev)
{
    return GPIOActuator_DeinitCommon(dev);
}

static GPIOActuatorPriv_t g_led_light_priv = {
    .pin_id = HAL_PIN_LED_LIGHT,
    .active_level = LED_LIGHT_ACTIVE_LEVEL,
    .current_on = 0
};

static const ActuatorOps_t g_led_light_ops = {
    .init = LED_Light_Init,
    .set = LED_Light_Set,
    .get = LED_Light_Get,
    .deinit = LED_Light_Deinit
};

ActuatorDevice_t g_led_light_actuator = {
    .name = "led_light",
    .type = ACTUATOR_TYPE_LED,
    .state = ACTUATOR_STATE_UNINIT,
    .private_data = &g_led_light_priv,
    .ops = &g_led_light_ops
};

/* =========================================================
 *                BEEP 实现
 * ========================================================= */

static int BEEP_Init(ActuatorDevice_t *dev)
{
    return GPIOActuator_InitCommon(dev);
}

static int BEEP_Set(ActuatorDevice_t *dev, const ActuatorValue_t *value)
{
    return GPIOActuator_SetCommon(dev, value);
}

static int BEEP_Get(ActuatorDevice_t *dev, ActuatorValue_t *value)
{
    return GPIOActuator_GetCommon(dev, value);
}

static int BEEP_Deinit(ActuatorDevice_t *dev)
{
    return GPIOActuator_DeinitCommon(dev);
}

static GPIOActuatorPriv_t g_beep_priv = {
    .pin_id =    HAL_PIN_BEEP_ALARM,                  /* 这里改成你的蜂鸣器引脚宏 */
    .active_level = BEEP_ACTIVE_LEVEL,
    .current_on = 0
};

static const ActuatorOps_t g_beep_ops = {
    .init = BEEP_Init,
    .set = BEEP_Set,
    .get = BEEP_Get,
    .deinit = BEEP_Deinit
};

ActuatorDevice_t g_beep_actuator = {
    .name = "beep_main",
    .type = ACTUATOR_TYPE_BEEP,
    .state = ACTUATOR_STATE_UNINIT,
    .private_data = &g_beep_priv,
    .ops = &g_beep_ops
};
