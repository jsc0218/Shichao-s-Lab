use tokio::fs::File;
use tokio::io::{AsyncWriteExt, BufWriter as AsyncBufWriter};
use std::time::Instant;

#[tokio::main(flavor = "multi_thread")]
async fn main() -> std::io::Result<()> {
    let iterations = 1_000_000;
    let data = b"hello world\n"; // 12 bytes

    // ----------------------
    // Case 1: async unbuffered
    // ----------------------
    {
        let mut file = File::create("case1_flush_each.log").await?;
        let start = Instant::now();
        for _ in 0..iterations {
            file.write_all(data).await?;
        }
        let elapsed = start.elapsed();
        println!("Case 1: unbuffered async -> {:?}", elapsed);
    }

    // ----------------------
    // Case 1a: async buffered, flush per iteration
    // ----------------------
    {
        let file = File::create("case1a_flush_each_buffered.log").await?;
        let mut writer = AsyncBufWriter::new(file);
        let start = Instant::now();
        for _ in 0..iterations {
            writer.write_all(data).await?;
            writer.flush().await?;
        }
        let elapsed = start.elapsed();
        println!("Case 1a: async buffered, flush each -> {:?}", elapsed);
    }

    // ----------------------
    // Case 1b: async buffered, flush once at the end
    // ----------------------
    {
        let file = File::create("case1b_flush_once_buffered.log").await?;
        let mut writer = AsyncBufWriter::new(file);
        let start = Instant::now();
        for _ in 0..iterations {
            writer.write_all(data).await?;
        }
        writer.flush().await?;
        let elapsed = start.elapsed();
        println!("Case 1b: async buffered, flush once -> {:?}", elapsed);
    }

    // ----------------------
    // Case 2: synchronous std::fs
    // ----------------------
    {
        let elapsed = tokio::task::block_in_place(|| {
            use std::fs::File as SyncFile;
            use std::io::Write;
            use std::time::Instant;

            let mut file = SyncFile::create("case2_sync_io.log")?;
            let start = Instant::now();
            for _ in 0..iterations {
                file.write_all(data)?;
                file.flush()?;
            }
            Ok::<_, std::io::Error>(start.elapsed())
        })?;
        println!("Case 2: unbuffered std::fs -> {:?}", elapsed);
    }

    // ----------------------
    // Case 2a: synchronous std::fs + BufWriter, flush per iteration
    // ----------------------
    {
        let elapsed = tokio::task::block_in_place(|| {
            use std::fs::File as SyncFile;
            use std::io::{BufWriter as SyncBufWriter, Write};
            use std::time::Instant;

            let file = SyncFile::create("case2a_flush_each_buffered.log")?;
            let mut writer = SyncBufWriter::new(file);
            let start = Instant::now();
            for _ in 0..iterations {
                writer.write_all(data)?;
                writer.flush()?;
            }
            Ok::<_, std::io::Error>(start.elapsed())
        })?;
        println!("Case 2a: std::fs + BufWriter + flush each -> {:?}", elapsed);
    }

    // ----------------------
    // Case 2b: synchronous std::fs + BufWriter, flush once at the end
    // ----------------------
    {
        let elapsed = tokio::task::block_in_place(|| {
            use std::fs::File as SyncFile;
            use std::io::{BufWriter as SyncBufWriter, Write};
            use std::time::Instant;

            let file = SyncFile::create("case2b_flush_once_buffered.log")?;
            let mut writer = SyncBufWriter::new(file);
            let start = Instant::now();
            for _ in 0..iterations {
                writer.write_all(data)?;
            }
            writer.flush()?;
            Ok::<_, std::io::Error>(start.elapsed())
        })?;
        println!("Case 2b: std::fs + BufWriter + flush once -> {:?}", elapsed);
    }

    // ----------------------
    // Case 3: bulk write in one call
    // ----------------------
    {
        let mut file = File::create("case3_bulk.log").await?;
        let big_buf = data.repeat(iterations);
        let start = Instant::now();
        file.write_all(&big_buf).await?;
        let elapsed = start.elapsed();
        println!("Case 3: single bulk write -> {:?}", elapsed);
    }

    Ok(())
}
