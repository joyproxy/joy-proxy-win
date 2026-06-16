use std::io::{Read, Write};
use std::net::{SocketAddr, TcpStream};
use std::time::Duration;

use crate::config::{ProxyConfig, ProxyType};
use crate::error::{RelayError, Result};

pub fn connect_via_socks5(
    proxy: &ProxyConfig,
    dest_host: &str,
    dest_port: u16,
    timeout: Duration,
) -> Result<TcpStream> {
    if proxy.proxy_type != ProxyType::Socks5 {
        return Err(RelayError::InvalidResponse("not socks5".into()));
    }

    let addr = resolve_proxy(proxy)?;
    let mut stream = TcpStream::connect_timeout(&addr, timeout)?;
    stream.set_read_timeout(Some(timeout))?;
    stream.set_write_timeout(Some(timeout))?;

    let methods: &[u8] = if proxy.has_auth() { &[0x00, 0x02] } else { &[0x00] };
    stream.write_all(&[0x05, methods.len() as u8])?;
    stream.write_all(methods)?;

    let mut method_resp = [0u8; 2];
    read_exact(&mut stream, &mut method_resp)?;
    if method_resp[0] != 0x05 {
        return Err(RelayError::InvalidResponse("bad socks version".into()));
    }

    match method_resp[1] {
        0x00 => {}
        0x02 => socks5_auth(&mut stream, proxy)?,
        0xFF => return Err(RelayError::ProxyAuthFailed),
        m => {
            return Err(RelayError::InvalidResponse(format!(
                "unsupported auth method {m}"
            )));
        }
    }

    let (addr_type, addr_body) = encode_dest(dest_host)?;
    let mut req = Vec::with_capacity(4 + addr_body.len() + 2);
    req.push(0x05);
    req.push(0x01);
    req.push(0x00);
    req.push(addr_type);
    req.extend_from_slice(&addr_body);
    req.extend_from_slice(&dest_port.to_be_bytes());
    stream.write_all(&req)?;

    let mut header = [0u8; 4];
    read_exact(&mut stream, &mut header)?;
    if header[1] != 0x00 {
        return Err(RelayError::InvalidResponse(format!(
            "socks connect failed code {}",
            header[1]
        )));
    }

    match header[3] {
        0x01 => {
            let mut rest = [0u8; 6];
            read_exact(&mut stream, &mut rest)?;
        }
        0x03 => {
            let mut len = [0u8; 1];
            read_exact(&mut stream, &mut len)?;
            let mut rest = vec![0u8; len[0] as usize + 2];
            read_exact(&mut stream, &mut rest)?;
        }
        0x04 => {
            let mut rest = [0u8; 18];
            read_exact(&mut stream, &mut rest)?;
        }
        t => {
            return Err(RelayError::InvalidResponse(format!(
                "unexpected bound addr type {t}"
            )));
        }
    }

    stream.set_read_timeout(None)?;
    stream.set_write_timeout(None)?;
    Ok(stream)
}

fn socks5_auth(stream: &mut TcpStream, proxy: &ProxyConfig) -> Result<()> {
    let user = proxy.username.as_deref().unwrap_or("");
    let pass = proxy.password.as_deref().unwrap_or("");
    if user.len() > 255 || pass.len() > 255 {
        return Err(RelayError::InvalidResponse("auth too long".into()));
    }
    let mut pkt = Vec::with_capacity(3 + user.len() + pass.len());
    pkt.push(0x01);
    pkt.push(user.len() as u8);
    pkt.extend_from_slice(user.as_bytes());
    pkt.push(pass.len() as u8);
    pkt.extend_from_slice(pass.as_bytes());
    stream.write_all(&pkt)?;

    let mut resp = [0u8; 2];
    read_exact(stream, &mut resp)?;
    if resp[1] != 0x00 {
        return Err(RelayError::ProxyAuthFailed);
    }
    Ok(())
}

fn encode_dest(host: &str) -> Result<(u8, Vec<u8>)> {
    if let Ok(ip) = host.parse::<std::net::Ipv4Addr>() {
        return Ok((0x01, ip.octets().to_vec()));
    }
    if let Ok(ip) = host.parse::<std::net::Ipv6Addr>() {
        return Ok((0x04, ip.octets().to_vec()));
    }
    if host.len() > 255 {
        return Err(RelayError::InvalidResponse("hostname too long".into()));
    }
    let mut body = Vec::with_capacity(1 + host.len());
    body.push(host.len() as u8);
    body.extend_from_slice(host.as_bytes());
    Ok((0x03, body))
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

fn read_exact(stream: &mut TcpStream, buf: &mut [u8]) -> Result<()> {
    stream.read_exact(buf)?;
    Ok(())
}
