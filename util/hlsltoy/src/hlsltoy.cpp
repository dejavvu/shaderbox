// from http://the-witness.net/news/2012/11/scopeexit-in-c11/
template <typename F>
struct ScopeExit {
	ScopeExit(F f) : f(f) {}
	~ScopeExit() { f(); }
	F f;
};
template <typename F>
ScopeExit<F> MakeScopeExit(F f) {
	return ScopeExit<F>(f);
};
#define STRING_JOIN2(arg1, arg2) DO_STRING_JOIN2(arg1, arg2)
#define DO_STRING_JOIN2(arg1, arg2) arg1 ## arg2
#define SCOPE_EXIT(code) \
    auto STRING_JOIN2(scope_exit_, __LINE__) = MakeScopeExit([&](){code;})

#define SafeRelease(T) if (T) { T->Release(); T = NULL; }

#include <d3d11.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <CRTDBG.H>
#include <cstdio>
#include <chrono>

void ShowError(LPCSTR szErrMsg, ID3D10Blob* pExtraErrorMsg = NULL)
{
	constexpr int MAX = 4096;
	char message[MAX];
	sprintf_s(message, MAX, szErrMsg);
	if (pExtraErrorMsg != NULL)
		sprintf_s(message, MAX, "%s\n%s", szErrMsg, (LPCTSTR)pExtraErrorMsg->GetBufferPointer());
	MessageBox(0, message, "Error", MB_ICONERROR);
}

LPCTSTR lpszClassName = "tinyDX11";
LPCTSTR lpszAppName = "hlsltoy";
constexpr int WIDTH = 800;
constexpr int HEIGHT = 600;

DXGI_SWAP_CHAIN_DESC SwapChainDesc =
{
	{ WIDTH, HEIGHT, { 0, 0 }, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED, DXGI_MODE_SCALING_UNSPECIFIED, },  // w, h, refreshrate, format, scanline ordering, scaling
	{ 1, 0 }, // number of multisamples per pixel, quality level
	DXGI_USAGE_RENDER_TARGET_OUTPUT, 1, // buffer usage, buffer count
	0, // output window (must be supplied later)
	1, DXGI_SWAP_EFFECT_DISCARD, 0 // windowed, swap effect, flags
};

// from http://altdevblog.com/2011/08/08/an-interesting-vertex-shader-trick/
CHAR szVertexShader[] =
"float4 main(uint id : SV_VertexID) : SV_Position {"
	"float2 tex = float2((id << 1) & 2, id & 2);"
	"return float4(tex * float2(2, -2) + float2(-1, 1), 0, 1);"
"}";

// from https://msdn.microsoft.com/en-us/library/windows/desktop/ff476521%28v=vs.85%29.aspx
ID3D11Texture2D* CreateTextureCheckboard(ID3D11Device *pd3dDevice, UINT w, UINT h, UINT checkFreq)
{
	UINT *buffer = new UINT[w * h];
	_ASSERT(buffer);
	SCOPE_EXIT(delete[] buffer);

	for (UINT y = 0; y < h; y++)
		for (UINT x = 0; x < w; x++)
			buffer[y*h + x] = ((x & checkFreq) == (y & checkFreq)) ? 0xff000000 : 0xffffffff;

	ID3D11Texture2D *tex = NULL;
	D3D11_TEXTURE2D_DESC tdesc = { w, h, 1 /*multisampled*/, 1 /*mip levels*/, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 } /*quality: no AA*/,
		D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0 /*no CPU access*/, 0 /*flags*/ };
	D3D11_SUBRESOURCE_DATA tbsd = { buffer, w * 4 /*pitch*/, w * h * 4 /*pitch elem in array, unused*/ };

	HRESULT hr = pd3dDevice->CreateTexture2D(&tdesc, &tbsd, &tex);
	_ASSERT(SUCCEEDED(hr));

	return tex;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CLOSE)
		PostQuitMessage(0);
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int __stdcall WinMain(HINSTANCE hThisInstance, HINSTANCE hPrevInstance, LPSTR lpszArgument, int nCmdShow)
{
	LPWSTR *szArglist = NULL;
	SCOPE_EXIT(if (szArglist) LocalFree(szArglist));
	int nArgs = 0;
	szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	if (!szArglist || nArgs < 2)
	{
		ShowError("Minimal DX11 Framework.\n\Usage:\nhlsltoy <shader file>");
		return ERROR_INVALID_COMMAND_LINE;
	}

// win32 window
	HRESULT hr = NULL;
	HINSTANCE hinst = GetModuleHandle(NULL);
	WNDCLASS wc = { CS_HREDRAW | CS_VREDRAW | CS_OWNDC, (WNDPROC)WndProc, 0, 0, hinst, LoadIcon(NULL, IDI_WINLOGO), LoadCursor(NULL, IDC_ARROW), 0, 0, lpszClassName };
	RegisterClass(&wc);
	SCOPE_EXIT(UnregisterClass(lpszClassName, hinst));

	HWND hWnd = CreateWindowExA(0, lpszClassName, lpszAppName, WS_POPUPWINDOW | WS_CAPTION | WS_MINIMIZEBOX | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, 0, 0, hinst, 0);
	SwapChainDesc.OutputWindow = hWnd;

// setting up device
	ID3D11Device* pd3dDevice = NULL;
	ID3D11DeviceContext* pImmediateContext = NULL;
	IDXGISwapChain* pSwapChain = NULL;
	SCOPE_EXIT(SafeRelease(pSwapChain));
	SCOPE_EXIT(SafeRelease(pImmediateContext));
	SCOPE_EXIT(SafeRelease(pd3dDevice));
	hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 
#ifdef _DEBUG
		D3D11_CREATE_DEVICE_DEBUG,
#else
		D3D11_CREATE_DEVICE_SINGLETHREADED,
#endif
		0, 0, D3D11_SDK_VERSION, &SwapChainDesc, &pSwapChain, &pd3dDevice, NULL, &pImmediateContext);	
	if (FAILED(hr)) return hr;

// textures
	ID3D11Texture2D* pTex = CreateTextureCheckboard(pd3dDevice, 128, 128, 16);
	ID3D11ShaderResourceView *pTexV = NULL;
	SCOPE_EXIT(SafeRelease(pTex));
	SCOPE_EXIT(SafeRelease(pTexV));
	hr = pd3dDevice->CreateShaderResourceView(pTex, NULL/*whole res*/, &pTexV);
	_ASSERT(SUCCEEDED(hr));

	ID3D11SamplerState *pSampler = NULL;
	SCOPE_EXIT(SafeRelease(pSampler));
	D3D11_SAMPLER_DESC sSamplerDesc = { D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP , D3D11_TEXTURE_ADDRESS_WRAP, 
		0., 1, D3D11_COMPARISON_NEVER , {1, 1, 1, 1}, -FLT_MAX, FLT_MAX };
	hr = pd3dDevice->CreateSamplerState(&sSamplerDesc, &pSampler);
	_ASSERT(SUCCEEDED(hr));

// backbuffer render target
	ID3D11Texture2D* pBackBuffer = NULL;
	SCOPE_EXIT(SafeRelease(pBackBuffer));
	hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	_ASSERT(SUCCEEDED(hr));

	ID3D11RenderTargetView* pRenderTargetView = NULL;
	SCOPE_EXIT(SafeRelease(pRenderTargetView));
	hr = pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &pRenderTargetView);
	_ASSERT(SUCCEEDED(hr));

	pImmediateContext->OMSetRenderTargets(1, &pRenderTargetView, NULL);

// viewport
	D3D11_VIEWPORT vp = { 0, 0, WIDTH, HEIGHT, 0., 1. };
	pImmediateContext->RSSetViewports(1, &vp);

// rasterizer params
	D3D11_RASTERIZER_DESC rasterizerDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0., 0., FALSE, FALSE, FALSE, FALSE };
	ID3D11RasterizerState* pd3dRasterizerState = NULL;
	SCOPE_EXIT(SafeRelease(pd3dRasterizerState));
	hr = pd3dDevice->CreateRasterizerState(&rasterizerDesc, &pd3dRasterizerState);
	_ASSERT(SUCCEEDED(hr));

// common tracking vars
	ID3D10Blob* pErrorBlob = NULL;
	ID3D10Blob* pBlob = NULL;
	SCOPE_EXIT(SafeRelease(pErrorBlob));
	SCOPE_EXIT(SafeRelease(pBlob));
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS;

// vertex shader
	ID3D11VertexShader* pVS = NULL;
	SCOPE_EXIT(SafeRelease(pVS));
	hr = D3DCompile(szVertexShader, sizeof(szVertexShader), 0, 0, 0, "main", "vs_5_0", flags, 0, &pBlob, &pErrorBlob);
	if (FAILED(hr))
	{
		ShowError("vertex compilation error", pErrorBlob);
		return ERROR_BAD_COMMAND;
	}
	hr = pd3dDevice->CreateVertexShader((DWORD*)pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &pVS);
	_ASSERT(SUCCEEDED(hr));
	SafeRelease(pBlob);
	SafeRelease(pErrorBlob);

// pixel shader
	ID3D11PixelShader* pPS = NULL;
	SCOPE_EXIT(SafeRelease(pPS));
	D3D_SHADER_MACRO PSShaderMacros[2] = { { "HLSL", "5.0" }, { 0, 0 } };
	hr = D3DCompileFromFile(szArglist[1], PSShaderMacros, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &pBlob, &pErrorBlob);
	if (FAILED(hr))
	{
		ShowError("pixel compilation error", pErrorBlob);
		return ERROR_BAD_COMMAND;
	}
	hr = pd3dDevice->CreatePixelShader((DWORD*)pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &pPS);
	_ASSERT(SUCCEEDED(hr));
	SafeRelease(pBlob);
	SafeRelease(pErrorBlob);

// uniforms buffer
	__declspec(align(16)) struct PS_CONSTANT_BUFFER
	{
		DirectX::XMFLOAT2 resolution;
		float time;
		DirectX::XMFLOAT2 mouse;
	};
	PS_CONSTANT_BUFFER PSConstBuff = { {float(WIDTH), float(HEIGHT)}, 0, {0, 0} };
	D3D11_BUFFER_DESC uniformBuffDesc = { sizeof(PS_CONSTANT_BUFFER), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	ID3D11Buffer* pUniformBuff = NULL;
	SCOPE_EXIT(SafeRelease(pUniformBuff));
	D3D11_SUBRESOURCE_DATA pData = { &PSConstBuff, 0, 0 };
	hr = pd3dDevice->CreateBuffer(&uniformBuffDesc, &pData, &pUniformBuff);
	_ASSERT(SUCCEEDED(hr));

// bind stuff to stages
	pImmediateContext->VSSetShader(pVS, NULL, 0);
	pImmediateContext->PSSetShader(pPS, NULL, 0);
	pImmediateContext->PSSetConstantBuffers(0, 1, &pUniformBuff);
	pImmediateContext->PSSetShaderResources(0, 1, &pTexV);
	pImmediateContext->PSSetSamplers(0, 1, &pSampler);

// message pump and rendering
	auto timerStart = std::chrono::high_resolution_clock::now();
	MSG msg = {};
	do
	{		
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		auto timerNow = std::chrono::high_resolution_clock::now();
		auto timeElapsted = std::chrono::duration_cast<std::chrono::milliseconds>(timerNow - timerStart).count();

		pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pImmediateContext->Draw(3, 0);

		pSwapChain->Present(0, 0);

		// update the uniforms - use DISCARD to avoid CPU/GPU fighting for the resource
		// but this means we need to re-send everything
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		hr = pImmediateContext->Map(pUniformBuff, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		_ASSERT(SUCCEEDED(hr));
		// must be careful not to read from data, only write
		volatile PS_CONSTANT_BUFFER *pBuff = (PS_CONSTANT_BUFFER *)mappedResource.pData;
		pBuff->resolution.x = WIDTH;
		pBuff->resolution.y = HEIGHT;
		pBuff->time = timeElapsted / 1000.f;
		pBuff->mouse.x = 0.;
		pBuff->mouse.y = 0.;
		pImmediateContext->Unmap(pUniformBuff, 0);
	} while (true);
	
	return ERROR_SUCCESS;
}
