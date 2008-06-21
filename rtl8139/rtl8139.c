/*
 * Copyright (c) 2007 The LOST Project. All rights reserved.
 *
 * This code is derived from software contributed to the LOST Project
 * by Kevin Wolf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *     This product includes software developed by the LOST Project
 *     and its contributors.
 * 4. Neither the name of the LOST Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "cdi/net.h"
#include "cdi/pci.h"
#include "cdi/io.h"
#include "cdi/misc.h"

#include "rtl8139.h"
#include "ethernet.h"

//Hier koennen die Debug-Nachrichten aktiviert werden
//#define DEBUG_MSG(s) printf("[RTL8139] debug: %s() '%s'\n", __FUNCTION__, s)
#define DEBUG_MSG(s) //

static void rtl8139_handle_interrupt();

static inline void write_register_byte(struct rtl8139_device* netcard, uint8_t reg, uint8_t value)
{
    cdi_outb(netcard->port_base + reg, value);
}

static inline void write_register_word(struct rtl8139_device* netcard, uint8_t reg, uint16_t value)
{
    cdi_outw(netcard->port_base + reg, value);
}

static inline void write_register_dword(struct rtl8139_device* netcard, uint8_t reg, uint32_t value)
{
    cdi_outl(netcard->port_base + reg, value);
}

static inline uint8_t read_register_byte(struct rtl8139_device* netcard, uint8_t reg)
{
    return cdi_inb(netcard->port_base + reg);
}

static inline uint16_t read_register_word(struct rtl8139_device* netcard, uint8_t reg)
{
    return cdi_inw(netcard->port_base + reg);
}

static inline uint32_t read_register_dword(struct rtl8139_device* netcard, uint8_t reg)
{
    return cdi_inl(netcard->port_base + reg);
}

void rtl8139_init_device(struct cdi_device* device)
{
    struct rtl8139_device* netcard = (struct rtl8139_device*) device;
    netcard->net.send_packet = rtl8139_send_packet;

    cdi_net_device_init((struct cdi_net_device*) device);

    // PCI-bezogenes Zeug initialisieren
    DEBUG_MSG("Interrupthandler und Ports registrieren");
    cdi_register_irq(netcard->pci->irq, rtl8139_handle_interrupt, device);
    cdi_pci_alloc_ioports(netcard->pci);

    cdi_list_t reslist = netcard->pci->resources;
    struct cdi_pci_resource* res;
    int i;
    for (i = 0; (res = cdi_list_get(reslist, i)); i++) {
        if (res->type == CDI_PCI_IOPORTS) {
            netcard->port_base = res->start;
        }
    }

    // Einen Reset der Karte durchf�hren
    DEBUG_MSG("Reset der Karte");
    write_register_byte(netcard, REG_COMMAND, CR_RESET);

    // Warten bis der Reset beendet ist
    while ((read_register_byte(netcard, REG_COMMAND) & REG_COMMAND) ==
        CR_RESET);

    // MAC-Adresse auslesen
    DEBUG_MSG("MAC-Adresse auslesen");
    netcard->net.mac = 
        read_register_dword(netcard, REG_ID0)
        | (((qword) read_register_dword(netcard, REG_ID4) & 0xFFFF) << 32);

    // Receive/Transmit (Rx/Tx) aktivieren
    DEBUG_MSG("Aktiviere Rx/Tx");
    write_register_byte(netcard, REG_COMMAND, 
        CR_RECEIVER_ENABLE | CR_TRANSMITTER_ENABLE);

    // RCR/TCR initialisieren
    // RBLEN = 00, 8K + 16 Bytes Rx-Ringpuffer
    DEBUG_MSG("Inititalisiere RCR/TCR");
    write_register_dword(netcard, REG_RECEIVE_CONFIGURATION,
        RCR_MXDMA_UNLIMITED | RCR_ACCEPT_BROADCAST | RCR_ACCEPT_PHYS_MATCH);

    write_register_dword(netcard, REG_TRANSMIT_CONFIGURATION,
        TCR_IFG_STANDARD | TCR_MXDMA_2048);

    // Wir wollen Interrupts, wann immer es was zu vermelden gibt
    DEBUG_MSG("Setze Interruptmaske");
    write_register_word(netcard, REG_INTERRUPT_STATUS, 0);
    write_register_word(netcard, REG_INTERRUPT_MASK, 0xFFFF);

    // Sende- und Empfangspuffer
    DEBUG_MSG("Initialisiere Buffer");
    netcard->buffer_used = FALSE;
    netcard->cur_buffer = 0;

    write_register_dword(netcard, REG_RECEIVE_BUFFER,
        PHYS(netcard, rx_buffer));

    netcard->rx_buffer_offset = 0;

    netcard->receive_handler = cdi_list_create();
    netcard->pending_sends = cdi_list_create();

    DEBUG_MSG("Fertig initialisiert");
}

void rtl8139_remove_device(struct cdi_device* device)
{
}

void rtl8139_send_packet
    (struct cdi_net_device* device, void* data, size_t size)
{
    struct rtl8139_device* netcard = (struct rtl8139_device*) device;

    if (size > 0x700) {
        // Spezialfall - keine Lust
        printf("rtl8139: size ist boese\n");
        return;
    }

    //DEBUG_MSG("Vor dem p()...");

    if (netcard->buffer_used) {
        DEBUG_MSG("Tx-Buffer ist schon besetzt");

        void* pending = malloc(size + sizeof(dword));
        cdi_list_insert(netcard->pending_sends,
            cdi_list_size(netcard->pending_sends), pending);
        *((dword*) pending) = size;
        pending += sizeof(dword);
        memcpy(pending, data, size);

        return;
    }
    //DEBUG_MSG("Tx-Buffer reserviert");
    // Kopiere das Packet in den Puffer. Wichtig: Der Speicherbereich
    // mu� physisch zusammenh�ngend sein.
    netcard->buffer_used = TRUE;
    memcpy(netcard->buffer, data, size);

    // Falls weniger als 60 Bytes gesendet werden sollen, muss mit Nullen
    // aufgefuellt werden.
    if (size < 60) {
        memset(netcard->buffer + size, 0, 60 - size);
        size = 60;
    }
    //printf("Phys Buffer = %08x\n", netcard->buffer.phys);

    // Adresse und Gr��e des Buffers setzen
    // In TASD (REG_TRANSMIT_STATUS) wird das OWN-Bit gel�scht, was die
    // tats�chliche �bertragung anst��t.
    write_register_dword(netcard, 
        REG_TRANSMIT_ADDR0 + (4 * netcard->cur_buffer), 
        PHYS(netcard, buffer));

    write_register_dword(netcard, 
        REG_TRANSMIT_STATUS0 + (4 * netcard->cur_buffer), 
        size);

    // Die vier Deskriptoren m�ssen der Reihe nach verwendet werden,
    // auch wenn wir momentan nur einen einzigen Puffer verwenden
    // und immer warten.
    netcard->cur_buffer++;
    netcard->cur_buffer %= 4;

    DEBUG_MSG("Gesendet");

    return;
}

static void receive_ok_handler(struct rtl8139_device* netcard)
{
    DEBUG_MSG("ROK");

    while (1)
    {
        dword cr = read_register_byte(netcard, REG_COMMAND);
        if (cr & CR_BUFFER_IS_EMPTY) {
            DEBUG_MSG("Rx-Puffer ist leer.\n");
            break;
        }

        void* buffer = netcard->rx_buffer + netcard->rx_buffer_offset;

        word packet_header = *((word*) buffer);
        buffer += 2;

        if ((packet_header & 1) == 0) {
            DEBUG_MSG("Paketheader: Kein ROK.\n");
            break;
        }

        // Die L�nge enth�lt die CRC-Pr�fsumme, nicht aber den Paketheader
        word length = *((word*) buffer);
        buffer += 2;

        /*
        printf("Empfangen: %d Bytes\n", length);
        printf("netcard->rx_buffer_offset = %08x\n", netcard->rx_buffer_offset);
        */
        /*int i;
        for (i = 0; i < length; i++) {
            printf("%02x ", ((char*) buffer)[i]);
        }
        printf("\n");*/

        netcard->rx_buffer_offset += 4;

        if (length >= sizeof(struct eth_packet_header))
        {
            uint8_t data[length - 4];
            if ((netcard->rx_buffer_offset + length - 4) >= RX_BUFFER_SIZE) {
                // FIXME Beim Umbruch gehen Daten kaputt
                memcpy(data, buffer, RX_BUFFER_SIZE - netcard->rx_buffer_offset);
                memcpy(
                    data + RX_BUFFER_SIZE - netcard->rx_buffer_offset,
                    netcard->rx_buffer,
                    (length - 4) - (RX_BUFFER_SIZE - netcard->rx_buffer_offset)
                );
            } else {
                memcpy(data, buffer, length - 4);
            }

            cdi_net_receive(
                (struct cdi_net_device*) netcard,
                data,
                length - 4);
        }

        // Den aktuellen Offset im Lesepuffer anpassen. Jedes Paket ist
        // dword-aligned, daher anschlie�end Aufrundung.
        netcard->rx_buffer_offset += length;
        netcard->rx_buffer_offset = (netcard->rx_buffer_offset + 3) & ~0x3;

        // �berl�ufe abfangen
        netcard->rx_buffer_offset %= RX_BUFFER_SIZE;

        write_register_word(netcard, REG_CUR_READ_ADDR,
            netcard->rx_buffer_offset - 0x10);
    }
}

static void transmit_ok_handler(struct rtl8139_device* netcard)
{
    DEBUG_MSG("TOK");
    netcard->buffer_used = FALSE;
}

static void rtl8139_handle_interrupt(struct cdi_device* device)
{
    DEBUG_MSG("<RTL8139 Interrupt>");

    struct rtl8139_device* netcard = (struct rtl8139_device*) device;
    dword isr = read_register_word(netcard, REG_INTERRUPT_STATUS);
    dword clear_isr = 0;

    if (isr & ISR_RECEIVE_OK) {
        receive_ok_handler(netcard);
        clear_isr |= ISR_RECEIVE_OK;
    }

    if (isr & ISR_TRANSMIT_OK){
        transmit_ok_handler(netcard);
        clear_isr |= ISR_TRANSMIT_OK;
    }

    // Achtung Stolperfalle: Bits, die beim Schreiben auf den Port
    // gesetzt sind, werden im Register gel�scht. Es ist also nicht
    // der neue Wert des Registers zu �bergeben, sondern die
    // verarbeiteten und somit zu l�schenden Bits.
    write_register_word(netcard, REG_INTERRUPT_STATUS, clear_isr);

    // Falls noch Pakete zu senden waren, die nicht gesendet werden
    // konnten, weil die Karte beschaeftigt war, das jetzt nachholen
    void* pending = cdi_list_pop(netcard->pending_sends);
    if (pending) {
        dword size = *((dword*) pending);
        pending += sizeof(dword);
        rtl8139_send_packet((struct cdi_net_device*) device, pending, size);
        free(pending - sizeof(dword));
    }
}