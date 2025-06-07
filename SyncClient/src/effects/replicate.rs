use crate::core::{led_strip::LedStrip, parameter::Parameter};
use effects::effect::Effect;
use std::{
    collections::BTreeMap,
    sync::Arc,
    thread::{self, JoinHandle},
};

use windows_capture::{
    capture::GraphicsCaptureApiHandler,
    monitor::Monitor,
    settings::{ColorFormat, CursorCaptureSettings, DrawBorderSettings, Settings},
};
use palette::{Srgb, Hsv, FromColor};

use effects::screen_capture::{Capture, CaptureState};
use tracing::{debug, error};

pub struct ReplicateEffect {
    dead_leds: u32,
    brightness: f32,
    _pixels: u32,
    _dimensions: (u32, u32),
    _capture: Arc<CaptureState>,
    _capture_thread: Option<JoinHandle<()>>,
    _frame_number: u32,
}

impl ReplicateEffect {
    pub fn new() -> Self {
        let n_pixels = 288;
        let dead_leds = 6;
        let aspect_ratio = 16.0 / 9.0;

        let dimentions = Self::calc_dimensions(n_pixels, dead_leds, aspect_ratio);

        // Shared capture state
        let state = Arc::new(CaptureState::new(dimentions));
        state.start(); // set running = true

        let primary_monitor = Monitor::primary().expect("There is no primary monitor");

        let settings = Settings::new(
            // Item to capture
            primary_monitor,
            // Capture cursor settings
            CursorCaptureSettings::WithoutCursor,
            // Draw border settings
            DrawBorderSettings::WithoutBorder,
            // The desired color format for the captured frame.
            ColorFormat::Rgba8,
            // Additional flags for the capture settings that will be passed to user defined `new` function.
            state.clone(),
        );

        let thread = thread::spawn(move || {
            Capture::start(settings).expect("Screen Capture Failed!");
        });

        Self {
            dead_leds,
            brightness: 0.1,
            _dimensions: dimentions,
            _pixels: n_pixels,
            _capture: state,
            _capture_thread: Some(thread),
            _frame_number: 0,
        }
    }

    fn calc_dimensions(n_pixels: u32, dead_leds: u32, aspect_ratio: f32) -> (u32, u32) {
        let total = (n_pixels - dead_leds + 4) as f32 / 2.0;

        let width = (total / (1.0 / aspect_ratio + 1.0)).floor() as u32;
        let height = (total / (1.0 + aspect_ratio)).floor() as u32;

        debug!("Updated Dimensions : ({}, {})", width, height);

        (width, height)
    }

    fn process_pixel_hsl(color: [u8; 3], brightness: f32, frame_number: u32, pixel_number: u32) -> [u8; 3] {
        debug!("Original Pixel {}_{}: {:?}", frame_number, pixel_number, color);

        // Convert to float RGB 0..1
        let rgb = Srgb::new(color[0] as f32 / 255.0, color[1] as f32 / 255.0, color[2] as f32 / 255.0);

        // Convert to HSV
        let mut hsv: Hsv = Hsv::from_color(rgb);

        // Scale Value (brightness)
        hsv.value = (hsv.value * brightness).clamp(0.0, 1.0);

        // Convert back to RGB
        let rgb_scaled = Srgb::from_color(hsv);

        // Map back to u8
        let scaled_pixel = [
            (rgb_scaled.red * 255.0).round() as u8,
            (rgb_scaled.green * 255.0).round() as u8,
            (rgb_scaled.blue * 255.0).round() as u8,
        ];

        debug!("Scaled Pixel   {}_{}: {:?}", frame_number, pixel_number, scaled_pixel);
        scaled_pixel
    }

}

impl Effect for ReplicateEffect {
    fn name(&self) -> &'static str {
        "Replicate"
    }

    fn tick(&mut self, strip: &mut LedStrip, _dt: f32) {
        let Some(img) = self._capture.get_frame() else {
            return;
        };

        self._frame_number += 1;

        let (w, h) = (img.width(), img.height());

        if w != self._dimensions.0 || h != self._dimensions.1 {
            error!(
                "Invalid image size, expected {:?} but found ({}, {})",
                self._dimensions, w, h
            );
            return;
        }

        let mut i: usize = 0;
        while (i as u32) < self.dead_leds {
            strip.set(i, 0, 0, 0);
            i += 1;
        }

        let img = img.to_rgb8();

        let mut push_pixel = |x: u32, y: u32| {
            if (i as u32) >= self._pixels {
                return;
            }
            let pixel = img.get_pixel(x, y).0;
            let [r, g, b] = Self::process_pixel_hsl(pixel, self.brightness, self._frame_number, i as u32);
            strip.set(i, r, g, b);
            i += 1;
        };

        // Left column
        for y in (0..h - 1).rev() {
            push_pixel(0, y);
        }

        // Top row
        for x in 0..w {
            push_pixel(x, 0);
        }

        // Right column
        for y in 1..h {
            push_pixel(w - 1, y);
        }

        // Bottom row
        for x in (0..w - 1).rev() {
            push_pixel(x, h - 1);
        }

        // Fill remaining
        while (i as u32) < self._pixels {
            strip.set(i, 0, 0, 0);
            i += 1;
        }
    }

    fn get_parameters(&self) -> BTreeMap<String, Parameter> {
        BTreeMap::from([
            ("brightness".to_string(), Parameter::Float(self.brightness)),
            (
                "dead_leds".to_string(),
                Parameter::Int(self.dead_leds as i32),
            ),
        ])
    }

    fn set_parameter(&mut self, key: &str, value: Parameter) {
        match (key, value) {
            ("brightness", Parameter::Float(f)) => self.brightness = f,
            ("dead_leds", Parameter::Int(i)) => {
                self.dead_leds = i as u32;
                self._dimensions = Self::calc_dimensions(self._pixels, self.dead_leds, 16.0 / 9.0);
                self._capture.set_dimensions(self._dimensions);
            },
            _ => {}
        }
    }
}

impl Drop for ReplicateEffect {
    fn drop(&mut self) {
        self._capture.stop();
    }
}
