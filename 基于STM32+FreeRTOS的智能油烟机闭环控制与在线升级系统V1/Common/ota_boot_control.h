#ifndef __OTA_BOOT_CONTROL_H
#define __OTA_BOOT_CONTROL_H

#include <stdint.h>

void Ota_BootControl_Init(void);
void Ota_BootControl_RequestUpdate(void);
uint8_t Ota_BootControl_UpdateRequested(void);
void Ota_BootControl_ClearUpdateRequest(void);
void Ota_BootControl_MarkHandoff(void);
uint8_t Ota_BootControl_ConsumeHandoff(void);
void Ota_BootControl_MarkApplicationOk(void);

#endif /* __OTA_BOOT_CONTROL_H */
