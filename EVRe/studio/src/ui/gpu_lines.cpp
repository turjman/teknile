/* SPDX-License-Identifier: Apache-2.0 */
/* The chart's plot on a graphics card: see gpu_lines.h. */
#include "ui/gpu_lines.h"

#include <utility>

#include <QColor>
#include <QHash>
#include <QObject>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace {

#ifdef Q_OS_WIN
constexpr UINT VENDOR_INTEL = 0x8086;            /* the processor's graphics on the machines this is for */
constexpr SIZE_T DEDICATED_MIN = 512u << 20;     /* a card's own memory, at least: a dedicated one */
constexpr UINT SAMPLES = 4;                      /* antialiasing */
constexpr UINT BUFFERS = 2;                      /* the layer's: one shown, one drawn */
constexpr int PICTURE_FRAMES = 3;                /* a picture not drawn for this many frames leaves the card */
/* the layer's pixels as Qt's pictures hold theirs (QImage::Format_ARGB32_Premultiplied: B, G, R, A in memory), so
 * a picture goes to the card as it is */
constexpr DXGI_FORMAT PIXEL_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

/* A segment is a quad: its corners (along 0..1, across -1..1); the vertex shader places it, widthPx wide and, with
 * caps, as long as the segment plus half the width at each end, so the segments of a line overlap at its points.
 * A picture is the same quad over its pixels, which the pixel shader copies one to one. */
const char *SHADER = R"(
cbuffer view : register(b0) { float4 scale; };      /* 2 / width, 2 / height, half the line's width, caps 0 or 1 */
cbuffer place : register(b1) { float4 picture; };   /* a picture's left, top, width, height in pixels */
Texture2D pixels : register(t0);
struct In { float2 corner : TEXCOORD0; float2 p0 : TEXCOORD1; float2 p1 : TEXCOORD2; float4 color : TEXCOORD3; };
struct Out { float4 pos : SV_POSITION; float4 color : COLOR0; };
Out vs(In i) {
	float2 d = i.p1 - i.p0;
	float len = length(d);
	float2 t = len > 0.0001 ? d / len : float2(1, 0);
	float2 n = float2(-t.y, t.x);
	float2 p = lerp(i.p0, i.p1, i.corner.x) + n * (i.corner.y * scale.z)
			+ t * ((i.corner.x * 2 - 1) * scale.z * scale.w);
	Out o;
	o.pos = float4(p.x * scale.x - 1, 1 - p.y * scale.y, 0, 1);
	o.color = float4(i.color.rgb * i.color.a, i.color.a);
	return o;
}
float4 ps(float4 pos : SV_POSITION, float4 color : COLOR0) : SV_TARGET { return color; }
float4 vsPicture(float2 corner : TEXCOORD0) : SV_POSITION {
	float2 p = picture.xy + float2(corner.x, (corner.y + 1) * 0.5) * picture.zw;
	return float4(p.x * scale.x - 1, 1 - p.y * scale.y, 0, 1);
}
float4 psPicture(float4 pos : SV_POSITION) : SV_TARGET { return pixels.Load(int3(int2(pos.xy - picture.xy), 0)); }
)";

QString hresultText(const QString &what, HRESULT hr) {
	return QObject::tr("%1 failed (0x%2)").arg(what).arg(quint32(hr), 8, 16, QLatin1Char('0'));
}
#endif

} // namespace

struct GpuLines::Impl {
	QString name;
	int dropped = 0;       /* frames let go: the system still busy (present) */
	QSize presented;       /* the last frame's that reached the layer */
	bool dropNext = false; /* tests: the next frame let go as if the system were busy */
#ifdef Q_OS_WIN
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<ID3D11VertexShader> vertexShader, pictureVertexShader;
	ComPtr<ID3D11PixelShader> pixelShader, picturePixelShader;
	ComPtr<ID3D11InputLayout> layout, pictureLayout;
	ComPtr<ID3D11BlendState> blend;
	ComPtr<ID3D11RasterizerState> raster;
	ComPtr<ID3D11Buffer> corners, segments, view, place;
	UINT segmentCapacity = 0;
	/* the layer: over the window `window` (DirectComposition), at `offset` of its client area, showing the swap
	 * chain's frames while `shown`; what is drawn in: 4x antialiased, its size */
	ComPtr<IDCompositionDevice> composition;
	ComPtr<IDCompositionTarget> target;
	ComPtr<IDCompositionVisual> visual;
	HWND window = nullptr;
	QPoint offset;
	bool shown = false;
	ComPtr<IDXGISwapChain1> swapChain;
	QSize size;
	ComPtr<ID3D11Texture2D> msaa;
	ComPtr<ID3D11RenderTargetView> targetView;
	bool drawn = false; /* msaa holds a frame (lastPicture) */
	/* the pictures on the card, by QImage::cacheKey(), and the frame each was last drawn in */
	struct Picture {
		ComPtr<ID3D11ShaderResourceView> view;
		quint64 frame = 0;
	};
	QHash<qint64, Picture> pictures;
	quint64 frame = 0;

	~Impl() {
		if (target && shown) { /* away at once, not when the objects are let go */
			target->SetRoot(nullptr);
			composition->Commit();
		}
	}
	bool makeLayer(HWND to, QString &error);
	bool makeSwapChain(const QSize &newSize, QString &error);
	bool makeTargets(const QSize &newSize, QString &error);
	bool makeSegmentBuffer(UINT count, QString &error);
	ID3D11ShaderResourceView *picture(const QImage &image, QString &error);
#endif
};

GpuLines::GpuLines() : d_(std::make_unique<Impl>()) {}
GpuLines::~GpuLines() = default;

QString GpuLines::name() const { return d_->name; }

#ifdef Q_OS_WIN

QVector<GpuLines::Adapter> GpuLines::adapters() {
	QVector<Adapter> found;
	ComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.GetAddressOf()))))
		return found;
	ComPtr<IDXGIAdapter1> adapter;
	for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; i++) {
		DXGI_ADAPTER_DESC1 desc;
		if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
		Adapter a;
		a.name = QString::fromWCharArray(desc.Description).trimmed();
		a.luidLow = desc.AdapterLuid.LowPart;
		a.luidHigh = desc.AdapterLuid.HighPart;
		a.dedicated = desc.VendorId != VENDOR_INTEL && desc.DedicatedVideoMemory >= DEDICATED_MIN;
		bool listed = false; /* an adapter can be listed once per output */
		for (const Adapter &other : std::as_const(found))
			listed = listed || (other.luidLow == a.luidLow && other.luidHigh == a.luidHigh);
		if (!listed) found << a;
	}
	std::stable_sort(found.begin(), found.end(), [](const Adapter &a, const Adapter &b) { return a.dedicated > b.dedicated; });
	return found;
}

bool GpuLines::open(const Adapter &wanted, QString &error) {
	d_ = std::make_unique<Impl>();
	ComPtr<IDXGIFactory1> factory;
	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.GetAddressOf()));
	if (FAILED(hr)) return error = hresultText(QStringLiteral("CreateDXGIFactory1"), hr), false;
	ComPtr<IDXGIAdapter1> adapter, chosen;
	for (UINT i = 0; !chosen && factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; i++) {
		DXGI_ADAPTER_DESC1 desc;
		if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.AdapterLuid.LowPart == wanted.luidLow
				&& desc.AdapterLuid.HighPart == wanted.luidHigh)
			chosen = adapter;
	}
	if (!chosen) return error = QObject::tr("the adapter %1 is not there").arg(wanted.name), false;
	const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
	hr = D3D11CreateDevice(chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
			d_->device.GetAddressOf(), nullptr, d_->context.GetAddressOf());
	if (FAILED(hr)) return error = hresultText(QStringLiteral("D3D11CreateDevice"), hr), false;

	ComPtr<ID3DBlob> code[4], messages;
	const char *entries[4] = { "vs", "ps", "vsPicture", "psPicture" };
	const char *targets[4] = { "vs_5_0", "ps_5_0", "vs_5_0", "ps_5_0" };
	for (int k = 0; k < 4 && SUCCEEDED(hr); k++)
		hr = D3DCompile(SHADER, std::strlen(SHADER), nullptr, nullptr, nullptr, entries[k], targets[k], 0, 0,
				code[k].GetAddressOf(), messages.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return error = hresultText(QStringLiteral("D3DCompile"), hr), false;
	ID3D11Device *dev = d_->device.Get();
	const D3D11_INPUT_ELEMENT_DESC inputs[] = {
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 1, 8, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "TEXCOORD", 3, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	};
	const float corners[] = { 0, -1, 1, -1, 0, 1, 1, 1 };
	D3D11_BUFFER_DESC cornersDesc = {};
	cornersDesc.ByteWidth = sizeof(corners);
	cornersDesc.Usage = D3D11_USAGE_IMMUTABLE;
	cornersDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	const D3D11_SUBRESOURCE_DATA cornersData = { corners, 0, 0 };
	D3D11_BUFFER_DESC constantsDesc = {};
	constantsDesc.ByteWidth = 16;
	constantsDesc.Usage = D3D11_USAGE_DYNAMIC;
	constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_BLEND_DESC blendDesc = {}; /* premultiplied colours over what is drawn */
	D3D11_RENDER_TARGET_BLEND_DESC &target = blendDesc.RenderTarget[0];
	target.BlendEnable = TRUE;
	target.SrcBlend = target.SrcBlendAlpha = D3D11_BLEND_ONE;
	target.DestBlend = target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	D3D11_RASTERIZER_DESC rasterDesc = {};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.MultisampleEnable = TRUE;
	if (FAILED(hr = dev->CreateVertexShader(code[0]->GetBufferPointer(), code[0]->GetBufferSize(), nullptr,
					   d_->vertexShader.GetAddressOf()))
			|| FAILED(hr = dev->CreatePixelShader(code[1]->GetBufferPointer(), code[1]->GetBufferSize(), nullptr,
							  d_->pixelShader.GetAddressOf()))
			|| FAILED(hr = dev->CreateVertexShader(code[2]->GetBufferPointer(), code[2]->GetBufferSize(), nullptr,
							  d_->pictureVertexShader.GetAddressOf()))
			|| FAILED(hr = dev->CreatePixelShader(code[3]->GetBufferPointer(), code[3]->GetBufferSize(), nullptr,
							  d_->picturePixelShader.GetAddressOf()))
			|| FAILED(hr = dev->CreateInputLayout(inputs, 4, code[0]->GetBufferPointer(), code[0]->GetBufferSize(),
							  d_->layout.GetAddressOf()))
			|| FAILED(hr = dev->CreateInputLayout(inputs, 1, code[2]->GetBufferPointer(), code[2]->GetBufferSize(),
							  d_->pictureLayout.GetAddressOf()))
			|| FAILED(hr = dev->CreateBuffer(&cornersDesc, &cornersData, d_->corners.GetAddressOf()))
			|| FAILED(hr = dev->CreateBuffer(&constantsDesc, nullptr, d_->view.GetAddressOf()))
			|| FAILED(hr = dev->CreateBuffer(&constantsDesc, nullptr, d_->place.GetAddressOf()))
			|| FAILED(hr = dev->CreateBlendState(&blendDesc, d_->blend.GetAddressOf()))
			|| FAILED(hr = dev->CreateRasterizerState(&rasterDesc, d_->raster.GetAddressOf()))) {
		d_ = std::make_unique<Impl>();
		return error = hresultText(QObject::tr("the drawing state"), hr), false;
	}
	d_->name = wanted.name;
	return true;
}

/* The layer of a window: a visual over all of the window's own pixels (topmost), with the swap chain as its content.
 * Not in the window's tree until shown. */
bool GpuLines::Impl::makeLayer(HWND to, QString &error) {
	shown = false;
	visual.Reset();
	target.Reset();
	HRESULT hr = S_OK;
	if (!composition)
		hr = DCompositionCreateDevice(nullptr, __uuidof(IDCompositionDevice), reinterpret_cast<void **>(composition.GetAddressOf()));
	if (SUCCEEDED(hr)) hr = composition->CreateTargetForHwnd(to, TRUE, target.GetAddressOf());
	if (SUCCEEDED(hr)) hr = composition->CreateVisual(visual.GetAddressOf());
	if (SUCCEEDED(hr) && swapChain) hr = visual->SetContent(swapChain.Get());
	if (SUCCEEDED(hr)) hr = visual->SetOffsetX(float(offset.x()));
	if (SUCCEEDED(hr)) hr = visual->SetOffsetY(float(offset.y()));
	if (FAILED(hr)) {
		visual.Reset();
		target.Reset();
		window = nullptr;
		return error = hresultText(QObject::tr("the window's layer"), hr), false;
	}
	window = to;
	return true;
}

/* The layer's swap chain: flip model, so the system shows the newest frame at the screen's next refresh and the
 * window's thread never waits for it. A new size: its buffers made again (the targets are let go first). */
bool GpuLines::Impl::makeSwapChain(const QSize &newSize, QString &error) {
	targetView.Reset();
	HRESULT hr = S_OK;
	if (swapChain) {
		hr = swapChain->ResizeBuffers(0, UINT(newSize.width()), UINT(newSize.height()), DXGI_FORMAT_UNKNOWN, 0);
	} else {
		ComPtr<IDXGIDevice> dxgiDevice;
		ComPtr<IDXGIAdapter> adapter;
		ComPtr<IDXGIFactory2> factory;
		hr = device.As(&dxgiDevice);
		if (SUCCEEDED(hr)) hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
		if (SUCCEEDED(hr)) hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(factory.GetAddressOf()));
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = UINT(newSize.width());
		desc.Height = UINT(newSize.height());
		desc.Format = PIXEL_FORMAT;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = BUFFERS;
		desc.Scaling = DXGI_SCALING_STRETCH; /* the only one a layer's swap chain takes */
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		if (SUCCEEDED(hr)) {
			hr = factory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, swapChain.GetAddressOf());
			if (FAILED(hr)) { /* before Windows 10: the queue of frames in turn */
				desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
				hr = factory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, swapChain.GetAddressOf());
			}
		}
		if (SUCCEEDED(hr) && visual) hr = visual->SetContent(swapChain.Get());
		if (SUCCEEDED(hr) && shown) hr = composition->Commit();
	}
	if (FAILED(hr)) {
		swapChain.Reset();
		return error = hresultText(QObject::tr("the layer's swap chain"), hr), false;
	}
	return makeTargets(newSize, error);
}

bool GpuLines::Impl::makeTargets(const QSize &newSize, QString &error) {
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = UINT(newSize.width());
	desc.Height = UINT(newSize.height());
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = PIXEL_FORMAT;
	desc.SampleDesc.Count = SAMPLES;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET;
	HRESULT hr = device->CreateTexture2D(&desc, nullptr, msaa.ReleaseAndGetAddressOf());
	if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(msaa.Get(), nullptr, targetView.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return error = hresultText(QObject::tr("the picture's textures"), hr), false;
	size = newSize;
	drawn = false;
	return true;
}

bool GpuLines::Impl::makeSegmentBuffer(UINT count, QString &error) {
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = UINT(count * sizeof(Segment));
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	const HRESULT hr = device->CreateBuffer(&desc, nullptr, segments.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return error = hresultText(QObject::tr("the segment buffer"), hr), false;
	segmentCapacity = count;
	return true;
}

/* a picture on the card: the one kept while it is the same QImage, else sent now */
ID3D11ShaderResourceView *GpuLines::Impl::picture(const QImage &image, QString &error) {
	auto it = pictures.find(image.cacheKey());
	if (it == pictures.end()) {
		const QImage pixels = image.format() == QImage::Format_ARGB32_Premultiplied
				? image : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = UINT(pixels.width());
		desc.Height = UINT(pixels.height());
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = PIXEL_FORMAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		const D3D11_SUBRESOURCE_DATA data = { pixels.constBits(), UINT(pixels.bytesPerLine()), 0 };
		ComPtr<ID3D11Texture2D> texture;
		Picture made;
		HRESULT hr = device->CreateTexture2D(&desc, &data, texture.GetAddressOf());
		if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(texture.Get(), nullptr, made.view.GetAddressOf());
		if (FAILED(hr)) {
			error = hresultText(QObject::tr("a picture's texture"), hr);
			return nullptr;
		}
		it = pictures.insert(image.cacheKey(), made);
	}
	it->frame = frame;
	return it->view.Get();
}

bool GpuLines::present(WId window, const QRect &pixels, const Frame &frame, QString &error) {
	Impl &d = *d_;
	auto fail = [&](const QString &why) {
		error = why;
		d_ = std::make_unique<Impl>(); /* closed, its layer gone: the chart draws on the CPU */
		return false;
	};
	if (!d.device) return fail(QObject::tr("no adapter open"));
	if (pixels.isEmpty()) return true; /* nothing to show */
	const QSize size = pixels.size();
	if (HWND(window) != d.window && !d.makeLayer(HWND(window), error)) return fail(error);
	if (size != d.size && !d.makeSwapChain(size, error)) return fail(error);
	HRESULT hr = S_OK;
	if (pixels.topLeft() != d.offset) { /* the window resized, the splitter moved: together with this frame */
		d.offset = pixels.topLeft();
		if (FAILED(hr = d.visual->SetOffsetX(float(d.offset.x()))) || FAILED(hr = d.visual->SetOffsetY(float(d.offset.y())))
				|| (d.shown && FAILED(hr = d.composition->Commit())))
			return fail(hresultText(QObject::tr("the window's layer"), hr));
	}
	d.frame++;

	/* one buffer: the layers' segments one after the other */
	UINT count = 0;
	for (const Layer &layer : frame.layers) count += UINT(layer.segments.size());
	if (count > d.segmentCapacity && !d.makeSegmentBuffer(std::max(count, d.segmentCapacity * 2), error)) return fail(error);
	ID3D11DeviceContext *c = d.context.Get();
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (count > 0) {
		if (FAILED(hr = c->Map(d.segments.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return fail(hresultText(QStringLiteral("Map"), hr));
		auto *to = static_cast<Segment *>(mapped.pData);
		for (const Layer &layer : frame.layers) {
			std::memcpy(to, layer.segments.constData(), size_t(layer.segments.size()) * sizeof(Segment));
			to += layer.segments.size();
		}
		c->Unmap(d.segments.Get(), 0);
	}
	auto setConstants = [&](ID3D11Buffer *buffer, const float (&values)[4]) {
		if (FAILED(hr = c->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		std::memcpy(mapped.pData, values, sizeof(values));
		c->Unmap(buffer, 0);
		return true;
	};

	const QColor background = QColor::fromRgba(frame.background);
	const float clear[4] = { float(background.redF()), float(background.greenF()), float(background.blueF()), 1 };
	c->ClearRenderTargetView(d.targetView.Get(), clear);
	ID3D11RenderTargetView *targets[] = { d.targetView.Get() };
	c->OMSetRenderTargets(1, targets, nullptr);
	c->OMSetBlendState(d.blend.Get(), nullptr, 0xffffffff);
	c->RSSetState(d.raster.Get());
	const D3D11_VIEWPORT viewport = { 0, 0, float(size.width()), float(size.height()), 0, 1 };
	c->RSSetViewports(1, &viewport);
	c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	ID3D11Buffer *constants[] = { d.view.Get(), d.place.Get() };
	const float toClip[2] = { 2.0f / float(size.width()), 2.0f / float(size.height()) };

	/* the layers: each at its own width, its instances one range of the buffer */
	c->IASetInputLayout(d.layout.Get());
	ID3D11Buffer *buffers[] = { d.corners.Get(), d.segments.Get() };
	const UINT strides[] = { 2 * sizeof(float), sizeof(Segment) }, offsets[] = { 0, 0 };
	c->IASetVertexBuffers(0, 2, buffers, strides, offsets);
	c->VSSetShader(d.vertexShader.Get(), nullptr, 0);
	c->VSSetConstantBuffers(0, 1, constants);
	c->PSSetShader(d.pixelShader.Get(), nullptr, 0);
	UINT first = 0;
	for (const Layer &layer : frame.layers) {
		const UINT n = UINT(layer.segments.size());
		if (n == 0) continue;
		if (!setConstants(d.view.Get(), { toClip[0], toClip[1], layer.widthPx / 2, layer.caps ? 1.0f : 0.0f }))
			return fail(hresultText(QStringLiteral("Map"), hr));
		c->DrawInstanced(4, n, 0, first);
		first += n;
	}

	/* the pictures over them, each copied pixel for pixel */
	if (!frame.sprites.isEmpty()) {
		if (!setConstants(d.view.Get(), { toClip[0], toClip[1], 0, 0 })) return fail(hresultText(QStringLiteral("Map"), hr));
		c->IASetInputLayout(d.pictureLayout.Get());
		c->IASetVertexBuffers(0, 1, buffers, strides, offsets);
		c->VSSetShader(d.pictureVertexShader.Get(), nullptr, 0);
		c->VSSetConstantBuffers(0, 2, constants);
		c->PSSetShader(d.picturePixelShader.Get(), nullptr, 0);
		c->PSSetConstantBuffers(1, 1, constants + 1);
		for (const Sprite &sprite : frame.sprites) {
			if (sprite.image.isNull()) continue;
			ID3D11ShaderResourceView *view = d.picture(sprite.image, error);
			if (!view) return fail(error);
			if (!setConstants(d.place.Get(), { float(sprite.at.x()), float(sprite.at.y()), float(sprite.image.width()),
						float(sprite.image.height()) }))
				return fail(hresultText(QStringLiteral("Map"), hr));
			c->PSSetShaderResources(0, 1, &view);
			c->Draw(4, 0);
		}
		ID3D11ShaderResourceView *none = nullptr;
		c->PSSetShaderResources(0, 1, &none);
	}
	for (auto it = d.pictures.begin(); it != d.pictures.end();)
		it = d.frame - it->frame >= PICTURE_FRAMES ? d.pictures.erase(it) : std::next(it);

	/* the layer's buffer: the antialiased frame resolved into it, then shown at the next refresh. A frame the
	 * system is still busy with is not waited for: this one is dropped, the next comes in a few ms */
	ComPtr<ID3D11Texture2D> buffer;
	if (FAILED(hr = d.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(buffer.GetAddressOf()))))
		return fail(hresultText(QStringLiteral("GetBuffer"), hr));
	c->ResolveSubresource(buffer.Get(), 0, d.msaa.Get(), 0, PIXEL_FORMAT);
	d.drawn = true;
	hr = std::exchange(d.dropNext, false) ? DXGI_ERROR_WAS_STILL_DRAWING : d.swapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
	if (hr == DXGI_ERROR_WAS_STILL_DRAWING) d.dropped++;
	else if (FAILED(hr)) return fail(hresultText(QStringLiteral("Present"), hr));
	else d.presented = size;
	return true;
}

bool GpuLines::setShown(bool shown, QString &error) {
	Impl &d = *d_;
	if (shown == d.shown || !d.target) return true; /* no frame presented yet: nothing to show */
	/* Taken away: what the window painted just before is on the screen with it. The system shows a layer's change at
	 * its next refresh, but what GDI drew (Qt's window) a refresh later: the layer waits for that refresh (else the
	 * screen showed the window's new pixels where the layer was, its old ones around it, for a frame). */
	if (!shown) {
		GdiFlush();
		DwmFlush();
	}
	HRESULT hr = d.target->SetRoot(shown ? d.visual.Get() : nullptr);
	if (SUCCEEDED(hr)) hr = d.composition->Commit();
	if (FAILED(hr)) {
		error = hresultText(QObject::tr("the window's layer"), hr);
		d_ = std::make_unique<Impl>();
		return false;
	}
	d.shown = shown;
	return true;
}

bool GpuLines::shown() const { return d_->shown; }

int GpuLines::droppedFrames() const { return d_->dropped; }

QSize GpuLines::presentedSize() const { return d_->presented; }

void GpuLines::dropNextFrame() { d_->dropNext = true; }

/* the last frame: resolved again from its antialiased target (it holds until the next frame), copied to memory the
 * processor reads, and waited for: under the layer while it is not shown yet (the chart paints it), and for tests */
QImage GpuLines::lastPicture() {
	Impl &d = *d_;
	if (!d.device || !d.drawn) return QImage();
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = UINT(d.size.width());
	desc.Height = UINT(d.size.height());
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = PIXEL_FORMAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	ComPtr<ID3D11Texture2D> resolved, staging;
	if (FAILED(d.device->CreateTexture2D(&desc, nullptr, resolved.GetAddressOf()))) return QImage();
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	if (FAILED(d.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) return QImage();
	d.context->ResolveSubresource(resolved.Get(), 0, d.msaa.Get(), 0, PIXEL_FORMAT);
	d.context->CopyResource(staging.Get(), resolved.Get());
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(d.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return QImage();
	const QImage picture = QImage(static_cast<const uchar *>(mapped.pData), d.size.width(), d.size.height(),
			qsizetype(mapped.RowPitch), QImage::Format_RGB32).copy();
	d.context->Unmap(staging.Get(), 0);
	return picture;
}

#else /* no Direct3D: no adapters, the chart draws on the CPU */

QVector<GpuLines::Adapter> GpuLines::adapters() { return {}; }

bool GpuLines::open(const Adapter &, QString &error) {
	error = QObject::tr("no GPU drawing on this system");
	return false;
}

bool GpuLines::present(WId, const QRect &, const Frame &, QString &error) {
	error = QObject::tr("no GPU drawing on this system");
	return false;
}

bool GpuLines::setShown(bool, QString &error) {
	error = QObject::tr("no GPU drawing on this system");
	return false;
}

bool GpuLines::shown() const { return false; }

int GpuLines::droppedFrames() const { return 0; }

QSize GpuLines::presentedSize() const { return QSize(); }

void GpuLines::dropNextFrame() {}

QImage GpuLines::lastPicture() { return QImage(); }

#endif
