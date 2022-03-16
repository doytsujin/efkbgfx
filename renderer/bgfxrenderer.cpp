#include <cstdint>
#include <cassert>
#include <cstring>
#include <EffekseerRendererCommon/EffekseerRenderer.IndexBufferBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.VertexBufferBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.ShaderBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.RenderStateBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer_Impl.h>
#include <EffekseerRendererCommon/EffekseerRenderer.RibbonRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.RingRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.SpriteRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.TrackRendererBase.h>
#include <EffekseerRendererCommon/EffekseerRenderer.ModelRendererBase.h>
#include <EffekseerRendererCommon/ModelLoader.h>
#include "bgfxrenderer.h"

#define BGFX(api) m_bgfx->api

#define MAX_PATH 2048

namespace EffekseerRendererBGFX {

static const int SHADERCOUNT = (int)EffekseerRenderer::RendererShaderType::Material;

// VertexBuffer for Renderer
class TransientVertexBuffer : public EffekseerRenderer::VertexBufferBase {
private:
	bgfx_transient_vertex_buffer_t m_buffer;
public:
	TransientVertexBuffer(int size) : VertexBufferBase(size, true) {}
	virtual ~TransientVertexBuffer() override = default;
	bgfx_transient_vertex_buffer_t * GetInterface() {
		return &m_buffer;
	}
	bool RingBufferLock(int32_t size, int32_t& offset, void*& data, int32_t alignment) override {
		assert(!m_isLock);
		m_isLock = true;
		m_offset = size;
		data = m_buffer.data;
		offset = 0;
		(void)alignment;
		return true;
	}
	bool TryRingBufferLock(int32_t size, int32_t& offset, void*& data, int32_t alignment) override {
		// Never used
		return RingBufferLock(size, offset, data, alignment);
	}
	void Lock() override {
		assert(!m_isLock);
		m_isLock = true;
		m_offset = 0;
		m_resource = m_buffer.data;
	}
	void Unlock() override {
		assert(m_isLock);
		m_offset = 0;
		m_resource = nullptr;
		m_isLock = false;
	}
};

// Renderer

class VertexLayout;
using VertexLayoutRef = Effekseer::RefPtr<VertexLayout>;

class VertexLayout : public Effekseer::Backend::VertexLayout {
private:
	Effekseer::CustomVector<Effekseer::Backend::VertexLayoutElement> elements_;
public:
	VertexLayout(const Effekseer::Backend::VertexLayoutElement* elements, int32_t elementCount) {
		elements_.resize(elementCount);
		for (int32_t i = 0; i < elementCount; i++) {
			elements_[i] = elements[i];
		}
	}
	~VertexLayout() = default;
	const Effekseer::CustomVector<Effekseer::Backend::VertexLayoutElement>& GetElements() const	{
		return elements_;
	}
};

class RendererImplemented;
using RendererImplementedRef = ::Effekseer::RefPtr<RendererImplemented>;

class Texture : public Effekseer::Backend::Texture {
private:
	const RendererImplemented *m_render;
	bgfx_texture_handle_t m_handle;
public:
	Texture(const RendererImplemented *render, bgfx_texture_handle_t handle) : m_render(render), m_handle(handle) {};
	~Texture() override;
	bgfx_texture_handle_t GetInterface() const {
		return m_handle;
	}
	bgfx_texture_handle_t RemoveInterface() {
		bgfx_texture_handle_t ret = m_handle;
		m_handle.idx = UINT16_MAX;
		return ret;
	}
};

class GraphicsDevice;
using GraphicsDeviceRef = Effekseer::RefPtr<GraphicsDevice>;

class GraphicsDevice : public Effekseer::Backend::GraphicsDevice {
private:
	RendererImplemented * m_render;
public:
	GraphicsDevice(RendererImplemented *render) : m_render(render) {};
	~GraphicsDevice() override = default;

	// For Renderer::Impl::CreateProxyTextures
	Effekseer::Backend::TextureRef CreateTexture(const Effekseer::Backend::TextureParameter& param, const Effekseer::CustomVector<uint8_t>& initialData) override;
	Effekseer::Backend::VertexLayoutRef CreateVertexLayout(const Effekseer::Backend::VertexLayoutElement* elements, int32_t elementCount) override {
		return Effekseer::MakeRefPtr<VertexLayout>(elements, elementCount);
	}
	// For ModelRenderer
	Effekseer::Backend::VertexBufferRef CreateVertexBuffer(int32_t size, const void* initialData, bool isDynamic) override;
	Effekseer::Backend::IndexBufferRef CreateIndexBuffer(int32_t elementCount, const void* initialData, Effekseer::Backend::IndexBufferStrideType stride) override;

	std::string GetDeviceName() const override {
		return "BGFX";
	}
};

class RenderState : public EffekseerRenderer::RenderStateBase {
private:
	RendererImplemented* m_renderer;
public:
	RenderState(RendererImplemented* renderer) : m_renderer(renderer) {}
	virtual ~RenderState() override = default;
	void Update(bool forced);
};

class TextureLoader : public Effekseer::TextureLoader {
private:
	const RendererImplemented *m_render;
	void *m_ud;
	bgfx_texture_handle_t (*m_loader)(const char *name, int srgb, void *ud);
	void (*m_unloader)(bgfx_texture_handle_t handle, void *ud);
public:
	TextureLoader(const RendererImplemented *render, InitArgs *init) : m_render(render) {
		m_ud = init->ud;
		m_loader = init->texture_load;
		m_unloader = init->texture_unload;
	}
	virtual ~TextureLoader() = default;
	Effekseer::TextureRef Load(const char16_t* path, Effekseer::TextureType textureType) override {
		char buffer[MAX_PATH];
		Effekseer::ConvertUtf16ToUtf8(buffer, MAX_PATH, path);
		int srgb = textureType == Effekseer::TextureType::Color;
		bgfx_texture_handle_t handle = m_loader(buffer, srgb, m_ud);

		auto texture = Effekseer::MakeRefPtr<Effekseer::Texture>();
		texture->SetBackend(Effekseer::MakeRefPtr<Texture>(m_render, handle));
		return texture;
	}
	void Unload(Effekseer::TextureRef texture) override {
		bgfx_texture_handle_t handle = texture->GetBackend().DownCast<Texture>()->RemoveInterface();
		m_unloader(handle, m_ud);
	}
};

class Renderer : public EffekseerRenderer::Renderer {
public:
	Renderer() = default;
	virtual ~Renderer() = default;
	virtual int32_t GetSquareMaxCount() const = 0;
	virtual void SetSquareMaxCount(int32_t count) = 0;
};

class RendererImplemented : public Renderer, public Effekseer::ReferenceObject {
private:
	// Shader
	class Shader : public EffekseerRenderer::ShaderBase {
		friend class RendererImplemented;
	private:
		static const int maxUniform = 64;
		static const int maxSamplers = 8;
		int m_vcbSize = 0;
		int m_pcbSize = 0;
		int m_vsSize = 0;
		int m_fsSize = 0;
		uint8_t * m_vcbBuffer = nullptr;
		uint8_t * m_pcbBuffer = nullptr;
		struct {
			bgfx_uniform_handle_t handle;
			int count;
			void * ptr;
		} m_uniform[maxUniform];
		bgfx_uniform_handle_t m_samplers[maxSamplers];
		bgfx_program_handle_t m_program;
		const RendererImplemented *m_render;
		bgfx_vertex_layout_handle_t m_layout;
	public:
		enum UniformType {
			Vertex,
			Pixel,
			Texture,
		};
		Shader(const RendererImplemented * render, bgfx_vertex_layout_handle_t layout)
			: m_render(render)
			, m_layout(layout) {};
		~Shader() override {
			delete[] m_vcbBuffer;
			delete[] m_pcbBuffer;
			if (m_render)
				m_render->ReleaseShader(this);
		}
		virtual void SetVertexConstantBufferSize(int32_t size) override {
			if (size > 0) {
				assert(m_vcbSize == 0);
				m_vcbSize = size;
				m_vcbBuffer = new uint8_t[size];
			}
		}
		virtual void SetPixelConstantBufferSize(int32_t size) override {
			if (size > 0) {
				assert(m_pcbSize == 0);
				m_pcbSize = size;
				m_pcbBuffer = new uint8_t[size];
			}
		}
		virtual void* GetVertexConstantBuffer() override {
			return m_vcbBuffer;
		}
		virtual void* GetPixelConstantBuffer() override {
			return m_pcbBuffer;
		}
		virtual void SetConstantBuffer() override {
			m_render->SumbitUniforms(this);
		}
		bool isValid() const {
			return m_render != nullptr;
		}
	};
	class StaticIndexBuffer : public Effekseer::Backend::IndexBuffer {
	private:
		const RendererImplemented * m_render;
		bgfx_index_buffer_handle_t m_buffer;
	public:
		StaticIndexBuffer(
			const RendererImplemented *render,
			bgfx_index_buffer_handle_t buffer,
			int stride,
			int count ) : m_render(render) , m_buffer(buffer) {
			strideType_ = stride == 4 ? Effekseer::Backend::IndexBufferStrideType::Stride4 : Effekseer::Backend::IndexBufferStrideType::Stride2;
			elementCount_ = count;
		}
		virtual ~StaticIndexBuffer() override {
			m_render->ReleaseIndexBuffer(this);
		}
		void UpdateData(const void* src, int32_t size, int32_t offset) override { assert(false); }	// Can't Update
		bgfx_index_buffer_handle_t GetInterface() const { return m_buffer; }
	};
	// For ModelRenderer
	class StaticVertexBuffer : public Effekseer::Backend::VertexBuffer {
	private:
		const RendererImplemented * m_render;
		bgfx_vertex_buffer_handle_t m_buffer;
	public:
		StaticVertexBuffer(
			const RendererImplemented *render,
			bgfx_vertex_buffer_handle_t buffer ) : m_render(render) , m_buffer(buffer) {}
		virtual ~StaticVertexBuffer() override {
			m_render->ReleaseVertexBuffer(this);
		}
		void UpdateData(const void* src, int32_t size, int32_t offset) override { assert(false); }	// Can't Update
		bgfx_vertex_buffer_handle_t GetInterface() const { return m_buffer; }
	};
	class ModelRenderer : public EffekseerRenderer::ModelRendererBase {
	private:
		static const int32_t MaxInstanced = 20;
		RendererImplemented* m_render;
		Shader * m_shaders[SHADERCOUNT];
	public:
		ModelRenderer(RendererImplemented* renderer) : m_render(renderer) {
			int i;
			for (i=0;i<SHADERCOUNT;i++) {
				m_shaders[i] = nullptr;
			}
		}
		virtual ~ModelRenderer() override {
			for (auto shader : m_shaders) {
				ES_SAFE_DELETE(shader);
			}
		}
		bool Initialize(struct InitArgs *init) {
			for (auto t : {
				EffekseerRenderer::RendererShaderType::Unlit,
				EffekseerRenderer::RendererShaderType::Lit,
				EffekseerRenderer::RendererShaderType::BackDistortion,
				EffekseerRenderer::RendererShaderType::AdvancedUnlit,
				EffekseerRenderer::RendererShaderType::AdvancedLit,
				EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
			}) {
				bgfx_vertex_layout_t layout;
				m_render->GenVertexLayout(&layout, t);
				Shader * s = m_render->CreateShader(&layout);
				int id = (int)t;
				m_shaders[id] = s;
				const char *shadername = NULL;
				switch (t) {
				case EffekseerRenderer::RendererShaderType::Unlit :
					shadername = "model_unlit";
					break;
				case EffekseerRenderer::RendererShaderType::Lit :
					shadername = "model_lit";
					break;
				case EffekseerRenderer::RendererShaderType::BackDistortion :
					shadername = "model_distortion";
					break;
				case EffekseerRenderer::RendererShaderType::AdvancedUnlit :
					shadername = "model_adv_unlit";
					break;
				case EffekseerRenderer::RendererShaderType::AdvancedLit :
					shadername = "model_adv_lit";
					break;
				case EffekseerRenderer::RendererShaderType::AdvancedBackDistortion :
					shadername = "model_adv_distortion";
					break;
				default:
					assert(false);
					break;
				}
				m_render->InitShader(s,
					m_render->LoadShader(NULL, shadername, "vs"),
					m_render->LoadShader(NULL, shadername, "fs"));
			}
			for (int i=0;i<SHADERCOUNT;i++) {
				Shader * s = m_shaders[i];
				if (!s->isValid())
					return false;
			}
			for (auto t : {
				EffekseerRenderer::RendererShaderType::Unlit,
				EffekseerRenderer::RendererShaderType::Lit,
				EffekseerRenderer::RendererShaderType::BackDistortion,
			}) {
				Shader * s = m_shaders[(int)t];
				typedef EffekseerRenderer::ModelRendererVertexConstantBuffer<MaxInstanced> VCB;
				s->SetVertexConstantBufferSize(sizeof(VCB));
#define VUNIFORM(uname, fname) m_render->AddUniform(s, uname, Shader::UniformType::Vertex, offsetof(VCB, fname));
					VUNIFORM("u_Camera", CameraMatrix)
					VUNIFORM("u_Model", ModelMatrix)
					VUNIFORM("u_ModelUV", ModelUV)
					VUNIFORM("u_ModelColor", ModelColor)
					VUNIFORM("u_LightDirection", LightDirection)
					VUNIFORM("u_LightColor", LightColor)
					VUNIFORM("u_LightAmbientColor", LightAmbientColor)
					VUNIFORM("u_UVInversed", UVInversed)
#undef VUNIFORM
			}
			for (auto t : {
				EffekseerRenderer::RendererShaderType::AdvancedUnlit,
				EffekseerRenderer::RendererShaderType::AdvancedLit,
				EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
			}) {
				Shader * s = m_shaders[(int)t];
				typedef EffekseerRenderer::ModelRendererAdvancedVertexConstantBuffer<MaxInstanced> VCB;
				s->SetVertexConstantBufferSize(sizeof(VCB));
#define VUNIFORM(uname, fname) m_render->AddUniform(s, uname, Shader::UniformType::Vertex, offsetof(VCB, fname));
					VUNIFORM("u_Camera", CameraMatrix)
					VUNIFORM("u_Model", ModelMatrix)
					VUNIFORM("u_ModelUV", ModelUV)
					VUNIFORM("u_ModelAlphaUV", ModelAlphaUV)
					VUNIFORM("u_ModelUVDistortionUV", ModelUVDistortionUV)
					VUNIFORM("u_ModelBlendUV", ModelBlendUV)
					VUNIFORM("u_ModelBlendAlphaUV", ModelBlendAlphaUV)
					VUNIFORM("u_ModelBlendUVDistortionUV", ModelBlendUVDistortionUV)
					VUNIFORM("u_ModelFlipbookParameter", ModelFlipbookParameter)
					VUNIFORM("u_ModelFlipbookIndexAndNextRate", ModelFlipbookIndexAndNextRate)
					VUNIFORM("u_ModelAlphaThreshold", ModelAlphaThreshold)
					VUNIFORM("u_ModelColor", ModelColor)
					VUNIFORM("u_LightDirection", LightDirection)
					VUNIFORM("u_LightColor", LightColor)
					VUNIFORM("u_LightAmbientColor", LightAmbientColor)
					VUNIFORM("u_UVInversed", UVInversed)
#undef VUNIFORM
			}
			m_render->SetPixelConstantBuffer(m_shaders);
			return true;
		}
		void BeginRendering(const Effekseer::ModelRenderer::NodeParameter& parameter, int32_t count, void* userData) override {
			BeginRendering_(m_render, parameter, count, userData);
		}
		virtual void Rendering(const Effekseer::ModelRenderer::NodeParameter& parameter, const Effekseer::ModelRenderer::InstanceParameter& instanceParameter, void* userData) override {
			Rendering_<RendererImplemented>(m_render, parameter, instanceParameter, userData);
		}
		void EndRendering(const Effekseer::ModelRenderer::NodeParameter& parameter, void* userData) override {
			Effekseer::ModelRef model = nullptr;

			if (parameter.IsProceduralMode)
				model = parameter.EffectPointer->GetProceduralModel(parameter.ModelIndex);
			else
				model = parameter.EffectPointer->GetModel(parameter.ModelIndex);
			if (!m_render->StoreModelToGPU(model)) {
				return;
			}
			Shader * shader_ad_lit_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedLit];
			Shader * shader_ad_unlit_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedUnlit];
			Shader * shader_ad_distortion_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::AdvancedBackDistortion];
			Shader * shader_lit_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::Lit];
			Shader * shader_unlit_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::Unlit];
			Shader * shader_distortion_ = m_shaders[(int)EffekseerRenderer::RendererShaderType::BackDistortion];
			EndRendering_<RendererImplemented, Shader, Effekseer::Model, true, MaxInstanced>(
				m_render, shader_ad_lit_, shader_ad_unlit_, shader_ad_distortion_, shader_lit_, shader_unlit_, shader_distortion_, parameter, userData);
		}
	};
	GraphicsDeviceRef m_device = nullptr;
	bgfx_interface_vtbl_t * m_bgfx = nullptr;
	EffekseerRenderer::RenderStateBase* m_renderState = nullptr;
	bool m_restorationOfStates = true;
	EffekseerRenderer::StandardRenderer<RendererImplemented, Shader>* m_standardRenderer = nullptr;
	EffekseerRenderer::DistortingCallback* m_distortingCallback = nullptr;
	StaticIndexBuffer* m_indexBuffer = nullptr;
	bgfx_vertex_buffer_handle_t m_currentVertexBuffer;
	TransientVertexBuffer* m_vertexBuffer = nullptr;
	Shader* m_currentShader = nullptr;
	int32_t m_squareMaxCount = 0;
	int32_t m_indexBufferStride = 2;
	bgfx_view_id_t m_viewid = 0;
	bgfx_vertex_layout_t m_maxlayout;
	bgfx_vertex_layout_t m_modellayout;
	bgfx_vertex_layout_handle_t m_currentlayout;
	Shader * m_shaders[SHADERCOUNT];
	InitArgs m_initArgs;

	//! because gleDrawElements has only index offset
	int32_t GetIndexSpriteCount() const {
		int vsSize = EffekseerRenderer::GetMaximumVertexSizeInAllTypes() * m_squareMaxCount * 4;

		size_t size = sizeof(EffekseerRenderer::SimpleVertex);
		size = (std::min)(size, sizeof(EffekseerRenderer::DynamicVertex));
		size = (std::min)(size, sizeof(EffekseerRenderer::LightingVertex));

		return (int32_t)(vsSize / size / 4 + 1);
	}
	StaticIndexBuffer * CreateIndexBuffer(const bgfx_memory_t *mem, int stride) {
		bgfx_index_buffer_handle_t handle = BGFX(create_index_buffer)(mem, stride == 4 ? BGFX_BUFFER_INDEX32 : BGFX_BUFFER_NONE);
		return new StaticIndexBuffer(this, handle, stride, mem->size / stride);
	}
	void InitIndexBuffer() {
		int n = GetIndexSpriteCount();
		int i,j;
		const bgfx_memory_t *mem = BGFX(alloc)(n * 6 * m_indexBufferStride);
		uint8_t * ptr = mem->data;
		for (i=0;i<n;i++) {
			int buf[6] = {
				3 + 4 * i,
				1 + 4 * i,
				0 + 4 * i,
				3 + 4 * i,
				0 + 4 * i,
				2 + 4 * i,
			};
			if (m_indexBufferStride == 2) {
				uint16_t * dst = (uint16_t *)ptr;
				for (j=0;j<6;j++)
					dst[j] = (uint16_t)buf[j];
			} else {
				memcpy(ptr, buf, sizeof(buf));
			}
			ptr += 6 * m_indexBufferStride;
		}
		if (m_indexBuffer)
			delete m_indexBuffer;

		m_indexBuffer = CreateIndexBuffer(mem, m_indexBufferStride);
	}
	void InitVertexLayoutModel() {
		bgfx_vertex_layout_t *layout = &m_modellayout;
		BGFX(vertex_layout_begin)(layout, BGFX_RENDERER_TYPE_NOOP);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_NORMAL, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_BITANGENT, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_TANGENT, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			BGFX(vertex_layout_add)(layout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
		BGFX(vertex_layout_end)(layout);
	}
	void GenVertexLayout(bgfx_vertex_layout_t *layout, EffekseerRenderer::RendererShaderType t) const {
		VertexLayoutRef v = EffekseerRenderer::GetVertexLayout(m_device, t).DownCast<VertexLayout>();
		const auto &elements = v->GetElements();
		BGFX(vertex_layout_begin)(layout, BGFX_RENDERER_TYPE_NOOP);
		for (int i = 0; i < elements.size(); i++) {
			const auto &e = elements[i];
			bgfx_attrib_t attrib = BGFX_ATTRIB_POSITION;
			uint8_t num = 0;
			bgfx_attrib_type_t type = BGFX_ATTRIB_TYPE_FLOAT;
			bool normalized = false;
			bool asInt = false;
			switch (e.Format) {
			case Effekseer::Backend::VertexLayoutFormat::R32_FLOAT :
				num = 1;
				type = BGFX_ATTRIB_TYPE_FLOAT;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R32G32_FLOAT :
				num = 2;
				type = BGFX_ATTRIB_TYPE_FLOAT;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R32G32B32_FLOAT :
				num = 3;
				type = BGFX_ATTRIB_TYPE_FLOAT;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R32G32B32A32_FLOAT :
				num = 4;
				type = BGFX_ATTRIB_TYPE_FLOAT;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R8G8B8A8_UNORM :
				num = 4;
				type = BGFX_ATTRIB_TYPE_UINT8;
				normalized = true;
				break;
			case Effekseer::Backend::VertexLayoutFormat::R8G8B8A8_UINT :
				num = 4;
				type = BGFX_ATTRIB_TYPE_UINT8;
				break;
			}
			if (e.SemanticName == "POSITION") {
				attrib = BGFX_ATTRIB_POSITION;
			} else if (e.SemanticName == "NORMAL") {
				switch (e.SemanticIndex) {
				case 0:
					attrib = BGFX_ATTRIB_COLOR0;
					break;
				case 1:
					attrib = BGFX_ATTRIB_NORMAL;
					break;
				case 2:
					attrib = BGFX_ATTRIB_TANGENT;
					break;
				case 3:
					attrib = BGFX_ATTRIB_BITANGENT;
					break;
				case 4:
					attrib = BGFX_ATTRIB_COLOR1;
					break;
				case 5:
					attrib = BGFX_ATTRIB_COLOR2;
					break;
				default:
					attrib = BGFX_ATTRIB_COLOR3;
					break;
				}
			} else if (e.SemanticName == "TEXCOORD") {
				attrib = (bgfx_attrib_t)((int)BGFX_ATTRIB_TEXCOORD0 + e.SemanticIndex);
			}
			BGFX(vertex_layout_add)(layout, attrib, num, type, normalized, asInt);
		}
		BGFX(vertex_layout_end)(layout);
	}
	void InitVertexBuffer() {
		m_vertexBuffer = new TransientVertexBuffer(m_squareMaxCount * m_maxlayout.stride);
	}
	void SetPixelConstantBuffer(Shader *shaders[]) const {
		for (auto t: {
			EffekseerRenderer::RendererShaderType::Unlit,
			// EffekseerRenderer::RendererShaderType::Lit,
			// EffekseerRenderer::RendererShaderType::AdvancedUnlit,
			// EffekseerRenderer::RendererShaderType::AdvancedLit,
		}) {
			int id = (int)t;
			Shader * s = shaders[id];
			s->SetPixelConstantBufferSize(sizeof(EffekseerRenderer::PixelConstantBuffer));
#define PUNIFORM(fname) AddUniform(s, "u_" #fname, Shader::UniformType::Pixel, offsetof(EffekseerRenderer::PixelConstantBuffer, fname));
			PUNIFORM(LightDirection)
			PUNIFORM(LightColor)
			PUNIFORM(LightAmbientColor)
			PUNIFORM(FlipbookParam)
			PUNIFORM(UVDistortionParam)
			PUNIFORM(BlendTextureParam)
			PUNIFORM(CameraFrontDirection)
			PUNIFORM(FalloffParam)
			PUNIFORM(EmmisiveParam)
			PUNIFORM(EdgeParam)
			PUNIFORM(SoftParticleParam)
			PUNIFORM(UVInversedBack)
			PUNIFORM(MiscFlags)
#undef PUNIFORM
		}
// 		for (auto t: {
// 			EffekseerRenderer::RendererShaderType::BackDistortion,
// 			EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
// 		}) {
// 			int id = (int)t;
// 			Shader * s = shaders[id];
// 			s->SetPixelConstantBufferSize(sizeof(EffekseerRenderer::PixelConstantBufferDistortion));
// #define PUNIFORM(fname) AddUniform(s, "u_" #fname, Shader::UniformType::Pixel, offsetof(EffekseerRenderer::PixelConstantBufferDistortion, fname));
// 			PUNIFORM(DistortionIntencity)
// 			PUNIFORM(UVInversedBack)
// 			PUNIFORM(FlipbookParam)
// 			PUNIFORM(BlendTextureParam)
// 			PUNIFORM(SoftParticleParam)
// #undef PUNIFORM
// 		}
	}
	bool InitShaders(struct InitArgs *init) {
		m_initArgs = *init;
		m_maxlayout.stride = 0;
		for (auto t : {
			EffekseerRenderer::RendererShaderType::Unlit,
			// EffekseerRenderer::RendererShaderType::Lit,
			// EffekseerRenderer::RendererShaderType::BackDistortion,
			// EffekseerRenderer::RendererShaderType::AdvancedUnlit,
			// EffekseerRenderer::RendererShaderType::AdvancedLit,
			// EffekseerRenderer::RendererShaderType::AdvancedBackDistortion,
		}) {
			bgfx_vertex_layout_t layout;
			GenVertexLayout(&layout, t);
			if (layout.stride > m_maxlayout.stride) {
				m_maxlayout = layout;
			}
			Shader * s = new Shader(this, BGFX(create_vertex_layout)(&layout));
			int id = (int)t;
			m_shaders[id] = s;
			const char *shadername = NULL;
			switch (t) {
			case EffekseerRenderer::RendererShaderType::Unlit :
				shadername = "sprite_unlit";
				break;
			case EffekseerRenderer::RendererShaderType::Lit :
				shadername = "sprite_lit";
				break;
			case EffekseerRenderer::RendererShaderType::BackDistortion :
				shadername = "sprite_distortion";
				break;
			case EffekseerRenderer::RendererShaderType::AdvancedUnlit :
				shadername = "sprite_adv_unlit";
				break;
			case EffekseerRenderer::RendererShaderType::AdvancedLit :
				shadername = "sprite_adv_lit";
				break;
			case EffekseerRenderer::RendererShaderType::AdvancedBackDistortion :
				shadername = "sprite_adv_distortion";
				break;
			default:
				assert(false);
				break;
			}
			InitShader(s,
				LoadShader(NULL, shadername, "vs"),
				LoadShader(NULL, shadername, "fs"));
			s->SetVertexConstantBufferSize(sizeof(EffekseerRenderer::StandardRendererVertexBuffer));
			AddUniform(s, "u_Camera", Shader::UniformType::Vertex,
				offsetof(EffekseerRenderer::StandardRendererVertexBuffer, constantVSBuffer[0]));
			AddUniform(s, "u_CameraProj", Shader::UniformType::Vertex,
				offsetof(EffekseerRenderer::StandardRendererVertexBuffer, constantVSBuffer[1]));
			AddUniform(s, "u_UVInversed", Shader::UniformType::Vertex,
				offsetof(EffekseerRenderer::StandardRendererVertexBuffer, uvInversed));
			AddUniform(s, "u_vsFlipbookParameter", Shader::UniformType::Vertex,
				offsetof(EffekseerRenderer::StandardRendererVertexBuffer, flipbookParameter));
			AddUniform(s, "s_sampler_colorTex", Shader::UniformType::Texture, 0);
		}
		for (int i=0;i<SHADERCOUNT;i++) {
			Shader * s = m_shaders[i];
			if (s && !s->isValid())
				return false;
		}
		SetPixelConstantBuffer(m_shaders);
		return true;
	}
public:
	RendererImplemented() {
		m_device = Effekseer::MakeRefPtr<GraphicsDevice>(this);
		int i;
		m_currentlayout.idx = UINT16_MAX;
		for (i=0;i<SHADERCOUNT;i++) {
			m_shaders[i] = nullptr;
		}
	}
	~RendererImplemented() {
		GetImpl()->DeleteProxyTextures(this);

		ES_SAFE_DELETE(m_distortingCallback);
		ES_SAFE_DELETE(m_standardRenderer);
		ES_SAFE_DELETE(m_renderState);
		for (auto shader : m_shaders) {
			ES_SAFE_DELETE(shader);
		}
		ES_SAFE_DELETE(m_indexBuffer);
		ES_SAFE_DELETE(m_vertexBuffer);
	}

	void OnLostDevice() override {}
	void OnResetDevice() override {}

	bool Initialize(struct InitArgs *init) {
		m_bgfx = init->bgfx;
		if (!InitShaders(init)) {
			return false;
		}
		InitVertexLayoutModel();
		m_viewid = init->viewid;
		m_squareMaxCount = init->squareMaxCount;
		if (GetIndexSpriteCount() * 4 > 65536) {
			m_indexBufferStride = 4;
		}
		InitIndexBuffer();
		InitVertexBuffer();
		m_renderState = new RenderState(this);
		
		m_standardRenderer = new EffekseerRenderer::StandardRenderer<RendererImplemented, Shader>(this);

		GetImpl()->isSoftParticleEnabled = true;
		GetImpl()->CreateProxyTextures(this);
		return true;
	}
	void SetRestorationOfStatesFlag(bool flag) override {
		m_restorationOfStates = flag;
	}
	bool BeginRendering() override {
		// Alloc TransientVertexBuffer
		bgfx_transient_vertex_buffer_t * tvb = m_vertexBuffer->GetInterface();
		BGFX(alloc_transient_vertex_buffer)(tvb, m_squareMaxCount, &m_maxlayout);

		GetImpl()->CalculateCameraProjectionMatrix();

		// todo:	currentTextures_.clear();
		m_renderState->GetActiveState().Reset();
		// todo:	m_renderState->Update(true);
		m_renderState->GetActiveState().TextureIDs.fill(0);

		m_standardRenderer->ResetAndRenderingIfRequired();
		return true;
	}
	bool EndRendering() override {
		m_standardRenderer->ResetAndRenderingIfRequired();
		return true;
	}
	TransientVertexBuffer* GetVertexBuffer() {
		return m_vertexBuffer;
	}
	StaticIndexBuffer* GetIndexBuffer() {
		return m_indexBuffer;
	}
	int32_t GetSquareMaxCount() const override {
		return m_squareMaxCount;
	}
	void SetSquareMaxCount(int32_t count) override {
		m_squareMaxCount = count;
		InitIndexBuffer();
	}
	EffekseerRenderer::RenderStateBase* GetRenderState() {
		return m_renderState;
	}

	Effekseer::SpriteRendererRef CreateSpriteRenderer() override {
		return Effekseer::SpriteRendererRef(new EffekseerRenderer::SpriteRendererBase<RendererImplemented, false>(this));
	}
	Effekseer::RibbonRendererRef CreateRibbonRenderer() override {
		return Effekseer::RibbonRendererRef(new EffekseerRenderer::RibbonRendererBase<RendererImplemented, false>(this));
	}
	Effekseer::RingRendererRef CreateRingRenderer() override {
		return Effekseer::RingRendererRef(new EffekseerRenderer::RingRendererBase<RendererImplemented, false>(this));
	}
	Effekseer::ModelRendererRef CreateModelRenderer() override {
		return Effekseer::MakeRefPtr<ModelRenderer>(this);
	}
	Effekseer::TrackRendererRef CreateTrackRenderer() override {
		return Effekseer::TrackRendererRef(new EffekseerRenderer::TrackRendererBase<RendererImplemented, false>(this));
	}
	Effekseer::TextureLoaderRef CreateTextureLoader(Effekseer::FileInterfaceRef fileInterface = nullptr) {
		return Effekseer::MakeRefPtr<TextureLoader>(this, &m_initArgs);
	}
	Effekseer::ModelLoaderRef CreateModelLoader(::Effekseer::FileInterfaceRef fileInterface = nullptr) {
		// todo: add our model loader (model loader callback in InitArgs)
		return Effekseer::MakeRefPtr<EffekseerRenderer::ModelLoader>(m_device, fileInterface);
	}
	Effekseer::MaterialLoaderRef CreateMaterialLoader(::Effekseer::FileInterfaceRef fileInterface = nullptr) {
		// todo: return Effekseer::MakeRefPtr<MaterialLoader>(m_device, fileInterface);
		return nullptr;
	}
	EffekseerRenderer::DistortingCallback* GetDistortingCallback() override {
		return m_distortingCallback;
	}
	void SetDistortingCallback(EffekseerRenderer::DistortingCallback* callback) override {
		ES_SAFE_DELETE(m_distortingCallback);
		m_distortingCallback = callback;
	}
	EffekseerRenderer::StandardRenderer<RendererImplemented, Shader>* GetStandardRenderer() {
		return m_standardRenderer;
	}
	// Only one transient vb, set in DrawSprites() with layout
	void SetVertexBuffer(TransientVertexBuffer* vertexBuffer, int32_t stride) {}
	// For ModelRenderer, See ModelRendererBase
	void SetVertexBuffer(const Effekseer::Backend::VertexBufferRef& vertexBuffer, int32_t stride) {
		(void)stride;
		m_currentVertexBuffer = vertexBuffer.DownCast<StaticVertexBuffer>()->GetInterface();
	}
	void SetIndexBuffer(StaticIndexBuffer* indexBuffer) {
		assert(indexBuffer == m_indexBuffer);
		//BGFX(set_index_buffer)(indexBuffer->GetInterface(), 0, UINT32_MAX);
	}
	void SetIndexBuffer(const Effekseer::Backend::IndexBufferRef& indexBuffer) const {
		bgfx_index_buffer_handle_t ib = indexBuffer.DownCast<StaticIndexBuffer>()->GetInterface();
		BGFX(set_index_buffer)(ib, 0, UINT32_MAX);
	}
	void SetLayout(Shader* shader) {
		m_currentlayout = shader->m_layout;
	}
	void DrawSprites(int32_t spriteCount, int32_t vertexOffset) {
		BGFX(set_transient_vertex_buffer_with_layout)(0, m_vertexBuffer->GetInterface(), 0, spriteCount*4, m_currentlayout);
		const uint32_t indexCount = spriteCount * 6;
		BGFX(set_index_buffer)(m_indexBuffer->GetInterface(), 0, indexCount);
		BGFX(submit)({m_viewid}, m_currentShader->m_program, 0, BGFX_DISCARD_ALL);
	}
	void DrawPolygon(int32_t vertexCount, int32_t indexCount) {
		BGFX(set_vertex_buffer_with_layout)(0, m_currentVertexBuffer, 0, vertexCount, m_currentlayout);
		//BGFX(set_index_buffer)();
		BGFX(submit)({m_viewid}, m_currentShader->m_program, 0, BGFX_DISCARD_ALL);
		// todo:
	}
	void DrawPolygonInstanced(int32_t vertexCount, int32_t indexCount, int32_t instanceCount) {
		assert(false);
		// todo:
	}
	Shader* GetShader(EffekseerRenderer::RendererShaderType type) const {
		int n = (int)type;
		if (n<0 || n>= SHADERCOUNT)
			return nullptr;
		return m_shaders[n];
	}
	void BeginShader(Shader* shader) {
		assert(m_currentShader == nullptr);
		m_currentShader = shader;
	}
	void EndShader(Shader* shader) {
		assert(m_currentShader == shader);
		m_currentShader = nullptr;
	}
	void SetVertexBufferToShader(const void* data, int32_t size, int32_t dstOffset) {
		assert(m_currentShader != nullptr);
		auto p = static_cast<uint8_t*>(m_currentShader->GetVertexConstantBuffer()) + dstOffset;
		memcpy(p, data, size);
	}
	void SetPixelBufferToShader(const void* data, int32_t size, int32_t dstOffset) {
		assert(m_currentShader != nullptr);
		auto p = static_cast<uint8_t*>(m_currentShader->GetPixelConstantBuffer()) + dstOffset;
		memcpy(p, data, size);
	}
	void SetTextures(Shader* shader, Effekseer::Backend::TextureRef* textures, int32_t count) {
		for (int32_t ii=0; ii<count; ++ii){
			auto tex = textures[ii].DownCast<EffekseerRendererBGFX::Texture>();
			auto sampler = shader->m_samplers[ii];
			if (BGFX_HANDLE_IS_VALID(sampler)){
				const auto &state = m_renderState->GetActiveState();
				uint32_t flags = BGFX_SAMPLER_NONE;	// default min/mag/mip as 'linear' and uv address as 'repeat'
				if (state.TextureFilterTypes[ii] == Effekseer::TextureFilterType::Nearest){
					flags |= BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
				}
				if (state.TextureWrapTypes[ii] == Effekseer::TextureWrapType::Clamp){
					flags |= BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
				}
				BGFX(set_texture)(ii, sampler, tex->GetInterface(), flags);
			}
		}
	}
	void ResetRenderState() override {
		m_renderState->GetActiveState().Reset();
		m_renderState->Update(true);
	}
	void SetCurrentState(uint64_t state) {
		BGFX(set_state)(state, 0);
	}
	Effekseer::Backend::GraphicsDeviceRef GetGraphicsDevice() const override {
		return m_device;
	}
	virtual int GetRef() override { return Effekseer::ReferenceObject::GetRef(); }
	virtual int AddRef() override { return Effekseer::ReferenceObject::AddRef(); }
	virtual int Release() override { return Effekseer::ReferenceObject::Release(); }

	bgfx_shader_handle_t LoadShader(const char *mat, const char *name, const char *type) const {
		return m_initArgs.shader_load(mat, name, type, m_initArgs.ud);
	}
	Shader * CreateShader(const bgfx_vertex_layout_t *layout) const {
		return new Shader(this, BGFX(create_vertex_layout)(layout));
	}
	// Shader API
	void InitShader(Shader *s, bgfx_shader_handle_t vs, bgfx_shader_handle_t fs) const {
		s->m_program = BGFX(create_program)(vs, fs, false);
		if (s->m_program.idx == UINT16_MAX) {
			s->m_render = nullptr;
			return;
		}
		bgfx_uniform_handle_t u[Shader::maxUniform];
		s->m_vsSize = BGFX(get_shader_uniforms)(vs, u, Shader::maxUniform);
		int i;
		for (i=0;i<s->m_vsSize;i++) {
			s->m_uniform[i].handle = u[i];
			s->m_uniform[i].count = 0;
			s->m_uniform[i].ptr = nullptr;
		}
		s->m_fsSize = BGFX(get_shader_uniforms)(fs, u, Shader::maxUniform - s->m_vsSize);
		for (i=0;i<s->m_fsSize;i++) {
			s->m_uniform[i+s->m_vsSize].handle = u[i];
			s->m_uniform[i+s->m_vsSize].count = 0;
			s->m_uniform[i+s->m_vsSize].ptr = nullptr;
		}
		for (i=0;i<Shader::maxSamplers;i++) {
			s->m_samplers[i].idx = UINT16_MAX;
		}
	}
	void ReleaseShader(Shader *s) const {
		BGFX(destroy_vertex_layout)(s->m_layout);
		s->m_layout.idx = UINT16_MAX;
		if (s->isValid()) {
			BGFX(destroy_program)(s->m_program);
			s->m_render = nullptr;
		}
	}
	void SumbitUniforms(Shader *s) const {
		if (!s->isValid())
			return;
		int i;
		for (i=0;i<s->m_vsSize + s->m_fsSize;i++) {
			if (s->m_uniform[i].ptr != nullptr) {
				BGFX(set_uniform)(s->m_uniform[i].handle, s->m_uniform[i].ptr, s->m_uniform[i].count);
			}
		}
	}
	void AddUniform(Shader *s, const char *name, Shader::UniformType type, int offset) const {
		if (!s->isValid())
			return;
		int i;
		int from = 0;
		int	to = s->m_vsSize + s->m_fsSize;
		switch(type) {
		case Shader::UniformType::Vertex:
			to = s->m_vsSize;
			break;
		case Shader::UniformType::Pixel:
			from = s->m_vsSize;
			to = s->m_vsSize + s->m_fsSize;
			break;
		default:
			break;
		}
		bgfx_uniform_info_t info;
		for (i=from;i<to;i++) {
			if (s->m_uniform[i].count == 0) {
				info.name[0] = 0;
				BGFX(get_uniform_info)(s->m_uniform[i].handle, &info);
				if (strcmp(info.name, name) == 0) {
					break;
				}
			}
		}

		if (i >= to) {
			//ReleaseShader(s);
			return;
		}

		switch(type) {
		case Shader::UniformType::Vertex:
			s->m_uniform[i].ptr = s->m_vcbBuffer + offset;
			s->m_uniform[i].count = info.num;
			break;
		case Shader::UniformType::Pixel:
			s->m_uniform[i].ptr = s->m_pcbBuffer + offset;
			s->m_uniform[i].count = info.num;
			break;
		case Shader::UniformType::Texture:
			assert(info.type == BGFX_UNIFORM_TYPE_SAMPLER);
			assert(0 <= offset && offset < Shader::maxSamplers);
			s->m_uniform[i].count = offset + 1;
			assert(!BGFX_HANDLE_IS_VALID(s->m_samplers[offset]));
			s->m_samplers[offset] = s->m_uniform[i].handle;
			break;
		}
	}

	Effekseer::Backend::TextureRef CreateTexture(const Effekseer::Backend::TextureParameter& param, const Effekseer::CustomVector<uint8_t>& initialData) const {
		// Only for CreateProxyTexture, See EffekseerRendererCommon/EffekseerRenderer.Renderer.cpp
		assert(param.Format == Effekseer::Backend::TextureFormatType::R8G8B8A8_UNORM);
		assert(param.Dimension == 2);

		const bgfx_memory_t *mem = BGFX(copy)(initialData.data(), (uint32_t)initialData.size());
		bgfx_texture_handle_t handle = BGFX(create_texture_2d)(param.Size[0], param.Size[1], false, 1, BGFX_TEXTURE_FORMAT_RGBA8,
			BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, mem);

		return Effekseer::MakeRefPtr<Texture>(this, handle);
	}
	void ReleaseTexture(Texture *t) const {
		BGFX(destroy_texture)(t->RemoveInterface());
	}
	Effekseer::Backend::IndexBufferRef CreateIndexBuffer(int32_t elementCount, const void* initialData, Effekseer::Backend::IndexBufferStrideType stride) const {
		int s = (stride == Effekseer::Backend::IndexBufferStrideType::Stride4) ? 4 : 2;
		const bgfx_memory_t *mem = BGFX(copy)(initialData, elementCount * s);
		bgfx_index_buffer_handle_t handle = BGFX(create_index_buffer)(mem, s == 4 ? BGFX_BUFFER_INDEX32 : BGFX_BUFFER_NONE);

		return Effekseer::MakeRefPtr<StaticIndexBuffer>(this, handle, s, elementCount);
	}
	Effekseer::Backend::VertexBufferRef CreateVertexBuffer(int32_t size, const void* initialData) const {
		const bgfx_memory_t *mem = BGFX(copy)(initialData, size);
		bgfx_vertex_buffer_handle_t handle = BGFX(create_vertex_buffer)(mem, &m_modellayout, BGFX_BUFFER_NONE);
		return  Effekseer::MakeRefPtr<StaticVertexBuffer>(this, handle);
	}
	void ReleaseIndexBuffer(StaticIndexBuffer *ib) const {
		BGFX(destroy_index_buffer)(ib->GetInterface());
	}
	void ReleaseVertexBuffer(StaticVertexBuffer *vb) const {
		BGFX(destroy_vertex_buffer)(vb->GetInterface());
	}
	bool StoreModelToGPU(Effekseer::ModelRef model) const {
		if (model == nullptr)
			return false;
		model->StoreBufferToGPU(m_device.Get());
		if (!model->GetIsBufferStoredOnGPU())
			return false;
		return true;
	}
private:
//	void DoDraw();
};

void RenderState::Update(bool forced) {
	(void)forced;	// ignore forced
	uint64_t state = 0
		| BGFX_STATE_WRITE_RGB
		| BGFX_STATE_WRITE_A
		| BGFX_STATE_FRONT_CCW
		| BGFX_STATE_MSAA;

	if (m_next.DepthTest) {
		state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
	} else {
		state |= BGFX_STATE_DEPTH_TEST_ALWAYS;
	}

	if (m_next.DepthWrite) {
		state |= BGFX_STATE_WRITE_Z;
	}

	// isCCW
	if (m_next.CullingType == Effekseer::CullingType::Front) {
		state |= BGFX_STATE_CULL_CW;
	}
	else if (m_next.CullingType == Effekseer::CullingType::Back) {
		state |= BGFX_STATE_CULL_CCW;
	}
	if (m_next.AlphaBlend == ::Effekseer::AlphaBlendType::Opacity ||
		m_renderer->GetRenderMode() == ::Effekseer::RenderMode::Wireframe) {
			state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_ADD, BGFX_STATE_BLEND_EQUATION_MAX);
			state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
		}
	else if (m_next.AlphaBlend == ::Effekseer::AlphaBlendType::Sub)	{
		state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_REVSUB, BGFX_STATE_BLEND_EQUATION_ADD);
		state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
	} else {
		if (m_next.AlphaBlend == ::Effekseer::AlphaBlendType::Blend) {
			state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_ADD, BGFX_STATE_BLEND_EQUATION_MAX);
			state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
		} else if (m_next.AlphaBlend == ::Effekseer::AlphaBlendType::Add) {
			state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_ADD, BGFX_STATE_BLEND_EQUATION_MAX);
			state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
		} else if (m_next.AlphaBlend == ::Effekseer::AlphaBlendType::Mul) {
			state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_ADD, BGFX_STATE_BLEND_EQUATION_ADD);
			state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_SRC_COLOR, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
		}
	}
	m_renderer->SetCurrentState(state);
	m_active = m_next;
}

Effekseer::Backend::TextureRef GraphicsDevice::CreateTexture(const Effekseer::Backend::TextureParameter& param, const Effekseer::CustomVector<uint8_t>& initialData) {
	return m_render->CreateTexture(param, initialData);
}

Effekseer::Backend::IndexBufferRef GraphicsDevice::CreateIndexBuffer(int32_t elementCount, const void* initialData, Effekseer::Backend::IndexBufferStrideType stride) {
	return m_render->CreateIndexBuffer(elementCount, initialData, stride);
}

Effekseer::Backend::VertexBufferRef GraphicsDevice::CreateVertexBuffer(int32_t size, const void* initialData, bool isDynamic) {
	assert(isDynamic == false);	// ModelRenderer use Static VB
	return m_render->CreateVertexBuffer(size, initialData);
}

Texture::~Texture() {
	m_render->ReleaseTexture(this);
}

// Create Renderer

EffekseerRenderer::RendererRef CreateRenderer(struct InitArgs *init) {
	auto renderer = Effekseer::MakeRefPtr<RendererImplemented>();
	if (renderer->Initialize(init))	{
		return renderer;
	}
	return nullptr;
}

}
