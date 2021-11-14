use anyhow::{anyhow, Context};
use clap::{App, Arg, Parser};
use indicatif::ProgressBar;
use log::debug;
use log::{self, info};
use serialport::{available_ports, SerialPort};
use simple_logger::SimpleLogger;
use std::fs::File;
use std::io::{Read, Write};
use std::os::unix::prelude::FileExt;
use std::{process::exit, time::Duration};

const BUFFER_LEN: usize = 1024;

const S_ACK: u8 = 0x06;
const S_CMD_Q_R_NBYTES: u8 = 0x0a;
const S_CMD_O_WRITEN: u8 = 0x0d;
const S_NAK: u8 = 0x15;
const S_CMD_NOP: u8 = 0x00;
const S_CMD_Q_PGMNAME: u8 = 0x03;
const S_CMD_O_EXEC: u8 = 0x0f;

struct Programmer {
    port: Box<dyn SerialPort>,
    pb: ProgressBar,
}

impl Programmer {
    fn new(port: Box<dyn SerialPort>) -> Self {
        Programmer {
            port,
            pb: ProgressBar::new(100),
        }
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    // start logging utilities
    SimpleLogger::new().init().unwrap();

    // parse command line arguments
    let opts = App::new("serprog")
        .version("1.0")
        .author("giomba <giomba@glgprograms.it>")
        .about("CLI for ÜRP Programmer by Retrofficina GLG")
        .arg(
            Arg::new("port")
                .short('p')
                .long("port")
                .about("serial port file")
                .default_value("/dev/ttyUSB0")
                .takes_value(true),
        )
        .arg(
            Arg::new("baudrate")
                .short('b')
                .long("baudrate")
                .about("serial port baud rate")
                .default_value("38400")
                .takes_value(true),
        )
        .arg(
            Arg::new("size")
                .short('s')
                .long("size")
                .about("size in bytes")
                .takes_value(true),
        )
        .arg(
            Arg::new("read")
                .short('r')
                .long("read")
                .about("read EEPROM to file")
                .value_name("filename")
                .takes_value(true)
                .requires("size"),
        )
        .arg(
            Arg::new("addr")
                .short('a')
                .long("addr")
                .about("starting address")
                .default_value("0")
                .takes_value(true),
        )
        .arg(
            Arg::new("write")
                .short('w')
                .long("write")
                .about("write file to EEPROM")
                .value_name("filename")
                .takes_value(true)
                .requires("size"),
        )
        .get_matches();

    // get available serial ports list
    let serial_ports = available_ports().with_context(|| "cannot enumerate serial ports")?;

    info!("{} serial ports found:", serial_ports.len());
    for serial_port_info in serial_ports {
        info!("{}", serial_port_info.port_name);
    }

    // build serial port
    let port = serialport::new(
        opts.value_of("port").unwrap(),
        opts.value_of("baudrate").unwrap().parse()?,
    )
    .timeout(Duration::from_millis(5000))
    .open()
    .with_context(|| "cannot open serial port")?;

    let mut programmer = Programmer::new(port);

    let name = programmer
        .get_name()
        .await
        .with_context(|| "cannot get programmer name")?;

    info!("succesfully connected to: {}", name);

    // if operation is read
    if let Some(filename) = opts.value_of("read") {
        // parse needed arguments
        let base = opts.value_of("addr").unwrap().parse()?;
        let size: usize = opts.value_of("size").unwrap().parse()?;
        println!("reading {} bytes into {}", size, filename);

        // read in memory
        let mut data: Vec<u8> = Vec::new();
        data.resize(size, 0);
        programmer.read_bytes(base, &mut data).await?;

        let mut file = File::create(filename).with_context(|| "cannot open output file")?;
        file.write_all(&data)
            .with_context(|| "cannot write data to file")?;
    }

    // if operation is write
    if let Some(filename) = opts.value_of("write") {
        // parse needed arguments
        let base = opts.value_of("addr").unwrap().parse()?;
        let size: usize = opts.value_of("size").unwrap().parse()?;
        println!("writing {} bytes from {}", size, filename);

        // write file
        let mut data: Vec<u8> = Vec::new();
        data.resize(size, 0);
        let mut file = File::open(filename).with_context(|| "cannot open input file")?;
        file.read_exact(&mut data)?;

        programmer.write_bytes(base, &data).await?;
    }

    Ok(())
}

impl Programmer {
    // get eeprom programmer name
    async fn get_name(&mut self) -> anyhow::Result<String> {
        let obuffer = vec![S_CMD_Q_PGMNAME];
        let _written = self.port.write(&obuffer)?;

        let mut ibuffer = [0; 16];
        //self.port.read_exact(&mut ibuffer)?;
        self.recv_with_ack(&mut ibuffer).await?;

        let name = String::from(String::from_utf8_lossy(&ibuffer));
        Ok(name)
    }

    async fn read_bytes(&mut self, base: usize, data: &mut [u8]) -> anyhow::Result<()> {
        let mut done = 0;
        let mut remaining = data.len();
        const CHUNK_SIZE: usize = 64;

        self.pb.reset();
        while done < data.len() {
            let current_chunk_size = if remaining > CHUNK_SIZE {
                CHUNK_SIZE
            } else {
                remaining
            };
            self.read_bytes_chunk(base + done, &mut data[done..done + current_chunk_size])
                .await?;
            let percent = (done as f64 / data.len() as f64 * 100.0) as u64;
            self.pb.set_position(percent);
            remaining -= current_chunk_size;
            done += current_chunk_size;
        }
        self.pb.finish();

        Ok(())
    }

    async fn read_bytes_chunk(&mut self, base: usize, data: &mut [u8]) -> anyhow::Result<()> {
        let mut obuffer = [0; 7];
        obuffer[0] = S_CMD_Q_R_NBYTES;
        obuffer[1] = (base & 0xff) as u8;
        obuffer[2] = (base >> 8 & 0xff) as u8;
        obuffer[3] = (base >> 16 & 0xff) as u8;
        obuffer[4] = (data.len() & 0xff) as u8;
        obuffer[5] = (data.len() >> 8 & 0xff) as u8;
        obuffer[6] = (data.len() >> 16 & 0xff) as u8;
        let _written = self.port.write(&obuffer)?;

        self.recv_with_ack(data).await?;

        Ok(())
    }

    async fn write_bytes(&mut self, base: usize, data: &[u8]) -> anyhow::Result<()> {
        let mut done = 0;
        let mut remaining = data.len();
        const CHUNK_SIZE: usize = 64;

        self.pb.reset();
        while done < data.len() {
            let current_chunk_size = if remaining > CHUNK_SIZE {
                CHUNK_SIZE
            } else {
                remaining
            };
            self.write_bytes_chunk(base + done, &data[done..done + current_chunk_size])
                .await?;

            let percent = (done as f64 / data.len() as f64 * 100.0) as u64;
            self.pb.set_position(percent);
            remaining -= current_chunk_size;
            done += current_chunk_size;
        }

        Ok(())
    }

    async fn write_bytes_chunk(&mut self, base: usize, data: &[u8]) -> anyhow::Result<()> {
        let mut obuffer = Vec::new();
        obuffer.resize(7, 0);
        obuffer[0] = S_CMD_O_WRITEN;
        obuffer[1] = (data.len() & 0xff) as u8;
        obuffer[2] = (data.len() >> 8 & 0xff) as u8;
        obuffer[3] = (data.len() >> 16 & 0xff) as u8;
        obuffer[4] = (base & 0xff) as u8;
        obuffer[5] = (base >> 8 & 0xff) as u8;
        obuffer[6] = (base >> 16 & 0xff) as u8;

        let _written = self.port.write(&obuffer)?;
        let _written = self.port.write(data)?;

        let obuffer = vec![S_CMD_O_EXEC];
        let _written = self.port.write(&obuffer)?;

        Ok(())
    }

    async fn recv_with_ack(&mut self, data: &mut [u8]) -> anyhow::Result<()> {
        let mut ack_buffer = [0; 1];
        self.port
            .read_exact(&mut ack_buffer)
            .with_context(|| "ack not received")?;

        if ack_buffer[0] != S_ACK {
            return Err(anyhow!("received NACK"));
        }

        self.port
            .read_exact(data)
            .with_context(|| "not enough data received")?;

        Ok(())
    }
}
