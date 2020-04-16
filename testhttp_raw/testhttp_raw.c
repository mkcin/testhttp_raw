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

const char *fetch_cookies_file(const char *cookies_file_name) {
  FILE *cookies_file = fopen(cookies_file_name, "r");
  if (!cookies_file) {
    fatal("Nie udało się otworzyć podanego pliku %s", cookies_file_name);
  }

  size_t buffer_size = 32;
  size_t current_length = 0;
  char *result = malloc(buffer_size * sizeof(char));
  memset(result, 0, buffer_size);
  if (!result) {
    fatal("Nie udało się alokować pamięci na ciasteczka");
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  while ((read = getline(&line, &len, cookies_file)) != -1) {
    for (size_t i = 0; i < read; i++) {
      if (line[i] == '\n') {
        break;
      }

      if (current_length + 1 >= buffer_size) {
        if ((result = extend_buffer(&buffer_size, result)) == NULL) {
          fatal("Nie udało się alokować pamięci na ciasteczka");
        }
      }
      result[current_length++] = line[i];
    }

    if (current_length + 2 >= buffer_size) {
      if ((result = extend_buffer(&buffer_size, result)) == NULL) {
        fatal("Nie udało się alokować pamięci na ciasteczka");
      }
    }
    result[current_length++] = ';';
    result[current_length++] = ' ';
  }
  free(line);

  if (current_length != 0) {
    result[current_length - 2] = '\0';
  }

  fclose(cookies_file);

  return result;

}

const char *fetch_cookies_header(const char *header) {
  const char *next_cookie;
  size_t response_length = 1024;
  size_t current_length = 0;
  char *response_cookies = malloc(response_length * sizeof(char));
  if(!response_cookies) {
    fatal("Nie udało się alokować pamięci na ciasteczka");
  }

  while((next_cookie = strstr(header, "Set-Cookie: ")) != NULL) {
    next_cookie += 12;
    const char *next_semicolon = strchr(next_cookie, ';');
    const char *next_newline = strchr(next_cookie, '\n');
    const char *cookie_end;
    if(next_semicolon && next_semicolon < next_newline) {
      cookie_end = next_semicolon;
    }
    else {
      cookie_end = next_newline;
    }
    header = cookie_end+1;

    while(next_cookie != cookie_end) {
      if (current_length + 1 >= response_length) {
        if ((response_cookies = extend_buffer(&response_length, response_cookies)) == NULL) {
          fatal("Nie udało się alokować pamięci na odpowiedz");
        }
      }
      response_cookies[current_length++] = *next_cookie;
      next_cookie++;
    }

    if (current_length + 1 >= response_length) {
      if ((response_cookies = extend_buffer(&response_length, response_cookies)) == NULL) {
        fatal("Nie udało się alokować pamięci na odpowiedz");
      }
    }
    response_cookies[current_length++] = '\n';
  }

  if (current_length + 1 >= response_length) {
    if ((response_cookies = extend_buffer(&response_length, response_cookies)) == NULL) {
      fatal("Nie udało się alokować pamięci na odpowiedz");
    }
  }
  response_cookies[current_length++] = '\0';
  return response_cookies;
}

char *create_get(const char *host, const char *address, const char *cookies) {
  char *result = NULL;
  asprintf(&result, "%s %s %s\n%s %s\n%s %s\n%s\n\n", GET_STRING, address, HTTP_STRING, HOST_STRING, host,
           COOKIE_STRING, cookies, CONNECTION_CLOSE_STRING);
  return result;
}

const char *get_response(int sock) {

  char *response_buffer = malloc(BUFFER_SIZE * sizeof(char));
  if (!response_buffer) {
    fatal("Nie udało się alokować pamięci na bufor odpowiedzi");
  }
  int32_t response_buffer_length;

  size_t response_length = 1024;
  size_t current_length = 0;
  char *response = malloc(response_length * sizeof(char));

  while(1){
    if((response_buffer_length = read(sock, response_buffer, BUFFER_SIZE)) > 0) {
      for(size_t i=0; i<response_buffer_length; i++) {
        if (current_length + 1 >= response_length) {
          if ((response = extend_buffer(&response_length, response)) == NULL) {
            fatal("Nie udało się alokować pamięci na odpowiedz");
          }
        }
        response[current_length++] = response_buffer[i];
      }

    }
    else if(response_buffer_length == 0) {
      break;
    }
    else {
      syserr("reading stream socket");
    }
  }

  response[current_length] = '\0';
  free(response_buffer);
  return response;
}

const char *check_status(const char *header) {
  char *first_space = strchr(header, ' ');
  if(strncmp(first_space+1, "200 OK", 6) != 0) {
    char *first_newline = strchr(header, '\r');
    *first_newline = '\0';
    return header;
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  int rc;
  int sock;
  struct addrinfo addr_hints, *addr_result;

  /* Kontrola dokumentów ... */
  if (argc != 4) {
    fatal("Użycie: %s <adres połączenia>:<port> <plik ciasteczek> <testowany adres http>", argv[0]);
  }

  const char *cookies_string = fetch_cookies_file(argv[2]);
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

  rc = getaddrinfo(host_address, port, &addr_hints, &addr_result);
  if (rc != 0) {
    fprintf(stderr, "rc=%d\n", rc);
    syserr("getaddrinfo: %s", gai_strerror(rc));
  }

  if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) != 0) {
    syserr("connect");
  }
  freeaddrinfo(addr_result);


  char *request = create_get(host_address, test_address, cookies_string);
  size_t length = strlen(request);

//  printf("REQUEST:\n%s", request);

  if (write(sock, request, length) < 0) {
    syserr("writing on stream socket");
  }
  const char *response_text = get_response(sock);
  if (close(sock) < 0)
    syserr("closing stream socket");

  char *data = strstr( response_text, "\r\n\r\n" );
  const char *header_text = response_text;
  const char *data_text = "";
  if ( data != NULL )
  {
    data_text = data+4;
    *(data+2) = '\0';
  }

  const char *status;
  if((status = check_status(header_text)) != NULL) {
    printf("%s", status);
    free((char *)response_text);
    free(request);
    free((char *) cookies_string);
    return 0;
  }
  size_t data_length = strlen(header_text);
  const char *fetched_cookies = fetch_cookies_header(header_text);

  printf("%s", fetched_cookies);
  printf("%zu", data_length);
//  printf("RESPONSE HEADER\n%s\nRESPONSE HEADER END\n", header_text);
//  printf("RESPONSE DATA\n%s\nRESPONSE DATA END\n", data_text);


  free((char *)response_text);
  free(request);
  free((char *) cookies_string);
  return 0;
}

