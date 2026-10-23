# Fiche soutenance — où trouver quoi

Station météo STM32 (NUCLEO-L476RG, horloge 4 MHz). Tout le code est dans `src/` (sauf `ff.c` = FatFs, pas de nous)
et tous les réglages dans `include/settings.h`.

## 1. En une phrase par fichier

| Fichier | Il fait quoi |
|---|---|
| `src/main.c` | Démarrage puis boucle de 10 ms qui appelle tout le reste |
| `src/setup.c` | Matériel : horloge, I2C, SPI, 2 liaisons série |
| `src/config.c` | Réglages (sauvegarde flash) + commandes du mode configuration |
| `src/console.c` | Texte sur la console série, réception d'une ligne de commande |
| `src/modes.c` | 4 modes, LED RGB, boutons, codes d'erreur |
| `src/capteurs.c` | Luminosité (ADC) + GPS (trames NMEA) |
| `src/i2c.c` | RTC (heure) + BME280 (T, P, H) |
| `src/mesures.c` | Cycle de mesure, écriture sur SD, rotation des fichiers |
| `src/sd_spi.c` | Carte SD en SPI (sert de « disque » à FatFs) |
| `include/settings.h` | **Tous** les chiffres réglables (défauts, min, max) |
| `include/board_pins.h` | Broches de la carte |

## 2. Questions probables → réponse → où

| Question | Réponse courte | Où |
|---|---|---|
| Comment fonctionne le programme ? | Initialisation, puis boucle sans fin de 10 ms, chaque fonction fait un petit travail | `main.c`, boucle `while (1)` |
| Pourquoi pas de RTOS ? | Code simple, prévisible, facile à déboguer | `main.c` |
| Comment est géré le GPS / la console sans perdre d'octet ? | Interruptions courtes qui rangent l'octet ; la boucle décode ensuite | `console.c` (callbacks UART), `capteurs.c` (`gps_rx_byte`, `gps_poll`) |
| Toutes les combien de temps on mesure ? | 10 min par défaut, réglable de 1 à 1440 ; ×2 en économique | `settings.h` (`LOG_INTERVAL_*`, `ECO_FACTOR`), `mesures.c` (`measure_cycle`) |
| Que se passe-t-il pendant une mesure ? | Lecture BME280 (relancée toutes les 200 ms), écoute du GPS jusqu'à `TIMEOUT`, valeur absente = `NA`, puis ligne CSV sur SD | `mesures.c` (`measure_cycle`) |
| Comment on économise l'énergie ? | BME280 en veille, ADC à la demande, réception GPS coupée entre deux mesures, GPS une mesure sur deux en économique | `mesures.c`, `capteurs.c` (`gps_start`, `gps_stop`), `i2c.c` (`bme_read`) |
| Comment on gère les erreurs capteur ? | Seuils min/max : valeur hors plage = `NA` + E06 ; 2 mesures de suite sans réponse = E04 ou E05 | `mesures.c`, `settings.h` (`SENSOR_MAX_MISSES`) |
| Comment marche la LED d'erreur ? | Deux couleurs qui alternent sur 1 s ; l'erreur la plus prioritaire s'affiche | `modes.c` (`err_info`, `error_led`) |
| Comment on change de mode ? | Appui 5 s : vert = standard ↔ économique, rouge = maintenance (et retour) ; rouge au reset = configuration | `modes.c` (`update_mode`, `button_held`) |
| Pourquoi la maintenance ? | Elle ferme le fichier SD : on peut retirer la carte sans corruption | `modes.c` (`set_mode`), `mesures.c` (`log_stop`) |
| Où sont gardés les réglages ? | En flash (dernière page de la banque 2), avec signature + somme de contrôle ; corrompu = valeurs par défaut | `config.c` (`params_load`, `params_save`) |
| Comment on règle la station ? | Mode configuration, commandes `NOM=valeur` sur la console | `config.c` (`param_table`, `config_command`) |
| Comment s'appellent les fichiers ? | `AAMMJJ00.LOG` (actif), `AAMMJJ01` à `99` (archives), un jeu par jour | `mesures.c` (`log_archive`) |
| Que se passe-t-il quand c'est plein ? | Rien n'est supprimé : après 99 archives, erreur E03 et on n'écrit plus (jusqu'au lendemain ou jusqu'à libération de place) | `mesures.c` (`log_archive`, `log_line`, `sd_has_room`) |
| Pourquoi des noms de 8 caractères ? | FatFs est configuré sans noms longs (`_USE_LFN 0`) | `include/ffconf.h` |
| Que perd-on si le courant coupe ? | Au plus la dernière ligne (`f_sync` après chaque écriture) | `mesures.c` (`log_line`) |
| Et si on retire la carte ? | Détectée en 2 s (E02), retrouvée toute seule au retour | `mesures.c` (`sd_watch`), `sd_spi.c` (`sd_present`, `sd_probe`) |
| Comment on parle à la carte SD ? | Commandes SPI (CMD0, CMD8, ACMD41, CMD17, CMD24…) ; `disk_*` fait le lien avec FatFs | `sd_spi.c` |
| D'où vient l'heure ? | Module RTC DS1307 (I2C, 0x68), lu 1 fois par seconde | `i2c.c` (`rtc_poll`) |
| D'où viennent T, P, H ? | BME280 en I2C, formules de compensation du constructeur | `i2c.c` (`bme_init`, `bme_read`, `comp_*`) |
| Comment la luminosité est-elle mesurée ? | ADC1 voie 5 (PA0), 10 bits : 0 à 1023 ; faible < 255, forte > 768 | `capteurs.c` (`lumin_read`), `settings.h` |
| Comment on lit le GPS ? | Trames NMEA : RMC (position) et GGA (altitude, satellites) | `capteurs.c` (`gps_rx_byte`, `gps_poll`, `to_deg`) |

## 3. Valeurs à connaître par cœur

| Réglage | Défaut | Min – Max | Nom console |
|---|---|---|---|
| Intervalle de mesure | 10 min | 1 – 1440 | `LOG_INTERVAL` |
| Taille d'un fichier | 2048 octets | 512 – 65536 | `FILE_MAX_SIZE` |
| Attente d'un capteur | 30 s | 1 – 600 | `TIMEOUT` |
| Luminosité faible / forte | 255 / 768 | 0 – 1023 | `LUMIN_LOW`, `LUMIN_HIGH` |
| Température air (erreur hors plage) | -10 / 60 °C | -40 – 85 | `MIN_TEMP_AIR`, `MAX_TEMP_AIR` |
| Hygrométrie ignorée hors | 0 / 50 °C | -40 – 85 | `HYGR_MINT`, `HYGR_MAXT` |
| Pression (erreur hors plage) | 850 / 1080 hPa | 300 – 1100 | `PRESSURE_MIN`, `PRESSURE_MAX` |
| Capteur actif (1) ou non (0) | 1 | 0 – 1 | `LUMIN`, `TEMP_AIR`, `HYGR`, `PRESSURE` |

Autres constantes (dans `settings.h`) : appui long **5 s**, mode économique **×2**, sortie automatique de la configuration après **30 min**, vérification de la carte toutes les **2 s**, console à **115200** bauds.

Autres commandes : `VERSION`, `RESET` (tout par défaut), `TIME`, `CLOCK=HH:MM:SS`, `DATE=MM,JJ,AAAA`, `DAY=MON..SUN`.
Elles ne sont acceptées **qu'en mode configuration**.

## 4. Codes d'erreur de la LED (période 1 s)

| Code | Cause | LED | Où |
|---|---|---|---|
| E01 | Horloge RTC inaccessible | rouge / bleu, durées égales | `modes.c` (`err_info`) |
| E02 | Accès ou écriture SD en échec | rouge 1/3, blanc 2/3 | idem |
| E03 | Carte SD pleine | rouge / blanc, durées égales | idem |
| E04 | GPS sans données | rouge / jaune, durées égales | idem |
| E05 | Capteur sans réponse | rouge / vert, durées égales | idem |
| E06 | Données hors seuils | rouge 1/3, vert 2/3 | idem |

Priorité d'affichage : E01 d'abord, E06 en dernier.
Couleurs des modes : configuration = jaune, standard = vert, économique = bleu, maintenance = orange (`modes.c`, `mode_color`).

## 5. Branchement

| Élément | Broches |
|---|---|
| Console (USB) | USART2 : PA2 / PA3 |
| GPS | UART4 : PA0 (TX) / PA1 (RX), 9600 bauds |
| I2C (RTC 0x68, BME280 0x76) | PB8 (SCL) / PB9 (SDA) |
| Carte SD (SPI1) | PA5 (SCK), PA6 (MISO), PA7 (MOSI), PB5 (CS) |
| LED RGB | PB10 (horloge), PA8 (données) |
| Bouton vert / rouge | PA10 (D2) / PB3 (D3) |
| Luminosité | PA0 (ADC1, voie 5) |

Piège connu : PA0 sert à la fois à la luminosité et au TX du GPS. C'est pourquoi on ne peut pas mettre le GPS en veille par commande : on coupe seulement sa réception (`capteurs.c`, `gps_stop`).

## 6. Limites connues (à dire avant qu'on nous les demande)

- Pas de watchdog : si le programme se bloque, la station reste figée (amélioration : IWDG).
- L'écriture SD bloque la boucle le temps de l'opération (amélioration : tampon et écriture en arrière-plan).
- L'heure du GPS n'est pas utilisée pour recaler la RTC.
- Modules optionnels (eau, vent, courant, particules) pas encore intégrés ; la structure permet d'ajouter une colonne par capteur.
- Capacité : environ 31 lignes de 65 octets par fichier de 2048 octets, 100 fichiers par jour au maximum, donc largement assez (47 fichiers/jour au pire, à 1 mesure par minute).
