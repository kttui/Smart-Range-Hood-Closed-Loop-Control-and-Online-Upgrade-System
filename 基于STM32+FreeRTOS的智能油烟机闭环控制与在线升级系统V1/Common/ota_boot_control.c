#include "ota_boot_control.h"
#include "ota_config.h"
#include "stm32f10x.h"
#include "stm32f10x_bkp.h"
#include "stm32f10x_pwr.h"
#include "stm32f10x_rcc.h"

void Ota_BootControl_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
}

void Ota_BootControl_RequestUpdate(void)
{
    Ota_BootControl_Init();
    BKP_WriteBackupRegister(BKP_DR1, OTA_BKP_REQUEST_MAGIC);
}

uint8_t Ota_BootControl_UpdateRequested(void)
{
    Ota_BootControl_Init();
    return (BKP_ReadBackupRegister(BKP_DR1) == OTA_BKP_REQUEST_MAGIC) ? 1U : 0U;
}

void Ota_BootControl_ClearUpdateRequest(void)
{
    Ota_BootControl_Init();
    BKP_WriteBackupRegister(BKP_DR1, 0U);
}

void Ota_BootControl_MarkHandoff(void)
{
    Ota_BootControl_Init();
    BKP_WriteBackupRegister(BKP_DR2, OTA_BKP_HANDOFF_MAGIC);
}

uint8_t Ota_BootControl_ConsumeHandoff(void)
{
    uint8_t handoff;

    Ota_BootControl_Init();
    handoff = (BKP_ReadBackupRegister(BKP_DR2) == OTA_BKP_HANDOFF_MAGIC) ? 1U : 0U;
    BKP_WriteBackupRegister(BKP_DR2, 0U);
    return handoff;
}

void Ota_BootControl_MarkApplicationOk(void)
{
    Ota_BootControl_Init();
    BKP_WriteBackupRegister(BKP_DR3, OTA_BKP_BOOT_OK_MAGIC);
    BKP_WriteBackupRegister(BKP_DR4, 0U);
    BKP_WriteBackupRegister(BKP_DR5, 0U);
}
