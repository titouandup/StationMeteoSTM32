```
                                            ,--,                                                                        ,----,                                                                              ,----,                                            
                    ,----..              ,---.'|                                                                      ,/   .`|       ,--,                                                                 ,/   .`|                  ,--,                      
           .---.   /   /   \  ,-.----.   |   | :       ,---,                       .---.    ,---,.   ,---,          ,`   .'  :     ,--.'|    ,---,.,-.----.                      .---.   ,---,          ,`   .'  : ,----..        ,--.'|    ,---,.,-.----.    
          /. ./|  /   .     : \    /  \  :   : |     .'  .' `\                    /. ./|  ,'  .' |  '  .' \       ;    ;     /  ,--,  | :  ,'  .' |\    /  \                    /. ./|  '  .' \       ;    ;     //   /   \    ,--,  | :  ,'  .' |\    /  \   
      .--'.  ' ; .   /   ;.  \;   :    \ |   ' :   ,---.'     \               .--'.  ' ;,---.'   | /  ;    '.   .'___,/    ,',---.'|  : ',---.'   |;   :    \               .--'.  ' ; /  ;    '.   .'___,/    ,'|   :     :,---.'|  : ',---.'   |;   :    \  
     /__./ \ : |.   ;   /  ` ;|   | .\ : ;   ; '   |   |  .`\  |             /__./ \ : ||   |   .':  :       \  |    :     | |   | : _' ||   |   .'|   | .\ :              /__./ \ : |:  :       \  |    :     | .   |  ;. /|   | : _' ||   |   .'|   | .\ :  
 .--'.  '   \' .;   |  ; \ ; |.   : |: | '   | |__ :   : |  '  |         .--'.  '   \' .:   :  |-,:  |   /\   \ ;    |.';  ; :   : |.'  |:   :  |-,.   : |: |          .--'.  '   \' .:  |   /\   \ ;    |.';  ; .   ; /--` :   : |.'  |:   :  |-,.   : |: |  
/___/ \ |    ' '|   :  | ; | '|   |  \ : |   | :.'||   ' '  ;  :        /___/ \ |    ' ':   |  ;/||  :  ' ;.   :`----'  |  | |   ' '  ; ::   |  ;/||   |  \ :         /___/ \ |    ' '|  :  ' ;.   :`----'  |  | ;   | ;    |   ' '  ; ::   |  ;/||   |  \ :  
;   \  \;      :.   |  ' ' ' :|   : .  / '   :    ;'   | ;  .  |        ;   \  \;      :|   :   .'|  |  ;/  \   \   '   :  ; '   |  .'. ||   :   .'|   : .  /         ;   \  \;      :|  |  ;/  \   \   '   :  ; |   : |    '   |  .'. ||   :   .'|   : .  /  
 \   ;  `      |'   ;  \; /  |;   | |  \ |   |  ./ |   | :  |  '         \   ;  `      ||   |  |-,'  :  | \  \ ,'   |   |  ' |   | :  | '|   |  |-,;   | |  \          \   ;  `      |'  :  | \  \ ,'   |   |  ' .   | '___ |   | :  | '|   |  |-,;   | |  \  
  .   \    .\  ; \   \  ',  / |   | ;\  \;   : ;   '   : | /  ;           .   \    .\  ;'   :  ;/||  |  '  '--'     '   :  | '   : |  : ;'   :  ;/||   | ;\  \          .   \    .\  ;|  |  '  '--'     '   :  | '   ; : .'|'   : |  : ;'   :  ;/||   | ;\  \ 
   \   \   ' \ |  ;   :    /  :   ' | \.'|   ,/    |   | '` ,/             \   \   ' \ ||   |    \|  :  :           ;   |.'  |   | '  ,/ |   |    \:   ' | \.'           \   \   ' \ ||  :  :           ;   |.'  '   | '/  :|   | '  ,/ |   |    \:   ' | \.' 
    :   '  |--"    \   \ .'   :   : :-'  '---'     ;   :  .'                :   '  |--" |   :   .'|  | ,'           '---'    ;   : ;--'  |   :   .':   : :-'              :   '  |--" |  | ,'           '---'    |   :    / ;   : ;--'  |   :   .':   : :-'   
     \   \ ;        `---`     |   |.'              |   ,.'                   \   \ ;    |   | ,'  `--''                      |   ,/      |   | ,'  |   |.'                 \   \ ;    `--''                       \   \ .'  |   ,/      |   | ,'  |   |.'     
      '---"                   `---'                '---'                      '---"     `----'                               '---'       `----'    `---'                    '---"                                  `---`    '---'       `----'    `---'       
                                                                                                                                                                                                                                                              
                                                                                                                                                                                                                                                            
```


# StationMeteoSTM32

Station météo embarquée sur carte **NUCLEO-L476RG** (STM32L476RG), projet de l'école CESI.
Elle mesure la température, la pression, l'hygrométrie, la luminosité et la position GPS, puis enregistre les mesures
au format CSV sur une carte SD.

## Matériel

| Élément | Module | Liaison |
|---|---|---|
| Carte | NUCLEO-L476RG | — |
| Température, pression, hygrométrie | BME280 (0x76) | I2C (PB8 / PB9) |
| Horloge | Grove RTC DS1307 (0x68) | I2C |
| GPS | Grove GPS Air530Z | UART4, 9600 bauds (PA0 / PA1) |
| Luminosité | Grove Light Sensor v1.3 | ADC1 voie 5 (PA0) |
| Stockage | SD Card Shield v4.3 | SPI1 (PA5, PA6, PA7, CS PB5) |
| LED | Grove Chainable RGB LED | PB10 (horloge), PA8 (données) |
| Boutons | Grove Dual Button | PA10 (vert), PB3 (rouge) |
| Console | USB (ST-Link) | USART2, 115200 bauds |

Toutes les broches sont dans `include/board_pins.h`.

## Fonctionnement

Après l'initialisation, une boucle de 10 ms appelle les différentes tâches (pas de RTOS : code simple et prévisible).
À chaque période, la station lit les capteurs, attend le GPS jusqu'à `TIMEOUT` secondes, remplace toute valeur absente
par `NA` et écrit une ligne CSV :

```
horodatage;température;pression;hygrométrie;luminosité;latitude;longitude;altitude
```

### Modes

| Mode | LED | Accès |
|---|---|---|
| Standard | verte | mode par défaut |
| Économique | bleue | appui 5 s sur le bouton vert (retour par le même appui) ; périodes ×2, GPS une mesure sur deux |
| Maintenance | orange | appui 5 s sur le bouton rouge ; ferme le fichier pour retirer la carte SD sans risque |
| Configuration | jaune | démarrage avec le bouton rouge appuyé ; sortie automatique après 30 min d'inactivité |

### Configuration

En mode configuration, les réglages se font sur la console série avec des commandes `NOM=valeur`
(`LOG_INTERVAL`, `FILE_MAX_SIZE`, `TIMEOUT`, `LUMIN`, `LUMIN_LOW`, `LUMIN_HIGH`, `TEMP_AIR`, `MIN_TEMP_AIR`,
`MAX_TEMP_AIR`, `HYGR`, `HYGR_MINT`, `HYGR_MAXT`, `PRESSURE`, `PRESSURE_MIN`, `PRESSURE_MAX`),
ainsi que `VERSION`, `RESET`, `TIME`, `CLOCK=HH:MM:SS`, `DATE=MM,JJ,AAAA` et `DAY=MON..SUN`.
Les réglages sont sauvegardés en flash (signature + somme de contrôle). Les valeurs par défaut, minimales et maximales
sont regroupées dans `include/settings.h`.

### Fichiers sur la carte SD

- `AAMMJJ00.LOG` : fichier actif du jour.
- Quand il atteint `FILE_MAX_SIZE` octets (2048 par défaut), il est archivé en `AAMMJJ01.LOG` à `AAMMJJ99.LOG`.
- Rien n'est jamais supprimé : après 99 archives dans la journée, ou si la carte est pleine, l'erreur E03 s'affiche.
- Retrait de la carte détecté en 2 s, reprise automatique à son retour.

### Codes d'erreur (LED, période 1 s)

| Code | Cause | LED |
|---|---|---|
| E01 | Horloge RTC inaccessible | rouge / bleu, durées égales |
| E02 | Accès ou écriture SD en échec | rouge 1/3, blanc 2/3 |
| E03 | Carte SD pleine | rouge / blanc, durées égales |
| E04 | GPS sans données | rouge / jaune, durées égales |
| E05 | Capteur sans réponse | rouge / vert, durées égales |
| E06 | Données hors seuils | rouge 1/3, vert 2/3 |

Si plusieurs erreurs sont présentes, la plus prioritaire s'affiche (E01 d'abord, E06 en dernier).

## Structure du projet

```
include/settings.h    tous les réglages (défauts, plages, périodes)
include/board_pins.h  broches de la carte
src/main.c            initialisation et boucle principale
src/setup.c           horloge, I2C, SPI, liaisons série
src/config.c          paramètres, sauvegarde flash, commandes
src/console.c         envoi et réception série
src/modes.c           modes, LED RGB, boutons, erreurs
src/capteurs.c        luminosité (ADC) et GPS (trames RMC et GGA)
src/i2c.c             RTC et BME280
src/mesures.c         cycle de mesure, écriture SD, archivage
src/sd_spi.c          pilote de carte SD en SPI
src/ff.c              FatFs R0.12c (bibliothèque tierce)
auto.sh               compilation et téléversement
```

Le fichier `FICHE_SOUTENANCE.md` détaille où trouver chaque fonctionnalité dans le code.

## Compilation et téléversement

Prérequis : [PlatformIO](https://platformio.org/) et `st-flash` (stlink).

```bash
bash auto.sh            # compile, génère build/firmware.hex et .bin, puis propose le téléversement
bash auto.sh init       # (re)crée le fichier de configuration .stm32cesi
```

Ou directement avec PlatformIO : `pio run`.

## Historique du développement

- **12 oct. 2026** : démarrage du projet, premiers exemples.
- **13 oct.** : découpage en modules (FatFs, pilote SD SPI, modes et LED, console, configuration, I2C, capteurs, mesures) ; `main.c` réduit à l'initialisation et à la boucle ; décodage GPS RMC puis GGA ; CSV avec latitude, longitude et altitude séparées.
- **14 oct.** : `settings.h` regroupe les réglages ; sauvegarde des paramètres en flash ; codes d'erreur de la LED par priorité ; capteurs désactivables, seuils et timeout ; script `auto.sh`.
- **20 oct.** : corrections des erreurs SD et capteur qui ne s'effaçaient pas ; détection immédiate de la perte du GPS ; capteurs arrêtés entre deux mesures.
- **22 oct.** : affichage de la luminosité (faible / moyenne / forte) ; commentaires sur le câblage.
- **23 oct.** : archivage sans suppression (99 archives par jour) ; commentaires sur chaque fonction ; fiche de soutenance ; version soutenance (`TIMEOUT` à 30 s).

## Limites connues

- Pas de watchdog (amélioration possible : IWDG).
- L'écriture SD bloque la boucle le temps de l'opération.
- L'heure du GPS ne recale pas la RTC.
- Capteurs optionnels (eau, vent, courant, particules) non intégrés.
- PA0 est partagée entre la luminosité et le TX du GPS : le GPS ne peut pas être mis en veille par commande, seule sa réception est coupée.
