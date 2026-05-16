#ifndef VMSUPPORT_H
#define VMSUPPORT_H

#include "../../headers/types.h"
#include "../../headers/const.h"

/**
 * Definizioni e prototipi per il supporto alla memoria virtuale (Paging).
 * Questo modulo gestisce il caricamento dinamico delle pagine dalla Flash alla RAM.
 */

#define EXC_TLBMOD 1
#define INTLINE_DISK 3
#define INTLINE_FLASH 4

/**
 * Inizializza le strutture dati per la Swap Pool e il puntatore FIFO.
 * Deve essere invocata dal processo Instantiator prima del lancio dei processi utente.
 */
void initSwapStructs();

/**
 * Il Pager: l'handler per le eccezioni TLB.
 * Invocato automaticamente quando un processo tenta di accedere a una pagina non presente in RAM.
 */
void Pager();

#endif