// adapted from Piotr Gwiazdowski <gwiazdorrr+github at gmail.com>

#pragma warning(disable: 4244) // disable return implicit conversion warning
#pragma warning(disable: 4305) // disable truncation warning
typedef float real_t;

#include <swizzle/glsl/naive/vector.h>
#include <swizzle/glsl/naive/matrix.h>
#include <swizzle/glsl/texture_functions.h>

typedef swizzle::glsl::naive::vector< int, 2 > ivec2;
typedef swizzle::glsl::naive::vector< real_t, 2 > vec2;
typedef swizzle::glsl::naive::vector< real_t, 3 > vec3;
typedef swizzle::glsl::naive::vector< real_t, 4 > vec4;

typedef swizzle::glsl::naive::matrix< swizzle::glsl::naive::vector, real_t, 2, 2> mat2;
typedef swizzle::glsl::naive::matrix< swizzle::glsl::naive::vector, real_t, 3, 3> mat3;
typedef swizzle::glsl::naive::matrix< swizzle::glsl::naive::vector, real_t, 4, 4> mat4;

//! A really, really simplistic sampler using SDLImage
struct SDL_Surface;
class sampler2D : public swizzle::glsl::texture_functions::tag
{
public:
	enum WrapMode
	{
		Clamp,
		Repeat,
		MirrorRepeat
	};

	typedef vec2 tex_coord_type;

	sampler2D(const char* path, WrapMode wrapMode);
	~sampler2D();
	vec4 sample(vec2 coord);

private:
	SDL_Surface *m_image;
	WrapMode m_wrapMode;

	// do not allow copies to be made
	sampler2D(const sampler2D&);
	sampler2D& operator=(const sampler2D&);
};

// this where the magic happens...
namespace glsl_sandbox
{
	// a nested namespace used when redefining 'inout' and 'out' keywords
	namespace ref
	{
		typedef ::vec2& vec2;
		typedef ::vec3& vec3;
		typedef ::vec4& vec4;
	}

#include <swizzle/glsl/vector_functions.h>

	// constants shaders are using
	real_t time;
	vec2 mouse;
	vec2 resolution;

	// constants some shaders from shader toy are using
	vec2& iResolution = resolution;
	real_t& iGlobalTime = time;
	vec2& iMouse = mouse;

	struct fragment_shader
	{
		vec2 gl_FragCoord;
		vec4 gl_FragColor;
		void mainImage(vec4 &fragColor, const vec2 &fragCoord);
	};

	// change meaning of glsl keywords to match sandbox
#define uniform extern
#define in
#define out ref::
#define inout ref::
#define mainImage fragment_shader::mainImage

#if defined(APP_EGG)
#include "app_egg.h"
#elif defined(APP_RAYTRACER)
#include "app_raytracer.h"
#elif defined(APP_SDF_AO)
#include "app_sdf_ao.h"
#elif defined(APP_CLOUDS)
#include "app_clouds.h"
#elif defined(APP_ATMOSPHERE)
#include "app_atmosphere.h"
#elif defined(APP_2D)
#include "app_2d.h"
#endif

#undef mainImage
#undef in
#undef out
#undef inout
#undef uniform
}

// these headers, especially SDL.h & time.h set up names that are in conflict
// with sandbox'es;
// sandbox should be moved to a separate h/cpp pair, but out of laziness...
// including them
// *after* sandbox solves it too

#include <iostream>
#include <sstream>
#include <SDL.h>
#include <SDL_image.h>
#include <time.h>
#include <memory>
#include <functional>
#ifdef OMP_ENABLED
#include <omp.h>
#endif

//! A handy way of creating (and checking) unique_ptrs of SDL objects
template <class T>
std::unique_ptr< T, std::function<void(T*)> > makeUnique(T* value, std::function<void(T*)> deleter)
{
	if (!value)
	{
		throw std::runtime_error("Null pointer");
	}
	return std::unique_ptr<T, decltype(deleter)>(value, deleter);
}

//! As above, but allows null initialisation
template <class T>
std::unique_ptr< T, std::function<void(T*)> > makeUnique(std::function<void(T*)> deleter)
{
	return std::unique_ptr<T, decltype(deleter)>(nullptr, deleter);
}

//! Just a RAII wrapper around SDL_mutex
struct ScopedLock
{
	SDL_mutex* mutex;

	explicit ScopedLock(SDL_mutex* mutex) : mutex(mutex)
	{
		SDL_LockMutex(mutex);
	}

	template <class T>
	explicit  ScopedLock(std::unique_ptr<SDL_mutex, T>& mutex) : mutex(mutex.get())
	{
		SDL_LockMutex(this->mutex);
	}

	~ScopedLock()
	{
		SDL_UnlockMutex(mutex);
	}
};


//! The surface to draw on.
auto g_surface = makeUnique<SDL_Surface>(SDL_FreeSurface);
//! Mutex used when exchaning frame between threads
auto g_frameHandshakeMutex = makeUnique<SDL_mutex>(SDL_CreateMutex(), SDL_DestroyMutex);
//! Signaled when a frame has been processed
auto m_frameReceivedEvent = makeUnique<SDL_cond>(SDL_CreateCond(), SDL_DestroyCond);
//! Signaled when a frame is ready to be processed
auto m_frameReadyEvent = makeUnique<SDL_cond>(SDL_CreateCond(), SDL_DestroyCond);
//! Additional flag set when a frame becomes ready, in case main thread is not waiting
bool g_frameReady = false;
//! Stop drawing
bool g_cancelDraw = false;
//! Quit!
bool g_quit = false;

#ifdef WRITE_GIF
#include "..\lib\gif-h\gif.h"
#include <time.h>
#endif

//! Thread used for rendering; it invokes the shader
static int renderThread(void*)
{
	while (true)
	{
		auto bmp = g_surface.get();

#if !defined(_DEBUG) && defined(OMP_ENABLED)
#pragma omp parallel 
		{
			int thredsCount = omp_get_num_threads();
			int threadNum = omp_get_thread_num();

			int heightPerThread = bmp->h / thredsCount;
			int heightStart = threadNum * heightPerThread;
			int heightEnd = (threadNum == thredsCount - 1) ? bmp->h : (heightStart + heightPerThread);
#else
		{
				int heightStart = 0;
				int heightEnd = bmp->h;
#endif

				glsl_sandbox::fragment_shader shader;
				for (int y = heightStart; !g_cancelDraw && y < heightEnd; ++y)
				{
					uint8_t * ptr = reinterpret_cast<uint8_t*>(bmp->pixels) + y * bmp->pitch;
					for (int x = 0; x < bmp->w; ++x)
					{
						shader.gl_FragCoord = vec2(static_cast<real_t>(x), bmp->h - 1.0f - y);

						// vvvvvvvvvvvvvvvvvvvvvvvvvv
						// THE SHADER IS INVOKED HERE
						// ^^^^^^^^^^^^^^^^^^^^^^^^^^
						shader.mainImage(shader.gl_FragColor, shader.gl_FragCoord);

						auto color = glsl_sandbox::clamp(shader.gl_FragColor, 0.0f, 1.0f);

#ifdef WRITE_GIF
						*ptr++ = 0;
						*ptr++ = static_cast<uint8_t>(255 * color.r + 0.5f);
						*ptr++ = static_cast<uint8_t>(255 * color.g + 0.5f);
						*ptr++ = static_cast<uint8_t>(255 * color.b + 0.5f);
#else
						*ptr++ = static_cast<uint8_t>(255 * color.r + 0.5f);
						*ptr++ = static_cast<uint8_t>(255 * color.g + 0.5f);
						*ptr++ = static_cast<uint8_t>(255 * color.b + 0.5f);
#endif
					}
				}
			}

		ScopedLock lock(g_frameHandshakeMutex);
		if (g_quit)
		{
			return 0;
		}
		else
		{
			// frame is ready, change bool and raise signal (in case main thread is waiting)
			g_frameReady = true;
			SDL_CondSignal(m_frameReadyEvent.get());

			// wait for the main thread to process the frame
			SDL_CondWait(m_frameReceivedEvent.get(), g_frameHandshakeMutex.get());
			if (g_quit)
			{
				return 0;
			}
		}
	}
}

int main(int argc, char* argv[])
{
	using namespace std;

	// initialise SDLImage
	int flags = IMG_INIT_JPG | IMG_INIT_PNG;
	int initted = IMG_Init(flags);
	if ((initted & flags) != flags)
	{
		cerr << "WARNING: failed to initialise required jpg and png support: " << IMG_GetError() << endl;
	}

	// get initial resolution
	ivec2 initialResolution(SCREEN_WIDTH, SCREEN_HEIGHT);
	if (argc == 2)
	{
		std::stringstream s;
		s << argv[1];
		if (!(s >> initialResolution))
		{
			cerr << "ERROR: unable to parse resolution argument" << endl;
			return 1;
		}
	}

	if (initialResolution.x <= 0 || initialResolution.y < 0)
	{
		cerr << "ERROR: invalid resolution: " << initialResolution << endl;
		return 1;
	}

	cout << "\n";
	cout << "+/-   - increase/decrease time scale\n";
	cout << "lmb   - update glsl_sandbox::mouse\n";
	cout << "space - blit now! (show incomplete render)\n";
	cout << "esc   - quit\n\n";

#ifdef WRITE_GIF
	time_t rawtime;
	time(&rawtime);
	tm timeinfo;
	localtime_s(&timeinfo, &rawtime);

	char gif_file[128];
	strftime(gif_file, 128, "anim-%Y-%m-%d-%H-%M-%S.gif\0", &timeinfo);

	GifWriter gif;
	if (!GifBegin(&gif, gif_file, initialResolution.x, initialResolution.y, 0)) return 1;
#endif

	// it doesn't need cleaning up
	SDL_Surface* screen = nullptr;

	try
	{
#ifdef WRITE_GIF
		int bit_depth = 32;
		int mask_r = 0xff000000;
		int mask_g = 0x00ff0000;
		int mask_b = 0x0000ff00;
#else
		int bit_depth = 24;
		int mask_r = 0x000000ff;
		int mask_g = 0x0000ff00;
		int mask_b = 0x00ff0000;
#endif

		// a function to resize the screen; throws if unsuccessful
		auto resizeOrCreateScreen = [&](int w, int h) -> void
		{
			screen = SDL_SetVideoMode(w, h, bit_depth, SDL_SWSURFACE | SDL_RESIZABLE);
			if (!screen)
			{
				throw std::runtime_error("Unable to set video mode");
			}
		};

		// a function used to resize the surface
		auto resizeOrCreateSurface = [&](int w, int h) -> void
		{
			g_surface.reset(SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, bit_depth, mask_r, mask_g, mask_b, 0));
			if (!g_surface)
			{
				throw std::runtime_error("Unable to create surface");
			}
			// update shader value
			glsl_sandbox::resolution.x = static_cast<real_t>(w);
			glsl_sandbox::resolution.y = static_cast<real_t>(h);
		};

		// initial setup
		if (SDL_Init(SDL_INIT_VIDEO) < 0)
		{
			throw std::runtime_error("Unable to init SDL");
		}
		SDL_EnableKeyRepeat(200, 16);
		SDL_WM_SetCaption("SDL/Swizzle", "SDL/Swizzle");

		resizeOrCreateScreen(initialResolution.x, initialResolution.y);
		resizeOrCreateSurface(initialResolution.x, initialResolution.y);

		real_t timeScale = 1;
		int frame = 0;
		real_t time = 0;
		vec2 mousePosition;
		bool pendingResize = false;
		bool mousePressed = false;


		auto renderThreadInstance = SDL_CreateThread(renderThread, nullptr);

		clock_t begin = clock();

		while (!g_quit)
		{
			bool blitNow = false;

			// process events
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				switch (event.type)
				{
				case SDL_VIDEORESIZE:
					if (event.resize.w != screen->w || event.resize.h != screen->h)
					{
						resizeOrCreateScreen(event.resize.w, event.resize.h);
						ScopedLock lock(g_frameHandshakeMutex);
						g_cancelDraw = pendingResize = true;
					}
					break;
				case SDL_QUIT:
				{
								 ScopedLock lock(g_frameHandshakeMutex);
								 g_quit = g_cancelDraw = true;
				}
					break;
				case SDL_KEYDOWN:
					switch (event.key.keysym.sym)
					{
					case SDLK_SPACE:
						blitNow = true;
						break;
					case SDLK_ESCAPE:
					{
										ScopedLock lock(g_frameHandshakeMutex);
										g_quit = g_cancelDraw = true;
					}
						break;
					case SDLK_PLUS:
					case SDLK_EQUALS:
						timeScale *= 2.0f;
						break;
					case SDLK_MINUS:
						timeScale /= 2.0f;
						break;
					default:
						break;
					}
					break;
				case SDL_MOUSEMOTION:
					if (mousePressed)
					{
						mousePosition.x = static_cast<real_t>(event.button.x);
						mousePosition.y = static_cast<real_t>(g_surface->h - 1 - event.button.y);
					}
					break;
				case SDL_MOUSEBUTTONDOWN:
					mousePressed = true;
					mousePosition.x = static_cast<real_t>(event.button.x);
					mousePosition.y = static_cast<real_t>(g_surface->h - 1 - event.button.y);
					break;
				case SDL_MOUSEBUTTONUP:
					mousePressed = false;
				default:
					break;
				}
			}

			bool doFlip = false;
			{
				ScopedLock lock(g_frameHandshakeMutex);
				if (g_quit)
				{
					if (g_frameReady)
					{
						// unlock waiting thread
						SDL_CondSignal(m_frameReceivedEvent.get());
					}
				}
				// if either the flag is set or variable has been signaled do the blit
				else if (blitNow || g_frameReady || SDL_CondWaitTimeout(m_frameReadyEvent.get(), g_frameHandshakeMutex.get(), 33) == 0)
				{
					doFlip = true;
					SDL_BlitSurface(g_surface.get(), NULL, screen, NULL);

					if (pendingResize)
					{
						resizeOrCreateSurface(screen->w, screen->h);
						pendingResize = false;
					}

					if (!blitNow || g_frameReady)
					{
						// transfer variables (resolution is transfered elsewhere)
						glsl_sandbox::time = time;
						glsl_sandbox::mouse = mousePosition;
						// reset flags
						g_cancelDraw = g_frameReady = false;
						SDL_CondSignal(m_frameReceivedEvent.get());
					}
				}
			}

			if (doFlip)
			{
				++frame;
				SDL_Flip(screen);
#ifdef WRITE_GIF
				GifWriteFrame(&gif, reinterpret_cast<const uint8_t *>(screen->pixels), screen->w, screen->h, 100 / 33);
#endif
			}

			cout << "frame: " << frame << "\t time: " << time << "\t timescale: " << timeScale << "         \r";
			cout.flush();

			clock_t delta = clock() - begin;
			time += static_cast<real_t>(delta / double(CLOCKS_PER_SEC) * timeScale);
			begin = clock();
		}

		// wait for the render thread to stop
		cout << "\nwaiting for the worker thread to finish...";
		SDL_WaitThread(renderThreadInstance, nullptr);
	}
	catch (exception& error)
	{
		cerr << "ERROR: " << error.what() << endl;
	}
	catch (...)
	{
		cerr << "ERROR: Unknown error" << endl;
	}

	SDL_Quit();

#ifdef WRITE_GIF
	GifEnd(&gif);
#endif

	return 0;
}


sampler2D::sampler2D(const char* path, WrapMode wrapMode) : m_wrapMode(wrapMode)
{
	m_image = IMG_Load(path);
	if (!m_image)
	{
		std::cerr << "WARNING: Failed to load texture " << path << "\n";
		std::cerr << "  SDL_Image message: " << IMG_GetError() << "\n";
	}
}

sampler2D::~sampler2D()
{
	if (m_image)
	{
		SDL_FreeSurface(m_image);
		m_image = nullptr;
	}
}

vec4 sampler2D::sample(vec2 coord)
{
	using namespace glsl_sandbox;

	switch (m_wrapMode)
	{
	case Repeat:
		coord = mod(coord, 1);
		break;
	case MirrorRepeat:
		coord = abs(mod(coord - 1, 2) - 1);
		break;
	case Clamp:
	default:
		coord = clamp(coord, 0, 1);
		break;
	}

	// OGL uses left-bottom corner as origin...
	coord.y = 1 - coord.y;

	if (!m_image)
	{
		// checkers
		if (coord.x < 0.5 && coord.y < 0.5 || coord.x > 0.5 && coord.y > 0.5)
		{
			return vec4(1, 0, 0, 1);
		}
		else
		{
			return vec4(0, 1, 0, 1);
		}
	}
	else
	{
		int x = static_cast<int>(coord.x * (m_image->w - 1) + 0.5);
		int y = static_cast<int>(coord.y * (m_image->h - 1) + 0.5);

		auto& format = *m_image->format;
		auto pixelPtr = static_cast<uint8_t*>(m_image->pixels) + (y * m_image->pitch + x * format.BytesPerPixel);
		uint32_t pixel = 0;
		for (size_t i = 0; i < format.BytesPerPixel; ++i)
		{
			pixel |= (pixelPtr[i] << (i * 8));
		}

		int r = (pixel & format.Rmask) >> format.Rshift;
		int g = (pixel & format.Gmask) >> format.Gshift;
		int b = (pixel & format.Bmask) >> format.Bshift;
		int a = format.Amask ? ((pixel & format.Amask) >> format.Ashift) : 255;
		return clamp(vec4(r, g, b, a) / 255.0f, 0.0f, 1.0f);
	}
}