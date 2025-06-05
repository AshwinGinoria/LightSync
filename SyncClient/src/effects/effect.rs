use std::collections::BTreeMap;

use crate::core::{led_strip::LedStrip, parameter::Parameter};

pub trait Effect {
    /// Unique effect name (used in dropdowns etc.)
    fn name(&self) -> &'static str;

    /// Update LED strip based on current state
    fn tick(&mut self, strip: &mut LedStrip, dt: f32);

    /// Get the current parameters (for GUI display)
    fn get_parameters(&self) -> BTreeMap<String, Parameter>;

    /// Default: set param if it exists
    fn set_parameter(&mut self, key: &str, value: Parameter);
}
