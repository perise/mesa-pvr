/*
 * Copyright © 2023 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_rt_dataset.h"

#include <string.h>

#include "pvr_device.h"
#include "pvr_free_list.h"

static void pvr_rt_rgn_headers_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(rt_dataset->rt_datas); i++)
      rt_dataset->rt_datas[i].rgn_headers_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->rgn_headers_bo);
   rt_dataset->rgn_headers_bo = NULL;
}

void pvr_rt_mlist_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(rt_dataset->rt_datas); i++)
      rt_dataset->rt_datas[i].mlist_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->mlist_bo);
   rt_dataset->mlist_bo = NULL;
}

void pvr_rt_mta_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   if (rt_dataset->mta_bo == NULL)
      return;

   for (uint32_t i = 0; i < ARRAY_SIZE(rt_dataset->rt_datas); i++)
      rt_dataset->rt_datas[i].mta_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->mta_bo);
   rt_dataset->mta_bo = NULL;
}

void pvr_rt_datas_fini(struct pvr_rt_dataset *rt_dataset)
{
   pvr_rt_rgn_headers_data_fini(rt_dataset);
   pvr_rt_mlist_data_fini(rt_dataset);
   pvr_rt_mta_data_fini(rt_dataset);
}

void pvr_rt_tpc_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   pvr_bo_free(rt_dataset->device, rt_dataset->tpc_bo);
   rt_dataset->tpc_bo = NULL;
}

void pvr_rt_vheap_rtc_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   rt_dataset->rtc_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->vheap_rtc_bo);
   rt_dataset->vheap_rtc_bo = NULL;
}

static void pvr_render_target_dataset_free(struct pvr_rt_dataset *rt_dataset)
{
   struct pvr_device *device = rt_dataset->device;

   device->ws->ops->render_target_dataset_destroy(rt_dataset->ws_rt_dataset);

   pvr_rt_datas_fini(rt_dataset);
   pvr_rt_tpc_data_fini(rt_dataset);
   pvr_rt_vheap_rtc_data_fini(rt_dataset);

   pvr_free_list_destroy(rt_dataset->local_free_list);

   vk_free(&device->vk.alloc, rt_dataset);
}

/**
 * pvr_render_target_dataset_destroy() - Return rt_dataset to device cache.
 *
 * Instead of immediately freeing, the rt_dataset is kept in a per-device
 * pool keyed by (width, height, samples, layers) so that the next
 * CmdBeginRendering with matching dimensions can reuse it without issuing
 * a DRM_IOCTL_PVR_CREATE_FREE_LIST ioctl.  The pool is flushed at device
 * destroy time.
 */
void pvr_render_target_dataset_destroy(struct pvr_rt_dataset *rt_dataset)
{
   struct pvr_device *device = rt_dataset->device;

   simple_mtx_lock(&device->rt_cache_mtx);
   if (device->rt_cache_enabled &&
       device->rt_cache_count < PVR_RT_DATASET_CACHE_SIZE) {
      device->rt_cache[device->rt_cache_count++] = rt_dataset;
      simple_mtx_unlock(&device->rt_cache_mtx);
      return;
   }
   simple_mtx_unlock(&device->rt_cache_mtx);

   pvr_render_target_dataset_free(rt_dataset);
}

/**
 * pvr_rt_dataset_cache_get() - Retrieve a matching rt_dataset from cache.
 *
 * Returns a cached rt_dataset with matching dimensions, or NULL if none
 * exists.  The caller takes ownership and must eventually call
 * pvr_render_target_dataset_destroy() on it.
 */
struct pvr_rt_dataset *
pvr_rt_dataset_cache_get(struct pvr_device *device,
                         uint32_t width,
                         uint32_t height,
                         uint32_t samples,
                         uint32_t layers)
{
   simple_mtx_lock(&device->rt_cache_mtx);
   for (uint32_t i = 0; i < device->rt_cache_count; i++) {
      struct pvr_rt_dataset *ds = device->rt_cache[i];
      if (ds->width == width && ds->height == height &&
          ds->samples == samples && ds->layers == layers) {
         /* Remove from cache by swapping with the last entry */
         device->rt_cache[i] = device->rt_cache[--device->rt_cache_count];
         simple_mtx_unlock(&device->rt_cache_mtx);
         return ds;
      }
   }
   simple_mtx_unlock(&device->rt_cache_mtx);
   return NULL;
}

/**
 * pvr_rt_dataset_cache_flush() - Destroy all cached rt_datasets.
 *
 * Called at device destroy time (after rt_cache_enabled is set to false).
 */
void pvr_rt_dataset_cache_flush(struct pvr_device *device)
{
   struct pvr_rt_dataset *to_free[PVR_RT_DATASET_CACHE_SIZE];
   uint32_t count;

   simple_mtx_lock(&device->rt_cache_mtx);
   count = device->rt_cache_count;
   memcpy(to_free, device->rt_cache, count * sizeof(*to_free));
   device->rt_cache_count = 0;
   simple_mtx_unlock(&device->rt_cache_mtx);

   for (uint32_t i = 0; i < count; i++)
      pvr_render_target_dataset_free(to_free[i]);
}
