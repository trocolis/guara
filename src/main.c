/*
 * Ponto de entrada do firmware: inicia o sistema e entra no laço principal.
 */
#include "app.h"
#include "debug_console.h"
#include "esp_log.h"
#include "ota.h"

/* Inicia a aplicação, o console de depuração, confirma o firmware (OTA) e roda o laço. */
void app_main(void)
{
    ESP_LOGI("guara", "Guará v%s", app_version());
    app_init();
    debug_console_init();
    ota_confirm_boot();
    app_run();
}
