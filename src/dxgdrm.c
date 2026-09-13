// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2022, Microsoft Corporation.
 *
 * DRM integration for dxgkrnl driver
 * This file provides DRM subsystem integration
 */

#include <linux/module.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_device.h>

#include "dxgkrnl.h"

#define DRIVER_NAME		"dxgkrnl"
#define DRIVER_DESC		"Microsoft Dxgkrnl virtual GPU Driver"
#define DRIVER_DATE		"20221201"
#define DRIVER_MAJOR		2
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	3

static int dxg_drm_open(struct drm_device *drm_dev, struct drm_file *file)
{
	struct dxgprocess *process;
	struct dxgadapter *adapter = drm_dev->dev_private;

	(void)adapter; /* Used in DXG_TRACE */
	DXG_TRACE("DRM open: %p, adapter: %p", file, adapter);

	/* Create or get existing process */
	process = dxgglobal_get_current_process();
	if (!process) {
		DXG_ERR("Failed to create dxgprocess");
		return -ENOMEM;
	}

	file->driver_priv = process;
	return 0;
}

static void dxg_drm_postclose(struct drm_device *drm_dev, struct drm_file *file)
{
	struct dxgprocess *process = file->driver_priv;

	DXG_TRACE("DRM postclose: %p, process: %p", file, process);

	if (process) {
		kref_put(&process->process_kref, dxgprocess_release);
		kref_put(&process->process_mem_kref, dxgprocess_mem_release);
		file->driver_priv = NULL;
	}
}

static long dxg_drm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct dxgprocess *process = file_priv->driver_priv;

	if (!process)
		return -EINVAL;

	/* Redirect to existing dxgkrnl IOCTL handler */
	return dxgk_unlocked_ioctl(filp, cmd, arg);
}

static const struct file_operations dxg_drm_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = dxg_drm_ioctl,
	.compat_ioctl = dxg_drm_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
};

static const struct drm_driver dxg_drm_driver = {
	.driver_features = DRIVER_RENDER,
	.open = dxg_drm_open,
	.postclose = dxg_drm_postclose,
	.fops = &dxg_drm_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

/**
 * dxg_drm_init_adapter - Initialize DRM device for an adapter
 * @adapter: dxgadapter to create DRM device for
 *
 * Creates and registers a DRM render node for the adapter.
 * Returns 0 on success, negative error code on failure.
 */
int dxg_drm_init_adapter(struct dxgadapter *adapter)
{
	struct drm_device *drm_dev;
	int ret;

	if (!adapter || !adapter->pci_dev) {
		DXG_ERR("Invalid adapter or PCI device");
		return -EINVAL;
	}

	DXG_TRACE("Initializing DRM for adapter %p (LUID: %x-%x)",
		  adapter, adapter->luid.a, adapter->luid.b);

	drm_dev = drm_dev_alloc(&dxg_drm_driver, &adapter->pci_dev->dev);
	if (IS_ERR(drm_dev)) {
		ret = PTR_ERR(drm_dev);
		DXG_ERR("Failed to allocate DRM device: %d", ret);
		return ret;
	}

	drm_dev->dev_private = adapter;
	adapter->drm_dev = drm_dev;

	ret = drm_dev_register(drm_dev, 0);
	if (ret) {
		DXG_ERR("Failed to register DRM device: %d", ret);
		drm_dev_put(drm_dev);
		adapter->drm_dev = NULL;
		return ret;
	}

	DXG_TRACE("DRM device registered successfully for adapter %p", adapter);
	return 0;
}

/**
 * dxg_drm_destroy_adapter - Cleanup DRM device for an adapter
 * @adapter: dxgadapter to destroy DRM device for
 *
 * Unregisters and destroys the DRM device associated with the adapter.
 */
void dxg_drm_destroy_adapter(struct dxgadapter *adapter)
{
	if (!adapter || !adapter->drm_dev)
		return;

	DXG_TRACE("Destroying DRM for adapter %p", adapter);

	drm_dev_unregister(adapter->drm_dev);
	drm_dev_put(adapter->drm_dev);
	adapter->drm_dev = NULL;
}
