use std::fs::{OpenOptions, File};
use std::io::{Seek, SeekFrom, Write};

fn main() -> std::io::Result<()> {
    let path = std::env::args().nth(1).expect("Укажите путь к бинарному файлу");
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .open(&path)?;

    // Патч 1
    file.seek(SeekFrom::Start(0x0000159E))?;
    file.write_all(&[0xEB])?;

    // Патч 2
    file.seek(SeekFrom::Start(0x0000159F))?;
    file.write_all(&[0x00])?;

    println!("Патч применён: 0x159E=0xEB, 0x159F=0x00");
    Ok(())
}
