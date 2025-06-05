use std::collections::BTreeMap;

use crate::core::{led_strip::LedStrip, parameter::Parameter};
use effects::effect::Effect;

pub struct PixelLocateEffect {
    color: [u8; 3],
    pixel_number: usize,
}

impl PixelLocateEffect {
    pub fn new() -> Self {
        Self {
            color: [50, 50, 50],
            pixel_number: 0,
        }
    }
}

impl Effect for PixelLocateEffect {
    fn name(&self) -> &'static str {
        "PixelLocate"
    }

    fn tick(&mut self, strip: &mut LedStrip, _dt: f32) {
        for i in 0..strip.length {
            strip.set(i, 0, 0, 0);
            if i == self.pixel_number {
                strip.set(i, self.color[0], self.color[1], self.color[2]);
            }
        }
    }

    fn get_parameters(&self) -> BTreeMap<String, Parameter> {
        BTreeMap::from([
            ("color".to_string(), Parameter::Color(self.color)),
            (
                "pixel_number".to_string(),
                Parameter::Int(self.pixel_number as i32),
            ),
        ])
    }

    fn set_parameter(&mut self, key: &str, value: Parameter) {
        match (key, value) {
            ("color", Parameter::Color(c)) => self.color = c,
            ("pixel_number", Parameter::Int(i)) => self.pixel_number = i as usize,
            _ => {}
        }
    }
}
