use std::io::{copy, ErrorKind};
use std::net::{Shutdown, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use crate::config::{ProxyConfig, ProxyType};
use crate::error::{RelayError, Result};
use crate::http_connect::connect_via_http;
use crate::socks5::connect_via_socks5;

const DEFAULT_TIMEOUT: Duration = Duration::from_secs(30);

pub fn connect_upstream(
    proxy: &ProxyConfig,
    dest_host: &str,
    dest_port: u16,
) -> Result<TcpStream> {
    match proxy.proxy_type {
        ProxyType::Socks5 => connect_via_socks5(proxy, dest_host, dest_port, DEFAULT_TIMEOUT),
        ProxyType::Http => connect_via_http(proxy, dest_host, dest_port, DEFAULT_TIMEOUT),
    }
}

pub fn relay_bidirectional(mut local: TcpStream, mut upstream: TcpStream) -> Result<()> {
    local.set_nodelay(true).ok();
    upstream.set_nodelay(true).ok();

    let local_reader = local.try_clone()?;
    let upstream_reader = upstream.try_clone()?;
    let done = Arc::new(AtomicBool::new(false));
    let done_a = Arc::clone(&done);
    let done_b = Arc::clone(&done);

    let mut upstream = upstream;
    let mut local = local;

    let t1 = thread::spawn(move || {
        let mut local_reader = local_reader;
        let _ = copy(&mut local_reader, &mut upstream);
        done_a.store(true, Ordering::SeqCst);
        let _ = upstream.shutdown(Shutdown::Both);
    });
    let t2 = thread::spawn(move || {
        let mut upstream_reader = upstream_reader;
        let _ = copy(&mut upstream_reader, &mut local);
        done_b.store(true, Ordering::SeqCst);
        let _ = local.shutdown(Shutdown::Both);
    });

    let _ = t1.join();
    let _ = t2.join();
    Ok(())
}

pub fn relay_tcp_socket(
    local: TcpStream,
    dest_host: &str,
    dest_port: u16,
    proxy: &ProxyConfig,
) -> Result<()> {
    let upstream = connect_upstream(proxy, dest_host, dest_port)?;
    relay_bidirectional(local, upstream)
}

pub fn test_proxy(proxy: &ProxyConfig) -> Result<()> {
    let upstream = connect_upstream(proxy, "example.com", 80)?;
    drop(upstream);
    Ok(())
}

pub fn map_io_result(r: Result<()>) -> i32 {
    match r {
        Ok(()) => 0,
        Err(RelayError::ProxyAuthFailed) => -2,
        Err(RelayError::Timeout) => -3,
        Err(RelayError::Io(e)) if e.kind() == ErrorKind::TimedOut => -3,
        Err(_) => -1,
    }
}
