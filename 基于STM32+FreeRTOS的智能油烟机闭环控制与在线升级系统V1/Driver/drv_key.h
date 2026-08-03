#ifndef __DRV_KEY_H
#define __DRV_KEY_H

#include <stdint.h>

typedef enum
{
    KEY_ID_NONE = 0,
    KEY_ID_MODE,
    KEY_ID_SPEED,
    KEY_ID_LIGHT,
    KEY_ID_UPGRADE
} KeyId_t;

typedef enum
{
    KEY_ACTION_NONE = 0,
    KEY_ACTION_SHORT,
    KEY_ACTION_LONG
} KeyAction_t;

typedef struct
{
    KeyId_t id;
    KeyAction_t action;
} KeyEvent_t;

void Key_Init(void);
uint8_t Key_Scan(KeyEvent_t *event);

#endif
