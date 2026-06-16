#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProxyType {
    Socks5 = 0,
    Http = 1,
}

#[derive(Debug, Clone)]
pub struct ProxyConfig {
    pub proxy_type: ProxyType,
    pub host: String,
    pub port: u16,
    pub username: Option<String>,
    pub password: Option<String>,
}

impl ProxyConfig {
    pub fn has_auth(&self) -> bool {
        self.username
            .as_ref()
            .is_some_and(|u| !u.is_empty())
            && self.password.is_some()
    }
}
