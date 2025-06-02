use crate::core::led_strip::LedStrip;
use crate::effects::effect::Effect;
use crate::network::udp_client::UdpClient;

pub struct Controller {
    current_effect: Option<Box<dyn Effect>>,
    strip: LedStrip,
    client: UdpClient,
}

impl Controller {
    pub fn new(strip: LedStrip, client: UdpClient) -> Self {
        Self {
            current_effect: None,
            strip,
            client,
        }
    }

    pub fn set_effect(&mut self, effect: Box<dyn Effect>) {
        self.current_effect = Some(effect);
    }

    pub fn tick(&mut self, dt: f32) {
        if let Some(effect) = &mut self.current_effect {
            effect.tick(&mut self.strip, dt);
            self.client.send(&self.strip.serialize()).unwrap();
        }
    }

    pub fn stop_effect(&mut self) {
        self.current_effect = None;
        self.strip.clear();
        self.client.send(&self.strip.serialize()).unwrap();
    }
}
