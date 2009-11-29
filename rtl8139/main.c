/*
 * Copyright (c) 2007 The tyndur Project. All rights reserved.
 *
 * This code is derived from software contributed to the tyndur Project
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
#include <string.h>

#include "cdi/net.h"
#include "cdi/pci.h"
#include "cdi/misc.h"

#include "rtl8139.h"

#define DRIVER_NAME "rtl8139"

static struct cdi_net_driver rtl8139_driver;

static int rtl8139_driver_init(void)
{
    // TODO Auf pci-Service warten
    // TODO Auf tcpip-Service warten

    // Konstruktor der Vaterklasse
    cdi_net_driver_init(&rtl8139_driver);

    // Passende PCI-Geraete suchen
    cdi_list_t pci_devices = cdi_list_create();
    cdi_pci_get_all_devices(pci_devices);

    struct cdi_pci_device* dev;
    int i;
    for (i = 0; (dev = cdi_list_get(pci_devices, i)); i++) {
        if ((dev->vendor_id == 0x10ec) && (dev->device_id == 0x8139)) {
            void* phys_device;
            struct rtl8139_device* device;

            cdi_alloc_phys_mem(sizeof(*device),
                (void**) &device, &phys_device);
            memset(device, 0, sizeof(*device));

            device->phys = phys_device;
            device->pci = dev;
            cdi_list_push(rtl8139_driver.drv.devices, device);
        } else {
            cdi_pci_device_destroy(dev);
        }
    }

    cdi_list_destroy(pci_devices);

    return 0;
}

/**
 * Deinitialisiert die Datenstrukturen fuer den rtl8139-Treiber
 */
static int rtl8139_driver_destroy(void)
{
    cdi_net_driver_destroy(&rtl8139_driver);

    // TODO Alle Karten deinitialisieren

    return 0;
}


static struct cdi_net_driver rtl8139_driver = {
    .drv = {
        .type           = CDI_NETWORK,
        .name           = DRIVER_NAME,
        .init           = rtl8139_driver_init,
        .destroy        = rtl8139_driver_destroy,
        .init_device    = rtl8139_init_device,
        .remove_device  = rtl8139_remove_device,
    },
};

CDI_DRIVER(DRIVER_NAME, rtl8139_driver)
