// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// libdrm and libgbm entry points GLContextEGLKMS loads at run time.
// KMS_FUNC(return type, name, (parameters)). No include guard: included once
// per expansion of KMS_FUNC.

// libdrm.so.2
KMS_FUNC(drmModeResPtr, drmModeGetResources, (int fd))
KMS_FUNC(void, drmModeFreeResources, (drmModeResPtr ptr))
KMS_FUNC(drmModeConnectorPtr, drmModeGetConnector, (int fd, uint32_t connector_id))
KMS_FUNC(void, drmModeFreeConnector, (drmModeConnectorPtr ptr))
KMS_FUNC(drmModeEncoderPtr, drmModeGetEncoder, (int fd, uint32_t encoder_id))
KMS_FUNC(void, drmModeFreeEncoder, (drmModeEncoderPtr ptr))
KMS_FUNC(drmModeCrtcPtr, drmModeGetCrtc, (int fd, uint32_t crtc_id))
KMS_FUNC(void, drmModeFreeCrtc, (drmModeCrtcPtr ptr))
KMS_FUNC(int, drmModeSetCrtc, (int fd, uint32_t crtc_id, uint32_t buffer_id, uint32_t x, uint32_t y, uint32_t* connectors, int count, drmModeModeInfoPtr mode))
KMS_FUNC(int, drmModeAddFB, (int fd, uint32_t width, uint32_t height, uint8_t depth, uint8_t bpp, uint32_t pitch, uint32_t bo_handle, uint32_t* buf_id))
KMS_FUNC(int, drmModeRmFB, (int fd, uint32_t buffer_id))
KMS_FUNC(int, drmModePageFlip, (int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void* user_data))
KMS_FUNC(int, drmHandleEvent, (int fd, drmEventContextPtr evctx))

// libgbm.so.1
KMS_FUNC(gbm_device*, gbm_create_device, (int fd))
KMS_FUNC(void, gbm_device_destroy, (gbm_device* gbm))
KMS_FUNC(gbm_surface*, gbm_surface_create, (gbm_device* gbm, uint32_t width, uint32_t height, uint32_t format, uint32_t flags))
KMS_FUNC(void, gbm_surface_destroy, (gbm_surface* surface))
KMS_FUNC(gbm_bo*, gbm_surface_lock_front_buffer, (gbm_surface* surface))
KMS_FUNC(void, gbm_surface_release_buffer, (gbm_surface* surface, gbm_bo* bo))
KMS_FUNC(uint32_t, gbm_bo_get_width, (gbm_bo* bo))
KMS_FUNC(uint32_t, gbm_bo_get_height, (gbm_bo* bo))
KMS_FUNC(uint32_t, gbm_bo_get_stride, (gbm_bo* bo))
KMS_FUNC(union gbm_bo_handle, gbm_bo_get_handle, (gbm_bo* bo))
KMS_FUNC(void*, gbm_bo_get_user_data, (gbm_bo* bo))
KMS_FUNC(void, gbm_bo_set_user_data, (gbm_bo* bo, void* data, void (*destroy_user_data)(gbm_bo*, void*)))
