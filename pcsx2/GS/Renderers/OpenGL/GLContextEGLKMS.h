// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/Renderers/OpenGL/GLContextEGL.h"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

// ARMSX2: OpenGL ES straight to the display on handhelds with no compositor and
// no Vulkan - the Allwinner H700 family (RG35XX H/Plus/SP, RG28XX, RG40XX) on
// Knulli, muOS and the stock firmware, whose Mali-G31 has only Arm's GLES blob.
//
// Two platforms, because those blobs come in two builds:
//  - GBM: Mesa, or libmali built for gbm. We pick a connector and mode through
//    KMS, render into a gbm_surface, and page-flip its front buffer each swap.
//  - fbdev: libmali built for fbdev. EGL draws to /dev/fb0 itself and wants only
//    a {width, height} struct for a window.
// GBM is tried first; ARMSX2_EGL_PLATFORM=gbm|fbdev forces one, and
// ARMSX2_DRM_DEVICE picks the card node. libdrm and libgbm are opened at run
// time, so an fbdev-only device without them still starts.
class GLContextEGLKMS final : public GLContextEGL
{
public:
	GLContextEGLKMS(const WindowInfo& wi);
	~GLContextEGLKMS() override;

	static std::unique_ptr<GLContext> Create(const WindowInfo& wi, std::span<const Version> versions_to_try, Error* error);

	bool ChangeSurface(const WindowInfo& new_wi) override;
	void ResizeSurface(u32 new_surface_width = 0, u32 new_surface_height = 0) override;
	bool SwapBuffers() override;
	bool SetSwapInterval(s32 interval) override;
	std::unique_ptr<GLContext> CreateSharedContext(const WindowInfo& wi, Error* error) override;

protected:
	EGLDisplay GetPlatformDisplay(Error* error) override;
	EGLSurface CreatePlatformSurface(EGLConfig config, void* win, Error* error) override;

private:
	// Mali's fbdev EGLNativeWindowType points at one of these.
	struct FBDevWindow
	{
		unsigned short width;
		unsigned short height;
	};

	struct FBInfo
	{
		GLContextEGLKMS* owner;
		u32 fb_id;
	};

	bool LoadModules(Error* error);
	bool OpenDRM(Error* error);
	bool OpenFBDev(Error* error);
	void DestroyGBMSurface();
	u32 GetFramebufferForBO(gbm_bo* bo);
	bool WaitForFlip();

	static void BODestroyCallback(gbm_bo* bo, void* data);
	static void PageFlipHandler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void* data);

	bool m_use_gbm = false;

	void* m_drm_module = nullptr;
	void* m_gbm_module = nullptr;

	int m_drm_fd = -1;
	u32 m_connector_id = 0;
	u32 m_crtc_id = 0;
	drmModeModeInfo m_mode = {};
	drmModeCrtc* m_saved_crtc = nullptr;
	bool m_crtc_set = false;
	bool m_flip_pending = false;

	gbm_device* m_gbm_device = nullptr;
	gbm_surface* m_gbm_surface = nullptr;
	gbm_bo* m_current_bo = nullptr;
	gbm_bo* m_next_bo = nullptr;

	FBDevWindow m_fbdev_window = {};

#define KMS_FUNC(ret, name, args) ret(*m_##name) args = nullptr;
#include "GS/Renderers/OpenGL/GLContextEGLKMSFuncs.inl"
#undef KMS_FUNC
};
