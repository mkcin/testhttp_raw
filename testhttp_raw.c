#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "err.h"


const char *GET_STRING = "GET";
const char *HTTP_STRING = "HTTP/1.1";
const char *HOST_STRING = "Host:";
const char *COOKIE_STRING = "Cookie:";
const char *CONNECTION_CLOSE_STRING = "Connection: Close";

// Funkcja rozszerzająca bufor na linię wejścia
// Przyjmuje:
// [line_length] - wskaźniek na obecną długość linii (długość jest modyfikowana przez funkcję)
// [line] - wskaźnik na linię
// Zwraca:
// wskaźnik na poszerzony bufor
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

// Funkcja wyciąga cookies z pliku.
// fetch_cookies_file otwiera plik ze ścieżki podanej w argumencie, alokuje pamięć na linijkę ciasteczek
// i dodaje je z kolejnych linijek pliku.
// Jeśli nie udało się zaalokować pamięci na linię, przerywa program z odpowiednim komentarzem wypisywanym na strumień
// błędu.
// Zwraca wskaźnik na linijkę z ciasteczkami.
const char *fetch_cookies_file(const char *cookies_file_name) {
  FILE *cookies_file = fopen(cookies_file_name, "r");
  if (!cookies_file) {
    fatal("Unable to open the file %s", cookies_file_name);
  }

  size_t buffer_size = 32;
  size_t current_length = 0;
  char *result = malloc(buffer_size * sizeof(char));
  if (!result) {
    fatal("Couldn't allocate memory for the cookies");
  }
  memset(result, 0, buffer_size);

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
          fatal("Couldn't allocate memory for the cookies");
        }
      }
      result[current_length++] = line[i];
    }

    if (current_length + 2 >= buffer_size) {
      if ((result = extend_buffer(&buffer_size, result)) == NULL) {
        fatal("Couldn't allocate memory for the cookies");
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

// Funkcja wyciąga host z podanego adresu.
// fetch_host alokuje pamięć na hosta i przepisuje go na podstawie adresu podanego w argumencie.
// Jeśli nie udało się zaalokować pamięci, przerywa program z odpowiednim komentarzem wypisywanym na strumień
// błędu.
// Zwraca wskaźnik na linijkę z hostem.
char *fetch_host(const char *address) {
  char *host_begin;
  if((host_begin = strstr(address, "https://")) != NULL) {
    host_begin += 8;
  }
  else if ((host_begin = strstr(address, "http://")) != NULL) {
    host_begin += 7;
  }
  else {
    fatal("Invalid address");
  }
  char *host_end;
  size_t host_length = strlen(host_begin);
  if((host_end = strchr(host_begin, '/')) != NULL) {
    host_length = host_end - host_begin;
  }
  char *host = malloc(strlen(address) * sizeof(char));
  if(!host) {
    fatal("couldn't allocate memory");
  }
  strcpy(host, host_begin);
  if(host_end) {
    host[host_length] = '\0';
  }
  return host;
}

// Funkcja konstruuje zapytanie GET
// create_get alokuje pamięć na zapytanie GET, konstruuje je z podanego adresu i wskaźnika na napis z ciasteczkami.
// Jeśli nie udało się zaalokować pamięci, przerywa program z odpowiednim komentarzem wypisywanym na strumień
// błędu.
// Jeśli nie udało się zaalokować pamięci na zapytanie.
char *create_get(const char *address, const char *cookies) {
  char *host = fetch_host(address);
  size_t request_length = 10 + strlen(GET_STRING) + strlen(address) + strlen(HTTP_STRING) + strlen(HOST_STRING) + strlen(host) + strlen(COOKIE_STRING) + strlen(cookies) + strlen(CONNECTION_CLOSE_STRING);
  char *result = malloc(request_length * sizeof(char));
  if(!result) {
    fatal("couldn't allocate memory");
  }
  sprintf(result, "%s %s %s\n%s %s\n%s %s\n%s\n\n", GET_STRING, address, HTTP_STRING, HOST_STRING, host,
           COOKIE_STRING, cookies, CONNECTION_CLOSE_STRING);
  free(host);
  return result;
}

// Funkcja czytająca linię pliku.
// read_line rezerwuje pamięć na linijkę zczytaną z pliku, zwraca tę linijkę oraz ustawia jej długość
// pod wskaźnikiem przekazanym jako argument.
// Zwraca wskaźnik na linijkę lub NULL, jeśli nie udało się zaalokować pamięci.
char *read_line(FILE *file, size_t *line_length) {
  char *line = NULL;
  size_t buffer_length = 0;
  if((*line_length = getline(&line, &buffer_length, file)) == -1) {
    free(line);
    return NULL;
  }
  return line;
}


// Funkcja parsująca odpowiedź serwera
// fetch_response_cookies_and_length sprawdza czy odpowiedź serwera ma status 200 OK
// Jeśli tak, wypisuje na standardowe wyjście ciasteczka z nagłówka odpowiedzi oraz
// rzeczywistą długość zasobu zwróconą przez serwer. Następnie zwraca NULL.
// Jeśli nie, nie wypisuje nic i zwraca linijkę ze statusem odpowiedzi.
// Jeśli nie udało się zaalokować pamięci, przerywa program z odpowiednim komentarzem wypisywanym na strumień
// błędu.
char *fetch_response_cookies_and_length(int socket) {
  FILE *response = NULL;
  if ((response = fdopen(socket, "r")) == NULL) {
    fatal("fdopen");
  }

  size_t response_status_length = 0;
  char *response_status = NULL;
  if((response_status = read_line(response, &response_status_length)) == NULL) {
    fatal("Invalid server response");
  }
  if(strcmp(response_status, "HTTP/1.1 200 OK\r\n") != 0) {
    fclose(response);
    char *end = strstr(response_status, "\r\n");
    if(end) {
      *(end+1) = '\0';
      *end = '\n';
    }
    return response_status;
  }
  free(response_status);

  char *response_header_line = NULL;
  size_t response_header_line_length = 0;
  bool chunked = false;
  while((response_header_line = read_line(response, &response_header_line_length)) != NULL) {
    if(strncmp(response_header_line, "Set-Cookie: ", 12) == 0) {
      char *cookie_end;
      if((cookie_end = strchr(response_header_line + 13, ';')) != NULL) {
        *cookie_end = '\0';
        printf("%s\n", response_header_line + 12);
      }
      else if((cookie_end = strchr(response_header_line + 13, '\r')) != NULL) {
        *cookie_end = '\0';
        printf("%s\n", response_header_line + 12);
      }
      else if((cookie_end = strchr(response_header_line + 13, ' ')) != NULL) {
        *cookie_end = '\0';
        printf("%s\n", response_header_line + 12);
      }
    }
    if(strcmp(response_header_line, "Transfer-Encoding: chunked\r\n") == 0) {
      chunked = true;
    }
    if(strcmp(response_header_line, "\r\n") == 0) {
      free(response_header_line);
      break;
    }

    free(response_header_line);
  }

  char *response_body_line = NULL;
  size_t response_body_line_length = 0;
  size_t response_length = 0;
  while((response_body_line = read_line(response, &response_body_line_length)) != NULL) {
    if(chunked) {
      size_t chunk_size = strtol(response_body_line, NULL, 16);
      response_length += chunk_size;
      char *chunk = malloc((chunk_size + 2) * sizeof(char));
      if(!chunk) {
        fatal("couldn't allocate memory");
      }
      fread(chunk, sizeof(char), chunk_size+2, response);
      free(chunk);
    }
    else {
      response_length += response_body_line_length;
    }

    free(response_body_line);
  }

  printf("Dlugosc zasobu: %zu\n", response_length);

  fclose(response);

  return NULL;
}

// Główna funkcja programmu.
// Funkcja nawiązuje połączenie z podanym w argv[1] adresem,
// pobiera ciasteczka z podanego w argv[2] pliku,
// wykonuje zapytanie GET o adres podany w argv[3].
// Jeśli nie udało się zaalokować pamięci, wystąpił błąd z połączeniem lub któryś z argumentów jest niepoprawny,
// przerywa program z odpowiednim komentarzem wypisywanym na strumień błędu.
int main(int argc, char *argv[]) {
  int rc;
  int sock;
  struct addrinfo addr_hints, *addr_result;

  if (argc != 4) {
    fatal("Użycie: %s <adres połączenia>:<port> <plik ciasteczek> <testowany adres http>", argv[0]);
  }

  const char *cookies_string = fetch_cookies_file(argv[2]);
  const char *test_address = argv[3];
  const char *host_address = argv[1];
  char *semicolon = strrchr(host_address, ':');
  if(!semicolon) {
    fatal("invalid first argument format");
  }
  char *port = semicolon + 1;
  *semicolon = '\0';

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    syserr("socket");
  }

  memset(&addr_hints, 0, sizeof(struct addrinfo));
  addr_hints.ai_flags = 0;
  addr_hints.ai_family = AF_INET;
  addr_hints.ai_socktype = SOCK_STREAM;
  addr_hints.ai_protocol = IPPROTO_TCP;

  rc = getaddrinfo(host_address, port, &addr_hints, &addr_result);
  if (rc != 0) {
    syserr("getaddrinfo: %s; rc=%d", gai_strerror(rc), rc);
  }

  if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) != 0) {
    syserr("connect");
  }
  freeaddrinfo(addr_result);


  char *request = create_get(test_address, cookies_string);
  size_t length = strlen(request);

  if (write(sock, request, length) < 0) {
    syserr("writing on stream socket");
  }

  char *wrong_status;
  if((wrong_status = fetch_response_cookies_and_length(sock)) != NULL) {
    printf("%s", wrong_status);
    free(wrong_status);
  }

  free(request);
  free((char *) cookies_string);
  return 0;
}

