/*
 * Atualização pelo cartão SD ou pelo GitHub: firmware (partição OTA livre) e
 * storage. Pelo GitHub, a última release é descoberta pelo redirecionamento de
 * /releases/latest e os arquivos vêm de /releases/download/<tag>/.
 */
#include "ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "ota";
#define BLOCK 4096

/* Confirma o firmware novo após um boot bem-sucedido (cancela o rollback). */
void ota_confirm_boot(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *current = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(current, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Firmware novo confirmado");
    }
}

/* Tamanho do arquivo aberto. */
static long file_size(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    return n;
}

/* Grava o firmware do SD na partição OTA livre e a marca para o próximo boot. */
bool ota_firmware_from_sd(const char *path, ota_progress_fn progress, char *error, int error_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(error, (size_t)error_size, "Arquivo não encontrado");
        return false;
    }
    long total = file_size(f);
    const esp_partition_t *destination = esp_ota_get_next_update_partition(NULL);
    if (!destination || total <= 0 || (size_t)total > destination->size) {
        fclose(f);
        snprintf(error, (size_t)error_size, "Firmware inválido");
        return false;
    }
    esp_ota_handle_t h;
    if (esp_ota_begin(destination, (size_t)total, &h) != ESP_OK) {
        fclose(f);
        snprintf(error, (size_t)error_size, "Falha ao preparar");
        return false;
    }
    uint8_t *buf = malloc(BLOCK);
    long written = 0;
    bool ok = buf != NULL;
    while (ok && written < total) {
        size_t n = fread(buf, 1, BLOCK, f);
        if (n == 0 || esp_ota_write(h, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        written += (long)n;
        if (progress) {
            progress((int)(written * 100 / total));
        }
    }
    free(buf);
    fclose(f);
    if (!ok || esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(destination) != ESP_OK) {
        snprintf(error, (size_t)error_size, "Imagem inválida");
        return false;
    }
    ESP_LOGI(TAG, "Firmware gravado em %s", destination->label);
    return true;
}

/* Regrava a partição storage a partir de um storage.bin válido. */
bool ota_storage_from_sd(const char *path, ota_progress_fn progress, char *error, int error_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(error, (size_t)error_size, "Arquivo não encontrado");
        return false;
    }
    long total = file_size(f);
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x99, "storage");
    uint8_t *buf = malloc(BLOCK);
    if (!p || !buf || total < 64 || (size_t)total > p->size) {
        fclose(f);
        free(buf);
        snprintf(error, (size_t)error_size, "storage.bin inválido");
        return false;
    }
    if (fread(buf, 1, 4, f) != 4 || memcmp(buf, "GSTR", 4) != 0) {
        fclose(f);
        free(buf);
        snprintf(error, (size_t)error_size, "storage.bin inválido");
        return false;
    }
    fseek(f, 0, SEEK_SET);
    size_t erase = ((size_t)total + BLOCK - 1) / BLOCK * BLOCK;
    bool ok = esp_partition_erase_range(p, 0, erase) == ESP_OK;
    long written = 0;
    while (ok && written < total) {
        size_t n = fread(buf, 1, BLOCK, f);
        if (n == 0 || esp_partition_write(p, (size_t)written, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        written += (long)n;
        if (progress) {
            progress((int)(written * 100 / total));
        }
    }
    free(buf);
    fclose(f);
    if (!ok) {
        snprintf(error, (size_t)error_size, "Falha ao gravar");
    }
    return ok;
}

/* ---------------- GitHub ---------------- */

#define GITHUB_URL "https://github.com/" OTA_GITHUB_REPO "/releases"
#define HTTP_TIMEOUT_MS 15000
#define MAX_REDIRECTS 5

/* Partição storage (dados, subtipo 0x99). */
static const esp_partition_t *storage_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x99, "storage");
}

/* Configuração comum das requisições HTTPS (certificados do bundle do IDF). */
static esp_http_client_config_t http_config(const char *url)
{
    return (esp_http_client_config_t){
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = BLOCK,
        .buffer_size_tx = 2048, /* as URLs assinadas dos arquivos são longas */
        .user_agent = "Guara/" GUARA_VERSION,
        .disable_auto_redirect = true,
    };
}

/* Guarda o cabeçalho Location da resposta. */
static esp_err_t location_header(esp_http_client_event_t *ev)
{
    if (ev->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(ev->header_key, "Location") == 0) {
        snprintf((char *)ev->user_data, 256, "%s", ev->header_value);
    }
    return ESP_OK;
}

/* Busca a tag da última release: /releases/latest redireciona para /releases/tag/<tag>. */
bool ota_github_latest(char *tag, int tag_size, char *error, int error_size)
{
    char location[256] = "";
    esp_http_client_config_t cfg = http_config(GITHUB_URL "/latest");
    cfg.event_handler = location_header;
    cfg.user_data = location;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        snprintf(error, (size_t)error_size, "Sem memória");
        return false;
    }
    esp_http_client_set_method(client, HTTP_METHOD_HEAD);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        snprintf(error, (size_t)error_size, "GitHub não respondeu");
        return false;
    }
    const char *found = strstr(location, "/tag/");
    if (status < 300 || status >= 400 || !found) {
        snprintf(error, (size_t)error_size, "Nenhuma versão publicada");
        return false;
    }
    snprintf(tag, (size_t)tag_size, "%s", found + 5);
    ESP_LOGI(TAG, "Última release: %s", tag);
    return true;
}

/* Lê "v#.#.#" (o v é opcional) num número comparável. */
static bool parse_version(const char *text, long *value)
{
    int a, b, c;
    if (*text == 'v' || *text == 'V') {
        text++;
    }
    if (sscanf(text, "%d.%d.%d", &a, &b, &c) != 3) {
        return false;
    }
    *value = (long)a * 1000000L + (long)b * 1000L + c;
    return true;
}

/* Verdadeiro se a tag for mais nova que o firmware atual. */
bool ota_is_newer(const char *tag)
{
    long remote, local;
    return parse_version(tag, &remote) && parse_version(GUARA_VERSION, &local) && remote > local;
}

/* Recebe um bloco baixado; total é o tamanho do arquivo (Content-Length). */
typedef bool (*chunk_fn)(const uint8_t *data, int n, int total, void *ctx);

/*
 * Baixa a URL seguindo os redirecionamentos (o GitHub manda os arquivos para
 * outro servidor) e entrega o corpo em blocos. *status recebe o código HTTP
 * final; um 404 não chama chunk nenhum.
 */
static bool download(const char *url, chunk_fn fn, void *ctx, ota_progress_fn progress, int *status, char *error,
                     int error_size)
{
    esp_http_client_config_t cfg = http_config(url);
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    uint8_t *buf = malloc(BLOCK);
    if (!client || !buf) {
        if (client) {
            esp_http_client_cleanup(client);
        }
        free(buf);
        snprintf(error, (size_t)error_size, "Sem memória");
        return false;
    }
    bool ok = false;
    int64_t total = 0;
    *status = 0;
    for (int redirects = 0;; redirects++) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            snprintf(error, (size_t)error_size, "Falha na conexão");
            goto end;
        }
        total = esp_http_client_fetch_headers(client);
        *status = esp_http_client_get_status_code(client);
        if (*status >= 300 && *status < 400 && redirects < MAX_REDIRECTS) {
            int dropped;
            esp_http_client_flush_response(client, &dropped);
            esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            continue;
        }
        break;
    }
    if (*status != 200) {
        snprintf(error, (size_t)error_size, "Erro HTTP %d", *status);
        goto end;
    }
    if (total <= 0) {
        snprintf(error, (size_t)error_size, "Tamanho desconhecido");
        goto end;
    }
    int64_t received = 0;
    while (received < total) {
        int n = esp_http_client_read(client, (char *)buf, BLOCK);
        if (n <= 0) {
            snprintf(error, (size_t)error_size, "Download interrompido");
            goto end;
        }
        if (!fn(buf, n, (int)total, ctx)) {
            snprintf(error, (size_t)error_size, "Arquivo inválido");
            goto end;
        }
        received += n;
        if (progress) {
            progress((int)(received * 100 / total));
        }
    }
    ok = true;
end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buf);
    return ok;
}

/* Estado da gravação do firmware baixado. */
typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *destination;
    bool started;
    uint8_t head[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)];
    int head_length;
} firmware_job_t;

/* Grava um bloco do firmware; antes do primeiro, confere se a imagem é do Guará. */
static bool firmware_chunk(const uint8_t *data, int n, int total, void *ctx)
{
    firmware_job_t *job = ctx;
    if (!job->started) {
        int copy = (int)sizeof(job->head) - job->head_length;
        if (copy > n) {
            copy = n;
        }
        memcpy(job->head + job->head_length, data, (size_t)copy);
        job->head_length += copy;
        if (job->head_length < (int)sizeof(job->head)) {
            return true; /* ainda juntando o cabeçalho */
        }
        const esp_app_desc_t *desc =
            (const esp_app_desc_t *)(job->head + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
        if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD || strcmp(desc->project_name, "guara") != 0 ||
            (size_t)total > job->destination->size) {
            return false;
        }
        if (esp_ota_begin(job->destination, (size_t)total, &job->handle) != ESP_OK) {
            return false;
        }
        job->started = true;
        /* grava o cabeçalho juntado e o resto deste bloco */
        if (esp_ota_write(job->handle, job->head, sizeof(job->head)) != ESP_OK) {
            return false;
        }
        return copy == n || esp_ota_write(job->handle, data + copy, (size_t)(n - copy)) == ESP_OK;
    }
    return esp_ota_write(job->handle, data, (size_t)n) == ESP_OK;
}

/* Estado da gravação do storage baixado. */
typedef struct {
    const esp_partition_t *partition;
    int written;
} storage_job_t;

/* Grava um bloco do storage; o primeiro confere o "GSTR" e só então apaga a partição. */
static bool storage_chunk(const uint8_t *data, int n, int total, void *ctx)
{
    storage_job_t *job = ctx;
    if (job->written == 0) {
        if (n < 4 || memcmp(data, "GSTR", 4) != 0 || (size_t)total > job->partition->size) {
            return false;
        }
        size_t erase = ((size_t)total + BLOCK - 1) / BLOCK * BLOCK;
        if (esp_partition_erase_range(job->partition, 0, erase) != ESP_OK) {
            return false;
        }
    }
    if (esp_partition_write(job->partition, (size_t)job->written, data, (size_t)n) != ESP_OK) {
        return false;
    }
    job->written += n;
    return true;
}

/*
 * Instala a release: o firmware vai para a partição OTA livre (validado, mas
 * ainda sem virar a de boot); depois o storage, se a release tiver um; só no
 * fim a partição nova é marcada para o próximo boot.
 */
bool ota_github_install(const char *tag, ota_progress_fn progress, char *error, int error_size)
{
    char url[160];
    int status;
    firmware_job_t fw = {.destination = esp_ota_get_next_update_partition(NULL)};
    if (!fw.destination) {
        snprintf(error, (size_t)error_size, "Sem partição livre");
        return false;
    }
    snprintf(url, sizeof(url), GITHUB_URL "/download/%s/firmware.bin", tag);
    if (!download(url, firmware_chunk, &fw, progress, &status, error, error_size)) {
        if (fw.started) {
            esp_ota_abort(fw.handle);
        }
        return false;
    }
    if (!fw.started || esp_ota_end(fw.handle) != ESP_OK) {
        snprintf(error, (size_t)error_size, "Firmware inválido");
        return false;
    }
    ESP_LOGI(TAG, "Firmware %s gravado em %s", tag, fw.destination->label);

    storage_job_t st = {.partition = storage_partition()};
    snprintf(url, sizeof(url), GITHUB_URL "/download/%s/storage.bin", tag);
    if (st.partition && !download(url, storage_chunk, &st, progress, &status, error, error_size)) {
        if (status != 404 || st.written > 0) {
            return false; /* 404: a release não traz storage; segue só com o firmware */
        }
    }
    if (esp_ota_set_boot_partition(fw.destination) != ESP_OK) {
        snprintf(error, (size_t)error_size, "Falha ao ativar");
        return false;
    }
    return true;
}
