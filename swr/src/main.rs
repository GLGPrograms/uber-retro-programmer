use anyhow::{anyhow, Context};
use clap::{App, Arg, Parser};
use indicatif::ProgressBar;
use log::debug;
use log::{self, info};
use serialport::{available_ports, SerialPort};
use simple_logger::SimpleLogger;
use std::os::unix::prelude::FileExt;
use std::{process::exit, time::Duration};

const BUFFER_LEN: usize = 1024;

const S_ACK: u8 = 0x06;
const S_CMD_Q_R_NBYTES: u8 = 0x0a;
const S_NAK: u8 = 0x15;
const S_CMD_NOP: u8 = 0x00;
const S_CMD_Q_PGMNAME: u8 = 0x03;

struct Programmer {
    port: Box<dyn SerialPort>,
}

impl Programmer {
    fn new(port: Box<dyn SerialPort>) -> Self {
        Programmer { port }
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    SimpleLogger::new().init().unwrap();
    log::info!("serprog");

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

    let pb = ProgressBar::new(100);

    if let Some(filename) = opts.value_of("read") {
        let size: usize = opts.value_of("size").unwrap().parse()?;
        println!("reading {} bytes into {}", size, filename);

        let mut data: Vec<u8> = Vec::new();
        data.resize(size, 0);
        let mut done = 0;
        let mut remaining = size;
        const CHUNK_SIZE: usize = 64;
        while done < size {
            let current_chunk_size = if remaining > CHUNK_SIZE {
                CHUNK_SIZE
            } else {
                remaining
            };
            programmer
                .get_bytes(done, &mut data[done..done + current_chunk_size])
                .await?;
            let percent = (done as f64 / size as f64 * 100.0) as u64;
            pb.set_position(percent);
            remaining -= current_chunk_size;
            done += current_chunk_size;
        }
        pb.finish();
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

    async fn get_bytes(&mut self, base: usize, data: &mut [u8]) -> anyhow::Result<()> {
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

/*
async fn read_bytes(base: usize, length: usize) -> Vec<u8> {
    assert!(base < (1 << 24));
    assert!(length < (1 << 24));

    let mut ibuffer

    let mut ibuffer : [u8; ]
}
*/
