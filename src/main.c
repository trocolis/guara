#include "app.h"
#include "depuracao.h"
#include "esp_log.h"
#include "ota.h"

void app_main(void)
{
    ESP_LOGI("guara", "Guará v%s", app_versao());
    app_iniciar();
    depuracao_iniciar();
    ota_confirmar_boot();
    app_executar();
}
