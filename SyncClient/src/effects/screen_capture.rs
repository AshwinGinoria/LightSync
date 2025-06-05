use image::{imageops::FilterType, DynamicImage, ImageBuffer, Rgba};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Mutex,
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

type SharedCaptureState = Arc<CaptureState>;

impl GraphicsCaptureApiHandler for Capture {
    type Flags = SharedCaptureState;
    type Error = Box<dyn std::error::Error + Send + Sync>;

    fn new(ctx: Context<Self::Flags>) -> Result<Self, Self::Error> {
        Ok(Capture {
            state: ctx.flags, // This is Arc<CaptureState>
        })
    }

    fn on_frame_arrived(
        &mut self,
        frame: &mut Frame,
        control: InternalCaptureControl,
    ) -> Result<(), Self::Error> {
        if !self.state.running.load(Ordering::Relaxed) {
            control.stop();
            return Ok(());
        }

        let (w, h) = (frame.width(), frame.height());
        let buffer = frame.buffer()?.as_raw_buffer().to_vec();
        let img_buf = ImageBuffer::<Rgba<u8>, _>::from_raw(w, h, buffer)
            .ok_or("Failed to construct image buffer")?;

        let (width, height) = self.state.get_dimensions();

        let dyn_img =
            DynamicImage::ImageRgba8(img_buf).resize_exact(width, height, FilterType::Nearest);

        self.state.set_frame(dyn_img);
        Ok(())
    }

    fn on_closed(&mut self) -> Result<(), Self::Error> {
        println!("[Capture] Source closed");
        Ok(())
    }
}

pub struct Capture {
    state: SharedCaptureState,
}
