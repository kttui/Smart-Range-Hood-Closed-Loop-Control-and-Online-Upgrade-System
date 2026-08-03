#include "drv_key.h"
#include "bsp_gpio.h"

/*
 * drv_key 只负责按键输入、消抖、短按/长按识别。
 * 事件含义和系统状态切换放到 control_service，避免驱动层掺入业务规则。
 */
#define KEY_SCAN_PERIOD_MS        10
#define KEY_DEBOUNCE_MS           30
#define KEY_LONG_PRESS_MS         1500

#define KEY_DEBOUNCE_TICKS        (KEY_DEBOUNCE_MS / KEY_SCAN_PERIOD_MS)
#define KEY_LONG_PRESS_TICKS      (KEY_LONG_PRESS_MS / KEY_SCAN_PERIOD_MS)

typedef struct
{
    KeyId_t id;
    HAL_Pin_t pin;
    uint8_t active_level;
    uint8_t last_raw_level;
    uint8_t stable_level;
    uint8_t debounce_ticks;
    uint16_t press_ticks;
    uint8_t pressed;
    uint8_t long_reported;
} KeyState_t;

static KeyState_t g_key_states[] = {
    {KEY_ID_MODE, HAL_PIN_KEY_MODE, KEY_MODE_ACTIVE_LEVEL, 1, 1, 0, 0, 0, 0},
    {KEY_ID_SPEED, HAL_PIN_KEY_SPEED, KEY_SPEED_ACTIVE_LEVEL, 1, 1, 0, 0, 0, 0},
    {KEY_ID_LIGHT, HAL_PIN_KEY_LIGHT, KEY_LIGHT_ACTIVE_LEVEL, 1, 1, 0, 0, 0, 0},
    {KEY_ID_UPGRADE, HAL_PIN_KEY_UPGRADE, KEY_UPGRADE_ACTIVE_LEVEL, 1, 1, 0, 0, 0, 0}
};

#define KEY_COUNT                 (sizeof(g_key_states) / sizeof(g_key_states[0]))

static uint8_t Key_IsPressedLevel(const KeyState_t *key, uint8_t level)
{
    return (level == key->active_level) ? 1 : 0;
}

void Key_Init(void)
{
    uint8_t i;
    uint8_t raw_level;

    for (i = 0; i < KEY_COUNT; i++)
    {
        HAL_GPIO_InitPin(g_key_states[i].pin, HAL_GPIO_MODE_INPUT_PU);

        raw_level = HAL_GPIO_ReadPin(g_key_states[i].pin);
        g_key_states[i].last_raw_level = raw_level;
        g_key_states[i].stable_level = raw_level;
        g_key_states[i].debounce_ticks = 0;
        g_key_states[i].press_ticks = 0;
        g_key_states[i].pressed = Key_IsPressedLevel(&g_key_states[i], raw_level);
        g_key_states[i].long_reported = 0;
    }
}

uint8_t Key_Scan(KeyEvent_t *event)
{
    uint8_t i;
    uint8_t raw_level;
    KeyState_t *key;

    if (event == 0)
    {
        return 0;
    }

    event->id = KEY_ID_NONE;
    event->action = KEY_ACTION_NONE;

    for (i = 0; i < KEY_COUNT; i++)
    {
        key = &g_key_states[i];
        raw_level = HAL_GPIO_ReadPin(key->pin);

        if (raw_level != key->last_raw_level)
        {
            key->last_raw_level = raw_level;
            key->debounce_ticks = 0;
            continue;
        }

        if (key->debounce_ticks < KEY_DEBOUNCE_TICKS)
        {
            key->debounce_ticks++;
            continue;
        }

        if (raw_level != key->stable_level)
        {
            key->stable_level = raw_level;

            if (Key_IsPressedLevel(key, raw_level))
            {
                key->pressed = 1;
                key->press_ticks = 0;
                key->long_reported = 0;
            }
            else
            {
                /*
                 * 松手时，如果本次按压没有上报过长按，就认为是短按。
                 * 长按已经上报后，松手不再补发短按，避免一次动作产生两个业务事件。
                 */
                if (key->pressed && !key->long_reported)
                {
                    event->id = key->id;
                    event->action = KEY_ACTION_SHORT;
                }

                key->pressed = 0;
                key->press_ticks = 0;
                key->long_reported = 0;
            }
        }
        else if (key->pressed && !key->long_reported)
        {
            if (key->press_ticks < KEY_LONG_PRESS_TICKS)
            {
                key->press_ticks++;
            }

            if (key->press_ticks >= KEY_LONG_PRESS_TICKS)
            {
                key->long_reported = 1;
                event->id = key->id;
                event->action = KEY_ACTION_LONG;
            }
        }

        if (event->action != KEY_ACTION_NONE)
        {
            return 1;
        }
    }

    return 0;
}
