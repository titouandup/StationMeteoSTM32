#include "mesures.h"
#include "settings.h"
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

/* ---------- Fichier de mesures (FatFs, noms 8.3) ----------
 * Fichier actif : AAMMJJ_0.LOG (date du jour). Plein, il devient AAMMJJ_N.LOG (N = 1 à 9, 1 = le plus ancien)
 * et un nouveau AAMMJJ_0.LOG démarre. À minuit, un nouveau fichier du jour est créé.
 */
uint8_t sd_error;   // 1 = la carte SD ne répond plus ou l'écriture a échoué
uint8_t sd_full;    // 1 = plus de place sur la carte

static FATFS fs;
static FIL logf;
static uint8_t log_is_open;
static char active_name[16];

// Horodatage des fichiers pour FatFs (1980-01-01 si la RTC ne répond pas)
DWORD get_fattime(void)
{
    if (rtc_fail) return (1UL << 21) | (1UL << 16);
    return ((DWORD)(now_dt.year - 1980) << 25) | ((DWORD)now_dt.month << 21) | ((DWORD)now_dt.day << 16) |
           ((DWORD)now_dt.h << 11) | ((DWORD)now_dt.m << 5) | (now_dt.s / 2);
}

static void log_name(char *buf, size_t n, int rev)    // "AAMMJJ_N.LOG"
{
    snprintf(buf, n, "%02u%02u%02u_%d.LOG",
             rtc_fail ? 0 : now_dt.year % 100, rtc_fail ? 0 : now_dt.month, rtc_fail ? 0 : now_dt.day, rev);
}

static int log_open(void)                     // ouvre le fichier du jour en ajout
{
    log_name(active_name, sizeof(active_name), 0);
    if (f_open(&logf, active_name, FA_OPEN_ALWAYS | FA_WRITE) != FR_OK) return -1;
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

// Fichier plein : AAMMJJ_0.LOG prend la première révision libre (1 à 9) et un nouveau fichier démarre.
// Avec 9 archives déjà présentes, la plus ancienne (1) est supprimée et les autres sont décalées.
static int log_archive(void)
{
    char from[16], to[16];
    FILINFO fi;
    int n;

    f_close(&logf);
    log_is_open = 0;
    for (n = 1; n <= 9; n++) {
        log_name(to, sizeof(to), n);
        if (f_stat(to, &fi) == FR_NO_FILE) break;
    }
    if (n == 10) {
        log_name(to, sizeof(to), 1);
        if (f_unlink(to) != FR_OK) return -1;
        for (int r = 2; r <= 9; r++) {
            log_name(from, sizeof(from), r);
            log_name(to, sizeof(to), r - 1);
            if (f_rename(from, to) != FR_OK) return -1;
        }
        n = 9;
    }
    log_name(from, sizeof(from), 0);
    log_name(to, sizeof(to), n);
    if (f_rename(from, to) != FR_OK) return -1;
    console("Archive : %s\r\n", to);
    return log_open();
}

// Place restante pour len octets ? (sans écrire de ligne tronquée)
static int sd_has_room(UINT len)
{
    DWORD nclst;
    FATFS *fp;
    if (f_getfree("", &nclst, &fp) != FR_OK || nclst > 0) return 1;
    DWORD cluster = (DWORD)fs.csize * 512;
    DWORD used = f_size(&logf) % cluster;                // octets déjà pris dans le dernier cluster
    return used != 0 && cluster - used >= len;
}

// Ajoute une ligne au fichier ; en cas d'échec, la carte est remontée au prochain essai
static void log_line(const char *text)
{
    UINT len = strlen(text), written;
    char name[16];

    log_name(name, sizeof(name), 0);
    if (log_is_open && strcmp(name, active_name)) {       // changement de jour : nouveau fichier
        f_close(&logf);
        log_is_open = 0;
    }
    if (!log_is_open && log_start() != 0) { sd_error = 1; sd_full = 0; log_stop(); return; }
    if ((uint32_t)f_size(&logf) + len > (uint32_t)params.file_max_size && log_archive() != 0) {
        sd_error = 1; sd_full = 0; log_stop(); return;
    }
    if (!sd_has_room(len)) { sd_full = 1; sd_error = 0; return; }
    if (f_write(&logf, text, len, &written) != FR_OK || f_sync(&logf) != FR_OK) {
        sd_error = 1; sd_full = 0; log_stop(); return;
    }
    if (written != len) { sd_full = 1; sd_error = 0; return; }
    sd_error = 0;
    sd_full = 0;
}

void sd_boot(void)
{
    sd_error = (log_start() != 0);
    if (sd_error) console("SD : erreur (%s)\r\n", sd_last_error() ? sd_error_text(sd_last_error()) : "carte illisible ou non FAT32");
    else console("SD : %s, %lu Mo, fichier %s (%lu octets)\r\n", sd_type_name(),
                 (unsigned long)(sd_sector_count() / 2048), active_name, (unsigned long)f_size(&logf));
    error_set(ERR_SD_IO, sd_error);
}

static void fmt_val(char *b, size_t n, int ok, const char *fmt, double v)   // valeur absente = "NA"
{
    if (ok) snprintf(b, n, fmt, v);
    else    snprintf(b, n, "NA");
}

/* ---------- Affichage des capteurs (x ECO_FACTOR en économique) ---------- */
void sensors_report(void)
{
    static uint32_t last;
    uint32_t period = (mode == MODE_ECO) ? ECO_FACTOR * SENSOR_PRINT_MS : SENSOR_PRINT_MS;
    uint32_t now = HAL_GetTick();
    if (mode == MODE_CONFIG || now - last < period) return;   // pas d'acquisition en configuration
    last = now;

    char ts[TS_LEN], tb[12], pb[12], hb[12];
    timestamp(ts, sizeof(ts));

    float t = 0, p = 0, h = 0;
    int ok = bme_ok && bme_read(&t, &p, &h) == 0;
    fmt_val(tb, sizeof(tb), ok && params.temp_air, "%.1f", t);
    fmt_val(pb, sizeof(pb), ok && params.pressure, "%.1f", p);
    fmt_val(hb, sizeof(hb), ok && params.hygr && t >= params.hygr_mint && t <= params.hygr_maxt, "%.1f", h);
    console("[%s] BME280 : T = %s C  P = %s hPa  H = %s %%\r\n", ts, tb, pb, hb);

    if (params.lumin) {
        uint16_t lum = lumin_read();
        const char *lvl = lum < params.lumin_low ? "faible" : (lum > params.lumin_high ? "forte" : "moyenne");
        console("[%s] Luminosite : %u (%s)\r\n", ts, lum, lvl);
    } else {
        console("[%s] Luminosite : NA\r\n", ts);
    }
}

/* ---------- Surveillance de la carte SD ----------
 * Toutes les SD_CHECK_MS : détecte le retrait de la carte et son retour, sans attendre la prochaine mesure.
 */
void sd_watch(void)
{
    static uint32_t last;
    uint32_t now = HAL_GetTick();
    if ((mode != MODE_STANDARD && mode != MODE_ECO) || now - last < SD_CHECK_MS) return;
    last = now;

    if (log_is_open) {
        if (sd_present()) return;
        sd_error = 1; sd_full = 0; log_stop();             // carte retirée
    } else if (sd_error) {
        if (!sd_probe() || log_start() != 0 || !sd_present()) { log_stop(); return; }   // toujours absente (test rapide d'abord)
        sd_error = 0;                                      // carte revenue
        console("SD : carte retrouvee, fichier %s\r\n", active_name);
    } else {
        return;
    }
    error_set(ERR_SD_IO, sd_error);
    error_set(ERR_SD_FULL, sd_full);
}

/* ---------- Surveillance du BME280 ----------
 * Toutes les SD_CHECK_MS : détecte le débranchement et le retour du capteur sans attendre la prochaine mesure.
 */
void sensor_watch(void)
{
    static uint32_t last;
    static uint8_t miss, lost;
    uint32_t now = HAL_GetTick();
    if ((mode != MODE_STANDARD && mode != MODE_ECO) || now - last < SD_CHECK_MS) return;
    last = now;

    if (!(params.temp_air || params.hygr || params.pressure)) {   // capteur désactivé : rien à surveiller
        miss = lost = 0;
        error_set(ERR_SENSOR, 0);
        return;
    }

    float t, p, h;
    if (bme_ok && bme_read(&t, &p, &h) == 0) {
        if (lost) { lost = 0; bme_init(); }                       // revenu : la puce est à reconfigurer
        miss = 0;
        error_set(ERR_SENSOR, 0);
    } else {
        lost = 1;
        if (miss < 255) miss++;
        if (miss >= SENSOR_CHECK_MISSES) error_set(ERR_SENSOR, 1);
        if (!bme_ok && miss % 5 == 0) bme_ok = (bme_init() == 0);
    }
}

/* ---------- Cycle de mesure ----------
 * Toutes les LOG_INTERVAL minutes (x ECO_FACTOR en économique) : une ligne horodatée sur la SD.
 * Le BME280 est relu toutes les 200 ms pendant TIMEOUT secondes au plus ; sans réponse, ses valeurs sont "NA"
 * et, après SENSOR_MAX_MISSES mesures de suite en timeout, le capteur est signalé en erreur (idem pour le GPS).
 * Une valeur hors des seuils min/max est enregistrée "NA" et signalée comme incohérente.
 */
void measure_cycle(void)
{
    static uint32_t last, n, acq_start, acq_try;
    static uint8_t first = 1, acquiring, got_bme, bme_miss, gps_miss;
    static float t, p, h;
    uint32_t now = HAL_GetTick();
    int need_bme = params.temp_air || params.hygr || params.pressure;

    // pas d'acquisition en configuration ni d'écriture SD en maintenance ; première mesure dès le démarrage
    if (mode == MODE_CONFIG || mode == MODE_MAINT) { acquiring = 0; return; }

    if (!acquiring) {
        uint32_t period = (uint32_t)params.log_interval * 60000UL * (mode == MODE_ECO ? ECO_FACTOR : 1);
        if (!first && now - last < period) return;
        first = 0;
        last = acq_start = now;
        acq_try = now - 1000;
        acquiring = 1;
        got_bme = !need_bme;
        n++;
        if (need_bme && !bme_ok) bme_ok = (bme_init() == 0);
    }

    if (!got_bme) {
        if (bme_ok && now - acq_try >= 200) {
            acq_try = now;
            got_bme = (bme_read(&t, &p, &h) == 0);
        }
        if (!got_bme && now - acq_start < (uint32_t)params.timeout * 1000UL) return;   // on réessaie
        bme_miss = got_bme ? 0 : bme_miss + 1;
    } else if (!need_bme) {
        bme_miss = 0;
    }
    acquiring = 0;

    // GPS : en économique, une mesure sur deux ; "accès" = trames reçues depuis moins de TIMEOUT secondes
    int gps_now = (mode != MODE_ECO || n % 2 == 0);
    int gps_alive_now = gps_alive((uint32_t)params.timeout * 1000UL);
    if (gps_now) gps_miss = gps_alive_now ? 0 : gps_miss + 1;
    error_set(ERR_GPS, gps_miss >= SENSOR_MAX_MISSES);
    if (bme_miss >= SENSOR_MAX_MISSES) error_set(ERR_SENSOR, 1);   // l'effacement est fait par sensor_watch()

    // Seuils : hors plage = NA + erreur "incohérent" (l'hygrométrie hors plage de température est seulement ignorée)
    int ok_t = got_bme && need_bme && params.temp_air;
    int ok_p = got_bme && params.pressure;
    int ok_h = got_bme && params.hygr && t >= params.hygr_mint && t <= params.hygr_maxt;
    int incoherent = 0;
    if (ok_t && (t < params.min_temp_air || t > params.max_temp_air)) {
        console("ERREUR E%02d : temperature %.1f C hors seuils (%ld a %ld)\r\n", ERR_INCOHERENT, t,
                (long)params.min_temp_air, (long)params.max_temp_air);
        ok_t = 0; incoherent = 1;
    }
    if (ok_p && (p < params.pressure_min || p > params.pressure_max)) {
        console("ERREUR E%02d : pression %.1f hPa hors seuils (%ld a %ld)\r\n", ERR_INCOHERENT, p,
                (long)params.pressure_min, (long)params.pressure_max);
        ok_p = 0; incoherent = 1;
    }
    error_set(ERR_INCOHERENT, incoherent);

    char ts[TS_LEN], tb[12], pb[12], hb[12], lb[8], latb[16], lonb[16], altb[12], line[128];
    int gps_ok = gps_now && gps_alive_now && gps_valid;
    timestamp(ts, sizeof(ts));
    fmt_val(tb, sizeof(tb), ok_t, "%.1f", t);
    fmt_val(pb, sizeof(pb), ok_p, "%.1f", p);
    fmt_val(hb, sizeof(hb), ok_h, "%.1f", h);
    fmt_val(lb, sizeof(lb), params.lumin, "%.0f", params.lumin ? (double)lumin_read() : 0.0);
    fmt_val(latb, sizeof(latb), gps_ok, "%.5f", gps_lat);
    fmt_val(lonb, sizeof(lonb), gps_ok, "%.5f", gps_lon);
    fmt_val(altb, sizeof(altb), gps_ok && gps_alt_valid, "%.1f", gps_alt);

    snprintf(line, sizeof(line), "%s;%s;%s;%s;%s;%s;%s;%s\r\n", ts, tb, pb, hb, lb, latb, lonb, altb);
    log_line(line);
    error_set(ERR_SD_IO, sd_error);
    error_set(ERR_SD_FULL, sd_full);
    console("Mesure #%lu : %s\r\n", (unsigned long)n,
            sd_error ? "echec ecriture SD" : (sd_full ? "carte SD pleine" : "enregistree sur SD"));
}
