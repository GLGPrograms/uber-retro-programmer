use clap::{App, Arg, Parser};
use log;
use serialport::available_ports;
use simple_logger::SimpleLogger;
use std::process::exit;

const BUFFER_LEN: usize = 1024;

fn main() {
    SimpleLogger::new().init().unwrap();
    log::info!("serprog");

    let opts = App::new("serprog")
        .version("1.0")
        .author("giomba <giomba@glgprograms.it>")
        .about("serial programmer ")
        .arg(
            Arg::new("device")
                .short('d')
                .long("device")
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

    let serial_ports = match available_ports() {
        Err(e) => {
            log::error!("cannot enumerate serial ports on this system");
            exit(exitcode::UNAVAILABLE)
        }
        Ok(r) => r,
    };

    println!("{:#?}", serial_ports);

    let builder = serialport::new(
        opts.value_of("device").unwrap(),
        match opts.value_of("baudrate").unwrap().parse() {
            Ok(r) => r,
            Err(e) => {
                log::error!("baudrate: {}", e);
                exit(exitcode::USAGE)
            }
        },
    );
    let mut port = match builder.open() {
        Err(e) => {
            log::error!("cannot open serial port: {}", e);
            exit(exitcode::IOERR);
        }
        Ok(r) => r,
    };

    let mut buffer: [u8; BUFFER_LEN] = [0; BUFFER_LEN];
    let result = port.read(&mut buffer);
    if let Err(e) = result {
        log::error!("cannot read serial port: {}", e);
        exit(exitcode::IOERR);
    }
    let n = result.ok().unwrap();
    println!("{} bytes available", n);
}
