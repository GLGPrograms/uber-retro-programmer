#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdarg.h>
#include <setjmp.h>
#include <getopt.h>
#include "serprog.h"
#include <sys/stat.h>

#define DEFAULT_DEVICE "/dev/ttyUSB0"

typedef int bool;

#define false (0)
#define true (1)

static jmp_buf err;

int config_serial(int fd, int speed, int parity) {
  int ret;
  struct termios tty;

  memset(&tty, 0, sizeof(tty));
  
  ret = tcgetattr(fd, &tty);
  if (ret < 0)
    longjmp(err, ret);

  cfsetospeed(&tty, speed);
  cfsetispeed(&tty, speed);
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;  // 8-bit chars

  // Input flags - Turn off input processing
  //
  // convert break to null byte, no CR to NL translation,
  // no NL to CR translation, don't mark parity errors or breaks
  // no input parity check, don't strip high bit off,
  // no XON/XOFF software flow control
  tty.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP |
                   IXON | IXOFF | IXANY);
  tty.c_lflag = 0;     // no signaling chars, no echo, no canonical processing
  tty.c_oflag = 0;     // no remapping, no delays
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 100;// ten seconds timeout
  tty.c_cflag |= (CLOCAL | CREAD);    // ignore modem controls, enable reading
  tty.c_cflag &= ~(PARENB | PARODD);  // no parity by default
  tty.c_cflag |= parity;
  tty.c_cflag &= ~CSTOPB;   // 1 stop bit

  ret = tcsetattr(fd, TCSANOW, &tty);
  if (ret < 0)
    longjmp(err, ret);

  return 0;
}

int init_serial(char const* path) {
  int fd = open(path, O_RDWR | O_NOCTTY | O_SYNC);
  if (fd < 0)
    longjmp(err, fd);
  
  config_serial(fd, B38400, 0);
    
  return fd;
}

#define STDIN 0
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

void hexdump(const void* buf, const unsigned len) {
  printf("%6.6X", 0);

  for (unsigned i = 0, chunk = 0; i < len; i++, chunk++) {
    printf(" %2.2X", *((const char*)buf + i) & 0xFF);

    if (chunk == 15) {
      printf("\n%6.6X", i+1);
      chunk = -1;
    }
  }
  printf("\n");
}

void printHex(char* p, int n) {
  for (int i = 0; i < n; ++i) {
    printf("%2X ", p[i] & 0xff);
  }
  printf("\n");
}

typedef enum _log_level {
  FATAL,
  ERROR,
  WARNING,
  INFO,
  DEBUG
} log_level;

log_level g_log_level = INFO;

void print(log_level l, const char* fmt, ...) {
  if (g_log_level < l)
    return;
  
  va_list ap;
  va_start(ap, fmt);
  switch(l){
    case DEBUG:
      printf("[DEBUG  ] ");
      break;
    case INFO:
      printf("[INFO   ] ");
      break;
    case WARNING:
      printf("[WARNING] ");
      break;
    case ERROR:
      printf("[ERROR  ] ");
      break;
    case FATAL:
      printf("[FATAL  ] ");
      break;
  }
  vprintf(fmt, ap);
  va_end(ap);
}

void timed_read(int fd, uint8_t* data, const uint32_t len) {
  int proc = 0;
  int tlen = len + 1; // ACK +  buffer
  uint8_t *buf = malloc(len+1);
  
  do {
    int ret;
    
    // Read must return after a certain timeout. See termios.
    ret = read(fd, buf + proc, tlen - proc);

    if (ret <= 0) {
      free(buf);
      longjmp(err, ret == 0 ? 1 : ret);
    }

    proc += ret;
  } while (proc < tlen);

  tcflush(fd, TCIOFLUSH);

  print(DEBUG, "Read %d bytes\n", proc);

  if (buf[0] == S_ACK)
    print(DEBUG, "ACK\n");
  else if (buf[0] == S_NAK)
    print(DEBUG, "NAK\n");
  else
    print(DEBUG, "WTF: %x\n", buf[0]);

  // Remove ack
  memcpy(data, buf+1, len);
  free(buf);
}

void op_pgmname(int fd) {
  int ret;
  uint8_t op = S_CMD_Q_PGMNAME;
  char pgmname[16];

  ret = write(fd, &op, 1);
  if (ret < 0)
    longjmp(err, ret);

  timed_read(fd, (uint8_t*)pgmname, sizeof(pgmname));

  print(INFO, "Successfully connected programmer %.*s\n", sizeof(pgmname), pgmname);
}

void op_opbuf_init(int fd) {
  int ret;
  uint8_t op = S_CMD_O_INIT;

  ret = write(fd, &op, 1);
  if (ret < 0)
    longjmp(err, ret);

  timed_read(fd, NULL, 0);
}

uint16_t op_opbuf_len(int fd) {
  int ret;
  uint8_t op = S_CMD_Q_OPBUF;
  uint16_t opbuf_len;

  ret = write(fd, &op, 1);
  if (ret < 0)
    longjmp(err, ret);

  timed_read(fd, (uint8_t*)&opbuf_len, sizeof(opbuf_len));

  print(DEBUG, "Opbuf len is %d\n", opbuf_len);
  return opbuf_len;
}

uint16_t op_serbuf_len(int fd) {
  int ret;
  uint8_t op = S_CMD_Q_SERBUF;
  uint16_t serbuf_len;

  ret = write(fd, &op, 1);
  if (ret < 0)
    longjmp(err, ret);

  timed_read(fd, (uint8_t*)&serbuf_len, sizeof(serbuf_len));

  print(DEBUG, "Serbuf len is %d\n", serbuf_len);
  return serbuf_len;
}

void op_opbuf_write(int fd, const uint32_t ba, const uint8_t* buf, const uint32_t len) {
  int ret;
  const uint8_t header[] = {
    S_CMD_O_WRITEN, // Opcode
    len & 0xFF, (len >> 8) & 0xFF, (len >> 16) & 0xFF, // 24-bit length, LE
    ba & 0xFF, (ba >> 8) & 0xFF, (ba >> 16) & 0xFF, // 24-bit addr, LE
  };

  ret = write(fd, header, sizeof(header));
  if (ret < 0)
    longjmp(err, ret);
  
  ret = write(fd, buf, len);
  if (ret < 0)
    longjmp(err, ret);

  timed_read(fd, NULL, 0);
}

void op_opbuf_sdp(int fd, bool enable) {
  int ret;
  uint8_t op = S_CMD_O_RESET_SDP;

  if (enable)
    op = S_CMD_O_SET_SDP;

  ret = write(fd, &op, 1);
  if (ret < 0)
    longjmp(err, ret);
    
  timed_read(fd, NULL, 0);
}

void op_opbuf_exec(int fd) {
  int ret;
  const uint8_t op = S_CMD_O_EXEC;

  ret = write(fd, &op, 1);
  if (ret < 0)
    longjmp(err, ret);
    
  timed_read(fd, NULL, 0);
}

void op_read(int fd, const uint32_t ba, uint8_t* buf, const uint32_t len) {
  int ret;
  const uint8_t header[] = {
    S_CMD_R_NBYTES, // Opcode
    ba & 0xFF, (ba >> 8) & 0xFF, (ba >> 16) & 0xFF, // 24-bit addr, LE
    len & 0xFF, (len >> 8) & 0xFF, (len >> 16) & 0xFF, // 24-bit length, LE
  };

  ret = write(fd, header, sizeof(header));
  if (ret < 0)
    longjmp(err, ret);

  print(INFO, "Beginning read\n");

  timed_read(fd, buf, len);
}

void op_errorcnt_reset(int fd) {
  int ret;
  const uint8_t op = S_CMD_S_ERRORCNT_RESET;

  ret = write(fd, &op, 1);
  if (ret < 0)
    longjmp(err, ret);
    
  timed_read(fd, NULL, 0);
}

uint32_t op_errorcnt(int fd) {
  int ret;
  const uint8_t op = S_CMD_Q_ERRORCNT;
  uint32_t errors_cnt;

  ret = write(fd, &op, 1);
  if (ret < 0)
    longjmp(err, ret);

  timed_read(fd, (uint8_t*)&errors_cnt, sizeof(errors_cnt));

  return errors_cnt;
}

static void buffer_write(int fd, const uint8_t *wbuf, const int len, const int ba, const uint32_t opbuf_len) {
  // const uint32_t chunk = 64 + 7;
  uint32_t avspace = opbuf_len;
  uint32_t off = 0;

  // Split this request in multiple ones to fit opbuf and to avoid
  // uart congestion
  while(off < (uint32_t)len) {
    // uint32_t plen = MIN(MIN(len-off, chunk), avspace - 7);
    const uint32_t plen = MIN(64, (len-off));
    
    print(DEBUG, "Im' writing size %d at addr %x\n", plen, ba+off);
    op_opbuf_write(fd, ba + off, wbuf + off, plen);

    off += plen;
    avspace -= (plen + 7);

    // No more space available in opbuf, time to commit
    // if (avspace < (7 + plen)) {
    op_opbuf_exec(fd);
    print(DEBUG, "Committed opbuf\n");
    avspace = opbuf_len;
    // }
  }

  if (avspace != opbuf_len) {
    op_opbuf_exec(fd);
    print(DEBUG, "Committed opbuf\n");
    avspace = opbuf_len;
  }

  print(INFO, "Write errors: %d\n", op_errorcnt(fd));
}

void load(const char* filename, uint8_t** buf, uint32_t* len) {
  struct stat st;
  stat(filename, &st);
  *len = st.st_size;

  *buf = malloc(*len);

  FILE *fp = fopen(filename, "rb");
  // TODO handle failures
  if (fp != NULL) {
    fread(*buf, sizeof(char), *len, fp);
    if (ferror(fp) != 0) {
      print(ERROR, "Error reading file");
      exit(-1);
    }
  } else {
    print(ERROR, "Error opening file");
    exit(-1);
  }

  print(DEBUG, "Successfully opened file %s, %d byte long\n", filename, *len);
  
  fclose(fp);
}

void save(const char* filename, const uint8_t* buf, const uint32_t len) {
  FILE *fp = fopen(filename, "wb");
  // TODO handle failures
  if (fp != NULL) {
    fwrite(buf, sizeof(char), len, fp);
    if (ferror(fp) != 0) {
      print(ERROR, "Error writing file");
      exit(-1);
    }
  } else {
    print(ERROR, "Error opening file");
    exit(-1);
  }
  fclose(fp);
}

// write, Read, verify
int main(int argc, char* argv[]) {
  // Internal flags
  bool rd = false, wr = false, vr = false;
  bool erase = false;
  bool preunlock = false, postlock = false;

  char* serial_port = NULL;

  uint8_t *wbuf = NULL, *rbuf = NULL;
  char *wfile = NULL, *rfile = NULL;
  int ba = -1;          // Base address
  int len = -1;         // Must fit at least 24-bit, serprog specification
  uint32_t opbuf_len;
  uint32_t serbuf_len;

  bool skip_verify = false;

  while (1) {
    static struct option long_options[] = {
      {"read",       required_argument, 0, 'r'},
      {"write",      required_argument, 0, 'w'},
      {"verify",     required_argument, 0, 'v'},
      {"noverify",   no_argument,       0, 'n'},
      {"erase",      no_argument,       0, 'e'},
      {"verbose",    optional_argument, 0, 'V'},
      {"size",       required_argument, 0, 's'},
      {"addr",       required_argument, 0, 'a'},
      {"device",     required_argument, 0, 'd'},
      {"unlock",     no_argument,       0, 'U'},
      {"lock",       no_argument,       0, 'P'},
      {"help",       no_argument,       0, 'h'},
      {0, 0, 0, 0}};
    
    static char* help[sizeof(long_options)/sizeof(*long_options)-1] = {
      "read the content of eeprom in file. Must specify size and addr",
      "write the content of file in eeprom, then verify. Must specify addr",
      "verify the content of the file with the eeprom. Must specify addr",
      "skip verification after write",
      "erase the eeprom (by software, i.e. write FF)",
      "set verbosity level to arg (0 low, 7 high)",
      "set reading size",
      "set starting address",
      "set serial device",
      "unlock before",
      "lock after",
      "print this help"
    };

    // int this_option_optind = optind ? optind : 1;
    int option_index = 0;
    int c = getopt_long(argc, argv, "r:w:v:neVs:a:d:UPh", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
      case 'r':
        rd = true;
        rfile = optarg;
        break;

      case 'w':
        wr = true;
        wfile = optarg;
        break;

      case 'v':
        vr = true;
        wfile = optarg;
        break;

      case 'n':
        skip_verify = true;
        break;

      case 'e':
        erase = true;
        break;

      case 'V':
        if (optarg)
          g_log_level = atoi(optarg);
        else
          g_log_level = DEBUG;
        break;

      case 's':
        if (optarg)
          len = atoi(optarg);
        break;

      case 'a':
        if (!optarg)
          break;

        if (0 == strncmp("0x", optarg, 2)) {
          ba = strtol(optarg + 2, NULL, 16);
        }
        else if (0 == strncmp("0b", optarg, 2))
          ba = strtol(optarg + 2, NULL, 2);
        else
          ba = strtol(optarg, NULL, 10);

        print(DEBUG, "%s ba %d\n", optarg, ba);
      break;

      case 'd':
        if (optarg && strlen(optarg))
          serial_port = optarg;
        break;
      
      case 'U':
        preunlock = true;
        break;

      case 'P':
        postlock = true;
        break;
      
      case 'h':
        printf("Usage: %s options\n\n", argv[0]);
        for (size_t i = 0; i < sizeof(help)/sizeof(*help); i++) {
          int space = 0;
          
          if (long_options[i].val != 0)
            space += printf(" -%c ", long_options[i].val);
          else
            space += printf("    ");
          space += printf("--%s", long_options[i].name);
          if (long_options[i].has_arg == required_argument)
            space += printf(" arg");
          else if (long_options[i].has_arg == optional_argument)
            space += printf(" [arg]");

          printf("%*c", 30-space, ' ');
          
          printf("%s\n", help[i]);
        }
        exit(0);

      default:
        printf("Invalid option %c\n", c);
        exit(-1);
    }
  }

  if (optind < argc) {
      printf("Invalid option: ");
      while (optind < argc)
          printf("%s ", argv[optind++]);
      printf("\n");
      return -1;
  }

  // Handle errors in provided options

  if (rd + wr + vr > 1) {
    print(ERROR, "Read, write, verify: choose one\n");
    return -1;
  }

  if ((rd || erase) && len < 0) {
    print(FATAL, "Invalid read length\n");
    exit(-1);
  }
  
  if ((rd || wr || vr) && ba < 0) {
    print(FATAL, "Invalid base address length\n");
    exit(-1);
  }


  if (skip_verify)
    vr = false;
  else if (wr)
    vr = true;


  // Check if writing file exists
  if (wfile && access(wfile, F_OK) != 0) {
    print(FATAL, "File %s does not exists\n", wfile);
    return -1;
  }

  // Check if reading file exists
  if (rfile && access(wfile, F_OK) == 0) {
    print(FATAL, "File %s exists, overwrite? (y)n\n", rfile);
    char c = getchar();
    if (c != 'y' && c != '\n')
      return 0;
    fflush(stdin);
  }

  int ecode = setjmp(err);

  if (ecode < 0) {
    print(FATAL, "Serial port: %s\n", strerror(errno));
    return ecode;
  } else if (ecode > 0) {
    print(FATAL, "Serial timeout\n");
    return ecode;
  }
  else {
    int fd = init_serial(serial_port != NULL ? serial_port : DEFAULT_DEVICE);

    // Fetch board name
    op_pgmname(fd);

    // Fetch opbuf size
    opbuf_len = op_opbuf_len(fd);

    // Fetch serial buffer size
    serbuf_len = op_serbuf_len(fd);
    (void)serbuf_len;

    op_errorcnt_reset(fd);
    print(INFO, "Write errors: %d\n", op_errorcnt(fd));

    if (erase) {
      uint8_t *blank_buffer = malloc(len);
      uint8_t *rbuf = malloc(len);

      print(INFO, "Erasing device...\n");
      buffer_write(fd, blank_buffer, len, 0, opbuf_len);

      print(INFO, "Blank checking...\n");
      op_read(fd, ba, rbuf, len);

      if (g_log_level >= DEBUG)
        hexdump(rbuf, len);

      if (0 == memcmp(wbuf, rbuf, len))
        print(INFO, "Erased successfully\n", len);
      else
        print(ERROR, "EEPROM is not blank\n", len);

      free(blank_buffer);
      free(rbuf);
    }

    if (wr || vr) {
      load(wfile, &wbuf, (uint32_t*)&len);
    }

    if (rd || vr)
      rbuf = malloc(len);
    
    if (preunlock) {
      print(INFO, "Unlocking memory...\n");
      op_opbuf_sdp(fd, false);
      op_opbuf_exec(fd);
    }

    // If write request, do it
    if (wr) {
      buffer_write(fd, wbuf, len, ba, opbuf_len);
    }

    // If read request, do it (read or verify)
    if (rd || vr) {
      op_read(fd, ba, rbuf, len);

      if (g_log_level >= DEBUG)
        hexdump(rbuf, len);

      if (rd) save(rfile, rbuf, len);

      if (vr) {
        if (0 == memcmp(wbuf, rbuf, len))
          print(INFO, "Verified successfully\n", len);
        else
          print(ERROR, "Failed verification\n", len);
      }
    }
    if (postlock) {
      print(INFO, "Locking memory...\n");
      op_opbuf_sdp(fd, true);
      op_opbuf_exec(fd);
    }
  }

  // TODO copiare dentro la return della setjmp oppure adottare altri stratagemmi
  if (rbuf) free(rbuf);
  if (wbuf) free(wbuf);

  return 0;
}
