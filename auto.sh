#!/bin/bash
# =============================================================================
# auto.sh — Compilation et televersement de la station meteo (NUCLEO-L476RG)
# =============================================================================
# Outils  : PlatformIO (pio), arm-none-eabi-objcopy, st-flash
# Usage   : bash auto.sh [f=<dossier>] [init]
#   f=<dossier> : dossier du projet PlatformIO (contient platformio.ini), "." par defaut
#   init        : (re)cree le fichier de configuration .stm32cesi
#
# Exemple : bash auto.sh
#           bash auto.sh f=StationMeteoSTM32 init
# =============================================================================

# Vide le contenu d'un dossier sans supprimer le dossier
function cleanInsideFolder() {
    if [ "$1" != "" ]; then
        rm -rf "$1"/*
    else
        echo "error: folder is null"
    fi
}

printf "#####################\n"
printf "#   COMPIL STM32    #\n"
printf "#####################\n"

FOLDER="."
SINIT=0
for i in "$@"; do
    case $i in
        f=*) FOLDER="${i#f=}" ;;
        init) SINIT=1 ;;
    esac
done

if [ ! -d "$FOLDER" ]; then
    echo "Le dossier $FOLDER n'existe pas"
    exit 1
fi
if [ ! -f "$FOLDER/platformio.ini" ]; then
    echo "Pas de platformio.ini dans $FOLDER"
    exit 1
fi
cd "$FOLDER" || exit 1

# Fichier de configuration .stm32cesi : { type: "STM32L476RG", tool: "st-flash", }
if [ "$SINIT" == "1" ]; then
    printf "INITIALISATION EN COURS\n"
    rm -f .stm32cesi
    echo "{" >> .stm32cesi

    printf 'Quel type de microcontroleur utilisez-vous ? [STM32L476RG] '
    read -r TYPE
    [[ "$TYPE" == "" ]] && TYPE="STM32L476RG"
    echo '  type: "'"$TYPE"'",' >> .stm32cesi

    printf 'Quel outil de televersement utilisez-vous ? [st-flash] '
    read -r FLASHTOOL
    [[ "$FLASHTOOL" == "" ]] && FLASHTOOL="st-flash"
    echo '  tool: "'"$FLASHTOOL"'",' >> .stm32cesi

    echo "}" >> .stm32cesi
fi

CONF_TYPE=$(grep -oP '(?<=type: ")[^"]*' .stm32cesi 2>/dev/null)
CONF_TOOL=$(grep -oP '(?<=tool: ")[^"]*' .stm32cesi 2>/dev/null)
[[ -z "$CONF_TYPE" ]] && CONF_TYPE="STM32L476RG"
[[ -z "$CONF_TOOL" ]] && CONF_TOOL="st-flash"
echo "Cible : $CONF_TYPE, outil : $CONF_TOOL"

# Preparation du dossier de sortie
if [ ! -d "build" ]; then
    mkdir build
else
    cleanInsideFolder "build/"
fi

# P1 : compilation et edition de liens (PlatformIO fournit HAL, demarrage et script de liens)
printf 'P1 compilation...\n'
if ! pio run; then
    echo "ERREUR : la compilation a echoue"
    exit 1
fi

# P2 : recuperation du firmware
ELF=$(ls .pio/build/*/firmware.elf 2>/dev/null | head -n 1)
if [ -z "$ELF" ]; then
    echo "ERREUR : firmware.elf introuvable"
    exit 1
fi
# objcopy : celui du PATH, sinon celui installe par PlatformIO
OBJCOPY=$(command -v arm-none-eabi-objcopy)
[ -z "$OBJCOPY" ] && OBJCOPY=$(ls ~/.platformio/packages/toolchain-gccarmnoneeabi*/bin/arm-none-eabi-objcopy 2>/dev/null | head -n 1)
if [ -z "$OBJCOPY" ]; then
    echo "ERREUR : arm-none-eabi-objcopy introuvable"
    exit 1
fi
printf 'P2'
"$OBJCOPY" -O ihex   "$ELF" build/firmware.hex || exit 1
"$OBJCOPY" -O binary "$ELF" build/firmware.bin || exit 1
printf '. done [build/firmware.hex, build/firmware.bin]\n'

# P3 : televersement a l'adresse 0x08000000 (debut de la Flash)
printf "Voulez-vous commencer le televersement ? [O/N] : "
read -r TELEV
TELEV=$(echo "$TELEV" | tr '[:lower:]' '[:upper:]')

if [[ "$TELEV" == 'N' ]]; then
    printf "Bye bye\n"
else
    if ! command -v "$CONF_TOOL" > /dev/null; then
        echo "ERREUR : $CONF_TOOL n'est pas installe"
        exit 1
    fi
    printf "Demarrage du televersement\n"
    $CONF_TOOL --reset write build/firmware.bin 0x08000000
fi

exit 0
