use crate::core::led_strip::LedStrip;
use crate::effects::effect::Effect;
use crate::network::udp_client::UdpClient;
use tracing::info;

pub struct Controller {
    effect: Option<Box<dyn Effect>>,
    strip: LedStrip,
    client: UdpClient,
}

impl Controller {
    pub fn new(strip: LedStrip, client: UdpClient) -> Self {
        info!("Creating controller!");
        Self {
            effect: None,
            strip,
            client,
        }
    }

    pub fn set_effect(&mut self, effect: Box<dyn Effect>) {
        info!("Setting controller effect {}", effect.name());
        self.effect = Some(effect);
    }

    pub fn tick(&mut self, dt: f32) {
        if let Some(effect) = &mut self.effect {
            effect.tick(&mut self.strip, dt);
            self.client.send(&self.strip.serialize()).unwrap();
        }
    }

    pub fn effect_mut(&mut self) -> Option<&mut dyn Effect> {
        if let Some(effect) = &mut self.effect {
            return Some(effect.as_mut());
        }
        None
    }

    pub fn stop_effect(&mut self) {
        self.effect = None;
        self.strip.clear();
        self.client.send(&self.strip.serialize()).unwrap();
    }
}
