use log;
use serialport::available_ports;
use simple_logger::SimpleLogger;
use std::process::exit;

const BUFFER_LEN: usize = 1024;

fn main() {
    SimpleLogger::new().init().unwrap();
    log::info!("serprog");

    let serial_ports = match available_ports() {
        Err(e) => {
            log::error!("cannot enumerate serial ports on this system");
            exit(exitcode::UNAVAILABLE)
        }
        Ok(r) => r,
    };

    println!("{:#?}", serial_ports);

    let builder = serialport::new("/dev/ttyUSB1", 9600);
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
