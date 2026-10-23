#ifndef SETTINGS_H
#define SETTINGS_H

/*
 * Choix de paramétrage de la station (compilation).
 * Tout ce qui se règle se trouve ici ; les valeurs modifiables à chaud
 * par la console (mode configuration) ne sont que les valeurs par défaut.
 */

/* ---------- Paramètres modifiables par la console : défaut, min, max ---------- */
#define LOG_INTERVAL_DEFAULT   10      // minutes entre deux mesures enregistrées
#define LOG_INTERVAL_MIN       1
#define LOG_INTERVAL_MAX       1440

#define FILE_MAX_SIZE_DEFAULT  2048    // octets avant archivage du fichier
#define FILE_MAX_SIZE_MIN      512
#define FILE_MAX_SIZE_MAX      65536

#define TIMEOUT_DEFAULT        5       // secondes avant d'abandonner l'acquisition d'un capteur
#define TIMEOUT_MIN            1
#define TIMEOUT_MAX            600

#define LUMIN_DEFAULT          1       // 1 = capteur activé
#define LUMIN_LOW_DEFAULT      255     // en dessous : luminosité "faible" (0 à 1023)
#define LUMIN_HIGH_DEFAULT     768     // au dessus : luminosité "forte"
#define LUMIN_VALUE_MIN        0
#define LUMIN_VALUE_MAX        1023

#define TEMP_AIR_DEFAULT       1
#define MIN_TEMP_AIR_DEFAULT   -10     // °C, en dessous : capteur en erreur
#define MAX_TEMP_AIR_DEFAULT   60      // °C, au dessus : capteur en erreur
#define TEMP_VALUE_MIN         -40
#define TEMP_VALUE_MAX         85

#define HYGR_DEFAULT           1
#define HYGR_MINT_DEFAULT      0       // °C, en dessous : hygrométrie ignorée
#define HYGR_MAXT_DEFAULT      50      // °C, au dessus : hygrométrie ignorée

#define PRESSURE_DEFAULT       1
#define PRESSURE_MIN_DEFAULT   850     // hPa, en dessous : capteur en erreur
#define PRESSURE_MAX_DEFAULT   1080    // hPa, au dessus : capteur en erreur
#define PRESSURE_VALUE_MIN     300
#define PRESSURE_VALUE_MAX     1100

/* ---------- Modes ---------- */
#define ECO_FACTOR             2       // en mode économique, toutes les périodes sont multipliées par ce facteur
#define HOLD_MS                5000UL  // durée d'appui long sur un bouton
#define CONFIG_TIMEOUT_MIN     30      // minutes d'inactivité avant de quitter le mode configuration
#define ERR_PERIOD_MS          1000    // période du clignotement de la LED en erreur (1 Hz)
#define SD_CHECK_MS            2000UL  // période de vérification de la carte SD (retrait, retour)
#define SENSOR_MAX_MISSES      2       // mesures consécutives en timeout avant de signaler le capteur en erreur

/* ---------- Liaison série ---------- */
#define CONSOLE_BAUD           115200

#endif
