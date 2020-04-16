/*
 Program uruchamiamy z dwoma parametrami: nazwa serwera i numer jego portu.
 Program spróbuje połączyć się z serwerem, po czym będzie od nas pobierał
 linie tekstu i wysyłał je do serwera.  Wpisanie BYE kończy pracę.
*/
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "err.h"

#define BUFFER_SIZE      4096

static const char bye_string[] = "BYE";
const char *GET_STRING = "GET";
const char *HTTP_STRING = "HTTP/1.1";
const char *HOST_STRING = "Host:";
const char *COOKIE_STRING = "Cookie:";
const char *CONNECTION_CLOSE_STRING = "Connection: Close";

char *extend_buffer(uint64_t *buffer_length, char *buffer) {
  *buffer_length *= 2;
  char *tmp = realloc(buffer, *buffer_length * sizeof(char));
  if (tmp) {
    return tmp;
  } else {
    free(buffer);
    return NULL;
  }
}

const char *fetch_cookies(const char *cookies_file_name) {
  FILE *cookies_file = fopen(cookies_file_name, "r");
  if(!cookies_file) {
    fatal("Nie udało się otworzyć podanego pliku %s", cookies_file_name);
  }

  size_t buffer_size = 1024;
  size_t current_length = 0;
  char *result = malloc(buffer_size * sizeof(char));
  if(!result) {
    fatal("Nie udało się alokować pamięci na ciasteczka");
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  while ((read = getline(&line, &len, cookies_file)) != -1) {
//    printf("Retrieved line of length %zu:\n", read);
//    printf("%s\n", line);
    for(size_t i=0; i<len; i++) {
      if(line[i] == '\n') {
        break;
      }

      if(current_length + 1 >= buffer_size) {
        if(extend_buffer(&buffer_size, result) == NULL ) {
          fatal("Nie udało się alokować pamięci na ciasteczka");
        }
      }
      result[current_length++] = line[i];
    }

    if(current_length + 2 >= buffer_size) {
      if(extend_buffer(&buffer_size, result) == NULL ) {
        fatal("Nie udało się alokować pamięci na ciasteczka");
      }
    }
    result[current_length++] = ';';
    result[current_length++] = ' ';
//    printf("partial cookies: %s, length: %d\n", result, current_length);
  }
  free(line);

  if(current_length != 0) {
    result[current_length-2] = '\0';
  }

//  printf("cookies: %s\n", result);

  return result;

}

char *create_get(const char *host, const char *address, const char* cookies) {
  char *result = NULL;
  asprintf(&result, "%s %s %s\n%s %s\n%s %s\n%s\n\n", GET_STRING, address, HTTP_STRING, HOST_STRING, host, COOKIE_STRING, cookies, CONNECTION_CLOSE_STRING);
  return result;
}

int main (int argc, char *argv[]) {
  int rc;
  int sock;
  struct addrinfo addr_hints, *addr_result;
  char *line;

  /* Kontrola dokumentów ... */
  if (argc != 4) {
    fatal("Użycie: %s <adres połączenia>:<port> <plik ciasteczek> <testowany adres http>", argv[0]);
  }

  const char *cookies_string = fetch_cookies(argv[2]);
  const char *test_address = argv[3];
  const char *host_address = argv[1];
  char *semicolon = strrchr(host_address, ':');
  char *port = semicolon + 1;
  *semicolon = '\0';

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    syserr("socket");
  }

  /* Trzeba się dowiedzieć o adres internetowy serwera. */
  memset(&addr_hints, 0, sizeof(struct addrinfo));
  addr_hints.ai_flags = 0;
  addr_hints.ai_family = AF_INET;
  addr_hints.ai_socktype = SOCK_STREAM;
  addr_hints.ai_protocol = IPPROTO_TCP;

  rc =  getaddrinfo(host_address, port, &addr_hints, &addr_result);
  if (rc != 0) {
    fprintf(stderr, "rc=%d\n", rc);
    syserr("getaddrinfo: %s", gai_strerror(rc));
  }

  if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) != 0) {
    syserr("connect");
  }
  freeaddrinfo(addr_result);


  line = create_get(host_address, test_address, cookies_string);
  size_t length = strlen(line);

  printf("%s", line);
  if (write(sock, line, length) < 0){
    syserr("writing on stream socket");
  }

  char *response = malloc(BUFFER_SIZE * sizeof(char));
  memset(response, 0, BUFFER_SIZE);

  if(read(sock, response, BUFFER_SIZE) < 0) {
    syserr("reading stream socket");
  }

  printf("%s", response);

  if (close(sock) < 0)
    syserr("closing stream socket");

  free(response);
  free(line);
  free((char *)cookies_string);
  return 0;
}

