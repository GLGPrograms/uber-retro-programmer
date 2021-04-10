# ÜRP - Über Retro Programmer

Un programmatore di EEPROM per il retro cazzeggio.
Supporta memorie EEPROM parallele della serie 28C.

## Albero delle directory

In questo repository troverai:
```
ÜRP
 |__ hw          --> schematici KiCAD
     |__ gerbers  -> i file per la produzione
     |__ urp.pdf  -> lo schematico in formato PDF
     |__ urp.pro  -> il progetto KiCAD
 |__ fw          --> codice sorgente per ATmega 328
 |__ sw          --> interfaccia a riga di comando per il PC
```

## Assemblaggio della PCB


|Designator      | Description                  | Notes                   |
|----------------|------------------------------|-------------------------|
|C1-C5           | capacitor ceramic 100n P5.08 |                         |
|C6-C7           | capacitor ceramic 22p P2.54  |                         |
|R1              | resistor 10k                 |                         |
|R2-R4           | resistor 330                 |                         |
|Y1              | crystal 16M                  |                         |
|SW1             | Tactile switch               |                         |
|D1-D3           | LED 5mm                      | Verde, Giallo, Rosso    |
|U1              | microcontroller ATMega 328   |                         |
|U2 U3           | shift register 74HC595       |                         |
|JP1-JP5         | 3-pin header 2.54            | Selettori tipo EEPROM   |
|J1              | 6-pin header 2.54            |                         |
|J2              | ZIF socket 32 pin wide       |                         |
|J3              | 2-pin header 2.54            | opzionale               |
|R5              | resistor 10k                 | opzionale               |
|R6              | resistor 3k3                 | opzionale               |
|Q1              | PMOS (2N7002)                | opzionale               |

**Attenzione:** Quando non si assembla Q1, è necessario cortocircuitare JP7 sul retro della scheda.

## Compilazione firmware

1. Installare l'ATmega 328 su una board Arduino o equivalente. Se il chip è vergine, caricarvi il bootloader standard (Arduino UNO @ 16MHz);
1. Compilare il sorgente nella cartella `fw` utilizzando l'apposito makefile e flashare il binario. Sostituire a `/dev/ttyXYZ123` il device seriale opportuno (es. /dev/ttyUSB0);

        cd fw
        make
        make program AVRDUDEPORT=/dev/ttyXYZ123

1. Trasferire il microcontrollore sullo zoccolo dell'ÜRP.

## Compilazione software

        cd sw
        gcc -Wall -Wextra -pedantic serprog.c -o serprog

## Prontuario

TODO
