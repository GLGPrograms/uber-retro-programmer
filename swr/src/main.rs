use anyhow::{anyhow, Context};
use clap::{App, Arg, Parser};
use log::debug;
use log::{self, info};
use serialport::{available_ports, SerialPort};
use simple_logger::SimpleLogger;
use std::{process::exit, time::Duration};

const BUFFER_LEN: usize = 1024;

const S_ACK: u8 = 0x06;
const S_NAK: u8 = 0x15;
const S_CMD_NOP: u8 = 0x00;
const S_CMD_Q_PGMNAME: u8 = 0x03;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    SimpleLogger::new().init().unwrap();
    log::info!("serprog");

    let opts = App::new("serprog")
        .version("1.0")
        .author("giomba <giomba@glgprograms.it>")
        .about("serial programmer ")
        .arg(
            Arg::new("port")
                .short('p')
                .long("port")
                .about("serial port file")
                .default_value("/dev/ttyS0")
                .takes_value(true),
        )
        .arg(
            Arg::new("baudrate")
                .short('b')
                .long("baudrate")
                .about("serial port baud rate")
                .default_value("9600")
                .takes_value(true),
        )
        .get_matches();

    // get available serial ports list
    let serial_ports =
        available_ports().with_context(|| "cannot enumerate serial ports on this system")?;

    info!("{} serial ports found:", serial_ports.len());
    for serial_port_info in serial_ports {
        info!("{}", serial_port_info.port_name);
    }

    // build serial port
    let mut port = serialport::new(
        opts.value_of("port").unwrap(),
        match opts.value_of("baudrate").unwrap().parse() {
            Ok(r) => r,
            Err(e) => {
                log::error!("baudrate: {}", e);
                exit(exitcode::USAGE)
            }
        },
    )
    .timeout(Duration::from_millis(5000))
    .open()
    .with_context(|| "cannot open serial port")?;

    // get eeprom programmer name
    let programmer_name = get_programmer_name(&mut port).await?;
    info!("programmer name: {}", programmer_name);

    Ok(())
}

async fn get_programmer_name(port: &mut Box<dyn SerialPort>) -> anyhow::Result<String> {
    let obuffer = vec![S_CMD_Q_PGMNAME];
    let written = port.write(&obuffer)?;

    let mut ibuffer = [0; 17];
    port.read_exact(&mut ibuffer)?;

    let name = String::from(String::from_utf8_lossy(&ibuffer));
    Ok(name)
}

fn read(buffer: &[u8], length: usize) {}
