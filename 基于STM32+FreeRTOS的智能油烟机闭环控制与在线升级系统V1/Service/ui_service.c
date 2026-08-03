#include "ui_service.h"
#include "drv_lcd.h"
#include "message_def.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>
#include "ota_config.h"

#define UI_LINE_HEIGHT          10
#define UI_PANEL_BG             LCD_COLOR_BLACK
#define UI_TEXT                 LCD_COLOR_WHITE
#define UI_TITLE                LCD_COLOR_CYAN
#define UI_OK                   LCD_COLOR_GREEN
#define UI_WARN                 LCD_COLOR_YELLOW
#define UI_BAD                  LCD_COLOR_RED
#define UI_LINE_COUNT           9
#define UI_LINE_CHARS           20
#define UI_LINE_BUF_SIZE        (UI_LINE_CHARS + 1)

typedef struct
{
    SystemMode_t mode;
    FanLevel_t fan_level;
    int16_t rpm;
    int16_t target_rpm;
    uint16_t duty;
    uint8_t gas_percent;
    uint8_t temperature;
    uint8_t humidity;
    uint8_t light_on;
    uint8_t backflow_active;
} UIStateSnapshot_t;

static uint8_t s_ui_inited = 0;
static char s_ui_last_lines[UI_LINE_COUNT][UI_LINE_BUF_SIZE];
static uint16_t s_ui_last_colors[UI_LINE_COUNT];

static const char *UI_Service_ModeText(SystemMode_t mode)
{
    switch (mode)
    {
    case SYS_MODE_OFF:
        return "OFF";
    case SYS_MODE_MANUAL:
        return "MANUAL";
    case SYS_MODE_AUTO:
        return "AUTO";
    case SYS_MODE_ALARM:
        return "ALARM";
    case SYS_MODE_UPDATE:
        return "UPDATE";
    case SYS_MODE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static const char *UI_Service_FanText(FanLevel_t level)
{
    switch (level)
    {
    case FAN_LEVEL_STOP:
        return "STOP";
    case FAN_LEVEL_LOW:
        return "LOW";
    case FAN_LEVEL_MID:
        return "MID";
    case FAN_LEVEL_HIGH:
        return "HIGH";
    default:
        return "UNK";
    }
}

static uint16_t UI_Service_ModeColor(SystemMode_t mode)
{
    switch (mode)
    {
    case SYS_MODE_MANUAL:
        return LCD_COLOR_CYAN;
    case SYS_MODE_AUTO:
        return UI_OK;
    case SYS_MODE_ALARM:
    case SYS_MODE_ERROR:
        return UI_BAD;
    case SYS_MODE_UPDATE:
        return LCD_COLOR_MAGENTA;
    case SYS_MODE_OFF:
    default:
        return UI_TEXT;
    }
}

static void UI_Service_ReadSnapshot(UIStateSnapshot_t *snapshot)
{
    if (snapshot == 0)
    {
        return;
    }

    /*
     * UI 只读取系统状态，不在这里修改模式、档位或输出。
     * 这样 LCD 刷新变慢或显示异常时，不会影响控制逻辑。
     */
    if (sys_data_mutex != 0)
    {
        (void)xSemaphoreTake(sys_data_mutex, portMAX_DELAY);
    }

    snapshot->mode = g_system_state.mode;
    snapshot->fan_level = g_system_state.fan_level;
    snapshot->rpm = g_system_state.motor.current_rpm;
    snapshot->target_rpm = g_system_state.motor.target_rpm;
    snapshot->duty = g_system_state.motor.pwm_duty;
    snapshot->gas_percent = g_system_state.sensor.gas_percent;
    snapshot->temperature = g_system_state.sensor.temperature;
    snapshot->humidity = g_system_state.sensor.humidity;
    snapshot->light_on = g_system_state.output.light_on;
    snapshot->backflow_active = g_system_state.motor.backflow_active;

    if (sys_data_mutex != 0)
    {
        xSemaphoreGive(sys_data_mutex);
    }
}

static void UI_Service_ClearLine(uint16_t y)
{
    /*
     * 每行刷新前先清掉整行，避免新字符串比旧字符串短时残留字符。
     * 相比整屏清屏，这样闪烁更小，也节省软件 SPI 的刷新时间。
     */
    Drv_Lcd_FillRect(0, y, LCD_DRV_WIDTH, UI_LINE_HEIGHT, UI_PANEL_BG);
}

static void UI_Service_DrawLine(uint16_t y, const char *text, uint16_t color)
{
    uint8_t i;
    uint8_t line_index;
    char fixed_line[UI_LINE_BUF_SIZE];

    /*
     * 原来的做法是每次先清整行再画字，软件 SPI 下会看到明显的黑条从上往下刷。
     * 现在改成固定宽度覆盖：先把本行补成 20 个字符，短字符串后面用空格补齐，
     * 再利用 Drv_Lcd_DrawChar() 会绘制背景色的特性，把旧字符覆盖掉。
     */
    for (i = 0; i < UI_LINE_CHARS; i++)
    {
        fixed_line[i] = ' ';
    }
    fixed_line[UI_LINE_CHARS] = '\0';

    if (text != 0)
    {
        for (i = 0; i < UI_LINE_CHARS && text[i] != '\0'; i++)
        {
            fixed_line[i] = text[i];
        }
    }

    /*
     * 数据行的 y 坐标是 18, 30, 42...，间隔 12 像素。
     * 通过 y 反推行号，缓存上一帧内容；内容没变就不写屏。
     */
    line_index = (y >= 18) ? (uint8_t)((y - 18) / 12) : 0;
    if (line_index < UI_LINE_COUNT &&
        strcmp(s_ui_last_lines[line_index], fixed_line) == 0 &&
        s_ui_last_colors[line_index] == color)
    {
        return;
    }

    Drv_Lcd_DrawString(2, y, fixed_line, color, UI_PANEL_BG);

    if (line_index < UI_LINE_COUNT)
    {
        strcpy(s_ui_last_lines[line_index], fixed_line);
        s_ui_last_colors[line_index] = color;
    }
}

void UI_Service_Init(void)
{
    Drv_Lcd_Init();
		char version_text[21];

		snprintf(version_text,
						 sizeof(version_text),
						 "SMART HOOD V%lu.%lu.%lu",
						 (unsigned long)(OTA_APP_VERSION_CODE / 10000U),
						 (unsigned long)((OTA_APP_VERSION_CODE / 100U) % 100U),
						 (unsigned long)(OTA_APP_VERSION_CODE % 100U));
    /*
     * 固定标题只画一次，后续周期刷新只更新数据行。
     * LCD 是 128x128 小屏，界面尽量使用短英文，避免中文字体占用过大。
     */
    Drv_Lcd_Clear(UI_PANEL_BG);
//    Drv_Lcd_DrawString(2, 2, "SMART HOOD", UI_TITLE, UI_PANEL_BG);
		Drv_Lcd_DrawString(2,
                   2,
                   version_text,
                   UI_TITLE,
                   UI_PANEL_BG);
//		Drv_Lcd_DrawString(2, 2, "HOOD V1.1", UI_TITLE, UI_PANEL_BG);
    Drv_Lcd_FillRect(0, 13, LCD_DRV_WIDTH, 1, LCD_COLOR_GRAY);
    memset(s_ui_last_lines, 0, sizeof(s_ui_last_lines));
    memset(s_ui_last_colors, 0xFF, sizeof(s_ui_last_colors));

    /*
     * 清一次数据区域即可，后续刷新不再整行清屏。
     * 这样初次进入界面干净，运行时又不会出现明显的逐行黑条。
     */
    UI_Service_ClearLine(18);
    UI_Service_ClearLine(30);
    UI_Service_ClearLine(42);
    UI_Service_ClearLine(54);
    UI_Service_ClearLine(66);
    UI_Service_ClearLine(78);
    UI_Service_ClearLine(90);
    UI_Service_ClearLine(102);
    UI_Service_ClearLine(114);

    s_ui_inited = 1;
}

void UI_Service_Process(void)
{
    UIStateSnapshot_t snapshot;
    char line[24];

    if (!s_ui_inited)
    {
        UI_Service_Init();
    }

    UI_Service_ReadSnapshot(&snapshot);

    snprintf(line, sizeof(line), "MODE:%s", UI_Service_ModeText(snapshot.mode));
    UI_Service_DrawLine(18, line, UI_Service_ModeColor(snapshot.mode));

    if (snapshot.mode == SYS_MODE_AUTO)
    {
        snprintf(line, sizeof(line), "FAN :AUTO");
    }
    else
    {
        snprintf(line, sizeof(line), "FAN :%s", UI_Service_FanText(snapshot.fan_level));
    }
    UI_Service_DrawLine(30, line, UI_TEXT);

    snprintf(line, sizeof(line), "GAS :%3d%%", snapshot.gas_percent);
    UI_Service_DrawLine(42, line, (snapshot.gas_percent >= 70) ? UI_WARN : UI_OK);

    snprintf(line, sizeof(line), "TEMP:%3dC", snapshot.temperature);
    UI_Service_DrawLine(54, line, UI_TEXT);

    snprintf(line, sizeof(line), "HUMI:%3d%%", snapshot.humidity);
    UI_Service_DrawLine(66, line, UI_TEXT);

    snprintf(line, sizeof(line), "RPM :%4d", snapshot.rpm);
    UI_Service_DrawLine(78, line, UI_TEXT);

    snprintf(line, sizeof(line), "TGT :%4d", snapshot.target_rpm);
    UI_Service_DrawLine(90, line, UI_TEXT);

    snprintf(line, sizeof(line), "PWM :%4d", snapshot.duty);
    UI_Service_DrawLine(102, line, UI_TEXT);

    /*
     * LAMP 是烟机照明灯，BF 是 BackFlow 防回流状态。
     * 小屏幕空间有限，所以最后一行把照明和防回流压缩在一起显示。
     */
    snprintf(line, sizeof(line), "LAMP:%s BF:%s",
             snapshot.light_on ? "ON" : "OFF",
             snapshot.backflow_active ? "ON" : "OFF");
    UI_Service_DrawLine(114, line, snapshot.backflow_active ? UI_WARN : (snapshot.light_on ? UI_OK : UI_TEXT));
}
