use std::collections::BTreeMap;

use crate::core::{led_strip::LedStrip, parameter::Parameter};
use effects::effect::Effect;

pub struct SolidEffect {
    color: [u8; 3],
    brightness: f32,
}

impl SolidEffect {
    pub fn new() -> Self {
        Self {
            color: [0, 0, 0],
            brightness: 0.1,
        }
    }

    fn scale_brightness(color: [u8; 3], brightness: f32) -> [u8; 3] {
        let scale = |v| ((v as f32 * brightness).clamp(0.0, 255.0)) as u8;
        [scale(color[0]), scale(color[1]), scale(color[2])]
    }
}

impl Effect for SolidEffect {
    fn name(&self) -> &'static str {
        "Solid"
    }

    fn tick(&mut self, strip: &mut LedStrip, _dt: f32) {
        let scaled = Self::scale_brightness(self.color, self.brightness);
        for i in 0..strip.length {
            strip.set(i, scaled[0], scaled[1], scaled[2]);
        }
    }

    fn get_parameters(&self) -> BTreeMap<String, Parameter> {
        BTreeMap::from([
            ("color".to_string(), Parameter::Color(self.color)),
            ("brightness".to_string(), Parameter::Float(self.brightness)),
        ])
    }

    fn set_parameter(&mut self, key: &str, value: Parameter) {
        match (key, value) {
            ("color", Parameter::Color(c)) => self.color = c,
            ("brightness", Parameter::Float(f)) => self.brightness = f,
            _ => {}
        }
    }
}
