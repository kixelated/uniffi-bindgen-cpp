/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

mod platform_specific;
use platform_specific::acquire_with_timeout;

use futures_timer::Delay;
use std::{
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
    time::Duration,
};

static PENDING_COUNT: AtomicU64 = AtomicU64::new(0);

struct PendingGuard;

impl Drop for PendingGuard {
    fn drop(&mut self) {
        PENDING_COUNT.fetch_sub(1, Ordering::SeqCst);
    }
}

#[uniffi::export]
pub fn greet(who: String) -> String {
    format!("Hello, {who}")
}

pub async fn always_ready() -> bool {
    true
}

#[uniffi::export]
pub async fn void_return() {}

#[uniffi::export]
pub async fn say() -> String {
    Delay::new(Duration::from_secs(2)).await;
    "Hello, Future!".to_string()
}

#[uniffi::export]
pub async fn say_after(ms: u16, who: String) -> String {
    Delay::new(Duration::from_millis(ms.into())).await;
    format!("Hello, {who}!")
}

#[uniffi::export]
pub async fn sleep(ms: u16) -> bool {
    Delay::new(Duration::from_millis(ms.into())).await;
    true
}

#[uniffi::export]
pub async fn sleep_no_return(ms: u16) {
    Delay::new(Duration::from_millis(ms.into())).await;
}

#[uniffi::export]
pub async fn pending() {
    PENDING_COUNT.fetch_add(1, Ordering::SeqCst);
    let _guard = PendingGuard;
    std::future::pending::<()>().await;
}

#[uniffi::export]
pub fn pending_count() -> u64 {
    PENDING_COUNT.load(Ordering::SeqCst)
}

#[derive(thiserror::Error, uniffi::Error, Debug)]
pub enum MyError {
    #[error("Foo")]
    Foo,
}

#[uniffi::export]
pub async fn fallible_me(do_fail: bool) -> Result<u8, MyError> {
    if do_fail {
        Err(MyError::Foo)
    } else {
        Ok(42)
    }
}

#[uniffi::export]
pub async fn fallible_struct(do_fail: bool) -> Result<Arc<Megaphone>, MyError> {
    if do_fail {
        Err(MyError::Foo)
    } else {
        Ok(new_megaphone())
    }
}

#[uniffi::export]
pub fn new_megaphone() -> Arc<Megaphone> {
    Arc::new(Megaphone)
}

#[uniffi::export]
pub async fn async_new_megaphone() -> Arc<Megaphone> {
    new_megaphone()
}

#[uniffi::export]
pub async fn async_maybe_new_megaphone(value: bool) -> Option<Arc<Megaphone>> {
    value.then(new_megaphone)
}

#[uniffi::export]
pub async fn say_after_with_megaphone(megaphone: Arc<Megaphone>, ms: u16, who: String) -> String {
    megaphone.say_after(ms, who).await
}

#[derive(uniffi::Object)]
pub struct Megaphone;

#[uniffi::export]
impl Megaphone {
    #[uniffi::constructor]
    pub async fn new() -> Arc<Self> {
        Delay::new(Duration::from_millis(0)).await;
        Arc::new(Self)
    }

    #[uniffi::constructor]
    pub async fn secondary() -> Arc<Self> {
        Delay::new(Duration::from_millis(0)).await;
        Arc::new(Self)
    }

    pub async fn say_after(self: Arc<Self>, ms: u16, who: String) -> String {
        say_after(ms, who).await.to_uppercase()
    }

    pub async fn silence(&self) -> String {
        String::new()
    }

    pub async fn fallible_me(self: Arc<Self>, do_fail: bool) -> Result<u8, MyError> {
        fallible_me(do_fail).await
    }
}

#[derive(uniffi::Object)]
pub struct FallibleMegaphone;

#[uniffi::export]
impl FallibleMegaphone {
    #[uniffi::constructor]
    pub async fn new() -> Result<Arc<Self>, MyError> {
        Err(MyError::Foo)
    }
}

pub struct UdlMegaphone;

impl UdlMegaphone {
    pub async fn new() -> Self {
        Self
    }

    pub async fn secondary() -> Self {
        Self
    }

    pub async fn say_after(self: Arc<Self>, ms: u16, who: String) -> String {
        say_after(ms, who).await.to_uppercase()
    }
}

#[derive(uniffi::Record)]
pub struct MyRecord {
    pub a: String,
    pub b: u32,
}

#[uniffi::export]
pub async fn new_my_record(a: String, b: u32) -> MyRecord {
    MyRecord { a, b }
}

#[derive(uniffi::Record)]
pub struct SharedResourceOptions {
    pub release_after_ms: u16,
    pub timeout_ms: u16,
}

#[derive(thiserror::Error, uniffi::Error, Debug)]
pub enum AsyncError {
    #[error("Timeout")]
    Timeout,
}

#[uniffi::export(async_runtime = "tokio")]
pub async fn use_shared_resource(options: SharedResourceOptions) -> Result<(), AsyncError> {
    acquire_with_timeout(options).await
}

uniffi::include_scaffolding!("async-calls");
