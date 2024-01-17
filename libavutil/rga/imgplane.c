/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * rga accellerated parallel image copy
 */
#include "libavutil/imgutils_internal.h"
#include "libavutil/mem.h"
#include "rga/RgaApi.h"
#include "rga/im2d.h"
#include "string.h"

#define IS_ALIGNED(p, a) (!((uintptr_t)(p) & ((a)-1)))

static int rga_async_copy(void *src_addr, void *dst_addr, uint16_t src_hstride,
                          uint16_t dst_hstride, uint16_t height, int *outfence) {
    int ret = 0;
    rga_info_t src = { 0 };
    rga_info_t dst = { 0 };

    src.mmuFlag = 1;
    dst.mmuFlag = 1;
    src.virAddr = src_addr;
    dst.virAddr = dst_addr;
    src.format = RK_FORMAT_RGBA_8888;
    dst.format = RK_FORMAT_RGBA_8888;
    rga_set_rect(&src.rect, 0, 0, src_hstride >> 2, height, src_hstride >> 2, height, RK_FORMAT_RGBA_8888);
    rga_set_rect(&dst.rect, 0, 0, src_hstride >> 2, height, dst_hstride >> 2, height, RK_FORMAT_RGBA_8888);
    src.rd_mode = IM_RASTER_MODE;
    dst.rd_mode = IM_RASTER_MODE;

    dst.sync_mode = RGA_BLIT_ASYNC;
    dst.in_fence_fd = -1;
    dst.out_fence_fd = *outfence;

    ret = c_RkRgaBlit(&src, &dst, NULL);
    *outfence = dst.out_fence_fd;

    return ret;
}

void image_copy_plane(uint8_t *dst, ptrdiff_t dst_linesize, const uint8_t *src, ptrdiff_t src_linesize,
        ptrdiff_t bytewidth, int height) {
    //TODO: check number of available cores here by constraints
    int num_cores = 2;
    int core_height = height / num_cores;
    int height_offset = 0;
    int ret = 0;
    int *fences = NULL;

    fences = av_mallocz(sizeof(int) * num_cores);
    if (!fences)
        goto clean;

    if (IS_ALIGNED(dst_linesize, 16) && IS_ALIGNED(src_linesize, 16)) {
        for (int core = 0; core < num_cores; core++) {
            ret = rga_async_copy((void*) src, (void*) dst, src_linesize, dst_linesize, core_height, &fences[core]);
            if (ret)
                goto clean;

            src += src_linesize * core_height;
            dst += dst_linesize * core_height;
            height_offset += core_height;
        }
    }

    for (; height > height_offset; height--) {
        memcpy(dst, src, bytewidth);
        dst += dst_linesize;
        src += src_linesize;
    }

    for (int core = 0; core < num_cores; core++) {
        ret = imsync(fences[core]);
        if (ret != IM_STATUS_SUCCESS)
            goto clean;
    }

    ret = 0;

clean:
    if (fences)
        av_free(fences);
}
;
