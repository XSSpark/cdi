/*
 * Copyright (c) 2008 The tyndur Project. All rights reserved.
 *
 * This code is derived from software contributed to the tyndur Project
 * by Alexander Siol and Janosch Graef.
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

#include "ramdisk_cdi.h"
#include "cdi/lists.h"

int ramdisk_fs_init(struct cdi_fs_filesystem* cdi_fs)
{
    struct ramdisk_fs_res* root_res;

    root_res = malloc(sizeof(*root_res));
    memset(root_res, 0, sizeof(*root_res));
    root_res->res.name = strdup("/");
    root_res->res.res = &ramdisk_fs_res;
    root_res->res.dir = &ramdisk_fs_dir;
    root_res->res.flags.remove = 1;
    root_res->res.flags.rename = 1;
    root_res->res.flags.move = 1;
    root_res->res.flags.browse = 1;
    root_res->res.flags.create_child = 1;
    root_res->res.loaded = 1;
    root_res->res.children = cdi_list_create();
    root_res->creation_time = 0;

    cdi_fs->root_res = (struct cdi_fs_res*) root_res;
    return 1;
}

static void destroy_res(struct ramdisk_fs_res *res)
{
    struct ramdisk_fs_res *child;

    while ((child = cdi_list_pop(res->res.children))) {
        destroy_res(child);
    }
    ramdisk_fs_res_destroy(res);
}

int ramdisk_fs_destroy(struct cdi_fs_filesystem* fs)
{
    destroy_res((struct ramdisk_fs_res*)fs->root_res);
    return 0;
}
