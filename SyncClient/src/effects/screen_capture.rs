use image::{DynamicImage, ImageBuffer, Rgba};
use std::{
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    thread::sleep,
    time::{Duration, Instant},
};
use tracing::info;
use windows::Win32::Graphics::{
    Direct3D::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
    Direct3D11::{
        ID3D11Device, ID3D11DeviceContext, ID3D11PixelShader, ID3D11SamplerState, ID3D11Texture2D,
        ID3D11VertexShader, D3D11_BIND_RENDER_TARGET, D3D11_BIND_SHADER_RESOURCE,
        D3D11_CPU_ACCESS_READ, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_MAPPED_SUBRESOURCE,
        D3D11_MAP_READ, D3D11_SAMPLER_DESC, D3D11_TEXTURE2D_DESC, D3D11_TEXTURE_ADDRESS_CLAMP,
        D3D11_USAGE_DEFAULT, D3D11_USAGE_STAGING, D3D11_VIEWPORT,
    },
    Dxgi::Common::{DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SAMPLE_DESC},
};

use windows_capture::{
    capture::{Context, GraphicsCaptureApiHandler},
    frame::Frame,
    graphics_capture_api::InternalCaptureControl,
};

// Shared container for the latest frame
#[derive(Clone)]
pub struct SharedFrame(Arc<Mutex<Option<DynamicImage>>>);

impl SharedFrame {
    pub fn new() -> Self {
        Self(Arc::new(Mutex::new(None)))
    }

    pub fn set(&self, img: DynamicImage) {
        *self.0.lock().unwrap() = Some(img);
    }

    pub fn get(&self) -> Option<DynamicImage> {
        self.0.lock().unwrap().clone()
    }
}

pub struct CaptureState {
    dimensions: Arc<Mutex<(u32, u32)>>,
    pub running: Arc<AtomicBool>,
    frame_store: SharedFrame,
}

impl CaptureState {
    pub fn new(dimensions: (u32, u32)) -> Self {
        Self {
            dimensions: Arc::new(Mutex::new(dimensions)),
            running: Arc::new(AtomicBool::new(false)),
            frame_store: SharedFrame::new(),
        }
    }

    pub fn set_dimensions(&self, dimensions: (u32, u32)) {
        *self.dimensions.lock().unwrap() = dimensions;
    }

    pub fn get_dimensions(&self) -> (u32, u32) {
        *self.dimensions.lock().unwrap()
    }

    pub fn start(&self) {
        self.running.store(true, Ordering::Relaxed);
    }

    pub fn stop(&self) {
        self.running.store(false, Ordering::Relaxed);
    }

    pub fn get_frame(&self) -> Option<DynamicImage> {
        self.frame_store.get()
    }

    pub fn set_frame(&self, img: DynamicImage) {
        self.frame_store.set(img);
    }
}

impl Clone for CaptureState {
    fn clone(&self) -> Self {
        Self {
            running: Arc::clone(&self.running),
            frame_store: self.frame_store.clone(),
            dimensions: Arc::clone(&self.dimensions),
        }
    }
}

pub struct GpuResources {
    pub device: ID3D11Device,
    pub context: ID3D11DeviceContext,
    sampler: ID3D11SamplerState,
    vs: ID3D11VertexShader,
    ps: ID3D11PixelShader,
}

impl GpuResources {
    pub fn new(
        d3d_device: ID3D11Device,
        context: ID3D11DeviceContext,
    ) -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        let device = d3d_device;
        let context = context;

        // Simple passthrough vertex shader
        const FULLSCREEN_VS: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/fullscreen_vs.cso"));
        const BILINEAR_PS: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/bilinear_ps.cso"));

        let vs = unsafe {
            let mut shader = None;
            device.CreateVertexShader(FULLSCREEN_VS, None, Some(&mut shader))?;
            shader.unwrap()
        };

        let ps = unsafe {
            let mut shader = None;
            device.CreatePixelShader(BILINEAR_PS, None, Some(&mut shader))?;
            shader.unwrap()
        };

        // Create sampler
        let sampler_desc = D3D11_SAMPLER_DESC {
            Filter: D3D11_FILTER_MIN_MAG_MIP_LINEAR,
            AddressU: D3D11_TEXTURE_ADDRESS_CLAMP,
            AddressV: D3D11_TEXTURE_ADDRESS_CLAMP,
            AddressW: D3D11_TEXTURE_ADDRESS_CLAMP,
            ..Default::default()
        };
        let sampler = unsafe {
            let mut state = None;
            device.CreateSamplerState(&sampler_desc, Some(&mut state))?;
            state.unwrap()
        };

        Ok(Self {
            device,
            context,
            sampler,
            vs,
            ps,
        })
    }

    pub fn create_staging_texture(
        &self,
        width: u32,
        height: u32,
    ) -> Result<ID3D11Texture2D, windows::core::Error> {
        let desc = D3D11_TEXTURE2D_DESC {
            Width: width,
            Height: height,
            MipLevels: 1,
            ArraySize: 1,
            Format: DXGI_FORMAT_R8G8B8A8_UNORM,
            SampleDesc: DXGI_SAMPLE_DESC {
                Count: 1,
                Quality: 0,
            },
            Usage: D3D11_USAGE_STAGING,
            BindFlags: 0,
            CPUAccessFlags: D3D11_CPU_ACCESS_READ.0 as u32,
            MiscFlags: 0,
        };

        let mut texture = None;
        unsafe {
            self.device
                .CreateTexture2D(&desc, None, Some(&mut texture))?;
        }

        Ok(texture.unwrap())
    }

    pub fn get_or_create_resized_texture(
        &self,
        width: u32,
        height: u32,
    ) -> Result<ID3D11Texture2D, windows::core::Error> {
        let desc = D3D11_TEXTURE2D_DESC {
            Width: width,
            Height: height,
            MipLevels: 1,
            ArraySize: 1,
            Format: DXGI_FORMAT_R8G8B8A8_UNORM,
            SampleDesc: DXGI_SAMPLE_DESC {
                Count: 1,
                Quality: 0,
            },
            Usage: D3D11_USAGE_DEFAULT,
            BindFlags: D3D11_BIND_RENDER_TARGET.0 as u32 | D3D11_BIND_SHADER_RESOURCE.0 as u32,
            CPUAccessFlags: 0,
            MiscFlags: 0,
        };

        let mut texture = None;
        unsafe {
            self.device
                .CreateTexture2D(&desc, None, Some(&mut texture))?;
        }

        Ok(texture.unwrap())
    }

    pub unsafe fn resize_with_shader(
        &self,
        src: &ID3D11Texture2D,
        dst: &ID3D11Texture2D,
    ) -> Result<(), windows::core::Error> {
        // SRV
        let mut srv = None;
        self.device
            .CreateShaderResourceView(src, None, Some(&mut srv))?;
        let srv = srv.unwrap();

        // RTV
        let mut rtv = None;
        self.device
            .CreateRenderTargetView(dst, None, Some(&mut rtv))?;
        let rtv = rtv.unwrap();
        let color = [0.0, 0.0, 0.0, 1.0];
        self.context.ClearRenderTargetView(&rtv, &color);
        let rtvs = [Some(rtv.clone())];

        self.context.OMSetRenderTargets(Some(&rtvs), None);
        self.context.VSSetShader(&self.vs, None);
        self.context.PSSetShader(&self.ps, None);

        let samplers = [Some(self.sampler.clone())];
        self.context.PSSetSamplers(0, Some(&samplers));

        let srvs = [Some(srv.clone())];
        self.context.PSSetShaderResources(0, Some(&srvs));

        // Set viewport
        let mut desc = D3D11_TEXTURE2D_DESC::default();
        dst.GetDesc(&mut desc);
        let vp = D3D11_VIEWPORT {
            Width: desc.Width as f32,
            Height: desc.Height as f32,
            MinDepth: 0.0,
            MaxDepth: 1.0,
            TopLeftX: 0.0,
            TopLeftY: 0.0,
        };

        let viewports = [vp];
        self.context.RSSetViewports(Some(&viewports));

        // Draw full-screen triangle
        self.context
            .IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        self.context.Draw(3, 0);
        Ok(())
    }
}

type SharedCaptureState = Arc<CaptureState>;

pub struct Capture {
    state: SharedCaptureState,
    gpu_resources: GpuResources,
}

impl GraphicsCaptureApiHandler for Capture {
    type Flags = SharedCaptureState;
    type Error = Box<dyn std::error::Error + Send + Sync>;

    fn new(ctx: Context<Self::Flags>) -> Result<Self, Self::Error> {
        let gpu_resources = match GpuResources::new(ctx.device, ctx.device_context) {
            Ok(r) => r,
            Err(e) => {
                eprintln!("Error: {}", e);
                return Err(e);
            }
        };

        Ok(Capture {
            state: ctx.flags, // This is Arc<CaptureState>
            gpu_resources,
        })
    }

    fn on_frame_arrived(
        &mut self,
        frame: &mut Frame,
        control: InternalCaptureControl,
    ) -> Result<(), Self::Error> {
        let start = Instant::now();

        if !self.state.running.load(Ordering::Relaxed) {
            info!("Capture stopped; skipping frame.");
            control.stop();
            return Ok(());
        }

        let src_tex = unsafe { frame.as_raw_texture() };
        let (dst_width, dst_height) = self.state.get_dimensions();

        let resized_texture = self
            .gpu_resources
            .get_or_create_resized_texture(dst_width, dst_height)?;
        let mut actual_desc = D3D11_TEXTURE2D_DESC::default();
        unsafe {
            resized_texture.GetDesc(&mut actual_desc);
        }

        unsafe {
            self.gpu_resources
                .resize_with_shader(src_tex, &resized_texture)?;
        }

        let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
        let staging_texture = self
            .gpu_resources
            .create_staging_texture(dst_width, dst_height)?;

        unsafe {
            self.gpu_resources
                .context
                .CopyResource(&staging_texture, &resized_texture);

            let _ = self.gpu_resources.context.Map(
                &staging_texture,
                0,
                D3D11_MAP_READ,
                0,
                Some(&mut mapped),
            )?;
        }

        let buffer = unsafe {
            std::slice::from_raw_parts(mapped.pData.cast(), (dst_height * mapped.RowPitch) as usize)
        };
        
        let mut tight_buffer = vec![0u8; (dst_width * dst_height * 4) as usize];
        for y in 0..dst_height as usize {
            let src_offset = y * mapped.RowPitch as usize;
            let dst_offset = y * (dst_width as usize * 4);
            tight_buffer[dst_offset..dst_offset + (dst_width as usize * 4)]
                .copy_from_slice(&buffer[src_offset..src_offset + (dst_width as usize * 4)]);
        }

        let img_buf = ImageBuffer::<Rgba<u8>, _>::from_raw(dst_width, dst_height, tight_buffer)
            .ok_or_else(|| "Failed to construct resized image buffer".to_string())?;

        unsafe {
            self.gpu_resources.context.Unmap(&staging_texture, 0);
        }

        self.state.set_frame(DynamicImage::ImageRgba8(img_buf));

        // --- Rate limit ---
        const TARGET_FPS: u64 = 30;
        const FRAME_TIME_MS: u64 = 1000 / TARGET_FPS;

        let elapsed = start.elapsed();
        if elapsed < Duration::from_millis(FRAME_TIME_MS) {
            sleep(Duration::from_millis(FRAME_TIME_MS) - elapsed);
        }

        Ok(())
    }

    fn on_closed(&mut self) -> Result<(), Self::Error> {
        println!("[Capture] Source closed");
        Ok(())
    }
}
