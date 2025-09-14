use tokio::io::{AsyncReadExt, AsyncWriteExt};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    for i in 0..100_000 {
        let filepath = "settings.log";

        // Create and write to file
        let mut file = tokio::fs::File::create(filepath).await?;
        file.write_all(b"test").await?;
        // file.flush().await?; // test push buffered data to OS
        file.sync_data().await?; // test sync pushed all data to disk 

        // Read back
        let mut data = Vec::new();
        let mut f = tokio::fs::File::open(filepath).await?;
        let count = f.read_to_end(&mut data).await?;

        if count == 0 {
            panic!("READ returned 0 bytes at run {i}");
        }

        if count != 4 {
            panic!(
                "READ not returned 4 bytes at run {i}"
            );
        }
    }

    println!("Finished read/write cycles without empty reads.");
    Ok(())
}
