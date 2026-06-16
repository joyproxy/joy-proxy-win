use std::fmt;

#[derive(Debug)]
pub enum RelayError {
    Io(std::io::Error),
    InvalidResponse(String),
    ProxyAuthFailed,
    UnsupportedAddress,
    Timeout,
}

impl fmt::Display for RelayError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RelayError::Io(e) => write!(f, "io: {e}"),
            RelayError::InvalidResponse(s) => write!(f, "invalid response: {s}"),
            RelayError::ProxyAuthFailed => write!(f, "proxy authentication failed"),
            RelayError::UnsupportedAddress => write!(f, "unsupported address family"),
            RelayError::Timeout => write!(f, "timeout"),
        }
    }
}

impl std::error::Error for RelayError {}

impl From<std::io::Error> for RelayError {
    fn from(value: std::io::Error) -> Self {
        RelayError::Io(value)
    }
}

pub type Result<T> = std::result::Result<T, RelayError>;
