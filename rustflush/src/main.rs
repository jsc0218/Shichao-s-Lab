use tokio::fs::{self, OpenOptions, File};
use tokio::io::{AsyncWriteExt, AsyncReadExt, AsyncSeekExt};
use std::error::Error;


#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    //single_writer().await?;
    multi_writer().await?;
    Ok(())
}

async fn single_writer() -> Result<(), Box<dyn Error>> {
    println!("Running single writer...");
    let filepath = "settings.log";

    // Delete the file if it exists
    if fs::metadata(filepath).await.is_ok() {
        fs::remove_file(filepath).await?;
    }

    let mut expected_len = 0u64;

    for i in 0..100_00 {
        // Open file in append mode
        let mut file = OpenOptions::new()
            .append(true)
            .create(true)
            .open(filepath)
            .await?;

        // Write 4 bytes
        file.write_all(b"test").await?;
        //file.flush().await?;
        file.sync_data().await?;

        // Check file length
        let metadata = tokio::fs::metadata(filepath).await?;
        let file_len = metadata.len();

        // Assert file length increased by 4 bytes
        expected_len += 4;
        assert_eq!(file_len, expected_len, "File length mismatch at run {i}");

        // Read last 4 bytes to verify write persisted
        let mut f = tokio::fs::File::open(filepath).await?;
        f.seek(tokio::io::SeekFrom::Start(file_len - 4)).await?;
        let mut last4 = [0u8; 4];
        f.read_exact(&mut last4).await?;
        assert_eq!(&last4, b"test", "Last 4 bytes mismatch at run {i}");
    }

    println!("Finished 100,000 append + sync_data() cycles successfully.");
    Ok(())
}

async fn multi_writer() -> Result<(), Box<dyn Error>> {
    println!("Running multi writer...");

    let filepath = "settings.log";

    // Clean up any existing file
    if fs::metadata(filepath).await.is_ok() {
        fs::remove_file(filepath).await?;
    }

    let mut expected_len = 0u64;

    // Writer A: only writes + flush
    let mut writer_a = OpenOptions::new()
        .append(true)
        .create(true)
        .open(filepath)
        .await?;

    // Writer B: only issues fsync/fdatasync
    let mut writer_b = OpenOptions::new()
        .write(true)
        .open(filepath)
        .await?;

    for i in 0..100_00 {
        // --- writer A writes ---
        writer_a.write_all(b"test").await?;
        //writer_a.flush().await?; // flush libc buffers (not disk yet)

        // --- writer B issues fdatasync ---
        writer_b.sync_data().await?;

        // --- reader validates ---
        let metadata = fs::metadata(filepath).await?;
        let file_len = metadata.len();

        expected_len += 4;
        assert_eq!(file_len, expected_len, "File length mismatch at run {i}");

        let mut reader = File::open(filepath).await?;
        reader
            .seek(tokio::io::SeekFrom::Start(file_len - 4))
            .await?;
        let mut last4 = [0u8; 4];
        reader.read_exact(&mut last4).await?;
        assert_eq!(&last4, b"test", "Last 4 bytes mismatch at run {i}");
    }

    println!("Finished 10,000 write+flush + sync_data cycles successfully.");
    Ok(())
}
