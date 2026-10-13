#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "modes.h"
#include "i2c.h"
#include "stm32l4xx_hal.h"

// Paramètres (en RAM pour l'instant, sauvegarde en flash plus tard)
Params params = DEFAULT_PARAMS;

// Valide et applique "NOM=valeur" pour un paramètre numérique
void set_param(uint32_t *p, const char *val, uint32_t min, uint32_t max)
{
    char *end;
    uint32_t v = strtoul(val, &end, 10);
    if (*val == 0 || *end != 0 || v < min || v > max)
        console("Valeur invalide (attendu : %lu a %lu)\r\n", (unsigned long)min, (unsigned long)max);
    else {
        *p = v;
        console("OK\r\n");
    }
}

/* ---------- Commandes de date et d'heure ---------- */
static void cmd_clock(const char *v)          // CLOCK=HH:MM:SS
{
    int h, m, s;
    DateTime t;
    if (sscanf(v, "%d:%d:%d", &h, &m, &s) != 3 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
        console("Format : CLOCK=HH:MM:SS\r\n");
        return;
    }
    if (rtc_load(&t) != 0) { console("RTC : erreur d'acces\r\n"); return; }
    t.h = h; t.m = m; t.s = s;
    if (rtc_write(&t) == 0) console("OK\r\n");
    else console("RTC : erreur d'ecriture\r\n");
}

static void cmd_date(const char *v)           // DATE=MOIS,JOUR,ANNEE
{
    int mo, d, y;
    DateTime t;
    if (sscanf(v, "%d,%d,%d", &mo, &d, &y) != 3 || mo < 1 || mo > 12 || d < 1 || d > 31 || y < 2000 || y > 2099) {
        console("Format : DATE=MM,JJ,AAAA (annee 2000 a 2099)\r\n");
        return;
    }
    if (rtc_load(&t) != 0) { console("RTC : erreur d'acces\r\n"); return; }
    t.month = mo; t.day = d; t.year = y;
    if (rtc_write(&t) == 0) console("OK\r\n");
    else console("RTC : erreur d'ecriture\r\n");
}

static void cmd_day(const char *v)            // DAY=MON..SUN
{
    DateTime t;
    int d = 0;
    for (int i = 1; i <= 7; i++)
        if (!strcmp(v, day_name[i])) d = i;
    if (!d) { console("Format : DAY=MON|TUE|WED|THU|FRI|SAT|SUN\r\n"); return; }
    if (rtc_load(&t) != 0) { console("RTC : erreur d'acces\r\n"); return; }
    t.dow = d;
    if (rtc_write(&t) == 0) console("OK\r\n");
    else console("RTC : erreur d'ecriture\r\n");
}

static void config_command(char *s)
{
    char *val = strchr(s, '=');
    if (val) *val++ = 0;                     // sépare le nom et la valeur

    if (!strcmp(s, "VERSION"))
        console("3W v0.1 - lot 0001\r\n");
    else if (!strcmp(s, "RESET")) {
        params = (Params)DEFAULT_PARAMS;
        console("Parametres reinitialises\r\n");
    }
    else if (val && !strcmp(s, "LOG_INTERVAL"))
        set_param(&params.log_interval, val, 1, 1440);
    else if (val && !strcmp(s, "FILE_MAX_SIZE"))
        set_param(&params.file_max_size, val, 512, 65536);
    else if (val && !strcmp(s, "TIMEOUT"))
        set_param(&params.timeout, val, 1, 600);
    else if (val && !strcmp(s, "CLOCK"))
        cmd_clock(val);
    else if (val && !strcmp(s, "DATE"))
        cmd_date(val);
    else if (val && !strcmp(s, "DAY"))
        cmd_day(val);
    else if (!strcmp(s, "TIME")) {            // pratique pour vérifier : affiche la date et l'heure
        char ts[TS_LEN];
        timestamp(ts, sizeof(ts));
        console("%s (%s)\r\n", ts, rtc_fail ? "?" : day_name[now_dt.dow > 7 ? 0 : now_dt.dow]);
    }
    else
        console("Commande inconnue : %s\r\n", s);
}

void console_poll(void)
{
    char *cmd = console_cmd();
    if (!cmd) return;
    if (mode == MODE_CONFIG) {
        mode_since = HAL_GetTick();          // activité : le délai de 30 min repart de zéro
        config_command(cmd);
    } else {
        console("Refuse : commandes acceptees uniquement en mode configuration\r\n");
    }
    console_cmd_done();
}
