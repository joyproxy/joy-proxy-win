use joyproxy_relay::{ProxyConfig, ProxyType};

#[test]
fn proxy_config_auth_detection() {
    let cfg = ProxyConfig {
        proxy_type: ProxyType::Socks5,
        host: "127.0.0.1".into(),
        port: 1080,
        username: Some("user".into()),
        password: Some("pass".into()),
    };
    assert!(cfg.has_auth());

    let cfg2 = ProxyConfig {
        proxy_type: ProxyType::Http,
        host: "127.0.0.1".into(),
        port: 8080,
        username: None,
        password: None,
    };
    assert!(!cfg2.has_auth());
}
