// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/OpenGL/GLContextEGLKMS.h"

#include "common/Console.h"
#include "common/Error.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

static const char* DRM_MODNAME = "libdrm.so.2";
static const char* GBM_MODNAME = "libgbm.so.1";

// How long a page flip may take before we stop waiting for its event. A 60Hz
// panel flips in ~17ms; this only guards against a driver that never reports.
static constexpr int FLIP_TIMEOUT_MS = 500;

GLContextEGLKMS::GLContextEGLKMS(const WindowInfo& wi)
	: GLContextEGL(wi)
{
}

GLContextEGLKMS::~GLContextEGLKMS()
{
	// Everything EGL holds has to go before the gbm surface and device it was
	// made from, so this cannot be left to ~GLContextEGL.
	DestroySurface();

	if (m_saved_crtc)
	{
		// Hand the display back the way we found it, so whatever launched us
		// (EmulationStation, the muOS frontend) comes back without a modeset.
		WaitForFlip();
		m_drmModeSetCrtc(m_drm_fd, m_saved_crtc->crtc_id, m_saved_crtc->buffer_id, m_saved_crtc->x,
			m_saved_crtc->y, &m_connector_id, 1, &m_saved_crtc->mode);
		m_drmModeFreeCrtc(m_saved_crtc);
		m_saved_crtc = nullptr;
	}

	DestroyGBMSurface();
	DestroyContext();

	if (m_gbm_device)
		m_gbm_device_destroy(m_gbm_device);
	if (m_drm_fd >= 0)
		close(m_drm_fd);
	if (m_gbm_module)
		dlclose(m_gbm_module);
	if (m_drm_module)
		dlclose(m_drm_module);
}

std::unique_ptr<GLContext> GLContextEGLKMS::Create(const WindowInfo& wi, std::span<const Version> versions_to_try,
	Error* error)
{
	const char* forced = std::getenv("ARMSX2_EGL_PLATFORM");
	const bool try_gbm = !forced || std::strcmp(forced, "fbdev") != 0;
	const bool try_fbdev = !forced || std::strcmp(forced, "gbm") != 0;

	if (try_gbm)
	{
		Error gbm_error;
		std::unique_ptr<GLContextEGLKMS> context = std::make_unique<GLContextEGLKMS>(wi);
		if (context->LoadModules(&gbm_error) && context->OpenDRM(&gbm_error) &&
			context->Initialize(versions_to_try, &gbm_error))
		{
			return context;
		}

		Console.WarningFmt("EGL KMS: GBM platform unavailable: {}", gbm_error.GetDescription());
		if (!try_fbdev)
		{
			Error::SetStringFmt(error, "GBM platform unavailable: {}", gbm_error.GetDescription());
			return nullptr;
		}
	}

	std::unique_ptr<GLContextEGLKMS> context = std::make_unique<GLContextEGLKMS>(wi);
	if (!context->OpenFBDev(error) || !context->Initialize(versions_to_try, error))
		return nullptr;

	return context;
}

bool GLContextEGLKMS::LoadModules(Error* error)
{
	m_drm_module = dlopen(DRM_MODNAME, RTLD_NOW | RTLD_LOCAL);
	m_gbm_module = dlopen(GBM_MODNAME, RTLD_NOW | RTLD_GLOBAL);
	if (!m_drm_module || !m_gbm_module)
	{
		const char* err = dlerror();
		Error::SetStringFmt(error, "Loading {} failed: {}", m_drm_module ? GBM_MODNAME : DRM_MODNAME,
			err ? err : "<UNKNOWN>");
		return false;
	}

#define KMS_FUNC(ret, name, args) \
	m_##name = reinterpret_cast<decltype(m_##name)>(dlsym(m_drm_module, #name)); \
	if (!m_##name) \
		m_##name = reinterpret_cast<decltype(m_##name)>(dlsym(m_gbm_module, #name)); \
	if (!m_##name) \
	{ \
		Error::SetStringFmt(error, "Missing {} in {} / {}", #name, DRM_MODNAME, GBM_MODNAME); \
		return false; \
	}
#include "GS/Renderers/OpenGL/GLContextEGLKMSFuncs.inl"
#undef KMS_FUNC

	m_use_gbm = true;
	return true;
}

bool GLContextEGLKMS::OpenDRM(Error* error)
{
	std::string devices[4];
	size_t num_devices = 0;
	if (const char* dev = std::getenv("ARMSX2_DRM_DEVICE"); dev && dev[0])
	{
		devices[num_devices++] = dev;
	}
	else
	{
		for (int i = 0; i < 4; i++)
			devices[num_devices++] = "/dev/dri/card" + std::to_string(i);
	}

	for (size_t d = 0; d < num_devices; d++)
	{
		const int fd = open(devices[d].c_str(), O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;

		drmModeResPtr res = m_drmModeGetResources(fd);
		if (!res)
		{
			close(fd);
			continue;
		}

		drmModeConnectorPtr conn = nullptr;
		for (int i = 0; i < res->count_connectors && !conn; i++)
		{
			drmModeConnectorPtr c = m_drmModeGetConnector(fd, res->connectors[i]);
			if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
				conn = c;
			else if (c)
				m_drmModeFreeConnector(c);
		}

		if (!conn)
		{
			m_drmModeFreeResources(res);
			close(fd);
			continue;
		}

		// A requested size picks a matching mode; otherwise the panel's preferred one.
		int mode_index = -1;
		for (int i = 0; i < conn->count_modes && mode_index < 0; i++)
		{
			if (m_wi.surface_width != 0 && conn->modes[i].hdisplay == m_wi.surface_width &&
				conn->modes[i].vdisplay == m_wi.surface_height)
			{
				mode_index = i;
			}
		}
		for (int i = 0; i < conn->count_modes && mode_index < 0; i++)
		{
			if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
				mode_index = i;
		}
		m_mode = conn->modes[(mode_index < 0) ? 0 : mode_index];

		// The CRTC the connector already drives, or the first one any of its encoders can.
		u32 crtc_id = 0;
		if (drmModeEncoderPtr enc = conn->encoder_id ? m_drmModeGetEncoder(fd, conn->encoder_id) : nullptr)
		{
			crtc_id = enc->crtc_id;
			m_drmModeFreeEncoder(enc);
		}
		for (int e = 0; e < conn->count_encoders && crtc_id == 0; e++)
		{
			drmModeEncoderPtr enc = m_drmModeGetEncoder(fd, conn->encoders[e]);
			if (!enc)
				continue;
			for (int c = 0; c < res->count_crtcs; c++)
			{
				if (enc->possible_crtcs & (1u << c))
				{
					crtc_id = res->crtcs[c];
					break;
				}
			}
			m_drmModeFreeEncoder(enc);
		}

		m_connector_id = conn->connector_id;
		m_drmModeFreeConnector(conn);
		m_drmModeFreeResources(res);

		if (crtc_id == 0)
		{
			close(fd);
			continue;
		}

		m_gbm_device = m_gbm_create_device(fd);
		if (!m_gbm_device)
		{
			close(fd);
			continue;
		}

		m_drm_fd = fd;
		m_crtc_id = crtc_id;
		m_saved_crtc = m_drmModeGetCrtc(fd, crtc_id);

		m_wi.display_connection = m_gbm_device;
		m_wi.surface_width = m_mode.hdisplay;
		m_wi.surface_height = m_mode.vdisplay;
		m_wi.surface_refresh_rate = static_cast<float>(m_mode.vrefresh);

		Console.WriteLnFmt("EGL KMS: {} connector {} crtc {} mode {}x{}@{}", devices[d], m_connector_id, m_crtc_id,
			m_mode.hdisplay, m_mode.vdisplay, m_mode.vrefresh);
		return true;
	}

	Error::SetStringView(error, "No DRM device with a connected display (is another program holding DRM master?)");
	return false;
}

bool GLContextEGLKMS::OpenFBDev(Error* error)
{
	const char* dev = std::getenv("ARMSX2_FBDEV");
	if (!dev || !dev[0])
		dev = "/dev/fb0";

	const int fd = open(dev, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
	{
		Error::SetStringFmt(error, "Opening {} failed: {}", dev, std::strerror(errno));
		return false;
	}

	fb_var_screeninfo vinfo = {};
	const bool ok = (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == 0);
	close(fd);
	if (!ok || vinfo.xres == 0 || vinfo.yres == 0)
	{
		Error::SetStringFmt(error, "FBIOGET_VSCREENINFO on {} failed", dev);
		return false;
	}

	m_use_gbm = false;
	m_fbdev_window.width = static_cast<unsigned short>(vinfo.xres);
	m_fbdev_window.height = static_cast<unsigned short>(vinfo.yres);
	m_wi.display_connection = nullptr;
	m_wi.surface_width = vinfo.xres;
	m_wi.surface_height = vinfo.yres;

	Console.WriteLnFmt("EGL KMS: using fbdev {} ({}x{})", dev, vinfo.xres, vinfo.yres);
	return true;
}

EGLDisplay GLContextEGLKMS::GetPlatformDisplay(Error* error)
{
	EGLDisplay dpy = EGL_NO_DISPLAY;
	if (m_use_gbm)
	{
		dpy = TryGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, "EGL_KHR_platform_gbm");
		if (dpy == EGL_NO_DISPLAY)
			dpy = TryGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, "EGL_MESA_platform_gbm");
	}

	// libmali takes its native display through plain eglGetDisplay: the gbm
	// device for its gbm build, EGL_DEFAULT_DISPLAY (null) for its fbdev one.
	if (dpy == EGL_NO_DISPLAY)
		dpy = GetFallbackDisplay(error);

	return dpy;
}

EGLSurface GLContextEGLKMS::CreatePlatformSurface(EGLConfig config, void* win, Error* error)
{
	if (!m_use_gbm)
		return CreateFallbackSurface(config, &m_fbdev_window, error);

	DestroyGBMSurface();

	// The gbm surface has to be in the format the EGL config renders, which on
	// Mesa is exactly what EGL_NATIVE_VISUAL_ID says.
	EGLint visual_id = 0;
	if (!eglGetConfigAttrib(m_display, config, EGL_NATIVE_VISUAL_ID, &visual_id) || visual_id == 0)
		visual_id = GBM_FORMAT_XRGB8888;

	m_gbm_surface = m_gbm_surface_create(m_gbm_device, m_mode.hdisplay, m_mode.vdisplay,
		static_cast<u32>(visual_id), GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!m_gbm_surface)
	{
		Error::SetStringFmt(error, "gbm_surface_create({}x{}, 0x{:08X}) failed", m_mode.hdisplay, m_mode.vdisplay,
			static_cast<u32>(visual_id));
		return EGL_NO_SURFACE;
	}

	EGLSurface surface = TryCreatePlatformSurface(config, m_gbm_surface, error);
	if (surface == EGL_NO_SURFACE)
		surface = CreateFallbackSurface(config, m_gbm_surface, error);
	if (surface == EGL_NO_SURFACE)
		DestroyGBMSurface();

	return surface;
}

bool GLContextEGLKMS::ChangeSurface(const WindowInfo& new_wi)
{
	// The frontend's WindowInfo knows nothing of the gbm device we display through.
	WindowInfo wi = new_wi;
	wi.display_connection = m_wi.display_connection;
	wi.surface_width = m_wi.surface_width;
	wi.surface_height = m_wi.surface_height;
	return GLContextEGL::ChangeSurface(wi);
}

void GLContextEGLKMS::ResizeSurface(u32 new_surface_width, u32 new_surface_height)
{
	// The mode is fixed for as long as we hold the display; report the real size.
	GLContextEGL::ResizeSurface(0, 0);
}

bool GLContextEGLKMS::SetSwapInterval(s32 interval)
{
	// Page flips always land on vblank, so there is nothing to change on GBM.
	if (m_use_gbm)
		return true;

	return GLContextEGL::SetSwapInterval(interval);
}

std::unique_ptr<GLContext> GLContextEGLKMS::CreateSharedContext(const WindowInfo& wi, Error* error)
{
	// A second context can't have the display, so it gets no window at all.
	WindowInfo shared_wi;
	shared_wi.type = WindowInfo::Type::Surfaceless;
	shared_wi.surface_width = m_wi.surface_width;
	shared_wi.surface_height = m_wi.surface_height;
	return GLContextEGL::CreateSharedContext(shared_wi, error);
}

bool GLContextEGLKMS::SwapBuffers()
{
	if (!m_use_gbm)
		return GLContextEGL::SwapBuffers();

	if (m_abandoned || m_surface == EGL_NO_SURFACE)
		return false;

	if (!eglSwapBuffers(m_display, m_surface))
		return false;

	gbm_bo* bo = m_gbm_surface_lock_front_buffer(m_gbm_surface);
	if (!bo)
	{
		Console.Error("EGL KMS: gbm_surface_lock_front_buffer() failed");
		return false;
	}

	const u32 fb_id = GetFramebufferForBO(bo);
	if (fb_id == 0)
	{
		m_gbm_surface_release_buffer(m_gbm_surface, bo);
		return false;
	}

	bool flipped = false;
	if (m_crtc_set)
	{
		flipped = (m_drmModePageFlip(m_drm_fd, m_crtc_id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, this) == 0);
		if (!flipped)
			Console.WarningFmt("EGL KMS: drmModePageFlip() failed: {}; falling back to a modeset", std::strerror(errno));
	}

	if (flipped)
	{
		m_next_bo = bo;
		m_flip_pending = true;
		WaitForFlip();
	}
	else
	{
		if (m_drmModeSetCrtc(m_drm_fd, m_crtc_id, fb_id, 0, 0, &m_connector_id, 1, &m_mode) != 0)
		{
			Console.ErrorFmt("EGL KMS: drmModeSetCrtc() failed: {}", std::strerror(errno));
			m_gbm_surface_release_buffer(m_gbm_surface, bo);
			return false;
		}

		m_crtc_set = true;
		if (m_current_bo)
			m_gbm_surface_release_buffer(m_gbm_surface, m_current_bo);
		m_current_bo = bo;
	}

	return true;
}

bool GLContextEGLKMS::WaitForFlip()
{
	if (!m_flip_pending)
		return true;

	drmEventContext ctx = {};
	ctx.version = 2;
	ctx.page_flip_handler = &GLContextEGLKMS::PageFlipHandler;

	while (m_flip_pending)
	{
		pollfd pfd = {m_drm_fd, POLLIN, 0};
		const int ret = poll(&pfd, 1, FLIP_TIMEOUT_MS);
		if (ret < 0 && errno == EINTR)
			continue;
		if (ret <= 0)
		{
			Console.Warning("EGL KMS: timed out waiting for a page flip");
			m_flip_pending = false;
			break;
		}

		m_drmHandleEvent(m_drm_fd, &ctx);
	}

	// The buffer that was on screen is free to render into again.
	if (m_next_bo)
	{
		if (m_current_bo)
			m_gbm_surface_release_buffer(m_gbm_surface, m_current_bo);
		m_current_bo = m_next_bo;
		m_next_bo = nullptr;
	}

	return true;
}

void GLContextEGLKMS::PageFlipHandler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void* data)
{
	static_cast<GLContextEGLKMS*>(data)->m_flip_pending = false;
}

u32 GLContextEGLKMS::GetFramebufferForBO(gbm_bo* bo)
{
	if (FBInfo* info = static_cast<FBInfo*>(m_gbm_bo_get_user_data(bo)))
		return info->fb_id;

	u32 fb_id = 0;
	if (m_drmModeAddFB(m_drm_fd, m_gbm_bo_get_width(bo), m_gbm_bo_get_height(bo), 24, 32, m_gbm_bo_get_stride(bo),
			m_gbm_bo_get_handle(bo).u32, &fb_id) != 0)
	{
		Console.ErrorFmt("EGL KMS: drmModeAddFB() failed: {}", std::strerror(errno));
		return 0;
	}

	m_gbm_bo_set_user_data(bo, new FBInfo{this, fb_id}, &GLContextEGLKMS::BODestroyCallback);
	return fb_id;
}

void GLContextEGLKMS::BODestroyCallback(gbm_bo* bo, void* data)
{
	FBInfo* info = static_cast<FBInfo*>(data);
	if (info->fb_id != 0)
		info->owner->m_drmModeRmFB(info->owner->m_drm_fd, info->fb_id);
	delete info;
}

void GLContextEGLKMS::DestroyGBMSurface()
{
	if (!m_gbm_surface)
		return;

	WaitForFlip();
	if (m_current_bo)
		m_gbm_surface_release_buffer(m_gbm_surface, m_current_bo);
	m_current_bo = nullptr;

	// Destroying the surface frees its buffers, and with them (through
	// BODestroyCallback) the framebuffer on screen - so the next swap modesets.
	m_gbm_surface_destroy(m_gbm_surface);
	m_gbm_surface = nullptr;
	m_crtc_set = false;
}
