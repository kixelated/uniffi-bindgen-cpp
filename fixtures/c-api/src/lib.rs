//! Every construct the C backend renders: objects, records with defaults, flat and data
//! enums, errors with and without `Display`, bytes, optionals, lists, maps, and async calls
//! that complete, fail, or stay pending until cancelled.

use std::{
    collections::{HashMap, VecDeque},
    future::Future,
    pin::Pin,
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc, Mutex,
    },
    task::{Context, Poll, Waker},
};

uniffi::setup_scaffolding!("capi");

/// A primary color.
#[derive(Clone, Copy, Debug, PartialEq, Eq, uniffi::Enum)]
pub enum CapiColor {
    Red,
    Green,
    Blue,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, uniffi::Record)]
pub struct CapiPoint {
    pub x: i32,
    pub y: i32,
}

/// A shape, with data on some variants.
#[derive(Clone, Debug, PartialEq, uniffi::Enum)]
pub enum CapiShape {
    Dot,
    Circle { radius: f64 },
    Label(String),
    Path { points: Vec<CapiPoint> },
}

/// One item, with a field of every kind.
#[derive(Clone, uniffi::Record)]
pub struct CapiItem {
    pub name: String,
    #[uniffi(default = 7)]
    pub count: u32,
    #[uniffi(default = "untitled \"draft\"")]
    pub title: String,
    #[uniffi(default = 1.5)]
    pub scale: f64,
    #[uniffi(default = true)]
    pub visible: bool,
    #[uniffi(default = -3)]
    pub offset: i64,
    #[uniffi(default = None)]
    pub note: Option<String>,
    #[uniffi(default = None)]
    pub limit: Option<u64>,
    #[uniffi(default = None)]
    pub tint: Option<CapiColor>,
    #[uniffi(default = None)]
    pub target: Option<CapiPoint>,
    #[uniffi(default = None)]
    pub extra: Option<Vec<u8>>,
    #[uniffi(default = None)]
    pub owner: Option<Arc<CapiStore>>,
    #[uniffi(default = [])]
    pub tags: Vec<String>,
    pub payload: Vec<u8>,
    pub origin: CapiPoint,
    pub color: CapiColor,
    pub shape: CapiShape,
    pub labels: HashMap<String, String>,
    pub points: HashMap<String, CapiPoint>,
    pub values: Vec<u64>,
    pub maybes: Vec<Option<u32>>,
}

#[derive(Debug, thiserror::Error, uniffi::Error)]
#[uniffi::export(Display)]
pub enum CapiError {
    #[error("not found")]
    NotFound,
    #[error("invalid: {0}")]
    Invalid(String),
    #[error("code {code} at {},{}", at.x, at.y)]
    Code { code: u32, at: CapiPoint },
}

/// Variants carry only their message.
#[derive(Debug, thiserror::Error, uniffi::Error)]
#[uniffi(flat_error)]
pub enum CapiFlatError {
    #[error("busy: {0}")]
    Busy(String),
    #[error("gone")]
    Gone,
}

#[derive(Default)]
struct Queue {
    values: VecDeque<u64>,
    wakers: Vec<Waker>,
    closed: bool,
}

/// A named list of items, plus a queue that async readers wait on.
#[derive(uniffi::Object)]
pub struct CapiStore {
    name: String,
    items: Mutex<Vec<CapiItem>>,
    queue: Mutex<Queue>,
}

#[uniffi::export]
impl CapiStore {
    #[uniffi::constructor]
    pub fn new(name: String) -> Arc<Self> {
        Self::with_items(name, Vec::new())
    }

    #[uniffi::constructor]
    pub fn with_items(name: String, items: Vec<CapiItem>) -> Arc<Self> {
        Arc::new(Self {
            name,
            items: Mutex::new(items),
            queue: Mutex::default(),
        })
    }

    /// Fails on an empty name.
    #[uniffi::constructor]
    pub fn open(name: String) -> Result<Arc<Self>, CapiError> {
        if name.is_empty() {
            return Err(CapiError::Invalid("empty name".into()));
        }
        Ok(Self::new(name))
    }

    pub fn name(&self) -> String {
        self.name.clone()
    }

    pub fn len(&self) -> u64 {
        self.items.lock().unwrap().len() as u64
    }

    /// Returns the new item's index, or fails on an empty name.
    pub fn add(&self, item: CapiItem) -> Result<u32, CapiError> {
        if item.name.is_empty() {
            return Err(CapiError::Invalid("empty item name".into()));
        }
        let mut items = self.items.lock().unwrap();
        items.push(item);
        Ok(items.len() as u32 - 1)
    }

    pub fn get(&self, index: u32) -> Result<CapiItem, CapiError> {
        self.items
            .lock()
            .unwrap()
            .get(index as usize)
            .cloned()
            .ok_or(CapiError::Code {
                code: index,
                at: CapiPoint { x: 1, y: 2 },
            })
    }

    pub fn find(&self, name: String) -> Option<CapiItem> {
        self.items
            .lock()
            .unwrap()
            .iter()
            .find(|item| item.name == name)
            .cloned()
    }

    pub fn note(&self, index: u32) -> Option<String> {
        self.items.lock().unwrap().get(index as usize)?.note.clone()
    }

    pub fn limit(&self, index: u32) -> Option<u64> {
        self.items.lock().unwrap().get(index as usize)?.limit
    }

    pub fn names(&self) -> Vec<String> {
        self.items
            .lock()
            .unwrap()
            .iter()
            .map(|item| item.name.clone())
            .collect()
    }

    pub fn clear(&self) {
        self.items.lock().unwrap().clear();
    }

    /// A new store with the same items, named `name` or this store's name.
    pub fn fork(&self, name: Option<String>) -> Arc<CapiStore> {
        Self::with_items(
            name.unwrap_or_else(|| self.name.clone()),
            self.items.lock().unwrap().clone(),
        )
    }

    /// The total number of items in this store, `other`, and `extra`.
    pub fn total(&self, other: Arc<CapiStore>, extra: Option<Arc<CapiStore>>) -> u64 {
        self.len() + other.len() + extra.map_or(0, |extra| extra.len())
    }

    /// Wakes one `next` call with `value`.
    pub fn push(&self, value: u64) {
        let mut queue = self.queue.lock().unwrap();
        queue.values.push_back(value);
        for waker in queue.wakers.drain(..) {
            waker.wake();
        }
    }

    /// Fails every pending and later `next` call.
    pub fn close(&self) {
        let mut queue = self.queue.lock().unwrap();
        queue.closed = true;
        for waker in queue.wakers.drain(..) {
            waker.wake();
        }
    }

    /// Waits for a pushed value, or fails once the store is closed.
    pub async fn next(&self) -> Result<u64, CapiError> {
        Next { store: self }.await
    }

    /// Never completes; `capi_pending_count` counts the calls not yet dropped.
    pub async fn wait_forever(&self) {
        PENDING.fetch_add(1, Ordering::SeqCst);
        let _guard = PendingGuard;
        std::future::pending::<()>().await;
    }
}

struct Next<'a> {
    store: &'a CapiStore,
}

impl Future for Next<'_> {
    type Output = Result<u64, CapiError>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut queue = self.store.queue.lock().unwrap();
        if let Some(value) = queue.values.pop_front() {
            Poll::Ready(Ok(value))
        } else if queue.closed {
            Poll::Ready(Err(CapiError::NotFound))
        } else {
            queue.wakers.push(cx.waker().clone());
            Poll::Pending
        }
    }
}

static PENDING: AtomicU64 = AtomicU64::new(0);

struct PendingGuard;

impl Drop for PendingGuard {
    fn drop(&mut self) {
        PENDING.fetch_sub(1, Ordering::SeqCst);
    }
}

/// How many `wait_forever` calls are still alive.
#[uniffi::export]
pub fn capi_pending_count() -> u64 {
    PENDING.load(Ordering::SeqCst)
}

#[uniffi::export]
pub fn capi_echo(item: CapiItem) -> CapiItem {
    item
}

#[uniffi::export]
pub fn capi_checksum(payload: Vec<u8>) -> u32 {
    payload.iter().map(|&b| b as u32).sum()
}

/// Describes which optionals are present.
#[uniffi::export]
pub fn capi_describe(
    value: Option<u64>,
    color: Option<CapiColor>,
    text: Option<String>,
    point: Option<CapiPoint>,
    bytes: Option<Vec<u8>>,
) -> String {
    format!("{value:?} {color:?} {text:?} {point:?} {bytes:?}")
}

#[uniffi::export]
pub fn capi_make_shape(sides: u32) -> CapiShape {
    match sides {
        0 => CapiShape::Dot,
        1 => CapiShape::Circle { radius: 2.5 },
        2 => CapiShape::Label("two".into()),
        _ => CapiShape::Path {
            points: (0..sides as i32).map(|x| CapiPoint { x, y: -x }).collect(),
        },
    }
}

#[uniffi::export]
pub fn capi_fail(code: u32) -> Result<(), CapiError> {
    match code {
        0 => Ok(()),
        1 => Err(CapiError::NotFound),
        2 => Err(CapiError::Invalid("two".into())),
        _ => Err(CapiError::Code {
            code,
            at: CapiPoint { x: 3, y: 4 },
        }),
    }
}

#[uniffi::export]
pub fn capi_flat_fail(gone: bool) -> Result<(), CapiFlatError> {
    Err(if gone {
        CapiFlatError::Gone
    } else {
        CapiFlatError::Busy("later".into())
    })
}

/// Ready on its first poll.
#[uniffi::export]
pub async fn capi_sum(values: Vec<u64>) -> u64 {
    values.iter().sum()
}

#[uniffi::export]
pub async fn capi_greet(name: String) -> Result<String, CapiError> {
    if name.is_empty() {
        return Err(CapiError::Invalid("nobody".into()));
    }
    Ok(format!("hello, {name}"))
}
