mod config;
mod error;
mod http_connect;
mod relay;
mod socks5;

pub use config::{ProxyConfig, ProxyType};
pub use error::{RelayError, Result};
pub use relay::{connect_upstream, relay_bidirectional, relay_tcp_socket, test_proxy};

use std::ffi::{CStr, c_char, c_int};
use std::net::TcpStream;
use std::os::raw::c_ushort;
use std::os::windows::io::{FromRawSocket, RawSocket};
use std::panic::{catch_unwind, AssertUnwindSafe};

fn cstr(s: *const c_char) -> Option<String> {
    if s.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(s) }.to_str().ok().map(str::to_string)
}

fn proxy_from_ffi(
    proxy_type: u8,
    proxy_host: *const c_char,
    proxy_port: c_ushort,
    proxy_user: *const c_char,
    proxy_pass: *const c_char,
) -> Option<ProxyConfig> {
    let host = cstr(proxy_host)?;
    if host.is_empty() {
        return None;
    }
    Some(ProxyConfig {
        proxy_type: if proxy_type == 0 {
            ProxyType::Socks5
        } else {
            ProxyType::Http
        },
        host,
        port: proxy_port,
        username: cstr(proxy_user).filter(|s| !s.is_empty()),
        password: cstr(proxy_pass),
    })
}

#[no_mangle]
pub extern "C" fn joyproxy_relay_init() -> c_int {
    0
}

#[no_mangle]
pub extern "C" fn joyproxy_relay_shutdown() -> c_int {
    0
}

#[no_mangle]
pub extern "C" fn joyproxy_relay_tcp(
    local_socket: usize,
    dest_host: *const c_char,
    dest_port: c_ushort,
    proxy_type: u8,
    proxy_host: *const c_char,
    proxy_port: c_ushort,
    proxy_user: *const c_char,
    proxy_pass: *const c_char,
) -> c_int {
    let result = catch_unwind(AssertUnwindSafe(|| {
        let dest = match cstr(dest_host) {
            Some(d) if !d.is_empty() => d,
            _ => return -1,
        };
        let proxy = match proxy_from_ffi(proxy_type, proxy_host, proxy_port, proxy_user, proxy_pass)
        {
            Some(p) => p,
            None => return -1,
        };
        let stream = unsafe { TcpStream::from_raw_socket(local_socket as RawSocket) };
        relay::map_io_result(relay::relay_tcp_socket(stream, &dest, dest_port, &proxy))
    }));
    match result {
        Ok(r) => relay::map_io_result(r),
        Err(_) => -99,
    }
}

#[no_mangle]
pub extern "C" fn joyproxy_relay_test_proxy(
    proxy_type: u8,
    proxy_host: *const c_char,
    proxy_port: c_ushort,
    proxy_user: *const c_char,
    proxy_pass: *const c_char,
) -> c_int {
    let result = catch_unwind(AssertUnwindSafe(|| {
        let proxy = match proxy_from_ffi(proxy_type, proxy_host, proxy_port, proxy_user, proxy_pass)
        {
            Some(p) => p,
            None => return Err(RelayError::InvalidResponse("bad proxy".into())),
        };
        test_proxy(&proxy)
    }));
    match result {
        Ok(r) => relay::map_io_result(r),
        Err(_) => -99,
    }
}
