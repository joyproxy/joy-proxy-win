use std::io::{Read, Write};
use std::net::{SocketAddr, TcpStream};
use std::time::Duration;

use base64::{engine::general_purpose::STANDARD, Engine as _};

use crate::config::{ProxyConfig, ProxyType};
use crate::error::{RelayError, Result};

pub fn connect_via_http(
    proxy: &ProxyConfig,
    dest_host: &str,
    dest_port: u16,
    timeout: Duration,
) -> Result<TcpStream> {
    if proxy.proxy_type != ProxyType::Http {
        return Err(RelayError::InvalidResponse("not http proxy".into()));
    }

    let addr = resolve_proxy(proxy)?;
    let mut stream = TcpStream::connect_timeout(&addr, timeout)?;
    stream.set_read_timeout(Some(timeout))?;
    stream.set_write_timeout(Some(timeout))?;

    let mut req = format!("CONNECT {dest_host}:{dest_port} HTTP/1.1\r\nHost: {dest_host}:{dest_port}\r\n");
    if proxy.has_auth() {
        let user = proxy.username.as_deref().unwrap_or("");
        let pass = proxy.password.as_deref().unwrap_or("");
        let token = STANDARD.encode(format!("{user}:{pass}"));
        req.push_str(&format!("Proxy-Authorization: Basic {token}\r\n"));
    }
    req.push_str("\r\n");
    stream.write_all(req.as_bytes())?;

    let mut buf = [0u8; 1024];
    let n = stream.read(&mut buf)?;
    let resp = std::str::from_utf8(&buf[..n])
        .map_err(|_| RelayError::InvalidResponse("non-utf8 response".into()))?;
    let status_line = resp.lines().next().unwrap_or("");
    if status_line.contains(" 407 ") {
        return Err(RelayError::ProxyAuthFailed);
    }
    if !status_line.contains(" 200 ") {
        return Err(RelayError::InvalidResponse(status_line.to_string()));
    }

    stream.set_read_timeout(None)?;
    stream.set_write_timeout(None)?;
    Ok(stream)
}

fn resolve_proxy(proxy: &ProxyConfig) -> Result<SocketAddr> {
    use std::net::ToSocketAddrs;
    let addrs: Vec<_> = (proxy.host.as_str(), proxy.port)
        .to_socket_addrs()?
        .collect();
    addrs
        .into_iter()
        .next()
        .ok_or_else(|| RelayError::InvalidResponse("cannot resolve proxy".into()))
}
