use std::net::{SocketAddr, UdpSocket};
use tracing::info;

pub struct UdpClient {
    target: SocketAddr,
    socket: UdpSocket,
}

impl UdpClient {
    pub fn new(ip: &str, port: u16) -> std::io::Result<Self> {
        let target: std::net::SocketAddr = format!("{}:{}", ip, port)
            .parse()
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidInput, e))?;
        let socket = UdpSocket::bind("0.0.0.0:0")?;
        socket.set_nonblocking(false)?;
        Ok(Self { target, socket })
    }

    pub fn send(&self, data: &[u8]) -> std::io::Result<usize> {
        info!("{:?}", data);
        self.socket.send_to(data, self.target)
    }
}
