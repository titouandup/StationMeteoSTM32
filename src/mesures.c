#include "mesures.h"
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "ff.h"
#include "config.h"
#include "i2c.h"
#include "sd_spi.h"
#include "capteurs.h"
#include "modes.h"
#include "stm32l4xx_hal.h"

/* ---------- Fichier de mesures (FatFs, noms 8.3) ---------- */
#define LOG_FILE "DATA.LOG"

uint8_t sd_error;   // 1 = la carte SD ne répond plus ou l'écriture a échoué

static FATFS fs;
static FIL logf;
static uint8_t log_is_open;

// Horodatage des fichiers pour FatFs (1980-01-01 si la RTC ne répond pas)
DWORD get_fattime(void)
{
    if (rtc_fail) return (1UL << 21) | (1UL << 16);
    return ((DWORD)(now_dt.year - 1980) << 25) | ((DWORD)now_dt.month << 21) | ((DWORD)now_dt.day << 16) |
           ((DWORD)now_dt.h << 11) | ((DWORD)now_dt.m << 5) | (now_dt.s / 2);
}

static int log_open(void)                     // ouvre le fichier en ajout
{
    if (f_open(&logf, LOG_FILE, FA_OPEN_ALWAYS | FA_WRITE) != FR_OK) return -1;
    if (f_lseek(&logf, f_size(&logf)) != FR_OK) { f_close(&logf); return -1; }
    log_is_open = 1;
    return 0;
}

static int log_start(void)                    // monte la carte puis ouvre le fichier
{
    return f_mount(&fs, "", 1) == FR_OK ? log_open() : -1;
}

void log_stop(void)
{
    if (log_is_open) f_close(&logf);
    log_is_open = 0;
    f_mount(NULL, "", 0);
}

// Fichier plein : DATA.LOG devient AAMMJJ_N.LOG (N = 0 à 9, premier nom libre) et un nouveau fichier démarre
static int log_archive(void)
{
    char name[16];
    FILINFO fi;
    int n;

    f_close(&logf);
    log_is_open = 0;
    for (n = 0; n < 10; n++) {
        snprintf(name, sizeof(name), "%02u%02u%02u_%d.LOG",
                 rtc_fail ? 0 : now_dt.year % 100, rtc_fail ? 0 : now_dt.month, rtc_fail ? 0 : now_dt.day, n);
        if (f_stat(name, &fi) == FR_NO_FILE) break;
    }
    if (n == 10 || f_rename(LOG_FILE, name) != FR_OK) return -1;
    console("Archive : %s\r\n", name);
    return log_open();
}

// Ajoute une ligne au fichier ; en cas d'échec, sd_error allume la LED et la carte est remontée au prochain essai
static void log_line(const char *text)
{
    UINT len = strlen(text), written;

    if (!log_is_open && log_start() != 0) { sd_error = 1; log_stop(); return; }
    if (f_size(&logf) + len > params.file_max_size && log_archive() != 0) { sd_error = 1; log_stop(); return; }
    if (f_write(&logf, text, len, &written) != FR_OK || written != len || f_sync(&logf) != FR_OK) {
        sd_error = 1; log_stop(); return;
    }
    sd_error = 0;
}

void sd_boot(void)
{
    sd_error = (log_start() != 0);
    if (sd_error) console("SD : erreur (%s)\r\n", sd_last_error() ? sd_error_text(sd_last_error()) : "carte illisible ou non FAT32");
    else console("SD : %s, %lu Mo, fichier %s (%lu octets)\r\n", sd_type_name(),
                 (unsigned long)(sd_sector_count() / 2048), LOG_FILE, (unsigned long)f_size(&logf));
}

#define SENSOR_PRINT_MS 2000UL   // période d'affichage des capteurs (x2 en économique)

/* ---------- Affichage des capteurs (x2 en économique) ---------- */
void sensors_report(void)
{
    static uint32_t last;
    uint32_t period = (mode == MODE_ECO) ? 2 * SENSOR_PRINT_MS : SENSOR_PRINT_MS;
    uint32_t now = HAL_GetTick();
    if (mode == MODE_CONFIG || now - last < period) return;   // pas d'acquisition en configuration
    last = now;

    char ts[TS_LEN];
    timestamp(ts, sizeof(ts));

    float t, p, h;
    if (bme_ok && bme_read(&t, &p, &h) == 0)
        console("[%s] BME280 : T = %.1f C  P = %.1f hPa  H = %.1f %%\r\n", ts, t, p, h);
    else
        console("[%s] BME280 : NA\r\n", ts);

    uint16_t lum = lumin_read();
    const char *lvl = lum < LUMIN_LOW ? "faible" : (lum > LUMIN_HIGH ? "forte" : "moyenne");
    console("[%s] Luminosite : %u (%s)\r\n", ts, lum, lvl);
}

/* ---------- Cycle de mesure ---------- */
static void fmt_val(char *b, size_t n, int ok, float v)   // valeur absente = "NA"
{
    if (ok) snprintf(b, n, "%.1f", v);
    else    snprintf(b, n, "NA");
}

void measure_cycle(void)
{
    static uint32_t last, n;
    static uint8_t first = 1;
    uint32_t period = params.log_interval * 60000UL * (mode == MODE_ECO ? 2 : 1);
    uint32_t now = HAL_GetTick();

    // pas d'acquisition en configuration ni d'écriture SD en maintenance ; première mesure dès le démarrage
    if (mode == MODE_CONFIG || mode == MODE_MAINT || (!first && now - last < period)) return;
    first = 0;
    last = now;
    n++;

    char ts[TS_LEN], tb[12], pb[12], hb[12], gb[32] = "NA", line[128];
    float t = 0, p = 0, h = 0;
    int ok = bme_ok && bme_read(&t, &p, &h) == 0;
    timestamp(ts, sizeof(ts));
    fmt_val(tb, sizeof(tb), ok, t);
    fmt_val(pb, sizeof(pb), ok, p);
    fmt_val(hb, sizeof(hb), ok, h);
    if (gps_valid && (mode != MODE_ECO || n % 2 == 0))      // économique : GPS une mesure sur deux
        snprintf(gb, sizeof(gb), "%.5f,%.5f", gps_lat, gps_lon);

    snprintf(line, sizeof(line), "%s;%s;%s;%s;%u;%s\r\n", ts, tb, pb, hb, lumin_read(), gb);
    log_line(line);
    console("Mesure #%lu : %s\r\n", (unsigned long)n, sd_error ? "echec ecriture SD" : "enregistree sur SD");
}
